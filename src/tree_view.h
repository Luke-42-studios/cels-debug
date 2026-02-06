#ifndef CELS_DEBUG_TREE_VIEW_H
#define CELS_DEBUG_TREE_VIEW_H

#include "data_model.h"
#include "scroll.h"
#include <ncurses.h>
#include <stdbool.h>

/* A display row is either a section header, phase sub-header, or entity node.
 * Section headers and phase sub-headers are navigable — Enter toggles collapse. */
typedef struct display_row {
    entity_node_t *node;   /* Non-NULL = entity row, NULL = header */
    int section_idx;       /* Which CELS-C section this belongs to */
    int phase_group;       /* -1 = section/entity row, >=0 = phase sub-header index */
} display_row_t;

/* Entity tree with virtual scrolling and collapsible CELS-C sections.
 *
 * tree_view does NOT own the entity_node_t data. It holds pointers into
 * the entity_list_t owned by app_state. When entity_list is replaced on
 * the next poll, the tree_view's rows[] becomes stale and must be
 * rebuilt via tree_view_rebuild_visible(). */
typedef struct tree_view {
    display_row_t *rows;       /* Flattened display list (headers + entities) */
    int row_count;
    scroll_state_t scroll;     /* Scroll state over rows[] */
    bool show_anonymous;       /* Toggle for 'f' key, default false */
    uint64_t prev_selected_id; /* Track selected entity across rebuilds */

    /* CELS-C section state */
    bool section_collapsed[ENTITY_CLASS_COUNT];  /* true = collapsed */
    int section_item_count[ENTITY_CLASS_COUNT];  /* entity count per section */

    /* Phase sub-header state (for Systems section) */
    char **phase_names;           /* unique phase names in execution order */
    int *phase_system_counts;     /* system count per phase */
    bool *phase_collapsed;        /* collapse state per phase */
    int phase_count;              /* number of active phases */
} tree_view_t;

/* Zero out all fields. All sections start collapsed. */
void tree_view_init(tree_view_t *tv);

/* Free rows[] array (not the nodes themselves). Reset all fields. */
void tree_view_fini(tree_view_t *tv);

/* Rebuild the display list from the entity tree.
 * Includes section headers as navigable rows, skips items in collapsed sections.
 * Preserves cursor on the same entity (by id) if still visible. */
void tree_view_rebuild_visible(tree_view_t *tv, entity_list_t *list);

/* Toggle: if cursor is on a section header, toggle collapse.
 * If cursor is on an entity with children, toggle tree expand. */
void tree_view_toggle_expand(tree_view_t *tv, entity_list_t *list);

/* Flip show_anonymous, rebuild, preserve cursor. */
void tree_view_toggle_anonymous(tree_view_t *tv, entity_list_t *list);

/* Return the entity at cursor, or NULL if cursor is on a header. */
entity_node_t *tree_view_selected(tree_view_t *tv);

/* Set phase grouping data for Systems section. Called before rebuild.
 * tree_view owns copies of phase_names (strdup'd internally). */
void tree_view_set_phases(tree_view_t *tv, char **phase_names,
                          int *phase_system_counts, int phase_count);

/* Render the visible portion of the display list into the window. */
void tree_view_render(tree_view_t *tv, WINDOW *win);

#endif /* CELS_DEBUG_TREE_VIEW_H */
