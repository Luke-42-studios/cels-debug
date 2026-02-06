# Phase 02: Tab System and Overview - Research

**Researched:** 2026-02-06
**Domain:** C99 vtable pattern, ncurses tab bar UI, bitmask endpoint filtering, smart polling
**Confidence:** HIGH

## Summary

Phase 02 adds three capabilities to the cels-debug TUI: (1) a tab vtable framework that abstracts per-tab behavior behind function pointers, (2) a tab bar UI row with keyboard navigation, and (3) an Overview dashboard tab that displays the live stats currently rendered directly in tui.c. The research covers five domains: C99 vtable struct patterns, ncurses window layout modification (inserting a tab bar row), keyboard handling for tab switching, bitmask-based endpoint filtering for smart polling, and the Overview tab's data requirements.

The existing Phase 01 code has a clean three-window layout (header, content, footer) with a single `tui_render()` function. Phase 02 must refactor this: the content area rendering moves out of tui.c and into per-tab draw functions dispatched through a vtable. A new `win_tabbar` window is inserted between the header and content windows (1 row). The main loop is modified to pass input through the tab system before handling global keys, and the poll timer only fetches endpoints the active tab declares it needs.

The critical design choice is keeping the vtable simple: 4 function pointers (init, fini, draw, handle_input) plus a `required_endpoints` bitmask and tab metadata. This mirrors htop's Panel pattern but is deliberately simpler since tabs do not overlap and never run concurrently. The tab_system module owns the array of tab instances and dispatches to the active tab.

**Primary recommendation:** Implement in two plans: (1) tab vtable framework + tab bar UI + keyboard navigation with 6 placeholder tabs, then (2) Overview tab implementation pulling data from the existing world_snapshot_t.

## Standard Stack

No new libraries are needed for Phase 02. The entire phase uses existing dependencies from Phase 01.

### Core (unchanged from Phase 01)
| Library | Version | Purpose | Phase 02 Usage |
|---------|---------|---------|----------------|
| ncursesw | 6.5+ | TUI rendering | New tab bar window, per-tab content rendering |
| libcurl | 8.x | HTTP GET | Smart polling (conditional fetch based on active tab) |
| yyjson | 0.12.0 | JSON parsing | Unchanged; parsing moves to new endpoint-specific parsers in future phases |

### Supporting (Phase 02 specific)
| Pattern | Purpose | Why |
|---------|---------|-----|
| C99 function pointer structs | Tab vtable dispatch | Standard C polymorphism; no external lib needed |
| Bitmask enum (uint32_t) | Endpoint filtering | Efficient, composable, zero-allocation |
| A_REVERSE + COLOR_PAIR | Active tab highlighting | ncurses built-in; works on all terminals |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Static tab array | Dynamic registration | Dynamic adds complexity for no benefit; tab count is fixed at 6 |
| Bitmask enum | Callback-based polling | Bitmask is simpler, O(1) check, no function pointer indirection for polling |
| Separate tab bar window | Draw tab bar in header | Separate window is cleaner for resize handling; 1 extra row is cheap |

## Architecture Patterns

### Recommended Project Structure (Phase 02 additions)
```
tools/cels-debug/src/
├── main.c              # Modified: routes input through tab_system
├── tui.c / tui.h       # Modified: adds win_tabbar, delegates content to tab draw
├── http_client.c/.h    # Unchanged
├── json_parser.c/.h    # Unchanged
├── data_model.c/.h     # Unchanged
├── tab_system.c/.h     # NEW: tab registry, vtable dispatch, active tab mgmt
└── tabs/               # NEW: directory for tab implementations
    ├── tab_overview.c/.h    # NEW: Overview dashboard (entity count, FPS, etc.)
    └── tab_placeholder.c/.h # NEW: shared placeholder for 5 unimplemented tabs
```

