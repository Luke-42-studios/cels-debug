#include "tab_components.h"
#include "../tui.h"
#include "../split_panel.h"
#include "../scroll.h"
#include "../data_model.h"
#include <stdlib.h>
#include <string.h>

/* Per-tab private state */
typedef struct components_state {
    split_panel_t panel;            /* Left/right split */
    scroll_state_t left_scroll;     /* Scroll for component type list */
    scroll_state_t right_scroll;    /* Scroll for entities with component */
    bool panel_created;
} components_state_t;

/* --- qsort comparator: sort component_info_t by name alphabetically --- */

static int compare_components(const void *a, const void *b) {
    const component_info_t *ca = (const component_info_t *)a;
    const component_info_t *cb = (const component_info_t *)b;
    if (!ca->name && !cb->name) return 0;
    if (!ca->name) return -1;
    if (!cb->name) return 1;
    return strcmp(ca->name, cb->name);
}

/* --- Lifecycle --- */

void tab_components_init(tab_t *self) {
    components_state_t *cs = calloc(1, sizeof(components_state_t));
    if (!cs) return;

    scroll_reset(&cs->left_scroll);
    scroll_reset(&cs->right_scroll);
    cs->panel_created = false;

    self->state = cs;
}

void tab_components_fini(tab_t *self) {
    components_state_t *cs = (components_state_t *)self->state;
    if (!cs) return;

    if (cs->panel_created) {
        split_panel_destroy(&cs->panel);
    }
    free(cs);
    self->state = NULL;
}

/* --- Draw --- */

