#ifndef CELS_DEBUG_TUI_H
#define CELS_DEBUG_TUI_H

#include "data_model.h"
#include "http_client.h"  /* for connection_state_t */
#include "tab_system.h"

/* Aggregated application state passed to tabs via void* */
typedef struct app_state {
    world_snapshot_t   *snapshot;
    connection_state_t  conn_state;
} app_state_t;

/* Initialize ncurses, signal handlers, atexit, color pairs, windows. */
void tui_init(void);

/* Shutdown ncurses, destroy windows, call endwin. */
void tui_fini(void);

/* Render one frame: header, tab bar, content (via tab dispatch), footer. */
void tui_render(const tab_system_t *tabs, const app_state_t *state);

/* Recalculate window sizes from LINES/COLS. Call on KEY_RESIZE. */
void tui_resize(void);

#endif /* CELS_DEBUG_TUI_H */