### Pattern 1: Tab Vtable (Function Pointer Struct)
**What:** Each tab type is defined by a struct of function pointers (init, fini, draw, handle_input) plus metadata (name, shortcut key, required endpoints). The tab_system holds an array of tab instances and dispatches to the active one.
**When to use:** Always for this phase. This is requirement T9.
**Why this pattern:** Used by htop (PanelClass), Linux kernel (file_operations), and virtually all C projects needing runtime polymorphism. Proven, zero overhead, no allocations.
**Example:**
```c
// Source: Standard C99 vtable pattern, verified against htop Panel design

/* Forward declaration */
typedef struct tab_t tab_t;

/* Vtable: function signatures for tab behavior */
typedef void (*tab_init_fn)(tab_t *self, void *app_state);
typedef void (*tab_fini_fn)(tab_t *self);
typedef void (*tab_draw_fn)(const tab_t *self, WINDOW *win, const void *app_state);
typedef bool (*tab_handle_input_fn)(tab_t *self, int ch, void *app_state);

/* Tab definition (vtable + metadata) -- one per tab TYPE */
typedef struct tab_def {
    const char       *name;              /* Display name for tab bar */
    int               shortcut;          /* '1'-'6' key to activate */
    uint32_t          required_endpoints; /* Bitmask of endpoints to poll */
    tab_init_fn       init;              /* Called once when tab system starts */
    tab_fini_fn       fini;              /* Called once when tab system shuts down */
    tab_draw_fn       draw;              /* Called every frame when tab is active */
    tab_handle_input_fn handle_input;    /* Called with input when tab is active */
} tab_def_t;

/* Tab instance (vtable pointer + per-tab state) -- one per tab */
struct tab_t {
    const tab_def_t  *def;              /* Points to shared tab definition */
    void             *state;            /* Per-tab private state (malloc'd) */
};
```

### Pattern 2: Endpoint Bitmask for Smart Polling
**What:** Each flecs REST endpoint is assigned a bit position. Tabs declare which endpoints they need via a bitmask. The main loop only polls endpoints where `active_tab->def->required_endpoints & ENDPOINT_X` is non-zero.
**When to use:** Always. This is a Phase 02 success criterion (criterion 4).
**Why this pattern:** Zero-cost check per endpoint. Composable (OR bits together for tabs that need multiple endpoints). No allocations. Extensible (add new bits for new endpoints in future phases).
**Example:**
```c
// Source: Standard C bitmask enum pattern

/* Endpoint identifiers -- one bit per REST endpoint */
typedef enum {
    ENDPOINT_NONE           = 0,
    ENDPOINT_STATS_WORLD    = (1u << 0),  /* /stats/world */
    ENDPOINT_STATS_PIPELINE = (1u << 1),  /* /stats/pipeline */
    ENDPOINT_QUERY          = (1u << 2),  /* /query?expr=... */
    ENDPOINT_ENTITY         = (1u << 3),  /* /entity/<path> */
    ENDPOINT_COMPONENTS     = (1u << 4),  /* /components */
    ENDPOINT_WORLD          = (1u << 5),  /* /world */
} endpoint_t;

/* Per-tab endpoint declarations */
/* Overview tab needs: entity count, system count, FPS, frame time */
#define OVERVIEW_ENDPOINTS    (ENDPOINT_STATS_WORLD)

/* Future: Entities tab needs query results */
#define ENTITIES_ENDPOINTS    (ENDPOINT_QUERY)

/* Future: Components tab needs component registry */
#define COMPONENTS_ENDPOINTS  (ENDPOINT_COMPONENTS)

/* Future: Systems tab needs pipeline stats */
#define SYSTEMS_ENDPOINTS     (ENDPOINT_STATS_PIPELINE)

/* Future: Performance tab needs both world and pipeline stats */
#define PERFORMANCE_ENDPOINTS (ENDPOINT_STATS_WORLD | ENDPOINT_STATS_PIPELINE)

/* In main loop polling logic: */
uint32_t needed = tab_system_required_endpoints(&tabs);
if (needed & ENDPOINT_STATS_WORLD) {
    /* poll /stats/world */
}
if (needed & ENDPOINT_STATS_PIPELINE) {
    /* poll /stats/pipeline */
}
```

### Pattern 3: Window Layout with Tab Bar
**What:** Insert a 1-line `win_tabbar` window between header and content. Content window shrinks by 1 row.
**When to use:** Phase 02 modifies the existing tui.c window layout.
**Example:**
```c
// Source: ncurses newwin() documentation, verified from Phase 01 tui.c

/* Phase 01 layout (3 windows):
 *   Row 0:           win_header  (1 line)
 *   Row 1..LINES-2:  win_content (LINES-2 lines)
 *   Row LINES-1:     win_footer  (1 line)
 *
 * Phase 02 layout (4 windows):
 *   Row 0:           win_header  (1 line)
 *   Row 1:           win_tabbar  (1 line)  <-- NEW
 *   Row 2..LINES-2:  win_content (LINES-3 lines)
 *   Row LINES-1:     win_footer  (1 line)
 */

static WINDOW *win_header  = NULL;
static WINDOW *win_tabbar  = NULL;  /* NEW */
static WINDOW *win_content = NULL;
static WINDOW *win_footer  = NULL;

static void create_windows(void) {
    win_header  = newwin(1, COLS, 0, 0);
    win_tabbar  = newwin(1, COLS, 1, 0);           /* NEW */
    win_content = newwin(LINES - 3, COLS, 2, 0);   /* Modified: was LINES-2 at row 1 */
    win_footer  = newwin(1, COLS, LINES - 1, 0);
}
```

