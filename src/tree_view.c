#include "tree_view.h"
#include "tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* UTF-8 box drawing characters */
#define TREE_VERT    "\xe2\x94\x82"   /* U+2502 vertical line */
#define TREE_BRANCH  "\xe2\x94\x9c"   /* U+251C branch (has sibling below) */
#define TREE_LAST    "\xe2\x94\x94"   /* U+2514 last child corner */
#define TREE_HORIZ   "\xe2\x94\x80"   /* U+2500 horizontal line */

/* --- Helpers --- */

static bool node_is_last_child(entity_node_t *node) {
    if (!node->parent) return true;  /* root nodes: treat as last */
    return node->parent->children[node->parent->child_count - 1] == node;
}

static bool ancestor_has_next_sibling(entity_node_t *node, int target_depth) {
    /* Walk up from node to find ancestor at target_depth */
    entity_node_t *ancestor = node;
    while (ancestor && ancestor->depth > target_depth) {
        ancestor = ancestor->parent;
    }
    if (!ancestor) return false;
    return !node_is_last_child(ancestor);
}

/* DFS traversal: add visible nodes to tv->visible[] */
static void dfs_collect(tree_view_t *tv, entity_node_t *node) {
    /* Skip anonymous entities (and their subtrees) if not showing them */
    if (node->is_anonymous && !tv->show_anonymous) return;

    tv->visible[tv->visible_count++] = node;

    if (node->expanded) {
        for (int i = 0; i < node->child_count; i++) {
            dfs_collect(tv, node->children[i]);
        }
    }
}

/* --- Public API --- */

void tree_view_init(tree_view_t *tv) {
    tv->visible = NULL;
    tv->visible_count = 0;
    scroll_reset(&tv->scroll);
    tv->show_anonymous = false;
    tv->prev_selected_id = 0;
}

void tree_view_fini(tree_view_t *tv) {
    free(tv->visible);
    tv->visible = NULL;
    tv->visible_count = 0;
    scroll_reset(&tv->scroll);
    tv->prev_selected_id = 0;
}

void tree_view_rebuild_visible(tree_view_t *tv, entity_list_t *list) {
    if (!list) {
        free(tv->visible);
        tv->visible = NULL;
        tv->visible_count = 0;
        tv->scroll.total_items = 0;
        scroll_ensure_visible(&tv->scroll);
        return;
    }

    /* Remember which entity was selected */
    uint64_t prev_id = 0;
    if (tv->visible && tv->visible_count > 0 &&
        tv->scroll.cursor >= 0 && tv->scroll.cursor < tv->visible_count) {
        prev_id = tv->visible[tv->scroll.cursor]->id;
    }
    tv->prev_selected_id = prev_id;

    /* Free old visible array */
    free(tv->visible);

    /* Allocate max possible size */
    tv->visible = calloc((size_t)list->count, sizeof(entity_node_t *));
    tv->visible_count = 0;

    if (!tv->visible) return;

    /* DFS traversal from each root */
    for (int i = 0; i < list->root_count; i++) {
        dfs_collect(tv, list->roots[i]);
    }

    /* Update scroll total */
    tv->scroll.total_items = tv->visible_count;

    /* Preserve cursor: find same entity by id */
    if (prev_id != 0) {
        bool found = false;
        for (int i = 0; i < tv->visible_count; i++) {
            if (tv->visible[i]->id == prev_id) {
                tv->scroll.cursor = i;
                found = true;
                break;
            }
        }
        if (!found) {
            /* Clamp cursor to valid range */
            if (tv->scroll.cursor >= tv->visible_count) {
                tv->scroll.cursor = tv->visible_count > 0 ? tv->visible_count - 1 : 0;
            }
        }
    }

    scroll_ensure_visible(&tv->scroll);
}

void tree_view_toggle_expand(tree_view_t *tv, entity_list_t *list) {
    if (!tv->visible || tv->visible_count == 0) return;
    if (tv->scroll.cursor < 0 || tv->scroll.cursor >= tv->visible_count) return;

    entity_node_t *node = tv->visible[tv->scroll.cursor];
    if (node->child_count > 0) {
        node->expanded = !node->expanded;
    }

    tree_view_rebuild_visible(tv, list);
}

void tree_view_toggle_anonymous(tree_view_t *tv, entity_list_t *list) {
    tv->show_anonymous = !tv->show_anonymous;
    tree_view_rebuild_visible(tv, list);
}

