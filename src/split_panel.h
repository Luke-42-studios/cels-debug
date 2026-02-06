#ifndef CELS_DEBUG_SPLIT_PANEL_H
#define CELS_DEBUG_SPLIT_PANEL_H

#include <ncurses.h>
#include <stdbool.h>

/* Two-panel layout with focus tracking */
typedef struct split_panel {
    WINDOW *left;
    WINDOW *right;
    int left_width;      /* Columns for left panel */
    int right_width;     /* Columns for right panel */
    int height;          /* Rows available */
    int start_row;       /* Starting row on screen */
    int focus;           /* 0 = left, 1 = right */
} split_panel_t;

/* Create left/right windows with 40/60 split. Sets focus=0. */
void split_panel_create(split_panel_t *sp, int height, int width, int start_row);

/* Destroy both windows, set pointers to NULL */
void split_panel_destroy(split_panel_t *sp);

/* Destroy then recreate with new dimensions */
void split_panel_resize(split_panel_t *sp, int height, int width, int start_row);

/* Draw box borders on both panels. Active panel uses A_BOLD, inactive uses A_DIM. */
void split_panel_draw_borders(split_panel_t *sp, const char *left_title,
                               const char *right_title);

/* Call wnoutrefresh() on both windows */
void split_panel_refresh(split_panel_t *sp);

/* Return the currently focused window */
WINDOW *split_panel_focused(split_panel_t *sp);

/* Handle KEY_LEFT/KEY_RIGHT for focus switching. Returns true if handled. */
bool split_panel_handle_focus(split_panel_t *sp, int ch);

#endif /* CELS_DEBUG_SPLIT_PANEL_H */