### Pattern 4: Tab Bar Rendering with Active Highlight
**What:** Render tab names horizontally in `win_tabbar`. Active tab uses `A_REVERSE | A_BOLD` with a color pair. Inactive tabs use normal text.
**When to use:** Every frame in tui_render.
**Example:**
```c
// Source: ncurses curs_attr.3x man page (A_REVERSE, A_BOLD attributes)

#define CP_TAB_ACTIVE   5   /* New color pair for active tab */
#define CP_TAB_INACTIVE 6   /* New color pair for inactive tabs */

static void render_tabbar(const tab_system_t *tabs) {
    werase(win_tabbar);
    int col = 1;

    for (int i = 0; i < tabs->count; i++) {
        const tab_def_t *def = tabs->tabs[i].def;

        if (i == tabs->active) {
            /* Active tab: reverse video + bold */
            wattron(win_tabbar, A_REVERSE | A_BOLD | COLOR_PAIR(CP_TAB_ACTIVE));
            mvwprintw(win_tabbar, 0, col, " %d:%s ", i + 1, def->name);
            wattroff(win_tabbar, A_REVERSE | A_BOLD | COLOR_PAIR(CP_TAB_ACTIVE));
        } else {
            /* Inactive tab: dim text */
            wattron(win_tabbar, COLOR_PAIR(CP_TAB_INACTIVE));
            mvwprintw(win_tabbar, 0, col, " %d:%s ", i + 1, def->name);
            wattroff(win_tabbar, COLOR_PAIR(CP_TAB_INACTIVE));
        }

        /* Advance column past the tab label */
        col += snprintf(NULL, 0, " %d:%s ", i + 1, def->name);
    }

    wnoutrefresh(win_tabbar);
}
```

### Pattern 5: Input Routing (Tab-first, then Global)
**What:** The main loop first offers input to the tab system (for tab switching keys and per-tab input), then handles global keys (q to quit, KEY_RESIZE). Tab switching consumes the input; per-tab handlers return whether they consumed it.
**When to use:** Always. This ensures tabs can have custom keybindings without conflicting with global ones.
**Example:**
```c
// Source: Standard TUI input dispatch pattern

/* In main loop: */
int ch = getch();

/* 1. Global keys that always work */
if (ch == 'q' || ch == 'Q') {
    g_running = 0;
    continue;
}
if (ch == KEY_RESIZE) {
    tui_resize();
    continue;  /* or fall through to render */
}

/* 2. Tab switching keys (handled by tab_system) */
if (ch >= '1' && ch <= '6') {
    tab_system_activate(&tabs, ch - '1');
    continue;
}
if (ch == '\t') {  /* TAB key = ASCII 9 */
    tab_system_next(&tabs);
    continue;
}

/* 3. Per-tab input (delegated to active tab's handle_input) */
tab_system_handle_input(&tabs, ch);
```

### Anti-Patterns to Avoid
- **Monolithic tui_render():** Do NOT keep all rendering in tui.c. Each tab's draw function handles its own content window rendering. tui.c renders header, tab bar, and footer; active tab renders content.
- **Global tab state:** Do NOT use global variables for active tab index. Encapsulate in a tab_system_t struct passed by pointer. This keeps the design testable and refactorable.
- **Polling all endpoints always:** Do NOT poll /stats/pipeline when Overview tab is active (it only needs /stats/world). The bitmask check must gate each HTTP request.
- **Tab-specific code in main.c:** Do NOT put tab drawing logic in main.c. Main loop should call `tab_system_handle_input()` and `tab_system_draw()`. Tab implementations are self-contained.
- **Recreating ncurses windows per frame:** Windows are created once in `create_windows()` and only recreated on KEY_RESIZE. Per-frame: werase + draw + wnoutrefresh.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Tab highlighting | Custom escape sequences | A_REVERSE + COLOR_PAIR via ncurses | ncurses handles terminal capability detection automatically |
| Tab key detection | Raw terminal escape parsing | `getch()` returns '\t' (9) for TAB | ncurses already decodes the input stream |
| Bitmask operations | String-based endpoint lists | uint32_t bitmask with bitwise OR/AND | O(1), zero allocation, type-safe enough in C99 |
| Tab cycling (wrap-around) | Complex index management | `(active + 1) % count` | Modular arithmetic handles the wrap; do not special-case |

