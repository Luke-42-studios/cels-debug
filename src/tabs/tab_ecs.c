#define _POSIX_C_SOURCE 200809L

#include "tab_ecs.h"
#include "../tui.h"
#include "../split_panel.h"
#include "../tree_view.h"
#include "../json_render.h"
#include "../scroll.h"
#include "../data_model.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

/* Per-tab private state */
typedef struct ecs_state {
    split_panel_t panel;             /* Left/right split */
    tree_view_t tree;                /* Entity tree + virtual scrolling */
    scroll_state_t inspector_scroll; /* Scroll state for component inspector */
    bool *comp_expanded;             /* Expand/collapse state per component group */
    int comp_expanded_count;         /* Number of component groups allocated */
    bool panel_created;              /* Whether split_panel windows exist */
} ecs_state_t;

/* --- Entity classification (CELS-C sections) --- */

/* Check if an entity has a specific tag (substring match on tag name) */
static bool has_tag(entity_node_t *node, const char *tag_name) {
    for (int i = 0; i < node->tag_count; i++) {
        if (node->tags[i] && strstr(node->tags[i], tag_name)) return true;
    }
    return false;
}

/* Check if an entity has "Component" in its component_names */
static bool has_component_component(entity_node_t *node) {
    for (int i = 0; i < node->component_count; i++) {
        if (node->component_names[i] &&
            strcmp(node->component_names[i], "Component") == 0) return true;
    }
    return false;
}

/* Extract pipeline phase from tags like "flecs.pipeline.OnLoad" -> "OnLoad" */
static char *extract_pipeline_phase(entity_node_t *node) {
    for (int i = 0; i < node->tag_count; i++) {
        if (node->tags[i] && strncmp(node->tags[i], "flecs.pipeline.", 15) == 0) {
            const char *phase = node->tags[i] + 15;
            if (phase[0] != '\0') return strdup(phase);
        }
    }
    return NULL;
}

/* Check if name ends with "Lifecycle" */
static bool name_is_lifecycle(entity_node_t *node) {
    if (!node->name) return false;
    const char *suffix = "Lifecycle";
    size_t nlen = strlen(node->name);
    size_t slen = strlen(suffix);
    if (nlen < slen) return false;
    return strcmp(node->name + nlen - slen, suffix) == 0;
}

/* Classify a single node (root-level only -- children inherit) */
static entity_class_t classify_node(entity_node_t *node) {
    free(node->class_detail);
    node->class_detail = NULL;

    /* S: Systems -- flecs.system.System tag, observers, provider-generated */
    if (has_tag(node, "flecs.system.System")) {
        node->class_detail = extract_pipeline_phase(node);
        if (!node->class_detail) node->class_detail = strdup("System");
        return ENTITY_CLASS_SYSTEM;
    }
    if (has_tag(node, "flecs.core.Observer")) {
        node->class_detail = strdup("Observer");
        return ENTITY_CLASS_SYSTEM;
    }
    /* C: Components -- component type entities */
    if (has_component_component(node)) {
        return ENTITY_CLASS_COMPONENT;
    }
    /* L: Lifecycles -- marker entities whose name ends with "Lifecycle" */
    if (name_is_lifecycle(node)) {
        return ENTITY_CLASS_LIFECYCLE;
    }
    /* E: Entities -- leaf scene entities (no children, have component data) */
    if (node->child_count == 0 && node->component_count > 0) {
        return ENTITY_CLASS_ENTITY;
    }
    /* C: Compositions -- parent entities (have children = scene structure) */
    return ENTITY_CLASS_COMPOSITION;
}

/* Propagate a class to all descendants */
static void propagate_class(entity_node_t *node, entity_class_t cls) {
    node->entity_class = cls;
    for (int i = 0; i < node->child_count; i++) {
        propagate_class(node->children[i], cls);
    }
}

/* Classify all roots in entity list -- children inherit root's class */
static void classify_all_entities(entity_list_t *list) {
    if (!list) return;
    for (int i = 0; i < list->root_count; i++) {
        entity_class_t cls = classify_node(list->roots[i]);
        propagate_class(list->roots[i], cls);
    }
}

/* --- Annotation: enrich component entities with registry data --- */

