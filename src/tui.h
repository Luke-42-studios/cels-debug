#ifndef CELS_DEBUG_TUI_H
#define CELS_DEBUG_TUI_H

#include "data_model.h"
#include "http_client.h"  /* for connection_state_t */
#include "tab_system.h"

/* Color pair IDs (shared with tab implementations) */
#define CP_CONNECTED    1
#define CP_DISCONNECTED 2
#define CP_RECONNECTING 3
#define CP_LABEL        4
#define CP_TAB_ACTIVE   5
#define CP_TAB_INACTIVE 6

/* Phase 03: entity/component UI color pairs */
#define CP_TREE_LINE          7
#define CP_ENTITY_NAME        8
#define CP_COMPONENT_HEADER   9
#define CP_JSON_KEY          10
#define CP_JSON_STRING       11
#define CP_JSON_NUMBER       12
#define CP_PANEL_ACTIVE      13
#define CP_PANEL_INACTIVE    14
#define CP_CURSOR            15

/* Phase 04: system phase color pairs */
#define CP_PHASE_ONLOAD      16
#define CP_PHASE_POSTLOAD    17
#define CP_PHASE_PREUPDATE   18
#define CP_PHASE_ONUPDATE    19
#define CP_PHASE_ONVALIDATE  20
#define CP_PHASE_POSTUPDATE  21
#define CP_PHASE_PRESTORE    22
#define CP_PHASE_ONSTORE     23
#define CP_PHASE_POSTFRAME   24
#define CP_PHASE_CUSTOM      25
#define CP_SYSTEM_DISABLED   26

/* Navigation back-stack for cross-tab jumps (Esc returns to origin) */
#define NAV_STACK_MAX 8

typedef struct nav_entry {
    int tab_index;              /* which tab was active */
    uint64_t entity_id;         /* entity ID for cursor restore (0 = none) */
} nav_entry_t;

typedef struct nav_stack {
    nav_entry_t entries[NAV_STACK_MAX];
    int top;                    /* -1 = empty */
} nav_stack_t;

/* Aggregated application state passed to tabs via void* */
typedef struct app_state {
    world_snapshot_t      *snapshot;
    connection_state_t     conn_state;
    /* Phase 03: entity and component data */
    entity_list_t         *entity_list;       /* from /query */
    entity_detail_t       *entity_detail;     /* from /entity/<path> */
    component_registry_t  *component_registry; /* from /components */
    system_registry_t     *system_registry;     /* from /stats/pipeline */
    char                  *selected_entity_path; /* slash-separated path of selected entity, or NULL */
    char                  *footer_message;    /* transient message (e.g., "Entity X removed") */
    int64_t                footer_message_expire; /* timestamp when message should clear */
    int                    pending_tab;      /* cross-tab navigation: >=0 = switch to tab, -1 = none */
    nav_stack_t            nav_stack;        /* back-navigation stack for cross-tab jumps */
    int                    poll_interval_ms; /* configurable refresh interval, default 500 */
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