**Key insight:** Phase 02 introduces no new dependencies. Every capability (vtable dispatch, bitmask filtering, tab bar rendering) uses C99 language features and existing ncurses APIs.

## Common Pitfalls

### Pitfall 1: Content Window Size Off-By-One After Tab Bar Insertion
**What goes wrong:** After adding the tab bar window (1 row), the content window is still created with `LINES - 2` rows starting at row 1, causing overlap with the tab bar or 1-row gap.
**Why it happens:** Phase 01 had content at row 1 with height LINES-2. Phase 02 must shift content to row 2 with height LINES-3.
**How to avoid:** Update `create_windows()` in tui.c atomically: tab bar at (1, 0) height 1, content at (2, 0) height LINES-3. Both `create_windows()` and `tui_resize()` must use the same layout math.
**Warning signs:** Content text overlaps with tab labels, or 1 blank row appears between tab bar and content.

### Pitfall 2: Tab Bar Overflows Terminal Width
**What goes wrong:** With 6 tab names ("Overview", "Entities", "Components", "Systems", "State", "Performance"), the labels may exceed COLS on narrow terminals (<80 columns).
**Why it happens:** Total label width including padding: " 1:Overview  2:Entities  3:Components  4:Systems  5:State  6:Performance " is approximately 76 characters. Terminals under 80 cols will clip.
**How to avoid:** Truncate tab names if COLS < 80. At minimum width, show "1:Ov 2:En 3:Co 4:Sy 5:St 6:Pe". Or simply let ncurses clip (writes past window width are silently ignored). For v0.1, letting ncurses clip is acceptable.
**Warning signs:** Rightmost tabs are invisible on narrow terminals.

### Pitfall 3: Input Consumed Twice (Tab Switch + Per-Tab Handler)
**What goes wrong:** User presses '1' to switch to Overview tab, but the Overview tab's handle_input also receives '1' and interprets it as something else.
**Why it happens:** Tab switching keys are dispatched before the input reaches the active tab's handler.
**How to avoid:** Tab switching keys ('1'-'6', TAB) are consumed by the tab_system and never passed to per-tab handlers. The dispatch order is: global keys -> tab switching keys -> per-tab handler.
**Warning signs:** Switching tabs triggers unexpected per-tab behavior.

### Pitfall 4: Forgetting to wnoutrefresh the New Tab Bar Window
**What goes wrong:** Tab bar window is drawn but never appears on screen, or appears stale.
**Why it happens:** Phase 01 batches wnoutrefresh for 3 windows. Phase 02 must add wnoutrefresh(win_tabbar) to the batch before the final doupdate().
**How to avoid:** Checklist: werase + draw + wnoutrefresh for ALL 4 windows (header, tabbar, content, footer), then one doupdate().
**Warning signs:** Tab bar shows stale content or never appears.

### Pitfall 5: Smart Polling Breaks Connection State
**What goes wrong:** When the active tab doesn't need ENDPOINT_STATS_WORLD (e.g., future Entities tab), no HTTP request is made. The connection state machine never transitions from DISCONNECTED to CONNECTED, so header always shows "Disconnected".
**Why it happens:** Connection state update is coupled to the /stats/world poll response.
**How to avoid:** Always poll at least one endpoint for connection health. Option A: always poll /stats/world (lightweight). Option B: use a dedicated /world or root endpoint for health check. Recommendation: always include ENDPOINT_STATS_WORLD in the bitmask for connection health. This is cheap (localhost, <1KB response) and keeps connection state accurate.
**Warning signs:** Switching to a tab that doesn't need /stats/world causes "Disconnected" status.