static void annotate_component_entities(entity_list_t *list,
                                         component_registry_t *reg) {
    if (!list || !reg) return;
    for (int i = 0; i < list->root_count; i++) {
        entity_node_t *node = list->roots[i];
        if (node->entity_class != ENTITY_CLASS_COMPONENT) continue;
        if (!node->name) continue;

        /* Find matching registry entry */
        for (int r = 0; r < reg->count; r++) {
            if (reg->components[r].name &&
                strcmp(reg->components[r].name, node->name) == 0) {
                free(node->class_detail);
                char buf[64];
                if (reg->components[r].has_type_info && reg->components[r].size > 0) {
                    snprintf(buf, sizeof(buf), "%d entities, %dB",
                             reg->components[r].entity_count,
                             reg->components[r].size);
                } else {
                    snprintf(buf, sizeof(buf), "%d entities",
                             reg->components[r].entity_count);
                }
                node->class_detail = strdup(buf);
                break;
            }
        }
    }
}

/* --- Helper: count inspector content rows for scroll total --- */

static int count_inspector_rows(const entity_detail_t *detail,
                                const bool *expanded, int expanded_count) {
    int rows = 0;

    if (detail->components && yyjson_is_obj(detail->components)) {
        size_t idx, max;
        yyjson_val *key, *val;
        int comp_idx = 0;
        yyjson_obj_foreach(detail->components, idx, max, key, val) {
            rows++;
            bool exp = (comp_idx < expanded_count) ? expanded[comp_idx] : true;
            if (exp && val && !yyjson_is_null(val)) {
                if (yyjson_is_obj(val))
                    rows += (int)yyjson_obj_size(val);
                else if (yyjson_is_arr(val))
                    rows += (int)yyjson_arr_size(val);
                else
                    rows += 1;
            }
            comp_idx++;
        }
    }

    if (detail->tags && yyjson_is_arr(detail->tags) && yyjson_arr_size(detail->tags) > 0) {
        rows++;
        rows += (int)yyjson_arr_size(detail->tags);
    }

    if (detail->pairs && yyjson_is_obj(detail->pairs) && yyjson_obj_size(detail->pairs) > 0) {
        rows++;
        rows += (int)yyjson_obj_size(detail->pairs);
    }

    return rows;
}

/* --- Helper: ensure comp_expanded array matches component count --- */

static void ensure_comp_expanded(ecs_state_t *es, int needed) {
    if (es->comp_expanded_count >= needed) return;

    bool *new_arr = realloc(es->comp_expanded, (size_t)needed * sizeof(bool));
    if (!new_arr) return;

    for (int i = es->comp_expanded_count; i < needed; i++) {
        new_arr[i] = true;
    }
    es->comp_expanded = new_arr;
    es->comp_expanded_count = needed;
}

/* --- Helper: count total component/tag/pair groups for expand tracking --- */

static int count_groups(const entity_detail_t *detail) {
    int count = 0;
    if (detail->components && yyjson_is_obj(detail->components)) {
        count += (int)yyjson_obj_size(detail->components);
    }
    if (detail->tags && yyjson_is_arr(detail->tags) && yyjson_arr_size(detail->tags) > 0) {
        count++;
    }
    if (detail->pairs && yyjson_is_obj(detail->pairs) && yyjson_obj_size(detail->pairs) > 0) {
        count++;
    }
    return count;
}

/* --- Helper: find which group index corresponds to the current inspector cursor --- */

static int cursor_to_group_index(const entity_detail_t *detail,
                                 const bool *expanded, int expanded_count,
                                 int cursor_row) {
    int row = 0;
    int group_idx = 0;

    if (detail->components && yyjson_is_obj(detail->components)) {
        size_t idx, max;
        yyjson_val *key, *val;
        yyjson_obj_foreach(detail->components, idx, max, key, val) {
            if (row == cursor_row) return group_idx;
            row++;
            bool exp = (group_idx < expanded_count) ? expanded[group_idx] : true;
            if (exp && val && !yyjson_is_null(val)) {
                if (yyjson_is_obj(val))
                    row += (int)yyjson_obj_size(val);
                else if (yyjson_is_arr(val))
                    row += (int)yyjson_arr_size(val);
                else
                    row += 1;
            }
            group_idx++;
        }
    }

    if (detail->tags && yyjson_is_arr(detail->tags) && yyjson_arr_size(detail->tags) > 0) {
        if (row == cursor_row) return group_idx;
        row++;
        row += (int)yyjson_arr_size(detail->tags);
        group_idx++;
    }

    if (detail->pairs && yyjson_is_obj(detail->pairs) && yyjson_obj_size(detail->pairs) > 0) {
        if (row == cursor_row) return group_idx;
        group_idx++;
    }

    return -1;
}

