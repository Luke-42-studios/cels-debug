#define _POSIX_C_SOURCE 200809L

#include "tab_entities.h"
#include "../tui.h"
#include "../split_panel.h"
#include "../tree_view.h"
#include "../json_render.h"
#include "../scroll.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

/* Per-tab private state */
typedef struct entities_state {
    split_panel_t panel;             /* Left/right split */
    tree_view_t tree;                /* Entity tree + virtual scrolling */
    scroll_state_t inspector_scroll; /* Scroll state for component inspector */
    bool *comp_expanded;             /* Expand/collapse state per component group */
    int comp_expanded_count;         /* Number of component groups allocated */
    bool panel_created;              /* Whether split_panel windows exist */
} entities_state_t;

/* --- Helper: count inspector content rows for scroll total --- */

static int count_inspector_rows(const entity_detail_t *detail,
                                const bool *expanded, int expanded_count) {
    int rows = 0;

    /* Components */
    if (detail->components && yyjson_is_obj(detail->components)) {
        size_t idx, max;
        yyjson_val *key, *val;
        int comp_idx = 0;
        yyjson_obj_foreach(detail->components, idx, max, key, val) {
            rows++; /* header row */
            bool exp = (comp_idx < expanded_count) ? expanded[comp_idx] : true;
            if (exp && val && !yyjson_is_null(val)) {
                /* Estimate rows for value: rough count based on type */
                if (yyjson_is_obj(val)) {
                    rows += (int)yyjson_obj_size(val);
                } else if (yyjson_is_arr(val)) {
                    rows += (int)yyjson_arr_size(val);
                } else {
                    rows += 1;
                }
            }
            comp_idx++;
        }
    }

    /* Tags section */
    if (detail->tags && yyjson_is_arr(detail->tags) && yyjson_arr_size(detail->tags) > 0) {
        rows++; /* "Tags" header */
        rows += (int)yyjson_arr_size(detail->tags);
    }

    /* Pairs section */
    if (detail->pairs && yyjson_is_obj(detail->pairs) && yyjson_obj_size(detail->pairs) > 0) {
        rows++; /* "Pairs" header */
        rows += (int)yyjson_obj_size(detail->pairs);
    }

    return rows;
}

/* --- Helper: ensure comp_expanded array matches component count --- */

static void ensure_comp_expanded(entities_state_t *es, int needed) {
    if (es->comp_expanded_count >= needed) return;

    bool *new_arr = realloc(es->comp_expanded, (size_t)needed * sizeof(bool));
    if (!new_arr) return;

    /* Default new entries to expanded (true) */
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
        count++; /* Tags group */
    }
    if (detail->pairs && yyjson_is_obj(detail->pairs) && yyjson_obj_size(detail->pairs) > 0) {
        count++; /* Pairs group */
    }
    return count;
}

/* --- Helper: find which group index corresponds to the current inspector cursor --- */

static int cursor_to_group_index(const entity_detail_t *detail,
                                 const bool *expanded, int expanded_count,
                                 int cursor_row) {
    int row = 0;
    int group_idx = 0;

    /* Components */
    if (detail->components && yyjson_is_obj(detail->components)) {
        size_t idx, max;
        yyjson_val *key, *val;
        yyjson_obj_foreach(detail->components, idx, max, key, val) {
            if (row == cursor_row) return group_idx;
            row++; /* header */
            bool exp = (group_idx < expanded_count) ? expanded[group_idx] : true;
            if (exp && val && !yyjson_is_null(val)) {
                if (yyjson_is_obj(val)) {
                    row += (int)yyjson_obj_size(val);
                } else if (yyjson_is_arr(val)) {
                    row += (int)yyjson_arr_size(val);
                } else {
                    row += 1;
                }
            }
            group_idx++;
        }
    }

    /* Tags */
    if (detail->tags && yyjson_is_arr(detail->tags) && yyjson_arr_size(detail->tags) > 0) {
        if (row == cursor_row) return group_idx;
        row++;
        row += (int)yyjson_arr_size(detail->tags);
        group_idx++;
    }

    /* Pairs */
    if (detail->pairs && yyjson_is_obj(detail->pairs) && yyjson_obj_size(detail->pairs) > 0) {
        if (row == cursor_row) return group_idx;
        /* row and group_idx not needed after this */
        group_idx++;
    }

    return -1; /* cursor is not on a header row */
}

/* --- Helper: update selected_entity_path from tree cursor --- */

static void sync_selected_path(entities_state_t *es, app_state_t *state) {
    entity_node_t *sel = tree_view_selected(&es->tree);
    free(state->selected_entity_path);
    state->selected_entity_path = sel ? strdup(sel->full_path) : NULL;

    /* Clear stale detail if entity changed */
    if (state->entity_detail && sel &&
        strcmp(state->entity_detail->path, sel->full_path) != 0) {
        entity_detail_free(state->entity_detail);
        state->entity_detail = NULL;
    }
}

/* --- Lifecycle --- */

void tab_entities_init(tab_t *self) {
    entities_state_t *es = calloc(1, sizeof(entities_state_t));
    if (!es) return;

    tree_view_init(&es->tree);
    scroll_reset(&es->inspector_scroll);
    es->panel_created = false;
    es->comp_expanded = NULL;
    es->comp_expanded_count = 0;

    self->state = es;
}