### Pitfall 6: Tab Init/Fini Called at Wrong Time
**What goes wrong:** Tab init is called before HTTP client is ready, or tab fini is called after ncurses endwin().
**Why it happens:** Initialization order matters: ncurses -> HTTP -> tab_system. Shutdown order is reverse.
**How to avoid:** tab_system_init() is called AFTER tui_init() and http_client_init(). tab_system_fini() is called BEFORE http_client_fini() and tui_fini(). Document the lifecycle ordering.
**Warning signs:** Segfault on startup (tab tries to draw before ncurses) or cleanup (tab tries to free after resources gone).

## Code Examples

### Tab System Public API
```c
// Source: Designed for this project based on vtable pattern research

#ifndef CELS_DEBUG_TAB_SYSTEM_H
#define CELS_DEBUG_TAB_SYSTEM_H

#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct tab_t tab_t;
typedef struct tab_def tab_def_t;
typedef struct tab_system tab_system_t;

/* Endpoint bitmask */
typedef enum {
    ENDPOINT_NONE           = 0,
    ENDPOINT_STATS_WORLD    = (1u << 0),
    ENDPOINT_STATS_PIPELINE = (1u << 1),
    ENDPOINT_QUERY          = (1u << 2),
    ENDPOINT_ENTITY         = (1u << 3),
    ENDPOINT_COMPONENTS     = (1u << 4),
    ENDPOINT_WORLD          = (1u << 5),
} endpoint_t;

/* Tab function signatures */
typedef void (*tab_init_fn)(tab_t *self);
typedef void (*tab_fini_fn)(tab_t *self);
typedef void (*tab_draw_fn)(const tab_t *self, WINDOW *win,
                            const void *app_state);
typedef bool (*tab_handle_input_fn)(tab_t *self, int ch, void *app_state);

/* Tab definition (shared, const, one per tab type) */
struct tab_def {
    const char       *name;
    uint32_t          required_endpoints;
    tab_init_fn       init;
    tab_fini_fn       fini;
    tab_draw_fn       draw;
    tab_handle_input_fn handle_input;
};

/* Tab instance (one per tab in the tab bar) */
struct tab_t {
    const tab_def_t  *def;
    void             *state;   /* per-tab private data */
};

/* Tab system (owns the tab array) */
#define TAB_COUNT 6

struct tab_system {
    tab_t tabs[TAB_COUNT];
    int   active;              /* index of currently active tab [0..5] */
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
```

### Tab System Implementation Sketch
```c
// Source: Designed for this project

#include "tab_system.h"
#include "tabs/tab_overview.h"
#include "tabs/tab_placeholder.h"

/* Tab definitions (static, const) */
static const tab_def_t tab_defs[TAB_COUNT] = {
    { "Overview",     ENDPOINT_STATS_WORLD,    tab_overview_init,
      tab_overview_fini,    tab_overview_draw,    tab_overview_input },
    { "Entities",     ENDPOINT_QUERY,           tab_placeholder_init,
      tab_placeholder_fini, tab_placeholder_draw, tab_placeholder_input },
    { "Components",   ENDPOINT_COMPONENTS,      tab_placeholder_init,
      tab_placeholder_fini, tab_placeholder_draw, tab_placeholder_input },
    { "Systems",      ENDPOINT_STATS_PIPELINE,  tab_placeholder_init,
      tab_placeholder_fini, tab_placeholder_draw, tab_placeholder_input },
    { "State",        ENDPOINT_NONE,            tab_placeholder_init,
      tab_placeholder_fini, tab_placeholder_draw, tab_placeholder_input },
    { "Performance",  ENDPOINT_STATS_WORLD | ENDPOINT_STATS_PIPELINE,
      tab_placeholder_init, tab_placeholder_fini, tab_placeholder_draw,
      tab_placeholder_input },
};

void tab_system_init(tab_system_t *ts) {
    ts->active = 0;
    for (int i = 0; i < TAB_COUNT; i++) {
        ts->tabs[i].def = &tab_defs[i];
        ts->tabs[i].state = NULL;
        if (ts->tabs[i].def->init) {
            ts->tabs[i].def->init(&ts->tabs[i]);
        }
    }
}

void tab_system_fini(tab_system_t *ts) {
    for (int i = 0; i < TAB_COUNT; i++) {
        if (ts->tabs[i].def->fini) {
            ts->tabs[i].def->fini(&ts->tabs[i]);
        }
    }
}

void tab_system_activate(tab_system_t *ts, int index) {
    if (index >= 0 && index < TAB_COUNT) {
        ts->active = index;
    }
}

void tab_system_next(tab_system_t *ts) {
    ts->active = (ts->active + 1) % TAB_COUNT;
}

bool tab_system_handle_input(tab_system_t *ts, int ch, void *app_state) {
    tab_t *active = &ts->tabs[ts->active];
    if (active->def->handle_input) {
        return active->def->handle_input(active, ch, app_state);
    }
    return false;
}

void tab_system_draw(const tab_system_t *ts, WINDOW *win,
                     const void *app_state) {
    const tab_t *active = &ts->tabs[ts->active];
    if (active->def->draw) {
        active->def->draw(active, win, app_state);
    }
}

uint32_t tab_system_required_endpoints(const tab_system_t *ts) {
    return ts->tabs[ts->active].def->required_endpoints;
}
```