/* --- Helper: update selected_entity_path from tree cursor --- */

static void sync_selected_path(ecs_state_t *es, app_state_t *state) {
    entity_node_t *sel = tree_view_selected(&es->tree);
    free(state->selected_entity_path);
    state->selected_entity_path = sel ? strdup(sel->full_path) : NULL;

    if (state->entity_detail && sel &&
        strcmp(state->entity_detail->path, sel->full_path) != 0) {
        entity_detail_free(state->entity_detail);
        state->entity_detail = NULL;
    }
}

/* --- Lifecycle --- */

void tab_ecs_init(tab_t *self) {
    ecs_state_t *es = calloc(1, sizeof(ecs_state_t));
    if (!es) return;

    tree_view_init(&es->tree);
    scroll_reset(&es->inspector_scroll);
    es->panel_created = false;
    es->comp_expanded = NULL;
    es->comp_expanded_count = 0;

    self->state = es;
}

void tab_ecs_fini(tab_t *self) {
    ecs_state_t *es = (ecs_state_t *)self->state;
    if (!es) return;

    if (es->panel_created) {
        split_panel_destroy(&es->panel);
    }
    tree_view_fini(&es->tree);
    free(es->comp_expanded);
    free(es);
    self->state = NULL;
}

/* --- Draw --- */