void tab_entities_fini(tab_t *self) {
    entities_state_t *es = (entities_state_t *)self->state;
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

void tab_entities_draw(const tab_t *self, WINDOW *win, const void *app_state) {
    entities_state_t *es = (entities_state_t *)self->state;
    if (!es) return;

    const app_state_t *state = (const app_state_t *)app_state;

    int h = getmaxy(win);
    int w = getmaxx(win);

    /* Create or resize split panel */
    if (!es->panel_created) {
        split_panel_create(&es->panel, h, w, getbegy(win));
        es->panel_created = true;
    } else if (h != es->panel.height ||
               w != es->panel.left_width + es->panel.right_width) {
        split_panel_resize(&es->panel, h, w, getbegy(win));
    }

    /* Erase and draw borders */
    werase(es->panel.left);
    werase(es->panel.right);
    split_panel_draw_borders(&es->panel, "Entities", "Inspector");

    /* --- Left panel: entity tree --- */
    if (state->entity_list) {
        tree_view_rebuild_visible(&es->tree, state->entity_list);

        /* Auto-select first entity if nothing selected yet */
        if (!state->selected_entity_path && es->tree.visible_count > 0) {
            entity_node_t *first = es->tree.visible[0];
            if (first && first->full_path) {
                /* Cast away const for this auto-select on first display */
                app_state_t *mut_state = (app_state_t *)state;
                mut_state->selected_entity_path = strdup(first->full_path);
            }
        }

        tree_view_render(&es->tree, es->panel.left);
    } else {
        /* No data yet */
        const char *msg = "Waiting for data...";
        int msg_len = (int)strlen(msg);
        int max_y = getmaxy(es->panel.left);
        int max_x = getmaxx(es->panel.left);
        wattron(es->panel.left, A_DIM);
        mvwprintw(es->panel.left, max_y / 2, (max_x - msg_len) / 2, "%s", msg);
        wattroff(es->panel.left, A_DIM);
    }

    /* --- Right panel: component inspector --- */
    entity_node_t *sel = tree_view_selected(&es->tree);
    WINDOW *rwin = es->panel.right;
    int rh = getmaxy(rwin) - 2;  /* usable rows inside border */
    int rw = getmaxx(rwin) - 2;  /* usable cols inside border */

    if (sel && state->entity_detail) {
        /* Check if detail matches selected entity */
        if (strcmp(state->entity_detail->path, sel->full_path) == 0) {
            const entity_detail_t *detail = state->entity_detail;

            /* Ensure expand array is sized for all groups */
            int group_count = count_groups(detail);
            ensure_comp_expanded(es, group_count);

            /* Update inspector scroll total */
            int total_rows = count_inspector_rows(detail, es->comp_expanded,
                                                   es->comp_expanded_count);
            es->inspector_scroll.total_items = total_rows;
            es->inspector_scroll.visible_rows = rh;
            scroll_ensure_visible(&es->inspector_scroll);

            int render_row = 1;  /* start after top border */
            int logical_row = 0; /* track for scroll offset */
            int group_idx = 0;

            /* Render components */
            if (detail->components && yyjson_is_obj(detail->components)) {
                size_t idx, max;
                yyjson_val *key, *val;
                yyjson_obj_foreach(detail->components, idx, max, key, val) {
                    bool exp = (group_idx < es->comp_expanded_count)
                                   ? es->comp_expanded[group_idx]
                                   : true;
                    int rows_for_this = json_render_component(
                        NULL, yyjson_get_str(key), val, 0, 0, 9999, rw, exp);

                    /* Only render rows within the visible scroll window */
                    if (logical_row + rows_for_this > es->inspector_scroll.scroll_offset &&
                        logical_row < es->inspector_scroll.scroll_offset + rh) {
                        int skip = 0;
                        if (logical_row < es->inspector_scroll.scroll_offset) {
                            skip = es->inspector_scroll.scroll_offset - logical_row;
                        }
                        /* Render directly at the appropriate row */
                        int start_render = logical_row - es->inspector_scroll.scroll_offset + 1;
                        if (start_render < 1) start_render = 1;
                        (void)skip; /* simple approach: render full component */
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

                    /* Tags header */
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

                    /* Pairs header */
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

            (void)render_row;
            (void)logical_row;
        } else {
            /* Detail is for a different entity, still loading */
            const char *msg = "Loading...";
            int msg_len = (int)strlen(msg);
            wattron(rwin, A_DIM);
            mvwprintw(rwin, rh / 2, (rw - msg_len) / 2 + 1, "%s", msg);
            wattroff(rwin, A_DIM);
        }
    } else if (!sel) {
        const char *msg = "Select an entity";
        int msg_len = (int)strlen(msg);
        wattron(rwin, A_DIM);
        mvwprintw(rwin, rh / 2, (rw - msg_len) / 2 + 1, "%s", msg);
        wattroff(rwin, A_DIM);
    } else {
        /* Entity selected but no detail yet */
        const char *msg = "Loading...";
        int msg_len = (int)strlen(msg);
        wattron(rwin, A_DIM);
        mvwprintw(rwin, rh / 2, (rw - msg_len) / 2 + 1, "%s", msg);
        wattroff(rwin, A_DIM);
    }

    split_panel_refresh(&es->panel);
}

/* --- Input --- */

bool tab_entities_input(tab_t *self, int ch, void *app_state) {
    entities_state_t *es = (entities_state_t *)self->state;
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

    /* Right panel focused: inspector scrolling and expand/collapse */
    if (es->panel.focus == 1) {
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
            /* Toggle expand/collapse on the component group at cursor */
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

    return false;
}