### Overview Tab Implementation
```c
// Source: Moves existing tui.c content rendering into tab pattern

/* tab_overview.h */
#ifndef CELS_DEBUG_TAB_OVERVIEW_H
#define CELS_DEBUG_TAB_OVERVIEW_H

#include "../tab_system.h"

void tab_overview_init(tab_t *self);
void tab_overview_fini(tab_t *self);
void tab_overview_draw(const tab_t *self, WINDOW *win, const void *app_state);
bool tab_overview_input(tab_t *self, int ch, void *app_state);

#endif

/* tab_overview.c -- renders the same stats as Phase 01 tui.c content area */
#include "tab_overview.h"

/* app_state provides access to current snapshot */
void tab_overview_draw(const tab_t *self, WINDOW *win,
                       const void *app_state) {
    (void)self;
    /* Cast app_state to access world_snapshot_t pointer */
    /* Render: entity count, system count, FPS, frame time */
    /* This is the same logic currently in tui_render() content section */

    /* Example (actual implementation uses app_state struct): */
    /*
    const app_state_t *state = app_state;
    if (state->snapshot) {
        int row = 1;
        wattron(win, COLOR_PAIR(CP_LABEL));
        mvwprintw(win, row, 2, "Entities:");
        wattroff(win, COLOR_PAIR(CP_LABEL));
        wprintw(win, "   %.0f", state->snapshot->entity_count);
        // ... same pattern for FPS, frame time, system count
    } else {
        mvwprintw(win, (getmaxy(win)) / 2, (getmaxx(win) - 19) / 2,
                  "Waiting for data...");
    }
    */
}

void tab_overview_init(tab_t *self) { self->state = NULL; /* no per-tab state needed */ }
void tab_overview_fini(tab_t *self) { (void)self; }
bool tab_overview_input(tab_t *self, int ch, void *app_state) {
    (void)self; (void)ch; (void)app_state;
    return false; /* Overview has no interactive elements yet */
}
```

### Placeholder Tab (shared for 5 unimplemented tabs)
```c
// Source: Designed for this project

/* tab_placeholder.c */
#include "tab_placeholder.h"

void tab_placeholder_init(tab_t *self) { self->state = NULL; }
void tab_placeholder_fini(tab_t *self) { (void)self; }

void tab_placeholder_draw(const tab_t *self, WINDOW *win,
                          const void *app_state) {
    (void)app_state;
    int max_y = getmaxy(win);
    int max_x = getmaxx(win);
    const char *msg = "Not implemented yet";
    int msg_len = 19;
    mvwprintw(win, max_y / 2, (max_x - msg_len) / 2, "%s", msg);

    /* Show tab name below */
    const char *name = self->def->name;
    int name_len = (int)strlen(name);
    wattron(win, A_DIM);
    mvwprintw(win, max_y / 2 + 1, (max_x - name_len) / 2, "%s", name);
    wattroff(win, A_DIM);
}

bool tab_placeholder_input(tab_t *self, int ch, void *app_state) {
    (void)self; (void)ch; (void)app_state;
    return false;
}
```

### App State Struct (Centralizes Data for Tab Access)
```c
// Source: Design pattern for passing multiple state items to tabs

/* A simple struct that aggregates all state the tabs might need.
 * Passed as void* app_state to tab draw/input functions.
 * Avoids coupling tabs to global variables. */
typedef struct app_state {
    world_snapshot_t    *snapshot;       /* current data from /stats/world */
    connection_state_t   conn_state;    /* connection health */
    /* Future: pipeline_snapshot_t, entity list, etc. */
} app_state_t;
```

