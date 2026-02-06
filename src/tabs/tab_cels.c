#define _POSIX_C_SOURCE 200809L

#include "tab_cels.h"
#include "../tui.h"
#include "../split_panel.h"
#include "../tree_view.h"
#include "../json_render.h"
#include "../scroll.h"
#include "../data_model.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Per-tab private state */
typedef struct cels_state {
    split_panel_t panel;             /* Left/right split */
    tree_view_t tree;                /* Entity tree + virtual scrolling */
    scroll_state_t inspector_scroll; /* Scroll state for component inspector */
    bool *comp_expanded;             /* Expand/collapse state per component group */
    int comp_expanded_count;         /* Number of component groups allocated */
    bool panel_created;              /* Whether split_panel windows exist */

    /* State entity change highlighting */
    char *prev_entity_json;          /* serialized previous component values */
    char *prev_entity_path;          /* which entity the prev_json belongs to */
    int64_t flash_expire_ms;         /* CLOCK_MONOTONIC ms when flash ends (0 = no flash) */
} cels_state_t;

/* Helper: get current monotonic time in milliseconds */
static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

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

/* Check if name ends with "State" (case-sensitive) */
static bool name_ends_with_state(entity_node_t *node) {
    if (!node->name) return false;
    const char *suffix = "State";
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
    /* S: State -- entities whose name ends with "State" (v0.1 heuristic) */
    if (name_ends_with_state(node)) {
        return ENTITY_CLASS_STATE;
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

/* Reclassify system entities as generic entities so the CELS tab
 * does not show a Systems section. The standalone Systems tab handles
 * system display instead. */
static void hide_systems_from_tree(entity_list_t *list) {
    if (!list) return;
    for (int i = 0; i < list->root_count; i++) {
        if (list->roots[i]->entity_class == ENTITY_CLASS_SYSTEM) {
            propagate_class(list->roots[i], ENTITY_CLASS_ENTITY);
        }
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

static void ensure_comp_expanded(cels_state_t *cs, int needed) {
    if (cs->comp_expanded_count >= needed) return;

    bool *new_arr = realloc(cs->comp_expanded, (size_t)needed * sizeof(bool));
    if (!new_arr) return;

    for (int i = cs->comp_expanded_count; i < needed; i++) {
        new_arr[i] = true;
    }
    cs->comp_expanded = new_arr;
    cs->comp_expanded_count = needed;
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

static void sync_selected_path(cels_state_t *cs, app_state_t *state) {
    entity_node_t *sel = tree_view_selected(&cs->tree);
    free(state->selected_entity_path);
    state->selected_entity_path = sel ? strdup(sel->full_path) : NULL;

    if (state->entity_detail && sel &&
        strcmp(state->entity_detail->path, sel->full_path) != 0) {
        entity_detail_free(state->entity_detail);
        state->entity_detail = NULL;
    }
}

/* --- Helper: cross-navigate from inspector to entity in tree --- */

static bool cross_navigate_to_entity(cels_state_t *cs, app_state_t *state,
                                      const char *entity_path) {
    if (!entity_path || !state->entity_list) return false;

    /* 1. Ensure Entities section is expanded */
    cs->tree.section_collapsed[ENTITY_CLASS_ENTITY] = false;

    /* 2. Rebuild visible rows to include Entities section items */
    tree_view_rebuild_visible(&cs->tree, state->entity_list);

    /* 3. Find the target entity in the display list */
    for (int i = 0; i < cs->tree.row_count; i++) {
        entity_node_t *node = cs->tree.rows[i].node;
        if (node && node->full_path &&
            strcmp(node->full_path, entity_path) == 0) {
            cs->tree.scroll.cursor = i;
            scroll_ensure_visible(&cs->tree.scroll);

            /* 4. Update selected path for detail polling */
            free(state->selected_entity_path);
            state->selected_entity_path = strdup(entity_path);

            /* 5. Switch focus to left panel */
            cs->panel.focus = 0;
            return true;
        }
    }

    /* Try expanding Compositions section too */
    cs->tree.section_collapsed[ENTITY_CLASS_COMPOSITION] = false;
    tree_view_rebuild_visible(&cs->tree, state->entity_list);

    for (int i = 0; i < cs->tree.row_count; i++) {
        entity_node_t *node = cs->tree.rows[i].node;
        if (node && node->full_path &&
            strcmp(node->full_path, entity_path) == 0) {
            cs->tree.scroll.cursor = i;
            scroll_ensure_visible(&cs->tree.scroll);
            free(state->selected_entity_path);
            state->selected_entity_path = strdup(entity_path);
            cs->panel.focus = 0;
            return true;
        }
    }

    /* Not found -- show footer message */
    free(state->footer_message);
    state->footer_message = strdup("Entity not found");
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    state->footer_message_expire = ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + 3000;
    return false;
}

/* --- Lifecycle --- */

void tab_cels_init(tab_t *self) {
    cels_state_t *cs = calloc(1, sizeof(cels_state_t));
    if (!cs) return;

    tree_view_init(&cs->tree);
    scroll_reset(&cs->inspector_scroll);
    cs->panel_created = false;
    cs->comp_expanded = NULL;
    cs->comp_expanded_count = 0;

    self->state = cs;
}

void tab_cels_fini(tab_t *self) {
    cels_state_t *cs = (cels_state_t *)self->state;
    if (!cs) return;

    if (cs->panel_created) {
        split_panel_destroy(&cs->panel);
    }
    tree_view_fini(&cs->tree);
    free(cs->comp_expanded);
    free(cs->prev_entity_json);
    free(cs->prev_entity_path);
    free(cs);
    self->state = NULL;
}

/* --- Draw --- */

void tab_cels_draw(const tab_t *self, WINDOW *win, const void *app_state) {
    cels_state_t *cs = (cels_state_t *)self->state;
    if (!cs) return;

    const app_state_t *state = (const app_state_t *)app_state;

    int h = getmaxy(win);
    int w = getmaxx(win);

    if (!cs->panel_created) {
        split_panel_create(&cs->panel, h, w, getbegy(win));
        cs->panel_created = true;
    } else if (h != cs->panel.height ||
               w != cs->panel.left_width + cs->panel.right_width) {
        split_panel_resize(&cs->panel, h, w, getbegy(win));
    }

    werase(cs->panel.left);
    werase(cs->panel.right);
    split_panel_draw_borders(&cs->panel, "CELS", "Inspector");

    /* --- Left panel: entity tree --- */
    if (state->entity_list) {
        /* Classify entities into CELS-C sections */
        classify_all_entities(state->entity_list);

        /* Hide system entities from the CELS tab tree -- the standalone
         * Systems tab handles system display. Reclassify as ENTITY_CLASS_ENTITY
         * so they appear in the Entities section instead. */
        hide_systems_from_tree(state->entity_list);

        /* Annotate component entities with registry data (entity count, size) */
        annotate_component_entities(state->entity_list, state->component_registry);

        tree_view_rebuild_visible(&cs->tree, state->entity_list);

        /* Auto-select first entity if nothing selected yet */
        if (!state->selected_entity_path && cs->tree.row_count > 0) {
            for (int i = 0; i < cs->tree.row_count; i++) {
                entity_node_t *n = cs->tree.rows[i].node;
                if (n && n->full_path) {
                    app_state_t *mut_state = (app_state_t *)state;
                    mut_state->selected_entity_path = strdup(n->full_path);
                    break;
                }
            }
        }

        tree_view_render(&cs->tree, cs->panel.left);
    } else {
        const char *msg = "Waiting for data...";
        int msg_len = (int)strlen(msg);
        int max_y = getmaxy(cs->panel.left);
        int max_x = getmaxx(cs->panel.left);
        wattron(cs->panel.left, A_DIM);
        mvwprintw(cs->panel.left, max_y / 2, (max_x - msg_len) / 2, "%s", msg);
        wattroff(cs->panel.left, A_DIM);
    }

    /* --- Right panel: context-sensitive inspector --- */
    entity_node_t *sel = tree_view_selected(&cs->tree);
    WINDOW *rwin = cs->panel.right;
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
                cs->inspector_scroll.total_items = match_count;
                cs->inspector_scroll.visible_rows = rh - 1; /* minus header row */
                scroll_ensure_visible(&cs->inspector_scroll);

                if (match_count > 0) {
                    /* Render visible matching entities */
                    int avail_rows = rh - 1;
                    for (int row = 0; row < avail_rows &&
                         cs->inspector_scroll.scroll_offset + row < match_count; row++) {
                        int idx = cs->inspector_scroll.scroll_offset + row;
                        entity_node_t *ent = matches[idx];

                        bool is_cursor = (idx == cs->inspector_scroll.cursor);

                        if (is_cursor && cs->panel.focus == 1) {
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

                        if (is_cursor && cs->panel.focus == 1) {
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

            /* --- Change highlighting for State entities --- */
            bool flash_active = false;
            if (sel->entity_class == ENTITY_CLASS_STATE && detail->components) {
                /* Serialize current component values for comparison */
                char *cur_json = yyjson_val_write(detail->components, 0, NULL);

                if (cs->prev_entity_path && cur_json &&
                    strcmp(cs->prev_entity_path, sel->full_path) == 0) {
                    /* Same entity -- compare values */
                    if (cs->prev_entity_json &&
                        strcmp(cs->prev_entity_json, cur_json) != 0) {
                        /* Values changed: start 2-second flash */
                        cs->flash_expire_ms = now_ms() + 2000;
                    }
                } else {
                    /* Different entity selected -- reset flash state */
                    cs->flash_expire_ms = 0;
                }

                /* Update stored previous values */
                free(cs->prev_entity_json);
                cs->prev_entity_json = cur_json; /* takes ownership */
                free(cs->prev_entity_path);
                cs->prev_entity_path = strdup(sel->full_path);

                /* Check if flash is still active */
                if (cs->flash_expire_ms > 0 && now_ms() < cs->flash_expire_ms) {
                    flash_active = true;
                }
            }

            if (flash_active) {
                wattron(rwin, A_BOLD | COLOR_PAIR(CP_RECONNECTING));
            }

            int group_count = count_groups(detail);
            ensure_comp_expanded(cs, group_count);

            int total_rows = count_inspector_rows(detail, cs->comp_expanded,
                                                   cs->comp_expanded_count);
            cs->inspector_scroll.total_items = total_rows;
            cs->inspector_scroll.visible_rows = rh;
            scroll_ensure_visible(&cs->inspector_scroll);

            int logical_row = 0;
            int group_idx = 0;

            /* Render components */
            if (detail->components && yyjson_is_obj(detail->components)) {
                size_t idx, max;
                yyjson_val *key, *val;
                yyjson_obj_foreach(detail->components, idx, max, key, val) {
                    bool exp = (group_idx < cs->comp_expanded_count)
                                   ? cs->comp_expanded[group_idx]
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

                    if (logical_row + rows_for_this > cs->inspector_scroll.scroll_offset &&
                        logical_row < cs->inspector_scroll.scroll_offset + rh) {
                        int start_render = logical_row - cs->inspector_scroll.scroll_offset + 1;
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
                bool tags_exp = (group_idx < cs->comp_expanded_count)
                                    ? cs->comp_expanded[group_idx]
                                    : true;
                int tag_rows = 1 + (tags_exp ? (int)yyjson_arr_size(detail->tags) : 0);

                if (logical_row + tag_rows > cs->inspector_scroll.scroll_offset &&
                    logical_row < cs->inspector_scroll.scroll_offset + rh) {
                    int start_render = logical_row - cs->inspector_scroll.scroll_offset + 1;
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
                bool pairs_exp = (group_idx < cs->comp_expanded_count)
                                     ? cs->comp_expanded[group_idx]
                                     : true;
                int pair_rows = 1 + (pairs_exp ? (int)yyjson_obj_size(detail->pairs) : 0);

                if (logical_row + pair_rows > cs->inspector_scroll.scroll_offset &&
                    logical_row < cs->inspector_scroll.scroll_offset + rh) {
                    int start_render = logical_row - cs->inspector_scroll.scroll_offset + 1;
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

            if (flash_active) {
                wattroff(rwin, A_BOLD | COLOR_PAIR(CP_RECONNECTING));
            }
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

    split_panel_refresh(&cs->panel);
}

/* --- Input --- */

bool tab_cels_input(tab_t *self, int ch, void *app_state) {
    cels_state_t *cs = (cels_state_t *)self->state;
    if (!cs) return false;

    app_state_t *state = (app_state_t *)app_state;

    /* Focus switching: left/right arrows */
    if (split_panel_handle_focus(&cs->panel, ch)) return true;

    /* Left panel focused: entity tree navigation */
    if (cs->panel.focus == 0) {
        switch (ch) {
        case KEY_UP:
        case 'k':
            scroll_move(&cs->tree.scroll, -1);
            sync_selected_path(cs, state);
            return true;

        case KEY_DOWN:
        case 'j':
            scroll_move(&cs->tree.scroll, +1);
            sync_selected_path(cs, state);
            return true;

        case KEY_PPAGE:
            scroll_page(&cs->tree.scroll, -1);
            sync_selected_path(cs, state);
            return true;

        case KEY_NPAGE:
            scroll_page(&cs->tree.scroll, +1);
            sync_selected_path(cs, state);
            return true;

        case 'g':
            scroll_to_top(&cs->tree.scroll);
            sync_selected_path(cs, state);
            return true;

        case 'G':
            scroll_to_bottom(&cs->tree.scroll);
            sync_selected_path(cs, state);
            return true;

        case KEY_ENTER:
        case '\n':
        case '\r':
            tree_view_toggle_expand(&cs->tree, state->entity_list);
            return true;

        case 'f':
            tree_view_toggle_anonymous(&cs->tree, state->entity_list);
            sync_selected_path(cs, state);
            return true;
        }
    }

    /* Right panel focused: context-sensitive input */
    if (cs->panel.focus == 1) {
        entity_node_t *sel = tree_view_selected(&cs->tree);

        if (sel && sel->entity_class == ENTITY_CLASS_COMPONENT) {
            /* Component mode: scroll through entities-with-component list */
            switch (ch) {
            case KEY_UP:
            case 'k':
                scroll_move(&cs->inspector_scroll, -1);
                return true;

            case KEY_DOWN:
            case 'j':
                scroll_move(&cs->inspector_scroll, +1);
                return true;

            case KEY_PPAGE:
                scroll_page(&cs->inspector_scroll, -1);
                return true;

            case KEY_NPAGE:
                scroll_page(&cs->inspector_scroll, +1);
                return true;

            case 'g':
                scroll_to_top(&cs->inspector_scroll);
                return true;

            case 'G':
                scroll_to_bottom(&cs->inspector_scroll);
                return true;
            }
        } else {
            /* Entity detail mode: scroll + expand/collapse component groups */
            switch (ch) {
            case KEY_UP:
            case 'k':
                scroll_move(&cs->inspector_scroll, -1);
                return true;

            case KEY_DOWN:
            case 'j':
                scroll_move(&cs->inspector_scroll, +1);
                return true;

            case KEY_PPAGE:
                scroll_page(&cs->inspector_scroll, -1);
                return true;

            case KEY_NPAGE:
                scroll_page(&cs->inspector_scroll, +1);
                return true;

            case KEY_ENTER:
            case '\n':
            case '\r':
                if (state->entity_detail) {
                    int gi = cursor_to_group_index(state->entity_detail,
                                                   cs->comp_expanded,
                                                   cs->comp_expanded_count,
                                                   cs->inspector_scroll.cursor);
                    if (gi >= 0 && gi < cs->comp_expanded_count) {
                        cs->comp_expanded[gi] = !cs->comp_expanded[gi];
                    }
                }
                return true;
            }
        }
    }

    return false;
}