void tab_ecs_draw(const tab_t *self, WINDOW *win, const void *app_state) {
    ecs_state_t *es = (ecs_state_t *)self->state;
    if (!es) return;

    const app_state_t *state = (const app_state_t *)app_state;

    int h = getmaxy(win);
    int w = getmaxx(win);

    if (!es->panel_created) {
        split_panel_create(&es->panel, h, w, getbegy(win));
        es->panel_created = true;
    } else if (h != es->panel.height ||
               w != es->panel.left_width + es->panel.right_width) {
        split_panel_resize(&es->panel, h, w, getbegy(win));
    }

    werase(es->panel.left);
    werase(es->panel.right);
    split_panel_draw_borders(&es->panel, "ECS", "Inspector");

    /* --- Left panel: entity tree --- */
    if (state->entity_list) {
        /* Classify entities into CELS-C sections */
        classify_all_entities(state->entity_list);

        /* Annotate component entities with registry data (entity count, size) */
        annotate_component_entities(state->entity_list, state->component_registry);

        tree_view_rebuild_visible(&es->tree, state->entity_list);

        /* Auto-select first entity if nothing selected yet */
        if (!state->selected_entity_path && es->tree.row_count > 0) {
            for (int i = 0; i < es->tree.row_count; i++) {
                entity_node_t *n = es->tree.rows[i].node;
                if (n && n->full_path) {
                    app_state_t *mut_state = (app_state_t *)state;
                    mut_state->selected_entity_path = strdup(n->full_path);
                    break;
                }
            }
        }

        tree_view_render(&es->tree, es->panel.left);
    } else {
        const char *msg = "Waiting for data...";
        int msg_len = (int)strlen(msg);
        int max_y = getmaxy(es->panel.left);
        int max_x = getmaxx(es->panel.left);
        wattron(es->panel.left, A_DIM);
        mvwprintw(es->panel.left, max_y / 2, (max_x - msg_len) / 2, "%s", msg);
        wattroff(es->panel.left, A_DIM);
    }

    /* --- Right panel: context-sensitive inspector --- */
    entity_node_t *sel = tree_view_selected(&es->tree);
    WINDOW *rwin = es->panel.right;
    int rh = getmaxy(rwin) - 2;
    int rw = getmaxx(rwin) - 2;

    if (sel && sel->entity_class == ENTITY_CLASS_COMPONENT) {
        /* --- Branch A: Component type selected -- show entities with this component --- */
        const char *comp_name = sel->name;

        if (comp_name && state->entity_list && state->entity_list->count > 0) {
            entity_list_t *elist = state->entity_list;

            /* Filter entities that have the selected component */
            int match_cap = elist->count;
            entity_node_t **matches = malloc((size_t)match_cap * sizeof(entity_node_t *));
            int match_count = 0;

            if (matches) {
                for (int i = 0; i < elist->count; i++) {
                    entity_node_t *node = elist->nodes[i];
                    for (int c = 0; c < node->component_count; c++) {
                        if (node->component_names[c] &&
                            strcmp(node->component_names[c], comp_name) == 0) {
                            matches[match_count++] = node;
                            break;
                        }
                    }
                }

                /* Header */
                wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
                mvwprintw(rwin, 1, 1, "Entities with %.*s", rw - 14, comp_name);
                wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

                /* Update scroll state for entity list (below header) */
                es->inspector_scroll.total_items = match_count;
                es->inspector_scroll.visible_rows = rh - 1; /* minus header row */
                scroll_ensure_visible(&es->inspector_scroll);

                if (match_count > 0) {
                    /* Render visible matching entities */
                    int avail_rows = rh - 1;
                    for (int row = 0; row < avail_rows &&
                         es->inspector_scroll.scroll_offset + row < match_count; row++) {
                        int idx = es->inspector_scroll.scroll_offset + row;
                        entity_node_t *ent = matches[idx];

                        bool is_cursor = (idx == es->inspector_scroll.cursor);

                        if (is_cursor && es->panel.focus == 1) {
                            wattron(rwin, A_REVERSE);
                        }

                        /* Clear the row inside border */
                        wmove(rwin, row + 2, 1);
                        for (int c = 0; c < rw; c++) waddch(rwin, ' ');

                        /* Entity name (or #<id> for anonymous) */
                        const char *display_name;
                        char id_buf[32];
                        if (ent->name && strlen(ent->name) > 0) {
                            display_name = ent->name;
                        } else {
                            snprintf(id_buf, sizeof(id_buf), "#%lu",
                                     (unsigned long)ent->id);
                            display_name = id_buf;
                        }

                        wattron(rwin, COLOR_PAIR(CP_ENTITY_NAME));
                        mvwprintw(rwin, row + 2, 2, "%.*s", rw / 2, display_name);
                        wattroff(rwin, COLOR_PAIR(CP_ENTITY_NAME));

                        /* Full path in dim to the right */
                        if (ent->full_path) {
                            int name_end = getcurx(rwin);
                            int path_col = name_end + 1;
                            int avail = rw - (path_col - 1);
                            if (avail > 2) {
                                wattron(rwin, A_DIM);
                                mvwprintw(rwin, row + 2, path_col, "%.*s",
                                          avail, ent->full_path);
                                wattroff(rwin, A_DIM);
                            }
                        }

                        if (is_cursor && es->panel.focus == 1) {
                            wattroff(rwin, A_REVERSE);
                        }
                    }
                } else {
                    const char *msg = "No entities";
                    int msg_len = (int)strlen(msg);
                    wattron(rwin, A_DIM);
                    mvwprintw(rwin, rh / 2 + 1, (rw - msg_len) / 2 + 1, "%s", msg);
                    wattroff(rwin, A_DIM);
                }

                free(matches);
            }
        } else if (comp_name && (!state->entity_list || state->entity_list->count == 0)) {
            const char *msg = "Waiting for entity data...";
            int msg_len = (int)strlen(msg);
            wattron(rwin, A_DIM);
            mvwprintw(rwin, rh / 2 + 1, (rw - msg_len) / 2 + 1, "%s", msg);
            wattroff(rwin, A_DIM);
        }
    } else if (sel && state->entity_detail) {
        /* --- Branch B: Non-component entity selected -- show entity detail --- */
        if (strcmp(state->entity_detail->path, sel->full_path) == 0) {
            const entity_detail_t *detail = state->entity_detail;

            int group_count = count_groups(detail);
            ensure_comp_expanded(es, group_count);

            int total_rows = count_inspector_rows(detail, es->comp_expanded,
                                                   es->comp_expanded_count);
            es->inspector_scroll.total_items = total_rows;
            es->inspector_scroll.visible_rows = rh;
            scroll_ensure_visible(&es->inspector_scroll);

            int logical_row = 0;
            int group_idx = 0;

            /* Render components */
            if (detail->components && yyjson_is_obj(detail->components)) {
                size_t idx, max;
                yyjson_val *key, *val;
                yyjson_obj_foreach(detail->components, idx, max, key, val) {
                    bool exp = (group_idx < es->comp_expanded_count)
                                   ? es->comp_expanded[group_idx]
                                   : true;

                    /* Count rows without rendering */
                    int rows_for_this = 1;
                    if (exp && val && !yyjson_is_null(val)) {
                        if (yyjson_is_obj(val))
                            rows_for_this += (int)yyjson_obj_size(val);
                        else if (yyjson_is_arr(val))
                            rows_for_this += (int)yyjson_arr_size(val);
                        else
                            rows_for_this += 1;
                    }

                    if (logical_row + rows_for_this > es->inspector_scroll.scroll_offset &&
                        logical_row < es->inspector_scroll.scroll_offset + rh) {
                        int start_render = logical_row - es->inspector_scroll.scroll_offset + 1;
                        if (start_render < 1) start_render = 1;
                        json_render_component(rwin, yyjson_get_str(key), val,
                                              start_render, 1, rh + 1, rw, exp);
                    }
                    logical_row += rows_for_this;
                    group_idx++;
                }
            }

            /* Render Tags section */
            if (detail->tags && yyjson_is_arr(detail->tags) &&
                yyjson_arr_size(detail->tags) > 0) {
                bool tags_exp = (group_idx < es->comp_expanded_count)
                                    ? es->comp_expanded[group_idx]
                                    : true;
                int tag_rows = 1 + (tags_exp ? (int)yyjson_arr_size(detail->tags) : 0);

                if (logical_row + tag_rows > es->inspector_scroll.scroll_offset &&
                    logical_row < es->inspector_scroll.scroll_offset + rh) {
                    int start_render = logical_row - es->inspector_scroll.scroll_offset + 1;
                    if (start_render < 1) start_render = 1;

                    if (start_render <= rh) {
                        wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
                        mvwprintw(rwin, start_render, 1, "%s Tags", tags_exp ? "v" : ">");
                        wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

                        if (tags_exp) {
                            size_t tidx, tmax;
                            yyjson_val *tag;
                            int trow = start_render + 1;
                            yyjson_arr_foreach(detail->tags, tidx, tmax, tag) {
                                if (trow > rh) break;
                                if (yyjson_is_str(tag)) {
                                    wattron(rwin, COLOR_PAIR(CP_JSON_STRING));
                                    mvwprintw(rwin, trow, 3, "%s", yyjson_get_str(tag));
                                    wattroff(rwin, COLOR_PAIR(CP_JSON_STRING));
                                }
                                trow++;
                            }
                        }
                    }
                }
                logical_row += tag_rows;
                group_idx++;
            }

            /* Render Pairs section */
            if (detail->pairs && yyjson_is_obj(detail->pairs) &&
                yyjson_obj_size(detail->pairs) > 0) {
                bool pairs_exp = (group_idx < es->comp_expanded_count)
                                     ? es->comp_expanded[group_idx]
                                     : true;
                int pair_rows = 1 + (pairs_exp ? (int)yyjson_obj_size(detail->pairs) : 0);

                if (logical_row + pair_rows > es->inspector_scroll.scroll_offset &&
                    logical_row < es->inspector_scroll.scroll_offset + rh) {
                    int start_render = logical_row - es->inspector_scroll.scroll_offset + 1;
                    if (start_render < 1) start_render = 1;

                    if (start_render <= rh) {
                        wattron(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);
                        mvwprintw(rwin, start_render, 1, "%s Pairs", pairs_exp ? "v" : ">");
                        wattroff(rwin, COLOR_PAIR(CP_COMPONENT_HEADER) | A_BOLD);

                        if (pairs_exp) {
                            size_t pidx, pmax;
                            yyjson_val *pkey, *pval;
                            int prow = start_render + 1;
                            yyjson_obj_foreach(detail->pairs, pidx, pmax, pkey, pval) {
                                if (prow > rh) break;
                                wattron(rwin, COLOR_PAIR(CP_JSON_KEY));
                                mvwprintw(rwin, prow, 3, "%s", yyjson_get_str(pkey));
                                wattroff(rwin, COLOR_PAIR(CP_JSON_KEY));
                                wprintw(rwin, ": ");
                                json_render_value(rwin, pval, prow, getcurx(rwin),
                                                  rh + 1, rw);
                                prow++;
                            }
                        }
                    }
                }
                logical_row += pair_rows;
                group_idx++;
            }

            (void)logical_row;
        } else {
            const char *msg = "Loading...";
            int msg_len = (int)strlen(msg);
            wattron(rwin, A_DIM);
            mvwprintw(rwin, rh / 2, (rw - msg_len) / 2 + 1, "%s", msg);
            wattroff(rwin, A_DIM);
        }
    } else if (!sel) {
        /* --- Branch C: Section header selected or no data --- */
        const char *msg = "Select an entity";
        int msg_len = (int)strlen(msg);
        wattron(rwin, A_DIM);
        mvwprintw(rwin, rh / 2, (rw - msg_len) / 2 + 1, "%s", msg);
        wattroff(rwin, A_DIM);
    } else {
        const char *msg = "Loading...";
        int msg_len = (int)strlen(msg);
        wattron(rwin, A_DIM);
        mvwprintw(rwin, rh / 2, (rw - msg_len) / 2 + 1, "%s", msg);
        wattroff(rwin, A_DIM);
    }

    split_panel_refresh(&es->panel);
}