### Modified Main Loop (Input Routing + Smart Polling)
```c
// Source: Extension of Phase 01 main.c loop

/* Main loop changes for Phase 02: */
while (g_running) {
    int ch = getch();

    /* 1. Global: quit */
    if (ch == 'q' || ch == 'Q') {
        g_running = 0;
        continue;
    }

    /* 2. Global: resize */
    if (ch == KEY_RESIZE) {
        tui_resize();
    }

    /* 3. Tab switching */
    if (ch >= '1' && ch <= '6') {
        tab_system_activate(&tabs, ch - '1');
    } else if (ch == '\t') {   /* TAB key is ASCII 9 */
        tab_system_next(&tabs);
    } else if (ch != ERR) {
        /* 4. Per-tab input */
        tab_system_handle_input(&tabs, ch, &app_state);
    }

    /* 5. Smart polling on timer */
    int64_t now = now_ms();
    if (now - last_poll >= POLL_INTERVAL_MS) {
        uint32_t needed = tab_system_required_endpoints(&tabs);

        if (needed & ENDPOINT_STATS_WORLD) {
            http_response_t resp = http_get(curl, url_stats_world);
            conn_state = connection_state_update(conn_state, resp.status);
            if (resp.status == 200 && resp.body.data) {
                world_snapshot_t *new_snap =
                    json_parse_world_stats(resp.body.data, resp.body.size);
                if (new_snap) {
                    world_snapshot_free(app_state.snapshot);
                    app_state.snapshot = new_snap;
                }
            }
            http_response_free(&resp);
        } else {
            /* Lightweight connection health check */
            /* Could poll /stats/world anyway, or skip and keep last state */
        }

        last_poll = now;
    }

    /* 6. Render */
    tui_render(&tabs, &app_state);
}
```

### Updated tui_render Signature
```c
// Source: tui.h modification for Phase 02

/* Phase 01 signature: */
/* void tui_render(const world_snapshot_t *snapshot, connection_state_t conn); */

/* Phase 02 signature -- takes tab_system for tab bar + content dispatch: */
void tui_render(const tab_system_t *tabs, const app_state_t *state);

/* Implementation renders:
 *   1. Header (connection status -- from state->conn_state)
 *   2. Tab bar (from tabs->active + tab names)
 *   3. Content (dispatches to active tab's draw function)
 *   4. Footer (help text, context-sensitive to active tab)
 */
```

### Keyboard Reference (ncurses key codes used)
```c
// Source: ncurses curs_getch.3x man page, verified

/* Tab switching keys: */
'\t'        /* TAB key: ASCII 9, cycles forward through tabs */
'1' - '6'   /* Direct tab selection: ASCII 49-54 */

/* Global keys (unchanged from Phase 01): */
'q' / 'Q'   /* Quit */
KEY_RESIZE   /* Terminal resize (ncurses special) */
ERR          /* getch() timeout, no input (returned as -1) */

/* Future F6 keys (not implemented in Phase 02, but reserved): */
KEY_UP       /* Scroll up (or 'k') */
KEY_DOWN     /* Scroll down (or 'j') */
'\n'         /* Enter (select) -- ASCII 10 */
27           /* Escape (back) -- ASCII 27 */
```

## Flecs REST Endpoints Reference

Complete list of GET endpoints from the flecs REST API (verified from rest.c source):

| Endpoint | Path | Returns | Used By Tab |
|----------|------|---------|-------------|
| Stats World | `/stats/world` | Entity count, FPS, frame time, system count, memory, etc. | Overview, Performance |
| Stats Pipeline | `/stats/pipeline` | Per-system execution times, sync points | Systems, Performance |
| Query | `/query?expr=...` | Entity iteration results as JSON | Entities |
| Entity | `/entity/<path>` | Single entity with components | Entities (detail view) |
| Components | `/components` | Component registry list | Components |
| World | `/world` | Full world serialization | State |
| Queries | `/queries` | All registered queries | (future) |
| Tables | `/tables` | All archetype tables | (future) |

Phase 02 only needs `/stats/world`. All other endpoints are declared in the bitmask for future phases but not polled.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| All rendering in tui.c | Tab vtable dispatch + per-tab draw | Phase 02 | Content rendering moves to tabs; tui.c becomes layout-only |
| Hardcoded URL in main.c | Endpoint bitmask + conditional polling | Phase 02 | Only active tab's endpoints are polled; saves bandwidth |
| tui_render(snapshot, conn) | tui_render(tabs, app_state) | Phase 02 | Signature change; app_state aggregates all data tabs might need |
| 3-window layout | 4-window layout (+ tab bar) | Phase 02 | win_tabbar inserted at row 1; content shifts to row 2 |