entity_node_t *tree_view_selected(tree_view_t *tv) {
    if (!tv->visible || tv->visible_count == 0) return NULL;
    if (tv->scroll.cursor < 0 || tv->scroll.cursor >= tv->visible_count) return NULL;
    return tv->visible[tv->scroll.cursor];
}

void tree_view_render(tree_view_t *tv, WINDOW *win) {
    if (!tv->visible || tv->visible_count == 0) {
        wattron(win, A_DIM);
        mvwprintw(win, 1, 2, "No entities");
        wattroff(win, A_DIM);
        return;
    }

    int max_rows = getmaxy(win) - 2;  /* Subtract 2 for border */
    int max_cols = getmaxx(win) - 2;  /* Subtract 2 for border */

    tv->scroll.visible_rows = max_rows;
    scroll_ensure_visible(&tv->scroll);

    for (int row = 0; row < max_rows; row++) {
        int item_idx = tv->scroll.scroll_offset + row;
        if (item_idx >= tv->visible_count) break;

        entity_node_t *node = tv->visible[item_idx];
        bool is_cursor = (item_idx == tv->scroll.cursor);

        int win_row = row + 1;  /* +1 for top border */
        int col = 1;            /* +1 for left border */

        if (is_cursor) {
            wattron(win, A_REVERSE);
            /* Fill the row with spaces first for full-width highlight */
            wmove(win, win_row, col);
            for (int c = 0; c < max_cols && c + col < getmaxx(win) - 1; c++) {
                waddch(win, ' ');
            }
        }

        /* Draw tree indentation */
        for (int d = 0; d < node->depth; d++) {
            if (col + 4 > max_cols + 1) break;

            if (d < node->depth - 1) {
                /* Ancestor continuation line or blank */
                if (ancestor_has_next_sibling(node, d)) {
                    wattron(win, COLOR_PAIR(CP_TREE_LINE) | A_DIM);
                    mvwprintw(win, win_row, col, TREE_VERT "   ");
                    wattroff(win, COLOR_PAIR(CP_TREE_LINE) | A_DIM);
                } else {
                    mvwprintw(win, win_row, col, "    ");
                }
            } else {
                /* Node's own depth level: branch or corner */
                wattron(win, COLOR_PAIR(CP_TREE_LINE) | A_DIM);
                if (node_is_last_child(node)) {
                    mvwprintw(win, win_row, col,
                              TREE_LAST TREE_HORIZ TREE_HORIZ " ");
                } else {
                    mvwprintw(win, win_row, col,
                              TREE_BRANCH TREE_HORIZ TREE_HORIZ " ");
                }
                wattroff(win, COLOR_PAIR(CP_TREE_LINE) | A_DIM);
            }
            col += 4;
        }

        /* Expand/collapse indicator */
        if (node->child_count > 0) {
            mvwprintw(win, win_row, col, "%s ", node->expanded ? "v" : ">");
            col += 2;
        }

        /* Entity name or anonymous ID */
        if (node->is_anonymous) {
            wattron(win, A_DIM);
            mvwprintw(win, win_row, col, "#%llu", (unsigned long long)node->id);
            wattroff(win, A_DIM);
        } else {
            mvwprintw(win, win_row, col, "%s", node->name);
        }

        /* Right-align component names inline */
        if (node->component_count > 0) {
            char comp_buf[256];
            int buf_pos = 0;
            int show_count = node->component_count > 3 ? 3 : node->component_count;

            for (int c = 0; c < show_count; c++) {
                if (c > 0) {
                    buf_pos += snprintf(comp_buf + buf_pos,
                                        sizeof(comp_buf) - (size_t)buf_pos, ", ");
                }
                buf_pos += snprintf(comp_buf + buf_pos,
                                    sizeof(comp_buf) - (size_t)buf_pos,
                                    "%s", node->component_names[c]);
            }
            if (node->component_count > 3) {
                buf_pos += snprintf(comp_buf + buf_pos,
                                    sizeof(comp_buf) - (size_t)buf_pos,
                                    " +%d more", node->component_count - 3);
            }

            int comp_len = (int)strlen(comp_buf);
            int comp_col = max_cols - comp_len;
            if (comp_col > col + 2) {
                wattron(win, A_DIM);
                mvwprintw(win, win_row, comp_col, "%s", comp_buf);
                wattroff(win, A_DIM);
            }
        }

        if (is_cursor) {
            wattroff(win, A_REVERSE);
        }
    }
}
