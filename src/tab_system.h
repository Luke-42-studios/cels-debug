#ifndef CELS_DEBUG_TAB_SYSTEM_H
#define CELS_DEBUG_TAB_SYSTEM_H

#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct tab_t tab_t;
typedef struct tab_def tab_def_t;
typedef struct tab_system tab_system_t;

/* Endpoint bitmask -- one bit per REST endpoint */
typedef enum {
    ENDPOINT_NONE           = 0,
    ENDPOINT_STATS_WORLD    = (1u << 0),  /* /stats/world */
    ENDPOINT_STATS_PIPELINE = (1u << 1),  /* /stats/pipeline */
    ENDPOINT_QUERY          = (1u << 2),  /* /query?expr=... */
    ENDPOINT_ENTITY         = (1u << 3),  /* /entity/<path> */
    ENDPOINT_COMPONENTS     = (1u << 4),  /* /components */
    ENDPOINT_WORLD          = (1u << 5),  /* /world */
} endpoint_t;

/* Tab function signatures -- void* for app_state avoids circular includes */
typedef void (*tab_init_fn)(tab_t *self);
typedef void (*tab_fini_fn)(tab_t *self);
typedef void (*tab_draw_fn)(const tab_t *self, WINDOW *win,
                            const void *app_state);
typedef bool (*tab_handle_input_fn)(tab_t *self, int ch, void *app_state);

/* Tab definition (vtable + metadata) -- one per tab TYPE, shared const */
struct tab_def {
    const char       *name;
    uint32_t          required_endpoints;
    tab_init_fn       init;
    tab_fini_fn       fini;
    tab_draw_fn       draw;
    tab_handle_input_fn handle_input;
};

/* Tab instance (vtable pointer + per-tab state) -- one per tab slot */
struct tab_t {
    const tab_def_t  *def;
    void             *state;   /* per-tab private data */
};

/* Tab system (owns the tab array) */
#define TAB_COUNT 5

struct tab_system {
    tab_t tabs[TAB_COUNT];
    int   active;              /* index of currently active tab [0..4] */
};

/* Lifecycle */
void tab_system_init(tab_system_t *ts);
void tab_system_fini(tab_system_t *ts);

/* Navigation */
void tab_system_activate(tab_system_t *ts, int index);
void tab_system_next(tab_system_t *ts);

/* Dispatch */
bool tab_system_handle_input(tab_system_t *ts, int ch, void *app_state);
void tab_system_draw(const tab_system_t *ts, WINDOW *win,
                     const void *app_state);

/* Smart polling */
uint32_t tab_system_required_endpoints(const tab_system_t *ts);

#endif /* CELS_DEBUG_TAB_SYSTEM_H */
