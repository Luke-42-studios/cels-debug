#ifndef CELS_DEBUG_TREE_VIEW_H
#define CELS_DEBUG_TREE_VIEW_H

#include "data_model.h"
#include "scroll.h"
#include <ncurses.h>
#include <stdbool.h>

/* Entity tree with virtual scrolling.
 *
 * tree_view does NOT own the entity_node_t data. It holds pointers into
 * the entity_list_t owned by app_state. When entity_list is replaced on
 * the next poll, the tree_view's visible[] becomes stale and must be
 * rebuilt via tree_view_rebuild_visible(). */
typedef struct tree_view {
    entity_node_t **visible;   /* Flattened visible nodes (pointers into entity_list) */
    int visible_count;
    scroll_state_t scroll;     /* Scroll state for the visible list */
    bool show_anonymous;       /* Toggle for 'f' key, default false */
    uint64_t prev_selected_id; /* Track selected entity across rebuilds */
} tree_view_t;

/* Zero out all fields */
void tree_view_init(tree_view_t *tv);

/* Free visible[] array (not the nodes themselves). Reset all fields. */
void tree_view_fini(tree_view_t *tv);

/* Rebuild the flattened visible array from the entity tree.
 * DFS traversal of roots, respecting expand/collapse and anonymous filter.
 * Preserves cursor on the same entity (by id) if still visible. */
void tree_view_rebuild_visible(tree_view_t *tv, entity_list_t *list);

/* Toggle expanded on the node at current cursor position, then rebuild. */
void tree_view_toggle_expand(tree_view_t *tv, entity_list_t *list);

/* Flip show_anonymous, rebuild visible, preserve cursor. */
void tree_view_toggle_anonymous(tree_view_t *tv, entity_list_t *list);

/* Return visible[scroll.cursor] or NULL if no items. */
entity_node_t *tree_view_selected(tree_view_t *tv);

/* Render the visible portion of the tree into the window. */
void tree_view_render(tree_view_t *tv, WINDOW *win);

#endif /* CELS_DEBUG_TREE_VIEW_H */