**Deprecated/outdated from Phase 01:**
- Direct content rendering in tui_render(): replaced by tab dispatch
- Single-URL polling in main loop: replaced by endpoint bitmask check

## Open Questions

1. **Footer help text: tab-specific or global?**
   - What we know: Phase 01 shows "q: quit". Phase 02 adds tab switching. Future phases add j/k/Enter/Esc.
   - What's unclear: Should the footer show tab-specific help (different per active tab) or a single global help line?
   - Recommendation: For Phase 02, show global help: "1-6:tabs  TAB:next  q:quit". Per-tab help text can be added when tabs have interactive elements (Phase 03+).

2. **app_state_t: forward declaration vs include chain**
   - What we know: tab_system.h needs to declare function signatures using `void *app_state`. Tab implementations need to cast it to `app_state_t *`.
   - What's unclear: Whether to use void* (loose coupling) or forward-declare app_state_t (type safety).
   - Recommendation: Use `void *` in the vtable signatures (tab_system.h has no dependency on app_state definition). Each tab .c file includes the app_state header and casts internally. This avoids circular includes.

3. **Should tabs own their own ncurses sub-windows?**
   - What we know: Phase 02 passes win_content to the active tab's draw function. The tab draws into the shared content window.
   - What's unclear: Whether future phases (scrollable lists, split panes) will need sub-windows.
   - Recommendation: For Phase 02, pass the single win_content window. If future phases need sub-windows, the tab's init function can create them from the content window dimensions. The vtable pattern supports this evolution.

## Sources

### Primary (HIGH confidence)
- `/home/cachy/workspaces/libs/cels/tools/cels-debug/src/tui.c` -- Phase 01 window layout, render function, color pairs
- `/home/cachy/workspaces/libs/cels/tools/cels-debug/src/main.c` -- Phase 01 main loop, input handling, poll timer
- `/home/cachy/workspaces/libs/cels/tools/cels-debug/src/data_model.h` -- world_snapshot_t struct definition
- `/home/cachy/workspaces/libs/cels/build/_deps/flecs-src/src/addons/rest.c` -- Complete REST endpoint routing (lines 1992-2032), stats endpoint handler (lines 1108-1144)
- [ncurses curs_getch.3x man page](https://invisible-island.net/ncurses/man/curs_getch.3x.html) -- TAB key is ASCII 9 ('\t'), KEY_BTAB for backtab
- [ncurses curs_attr.3x man page](https://invisible-island.net/ncurses/man/curs_attr.3x.html) -- A_REVERSE, A_BOLD, A_DIM attribute constants

### Secondary (MEDIUM confidence)
- [htop Panel.h](https://github.com/htop-dev/htop/blob/main/Panel.h) -- PanelClass vtable pattern (function pointers in const struct)
- [htop ScreenManager.h](https://github.com/htop-dev/htop/blob/main/ScreenManager.h) -- Composition-based screen management
- [C vtable pattern discussion](https://news.ycombinator.com/item?id=34298135) -- Community consensus on struct-of-function-pointers approach

### Tertiary (LOW confidence)
- [ncurses tab key discussion](https://www.daniweb.com/programming/software-development/threads/74113/trapping-the-tab-key) -- Forum confirming TAB = ASCII 9 in ncurses (verified against official man page)

## Metadata

**Confidence breakdown:**
- Tab vtable pattern: HIGH -- Standard C99 pattern verified against htop source, well-documented in C programming literature
- Window layout: HIGH -- Direct extension of existing Phase 01 code, ncurses newwin() API verified
- Keyboard handling: HIGH -- TAB key = ASCII 9 verified from ncurses man page; '1'-'6' are standard ASCII
- Endpoint bitmask: HIGH -- Standard C bitmask enum pattern; endpoint list verified from flecs rest.c source
- Smart polling: HIGH -- Simple conditional check against bitmask; connection health pitfall identified and mitigated
- Overview tab: HIGH -- Directly moves existing Phase 01 rendering code into tab draw function

**Research date:** 2026-02-06
**Valid until:** 2026-04-06 (stable patterns, no version-dependent findings)