/* --- Input --- */

bool tab_ecs_input(tab_t *self, int ch, void *app_state) {
    ecs_state_t *es = (ecs_state_t *)self->state;
    if (!es) return false;

    app_state_t *state = (app_state_t *)app_state;

    /* Focus switching: left/right arrows */
    if (split_panel_handle_focus(&es->panel, ch)) return true;

    /* Left panel focused: entity tree navigation */
    if (es->panel.focus == 0) {
        switch (ch) {
        case KEY_UP:
        case 'k':
            scroll_move(&es->tree.scroll, -1);
            sync_selected_path(es, state);
            return true;

        case KEY_DOWN:
        case 'j':
            scroll_move(&es->tree.scroll, +1);
            sync_selected_path(es, state);
            return true;

        case KEY_PPAGE:
            scroll_page(&es->tree.scroll, -1);
            sync_selected_path(es, state);
            return true;

        case KEY_NPAGE:
            scroll_page(&es->tree.scroll, +1);
            sync_selected_path(es, state);
            return true;

        case 'g':
            scroll_to_top(&es->tree.scroll);
            sync_selected_path(es, state);
            return true;

        case 'G':
            scroll_to_bottom(&es->tree.scroll);
            sync_selected_path(es, state);
            return true;

        case KEY_ENTER:
        case '\n':
        case '\r':
            tree_view_toggle_expand(&es->tree, state->entity_list);
            return true;

        case 'f':
            tree_view_toggle_anonymous(&es->tree, state->entity_list);
            sync_selected_path(es, state);
            return true;
        }
    }

    /* Right panel focused: context-sensitive input */
    if (es->panel.focus == 1) {
        entity_node_t *sel = tree_view_selected(&es->tree);

        if (sel && sel->entity_class == ENTITY_CLASS_COMPONENT) {
            /* Component mode: scroll through entities-with-component list */
            switch (ch) {
            case KEY_UP:
            case 'k':
                scroll_move(&es->inspector_scroll, -1);
                return true;

            case KEY_DOWN:
            case 'j':
                scroll_move(&es->inspector_scroll, +1);
                return true;

            case KEY_PPAGE:
                scroll_page(&es->inspector_scroll, -1);
                return true;

            case KEY_NPAGE:
                scroll_page(&es->inspector_scroll, +1);
                return true;

            case 'g':
                scroll_to_top(&es->inspector_scroll);
                return true;

            case 'G':
                scroll_to_bottom(&es->inspector_scroll);
                return true;
            }
        } else {
            /* Entity detail mode: scroll + expand/collapse component groups */
            switch (ch) {
            case KEY_UP:
            case 'k':
                scroll_move(&es->inspector_scroll, -1);
                return true;

            case KEY_DOWN:
            case 'j':
                scroll_move(&es->inspector_scroll, +1);
                return true;

            case KEY_PPAGE:
                scroll_page(&es->inspector_scroll, -1);
                return true;

            case KEY_NPAGE:
                scroll_page(&es->inspector_scroll, +1);
                return true;

            case KEY_ENTER:
            case '\n':
            case '\r':
                if (state->entity_detail) {
                    int gi = cursor_to_group_index(state->entity_detail,
                                                   es->comp_expanded,
                                                   es->comp_expanded_count,
                                                   es->inspector_scroll.cursor);
                    if (gi >= 0 && gi < es->comp_expanded_count) {
                        es->comp_expanded[gi] = !es->comp_expanded[gi];
                    }
                }
                return true;
            }
        }
    }

    return false;
}