void tab_components_draw(const tab_t *self, WINDOW *win, const void *app_state) {
    components_state_t *cs = (components_state_t *)self->state;
    if (!cs) return;

    const app_state_t *state = (const app_state_t *)app_state;

    int h = getmaxy(win);
    int w = getmaxx(win);

    /* Create or resize split panel */
    if (!cs->panel_created) {
        split_panel_create(&cs->panel, h, w, getbegy(win));
        cs->panel_created = true;
    } else if (h != cs->panel.height ||
               w != cs->panel.left_width + cs->panel.right_width) {
        split_panel_resize(&cs->panel, h, w, getbegy(win));
    }

    /* Erase and draw borders */
    werase(cs->panel.left);
    werase(cs->panel.right);
    split_panel_draw_borders(&cs->panel, "Components", "Entities");

    WINDOW *lwin = cs->panel.left;
    WINDOW *rwin = cs->panel.right;
    int lh = getmaxy(lwin) - 2;  /* usable rows inside border */
    int lw = getmaxx(lwin) - 2;  /* usable cols inside border */
    int rh = getmaxy(rwin) - 2;
    int rw = getmaxx(rwin) - 2;

    /* --- Left panel: component registry --- */
    if (state->component_registry && state->component_registry->count > 0) {
        component_registry_t *reg = state->component_registry;

        /* Sort alphabetically by name */
        qsort(reg->components, (size_t)reg->count,
              sizeof(component_info_t), compare_components);

        /* Update scroll state */
        cs->left_scroll.total_items = reg->count;
        cs->left_scroll.visible_rows = lh;
        scroll_ensure_visible(&cs->left_scroll);

        /* Render visible component types */
        for (int row = 0; row < lh && cs->left_scroll.scroll_offset + row < reg->count; row++) {
            int idx = cs->left_scroll.scroll_offset + row;
            component_info_t *comp = &reg->components[idx];

            bool is_cursor = (idx == cs->left_scroll.cursor);

            if (is_cursor && cs->panel.focus == 0) {
                wattron(lwin, A_REVERSE);
            }

            /* Clear the row inside border */
            wmove(lwin, row + 1, 1);
            for (int c = 0; c < lw; c++) waddch(lwin, ' ');

            /* Component name left-aligned */
            wattron(lwin, COLOR_PAIR(CP_ENTITY_NAME));
            mvwprintw(lwin, row + 1, 2, "%.*s", lw - 2, comp->name ? comp->name : "(unnamed)");
            wattroff(lwin, COLOR_PAIR(CP_ENTITY_NAME));

            /* Entity count + optional size right-aligned */
            char info[64];
            if (comp->has_type_info && comp->size > 0) {
                snprintf(info, sizeof(info), "%d (%dB)", comp->entity_count, comp->size);
            } else {
                snprintf(info, sizeof(info), "%d", comp->entity_count);
            }
            int info_len = (int)strlen(info);
            int info_col = lw - info_len;
            if (info_col < 2) info_col = 2;

            wattron(lwin, A_DIM);
            mvwprintw(lwin, row + 1, info_col, "%s", info);
            wattroff(lwin, A_DIM);

            if (is_cursor && cs->panel.focus == 0) {
                wattroff(lwin, A_REVERSE);
            }
        }
    } else if (state->component_registry && state->component_registry->count == 0) {
        const char *msg = "No components";
        int msg_len = (int)strlen(msg);
        wattron(lwin, A_DIM);
        mvwprintw(lwin, lh / 2 + 1, (lw - msg_len) / 2 + 1, "%s", msg);
        wattroff(lwin, A_DIM);
    } else {
        const char *msg = "Waiting for data...";
        int msg_len = (int)strlen(msg);
        wattron(lwin, A_DIM);
        mvwprintw(lwin, lh / 2 + 1, (lw - msg_len) / 2 + 1, "%s", msg);
        wattroff(lwin, A_DIM);
    }

    /* --- Right panel: entities with selected component --- */
    if (state->component_registry && state->component_registry->count > 0 &&
        cs->left_scroll.cursor >= 0 &&
        cs->left_scroll.cursor < state->component_registry->count) {

        const char *sel_name = state->component_registry->components[cs->left_scroll.cursor].name;

        if (sel_name && state->entity_list && state->entity_list->count > 0) {
            /* Filter entities that have the selected component */
            entity_list_t *elist = state->entity_list;

            /* Temporary array of matching entity pointers (stack-allocated up to a limit) */
            int match_cap = elist->count;
            entity_node_t **matches = malloc((size_t)match_cap * sizeof(entity_node_t *));
            int match_count = 0;

            if (matches) {
                for (int i = 0; i < elist->count; i++) {
                    entity_node_t *node = elist->nodes[i];
                    for (int c = 0; c < node->component_count; c++) {
                        if (node->component_names[c] &&
                            strcmp(node->component_names[c], sel_name) == 0) {
                            matches[match_count++] = node;
                            break;
                        }
                    }
                }

                /* Update right scroll */
                cs->right_scroll.total_items = match_count;
                cs->right_scroll.visible_rows = rh;
                scroll_ensure_visible(&cs->right_scroll);

                if (match_count > 0) {
                    /* Render visible matching entities */
                    for (int row = 0; row < rh && cs->right_scroll.scroll_offset + row < match_count; row++) {
                        int idx = cs->right_scroll.scroll_offset + row;
                        entity_node_t *ent = matches[idx];

                        bool is_cursor = (idx == cs->right_scroll.cursor);

                        if (is_cursor && cs->panel.focus == 1) {
                            wattron(rwin, A_REVERSE);
                        }

                        /* Clear the row inside border */
                        wmove(rwin, row + 1, 1);
                        for (int c = 0; c < rw; c++) waddch(rwin, ' ');

                        /* Entity name (or #<id> for anonymous) */
                        const char *display_name;
                        char id_buf[32];
                        if (ent->name && strlen(ent->name) > 0) {
                            display_name = ent->name;
                        } else {
                            snprintf(id_buf, sizeof(id_buf), "#%lu", (unsigned long)ent->id);
                            display_name = id_buf;
                        }

                        wattron(rwin, COLOR_PAIR(CP_ENTITY_NAME));
                        mvwprintw(rwin, row + 1, 2, "%.*s", rw / 2, display_name);
                        wattroff(rwin, COLOR_PAIR(CP_ENTITY_NAME));

                        /* Full path in dim to the right (truncated to fit) */
                        if (ent->full_path) {
                            int name_end = getcurx(rwin);
                            int path_col = name_end + 1;
                            int avail = rw - (path_col - 1);
                            if (avail > 2) {
                                wattron(rwin, A_DIM);
                                mvwprintw(rwin, row + 1, path_col, "%.*s", avail, ent->full_path);
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
        } else if (sel_name && (!state->entity_list || state->entity_list->count == 0)) {
            const char *msg = "Waiting for entity data...";
            int msg_len = (int)strlen(msg);
            wattron(rwin, A_DIM);
            mvwprintw(rwin, rh / 2 + 1, (rw - msg_len) / 2 + 1, "%s", msg);
            wattroff(rwin, A_DIM);
        }
    } else {
        const char *msg = "Select a component";
        int msg_len = (int)strlen(msg);
        wattron(rwin, A_DIM);
        mvwprintw(rwin, rh / 2 + 1, (rw - msg_len) / 2 + 1, "%s", msg);
        wattroff(rwin, A_DIM);
    }

    split_panel_refresh(&cs->panel);
}

/* --- Input --- */

bool tab_components_input(tab_t *self, int ch, void *app_state) {
    components_state_t *cs = (components_state_t *)self->state;
    if (!cs) return false;

    (void)app_state;

    /* Focus switching: left/right arrows */
    if (split_panel_handle_focus(&cs->panel, ch)) return true;

    /* Left panel focused: component list navigation */
    if (cs->panel.focus == 0) {
        switch (ch) {
        case KEY_UP:
        case 'k':
            scroll_move(&cs->left_scroll, -1);
            /* Reset right panel cursor when selecting a new component */
            cs->right_scroll.cursor = 0;
            cs->right_scroll.scroll_offset = 0;
            return true;

        case KEY_DOWN:
        case 'j':
            scroll_move(&cs->left_scroll, +1);
            cs->right_scroll.cursor = 0;
            cs->right_scroll.scroll_offset = 0;
            return true;

        case KEY_PPAGE:
            scroll_page(&cs->left_scroll, -1);
            cs->right_scroll.cursor = 0;
            cs->right_scroll.scroll_offset = 0;
            return true;

        case KEY_NPAGE:
            scroll_page(&cs->left_scroll, +1);
            cs->right_scroll.cursor = 0;
            cs->right_scroll.scroll_offset = 0;
            return true;

        case 'g':
            scroll_to_top(&cs->left_scroll);
            cs->right_scroll.cursor = 0;
            cs->right_scroll.scroll_offset = 0;
            return true;

        case 'G':
            scroll_to_bottom(&cs->left_scroll);
            cs->right_scroll.cursor = 0;
            cs->right_scroll.scroll_offset = 0;
            return true;
        }
    }

    /* Right panel focused: entity list navigation */
    if (cs->panel.focus == 1) {
        switch (ch) {
        case KEY_UP:
        case 'k':
            scroll_move(&cs->right_scroll, -1);
            return true;

        case KEY_DOWN:
        case 'j':
            scroll_move(&cs->right_scroll, +1);
            return true;

        case KEY_PPAGE:
            scroll_page(&cs->right_scroll, -1);
            return true;

        case KEY_NPAGE:
            scroll_page(&cs->right_scroll, +1);
            return true;

        case 'g':
            scroll_to_top(&cs->right_scroll);
            return true;

        case 'G':
            scroll_to_bottom(&cs->right_scroll);
            return true;
        }
    }

    return false;
}
