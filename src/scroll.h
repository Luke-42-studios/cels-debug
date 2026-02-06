#ifndef CELS_DEBUG_SCROLL_H
#define CELS_DEBUG_SCROLL_H

/* Generic scroll state for any list with virtual scrolling */
typedef struct scroll_state {
    int total_items;    /* Total number of items in the list */
    int visible_rows;   /* Number of rows visible in the window */
    int cursor;         /* Currently selected item index [0..total_items-1] */
    int scroll_offset;  /* First visible item index */
} scroll_state_t;

/* Set all fields to 0 */
void scroll_reset(scroll_state_t *s);

/* Adjust scroll_offset so cursor is within the visible range */
void scroll_ensure_visible(scroll_state_t *s);

/* Move cursor by delta, clamp, then ensure visible */
void scroll_move(scroll_state_t *s, int delta);

/* Move by +/- visible_rows (Page Up/Page Down) */
void scroll_page(scroll_state_t *s, int direction);

/* Jump to first item */
void scroll_to_top(scroll_state_t *s);

/* Jump to last item */
void scroll_to_bottom(scroll_state_t *s);

#endif /* CELS_DEBUG_SCROLL_H */
