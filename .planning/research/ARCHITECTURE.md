# Architecture Patterns: CELS Debug TUI

**Domain:** TUI debugger polling REST API for ECS world inspection
**Researched:** 2026-02-05
**Overall confidence:** HIGH (flecs REST API verified from source code, ncurses patterns well-established)

## Recommended Architecture

The TUI debugger follows a **Model-View-Controller (MVC) loop with polling** pattern. This separates data fetching (HTTP polling), data storage (model), and rendering (ncurses) into distinct layers. The main loop runs on a single thread with non-blocking I/O.

```
                    +---------------------+
                    |   Main Event Loop   |
                    |  (single thread)    |
                    +----------+----------+
                               |
              +----------------+----------------+
              |                |                |
              v                v                v
    +---------+------+  +------+-------+  +-----+-------+
    |  Input Handler |  |  HTTP Poller |  |  Renderer   |
    |  (ncurses      |  |  (REST API   |  |  (ncurses   |
    |   getch())     |  |   client)    |  |   draw)     |
    +---------+------+  +------+-------+  +-----+-------+
              |                |                |
              v                v                v
    +---------+------+  +------+-------+  +-----+-------+
    |   App State    |  | Data Model   |  |  Tab System |
    |  (active tab,  |  | (entities,   |  |  (tab_t     |
    |   scroll pos,  |  |  components, |  |   vtable)   |
    |   connection)  |  |  systems,    |  |             |
    |                |  |  stats)      |  |             |
    +----------------+  +--------------+  +-------------+
```

### Design Principles

1. **Data fetching is decoupled from rendering.** The HTTP poller populates a data model. The renderer reads from the data model. They never interact directly.
2. **Tabs are just views into the data model.** A tab does not fetch data. It reads from the shared model and renders its portion of the screen.
3. **Non-blocking everything.** The main loop must never block. ncurses input uses `timeout(0)` or `nodelay()`. HTTP requests are non-blocking or have short timeouts.
4. **Polling is timer-driven.** HTTP requests fire at configurable intervals (e.g., 500ms), not every frame. Rendering can happen more often than polling.

## Component Boundaries

| Component | Responsibility | Communicates With |
|-----------|---------------|-------------------|
| `main.c` | Entry point, argument parsing, main loop orchestration | All components |
| `http_client.c/h` | TCP socket HTTP GET against localhost, response buffering | `data_model` (writes parsed data) |
| `json_parser.c/h` | Parse JSON responses from flecs REST API into C structs | `http_client` (receives raw JSON), `data_model` (writes parsed results) |
| `data_model.c/h` | Central data store: entities, components, systems, stats | Read by all tabs, written by `json_parser` |
| `tui.c/h` | ncurses initialization, window layout, chrome (header/footer/tabs bar) | `tab_system`, `app_state` |
| `tab_system.c/h` | Tab registry, vtable dispatch, active tab management | `tui` (renders into provided WINDOW), `data_model` (reads data) |
| `tab_overview.c/h` | Overview tab rendering | `data_model` |
| `tab_entities.c/h` | Entity tree with hierarchy, component inspection | `data_model` |
| `tab_components.c/h` | Component type registry view | `data_model` |
| `tab_systems.c/h` | System/pipeline execution view | `data_model` |
| `tab_state.c/h` | CELS state values with change highlighting | `data_model` |
| `tab_performance.c/h` | Frame timing, system execution times | `data_model` |
| `app_state.c/h` | Application state: active tab, scroll positions, connection status, poll interval | All UI components |

### Component Dependency Graph

```
main.c
  |-- app_state (owns)
  |-- http_client (calls poll)
  |     |-- json_parser (parses response)
  |           |-- data_model (writes to)
  |-- tui (calls render)
  |     |-- tab_system (dispatches to active tab)
  |           |-- tab_overview (reads data_model)
  |           |-- tab_entities (reads data_model)
  |           |-- tab_components (reads data_model)
  |           |-- tab_systems (reads data_model)
  |           |-- tab_state (reads data_model)
  |           |-- tab_performance (reads data_model)
  |-- input handler (inline in main loop, updates app_state)
```

## Data Flow

### HTTP Request to Rendered Screen

```
1. Timer fires (poll interval elapsed)
     |
2. http_client_poll() sends GET to localhost:27750
   - GET /entity/root?values=true&inherited=true    (entity tree)
   - GET /query?expr=...                             (entity queries)
   - GET /stats/world                                (world stats)
   - GET /stats/pipeline?name=...                    (pipeline stats)
   - GET /components                                 (component registry)
   - GET /queries                                    (system/query info)
     |
3. Response buffer filled (raw HTTP response body = JSON)
     |
4. json_parse_response() converts JSON -> C structs
   - Entities: name, parent, tags[], components{}
   - Stats: fps, frame_time, entity_count, system_time
   - Systems: name, phase, time_spent, matched_count
   - Components: name, entity_count, type info
     |
5. data_model_update() replaces stale data with fresh data
   - Atomic swap: old model freed, new model installed
   - Change detection: diff for highlighting in State tab
     |
6. Next render cycle reads from data_model
   - Active tab's draw() function reads relevant data
   - ncurses wnoutrefresh() on each window
   - doupdate() flushes to terminal
```

### Input Flow

```
1. getch() returns immediately (nodelay mode)
     |
2. Key mapping:
   - TAB / 1-6: switch active tab
   - UP/DOWN: scroll within tab
   - ENTER: expand/collapse tree nodes
   - q: quit
   - +/-: adjust poll interval
   - r: force immediate refresh
     |
3. app_state updated (active_tab, scroll_offset, etc.)
     |
4. Next render cycle picks up new state
```

## Flecs REST API Endpoints (Verified from Source)

**Confidence: HIGH** -- Verified by reading `/home/cachy/workspaces/libs/cels/build/_deps/flecs-src/src/addons/rest.c` and `/home/cachy/workspaces/libs/cels/build/_deps/flecs-src/docs/FlecsRemoteApi.md`.

### Endpoints Used by Each Tab

#### Overview Tab
| Endpoint | Method | Purpose | Key Response Fields |
|----------|--------|---------|---------------------|
| `GET /stats/world` | GET | World statistics | `entities.count`, `performance.fps`, `performance.frame_time`, `performance.system_time`, `queries.system_count`, `tables.count`, `memory.*` |
| `GET /stats/pipeline?name=all` | GET | All system stats | Array of `{name, time_spent, matched_entity_count, disabled}` |

#### Entities Tab
| Endpoint | Method | Purpose | Key Response Fields |
|----------|--------|---------|---------------------|
| `GET /query?expr=...&table=true` | GET | Query entities with full table data | `results[].{parent, name, tags[], components{}, pairs{}}` |
| `GET /entity/<path>?values=true&type_info=true` | GET | Single entity detail | `{parent, name, id, tags[], components{}, type_info{}}` |

#### Components Tab
| Endpoint | Method | Purpose | Key Response Fields |
|----------|--------|---------|---------------------|
| `GET /components` | GET | All registered components | Array of `{name, tables[], entity_count, entity_size, type{size, alignment, hooks...}, traits[], memory{}}` |

#### Systems Tab
| Endpoint | Method | Purpose | Key Response Fields |
|----------|--------|---------|---------------------|
| `GET /queries` | GET | All queries/systems/observers | Array of `{name, kind("System"/"Observer"/"Query"), eval_count, eval_time, results, count, expr, plan}` |
| `GET /stats/pipeline?name=<pipeline>` | GET | Pipeline execution order with sync points | Array of system stats interleaved with sync point stats `{multi_threaded, immediate, time_spent, commands_enqueued}` |

#### State Tab
| Endpoint | Method | Purpose | Key Response Fields |
|----------|--------|---------|---------------------|
| `GET /query?expr=...` | GET | Query for CELS state components | Entities with state-related component values |
| `GET /entity/<path>?values=true` | GET | Individual state entity values | Component values for state inspection |

#### Performance Tab
| Endpoint | Method | Purpose | Key Response Fields |
|----------|--------|---------|---------------------|
| `GET /stats/world?period=1s` | GET | 1-second resolution stats | All `performance.*` and `frame.*` gauges with `avg/min/max` arrays (60-sample window) |
| `GET /stats/world?period=1m` | GET | 1-minute resolution stats | Same structure, longer window |
| `GET /stats/pipeline?name=all&period=1s` | GET | Per-system timing | `time_spent` per system |

### Key Query Patterns

**Get all user entities (exclude builtins):**
```
GET /query?expr=!ChildOf(self|up, flecs), !Module(self|up)&table=true&values=true
```
This is equivalent to `GET /world` but as a query.

**Get entity hierarchy under a root:**
```
GET /query?expr=(ChildOf, $p)&table=true&values=true
```

**Get entities with specific CELS components:**
```
GET /query?expr=<ComponentName>&values=true&type_info=true
```

### Stats Response Structure (from flecs source)

The `/stats/world` endpoint returns a JSON object with named metric groups. Each metric contains:
- **Gauges:** `{avg: [60 floats], min: [60 floats], max: [60 floats]}` -- sliding window
- **Counters:** Same structure as gauges, but values represent rates

Available stat periods: `1s`, `1m`, `5m`, `1h`, `1d` (configured via `?period=` param).

Key world stats metrics (verified from rest.c lines 891-955):
```
entities.count                    -- Alive entities
entities.not_alive_count          -- Dead entity slots
performance.fps                   -- Frames per second
performance.frame_time            -- Total frame time
performance.system_time           -- Time in systems
performance.emit_time             -- Time in observers
performance.merge_time            -- Time merging commands
commands.add_count                -- Add operations
commands.remove_count             -- Remove operations
commands.delete_count             -- Delete operations
commands.set_count                -- Set operations
frame.merge_count                 -- Sync points per frame
frame.systems_ran                 -- Systems executed
frame.observers_ran               -- Observer invocations
tables.count                      -- Total tables
components.component_count        -- Component types
components.tag_count              -- Tag types
components.pair_count             -- Pair types
queries.query_count               -- Active queries
queries.system_count              -- Active systems
queries.observer_count            -- Active observers
memory.alloc_count                -- malloc calls
memory.outstanding_alloc_count    -- Current allocations
```

### Important API Details

1. **FlecsStats must be imported.** The `/stats/*` endpoints only work when `ECS_IMPORT(world, FlecsStats)` is called. CELS currently does NOT import FlecsStats. This must be added or the Performance tab will get empty responses.

2. **Query pagination.** The `/query` endpoint supports `?offset=N&limit=M` (default limit 1000). Essential for large entity counts.

3. **Entity paths use `/` separator.** In the REST API, entity paths use `/` (e.g., `GET /entity/Sun/Earth`), not `.` as in flecs C++ API.

4. **JSON response for entities includes `parent` and `name` fields** (not full path). Client must reconstruct full paths from hierarchy.

5. **Component values require reflection data.** Components without registered reflection metadata return `null` values. CELS registers component types but may not register full reflection metadata for all fields.

## Main Loop Architecture

```c
// Pseudocode for the main event loop
int main(int argc, char *argv[]) {
    // 1. Parse arguments (host, port, poll interval)
    app_state_t state = app_state_init(argc, argv);

    // 2. Initialize ncurses
    tui_init();

    // 3. Initialize HTTP client
    http_client_t *client = http_client_init(state.host, state.port);

    // 4. Initialize data model
    data_model_t *model = data_model_init();

    // 5. Initialize tab system
    tab_system_t *tabs = tab_system_init(model);

    // 6. Main loop
    struct timespec last_poll = {0};
    while (state.running) {
        // 6a. Handle input (non-blocking)
        int ch = getch();
        if (ch != ERR) {
            handle_input(&state, tabs, ch);
        }

        // 6b. Poll if interval elapsed
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (elapsed_ms(last_poll, now) >= state.poll_interval_ms) {
            http_poll_endpoints(client, model, state.active_tab);
            last_poll = now;
        }

        // 6c. Render
        tui_render_chrome(&state);         // header, footer, tab bar
        tab_system_render(tabs, &state);   // active tab content
        doupdate();                        // flush to terminal

        // 6d. Sleep to avoid busy-wait (target ~30fps for TUI)
        usleep(33000);  // ~30ms
    }

    // 7. Cleanup
    tab_system_fini(tabs);
    data_model_fini(model);
    http_client_fini(client);
    tui_fini();
    return 0;
}
```

### Non-Blocking I/O Strategy

**ncurses input:** Use `timeout(0)` at initialization so `getch()` returns `ERR` immediately when no key is pressed. This prevents blocking.

**HTTP requests:** For v0.1, use **blocking sockets with a short timeout** (e.g., 100ms `SO_RCVTIMEO`). This is acceptable because:
- The server is localhost (sub-millisecond latency)
- Requests are small (JSON payloads typically under 100KB)
- The 30ms render loop already provides natural pacing

Future optimization: non-blocking sockets with `poll()` or `select()`.

### Smart Polling Strategy

Not all endpoints need to be polled at the same rate. The active tab drives which endpoints are polled:

| Active Tab | Endpoints Polled | Suggested Interval |
|------------|------------------|--------------------|
| Overview | `/stats/world` | 500ms |
| Entities | `/query?expr=...` + entity detail | 1000ms |
| Components | `/components` | 2000ms (rarely changes) |
| Systems | `/queries` + `/stats/pipeline` | 1000ms |
| State | `/query?expr=...` (state components) | 500ms |
| Performance | `/stats/world` + `/stats/pipeline` | 250ms (needs frequent updates) |

Always poll `/stats/world` at low frequency for the status bar (connection alive, fps display).

## Tab System Extensibility Design

### Tab Interface (vtable pattern)

```c
typedef struct tab_t {
    const char *name;          // Display name for tab bar
    int  key;                  // Keyboard shortcut (1-9)

    // Lifecycle
    void (*init)(struct tab_t *self, data_model_t *model);
    void (*fini)(struct tab_t *self);

    // Rendering
    void (*draw)(struct tab_t *self, WINDOW *win, app_state_t *state);

    // Input (tab-specific keys, return true if handled)
    bool (*handle_input)(struct tab_t *self, int ch, app_state_t *state);

    // Data requirements (which endpoints this tab needs)
    uint32_t  required_endpoints;   // Bitmask of ENDPOINT_* flags

    // Tab-private data
    void *userdata;
} tab_t;

// Endpoint bitmask flags
#define ENDPOINT_STATS_WORLD     (1 << 0)
#define ENDPOINT_STATS_PIPELINE  (1 << 1)
#define ENDPOINT_COMPONENTS      (1 << 2)
#define ENDPOINT_QUERIES         (1 << 3)
#define ENDPOINT_ENTITY_QUERY    (1 << 4)
#define ENDPOINT_ENTITY_DETAIL   (1 << 5)
```

### Tab Registry

```c
typedef struct tab_system_t {
    tab_t  *tabs;          // Array of registered tabs
    int     tab_count;
    int     active_tab;    // Index of currently active tab
    WINDOW *content_win;   // ncurses window for tab content
    data_model_t *model;   // Shared data model
} tab_system_t;

// Registration (called during init)
void tab_system_register(tab_system_t *sys, tab_t *tab);

// Runtime
void tab_system_switch(tab_system_t *sys, int tab_index);
void tab_system_render(tab_system_t *sys, app_state_t *state);
bool tab_system_handle_input(tab_system_t *sys, int ch, app_state_t *state);
uint32_t tab_system_active_endpoints(tab_system_t *sys);
```

### Future: Custom Developer Tabs

The vtable pattern enables custom tabs in future versions:

```c
// Developer creates a custom tab
tab_t my_debug_tab = {
    .name = "Inventory",
    .key = 7,
    .init = my_tab_init,
    .draw = my_tab_draw,
    .handle_input = my_tab_input,
    .required_endpoints = ENDPOINT_ENTITY_QUERY,
};
tab_system_register(tabs, &my_debug_tab);
```

This is out of scope for v0.1 but the vtable design supports it without architectural changes.

## Data Model Design

### Central Data Store

```c
typedef struct data_model_t {
    // Connection state
    bool            connected;
    double          last_poll_time;
    int             consecutive_failures;

    // World stats (from /stats/world)
    world_stats_t   world_stats;
    bool            world_stats_valid;

    // Entity data (from /query or /entity)
    entity_node_t  *entity_tree;     // Root of entity hierarchy
    int             entity_count;
    bool            entities_valid;

    // Component registry (from /components)
    component_info_t *components;
    int               component_count;
    bool              components_valid;

    // System/query info (from /queries)
    query_info_t   *queries;
    int             query_count;
    bool            queries_valid;

    // Pipeline stats (from /stats/pipeline)
    pipeline_stats_t pipeline;
    bool             pipeline_valid;

    // Timestamp of last successful update per category
    double          last_update[6];  // One per data category
} data_model_t;
```

### Entity Tree Node

```c
typedef struct entity_node_t {
    char           *name;
    char           *full_path;
    uint64_t        entity_id;

    // Tags
    char          **tags;
    int             tag_count;

    // Components (name -> JSON value string)
    component_value_t *component_values;
    int                component_value_count;

    // Hierarchy
    struct entity_node_t *parent;
    struct entity_node_t *children;
    int                   child_count;

    // UI state
    bool            expanded;       // Tree expand/collapse
} entity_node_t;
```

### Change Detection for State Tab

```c
typedef struct state_entry_t {
    char   *name;
    char   *current_value;    // JSON string of current value
    char   *previous_value;   // JSON string from last poll
    double  changed_at;       // Timestamp of last change (for highlight decay)
    bool    changed;          // True if current != previous
} state_entry_t;
```

## ncurses Window Layout

```
+--------------------------------------------------------------+
| CELS Debug v0.1 | Connected to localhost:27750 | 60 fps | 5e |  <- header (1 line)
+--------------------------------------------------------------+
| [Overview] Entities  Components  Systems  State  Performance  |  <- tab bar (1 line)
+--------------------------------------------------------------+
|                                                               |
|                                                               |
|                     Tab Content Area                          |
|                   (height - 4 lines)                          |
|                                                               |
|                                                               |
+--------------------------------------------------------------+
| q:quit  TAB:next  1-6:tab  r:refresh  Poll: 500ms            |  <- footer (1 line)
+--------------------------------------------------------------+
```

### Window Structure

```c
// Three ncurses WINDOWs (not overlapping, no panels needed)
WINDOW *header_win;   // 1 line at top
WINDOW *tab_bar_win;  // 1 line below header
WINDOW *content_win;  // Remaining space (passed to active tab)
WINDOW *footer_win;   // 1 line at bottom
```

Panels library is NOT needed because the windows do not overlap. Simple `newwin()` with `wresize()` on terminal resize (`KEY_RESIZE` handling) is sufficient.

## HTTP Client Design

### Why Raw Sockets (Not libcurl)

For v0.1, a minimal HTTP/1.1 GET client using POSIX sockets is recommended:

1. **Zero dependencies.** No need to link libcurl or any other library.
2. **Localhost only.** No TLS, no redirects, no authentication needed.
3. **Simple protocol.** Only GET requests with query parameters.
4. **Small responses.** JSON payloads are typically < 100KB.

The entire HTTP client can be ~200 lines of C.

### Implementation Sketch

```c
typedef struct http_client_t {
    char    host[256];
    int     port;
    int     socket_fd;       // -1 when disconnected
    int     timeout_ms;      // SO_RCVTIMEO
    char   *recv_buffer;     // Response accumulator
    size_t  recv_buffer_size;
} http_client_t;

// Core API
http_client_t *http_client_init(const char *host, int port);
void            http_client_fini(http_client_t *client);

// Returns malloc'd response body (caller frees), or NULL on error
char *http_get(http_client_t *client, const char *path);

// Connection management
bool http_client_is_connected(http_client_t *client);
void http_client_reconnect(http_client_t *client);
```

### Connection Resilience

The HTTP client must handle the target application not running, crashing, or restarting:

1. **Connection refused:** Set `connected = false`, show "Disconnected" in header, retry every 2 seconds.
2. **Timeout:** Increment `consecutive_failures`. After 3 failures, mark disconnected.
3. **Reconnect:** On successful response after disconnection, reset failure counter, show "Connected".
4. **Graceful degradation:** When disconnected, tabs show "Waiting for connection..." with last-known data grayed out.

## JSON Parser Selection

### Recommendation: cJSON

**Use cJSON** for JSON parsing. Rationale:

| Criterion | cJSON | yyjson | Hand-rolled |
|-----------|-------|--------|-------------|
| C99 compatible | Yes (ANSI C) | Yes | Yes |
| Integration | 2 files (cJSON.c + cJSON.h) | 2 files | 0 files |
| API simplicity | Very simple DOM API | Fast but more complex | Maximum control |
| Performance | Adequate for <100KB payloads | Fastest | Variable |
| Maintenance | Well-maintained, MIT license | Well-maintained | On us |

cJSON is the right choice because:
- Payloads are small (localhost, <100KB typically)
- Performance is not the bottleneck (polling at 250ms-2000ms)
- The DOM API (`cJSON_GetObjectItem`, `cJSON_GetArrayItem`) maps naturally to the flecs JSON structure
- Single-file integration, no build system changes needed
- **Confidence: HIGH** -- cJSON is mature, widely used, ANSI C compatible

### Integration

```bash
# Add to tools/cels-debug/vendor/
curl -O https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
curl -O https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h
```

Or use FetchContent in CMake (consistent with CELS's approach to flecs).

## Suggested Build Order

This ordering minimizes risk and provides visible progress at each step.

### Phase 1: Foundation (build, connect, prove it works)
1. **CMake setup** -- `tools/cels-debug/CMakeLists.txt`, link ncurses, integrate cJSON
2. **HTTP client** -- Raw socket GET against `localhost:27750/stats/world`
3. **JSON parser integration** -- Parse `/stats/world` response into a struct
4. **Minimal TUI** -- ncurses init, single screen showing "Connected: yes, FPS: 60"

**Why first:** This proves the entire data pipeline works end-to-end. Every subsequent phase builds on this proven foundation.

### Phase 2: Chrome and Tab System
5. **Window layout** -- Header, tab bar, content area, footer
6. **Tab system vtable** -- Registry, switching, dispatch
7. **Overview tab** -- Dashboard with entity count, fps, system count from `/stats/world`
8. **Input handling** -- Tab switching (1-6, TAB), quit (q), resize (KEY_RESIZE)

**Why second:** Establishes the UI framework that all subsequent tabs plug into.

### Phase 3: Entity and Component Tabs
9. **Data model** -- Entity tree structure, component info storage
10. **Entity tree queries** -- Query `/world` or `/query?expr=...` for entity list
11. **Entities tab** -- Tree rendering with expand/collapse, component value display
12. **Components tab** -- Component registry from `/components` endpoint

**Why third:** Entities are the core of ECS debugging. This is the highest-value tab.

### Phase 4: Systems and Pipeline
13. **System/query data** -- Parse `/queries` and `/stats/pipeline` responses
14. **Systems tab** -- System list grouped by phase, execution times, sync points
15. **Smart polling** -- Endpoint selection based on active tab, variable intervals

**Why fourth:** Systems visibility completes the "understand the ECS world" story.

### Phase 5: State and Performance
16. **State tab** -- CELS state values, change detection, highlight on change
17. **Performance tab** -- Frame timing sparklines, per-system bar charts
18. **Connection resilience** -- Reconnect logic, graceful degradation, status display

**Why last:** These are the most CELS-specific tabs and benefit from all prior infrastructure.

## Anti-Patterns to Avoid

### Anti-Pattern 1: Fetching Data in Render Functions
**What:** Tab's `draw()` function makes HTTP requests inline.
**Why bad:** Blocks rendering, causes UI freezes, makes render time unpredictable.
**Instead:** Tabs read from `data_model`. HTTP polling happens in the main loop before rendering.

### Anti-Pattern 2: One Giant Main Loop Function
**What:** All input handling, HTTP, parsing, and rendering in a single 500-line function.
**Why bad:** Unmaintainable, impossible to add new tabs, hard to test.
**Instead:** Separate concerns into distinct modules with clear interfaces.

### Anti-Pattern 3: Blocking HTTP Reads
**What:** Using `recv()` without timeout, causing the entire TUI to freeze when the server is slow or dead.
**Why bad:** User cannot interact during a frozen read. Terminal may appear hung.
**Instead:** Set `SO_RCVTIMEO` on the socket (100ms is reasonable for localhost).

### Anti-Pattern 4: Polling All Endpoints Every Cycle
**What:** Hitting every REST endpoint regardless of which tab is active.
**Why bad:** Wastes bandwidth, increases server load, slower responses.
**Instead:** Use `required_endpoints` bitmask from the active tab. Always poll `/stats/world` for connection health.

### Anti-Pattern 5: Repainting the Entire Screen Every Frame
**What:** Calling `clear()` + full redraw on every loop iteration.
**Why bad:** Terminal flicker, high CPU usage, poor UX.
**Instead:** Use `wnoutrefresh()` per window, `doupdate()` once. Only repaint changed regions. ncurses handles diff internally.

### Anti-Pattern 6: Storing Raw JSON Strings as the Data Model
**What:** Keeping the raw JSON response and re-parsing it every render frame.
**Why bad:** Slow, fragile, forces every tab to know JSON structure.
**Instead:** Parse once into typed C structs. Tabs work with clean domain objects.

## Scalability Considerations

| Concern | At 100 entities | At 10K entities | At 100K entities |
|---------|-----------------|-----------------|------------------|
| Entity query response size | ~10KB, no issue | ~1MB, needs pagination | ~10MB, must paginate |
| Entity tree rendering | All visible | Virtual scrolling needed | Virtual scrolling + search |
| Component data | Inline display | Inline display | Summary only, detail on select |
| Poll interval impact | Negligible | Noticeable latency | Significant, reduce poll rate |
| Memory usage | ~1MB | ~10MB | ~100MB, need lazy loading |

### Pagination Strategy

The flecs `/query` endpoint supports `?offset=N&limit=M`. For v0.1:
- Default limit: 1000 entities per query
- Implement basic "load more" or page navigation
- Display total count in tab header

## Future: Embedded Mode Architecture

The architecture supports a future embedded mode where cels-debug runs in-process with the CELS application instead of over HTTP:

```c
// data_source.h -- abstract data source interface
typedef struct data_source_t {
    // Poll for fresh data (implementation differs)
    void (*poll)(struct data_source_t *self, data_model_t *model, uint32_t endpoints);

    // Connection status
    bool (*is_connected)(struct data_source_t *self);

    // Cleanup
    void (*fini)(struct data_source_t *self);

    void *ctx;  // Implementation-specific context
} data_source_t;

// Two implementations:
data_source_t *data_source_http_init(const char *host, int port);    // v0.1
data_source_t *data_source_embedded_init(CELS_Context *cels_ctx);    // Future
```

This abstraction is NOT needed for v0.1 but the component boundaries are designed so it can be added later by replacing `http_client` + `json_parser` with a direct memory reader.

## File Structure

```
tools/cels-debug/
  CMakeLists.txt
  src/
    main.c              -- Entry point, main loop, argument parsing
    app_state.h         -- Application state struct and helpers
    app_state.c
    http_client.h       -- Minimal HTTP GET client
    http_client.c
    json_parser.h       -- flecs JSON response -> C structs
    json_parser.c
    data_model.h        -- Central data store types and API
    data_model.c
    tui.h               -- ncurses init/fini, window layout, chrome
    tui.c
    tab_system.h        -- Tab registry, vtable, dispatch
    tab_system.c
    tabs/
      tab_overview.h    -- Overview dashboard
      tab_overview.c
      tab_entities.h    -- Entity tree browser
      tab_entities.c
      tab_components.h  -- Component type registry
      tab_components.c
      tab_systems.h     -- System/pipeline viewer
      tab_systems.c
      tab_state.h       -- CELS state inspector
      tab_state.c
      tab_performance.h -- Performance metrics
      tab_performance.c
  vendor/
    cJSON.h             -- JSON parser (vendored)
    cJSON.c
  .planning/
    ...
```

## Sources

**HIGH confidence (verified from source code):**
- flecs REST API endpoints: `/home/cachy/workspaces/libs/cels/build/_deps/flecs-src/src/addons/rest.c` (request dispatcher at line 1976-2067, stats serialization at lines 802-1105)
- flecs REST API documentation: `/home/cachy/workspaces/libs/cels/build/_deps/flecs-src/docs/FlecsRemoteApi.md`
- flecs REST header: `/home/cachy/workspaces/libs/cels/build/_deps/flecs-src/include/flecs/addons/rest.h` (default port 27750)
- CELS architecture: `/home/cachy/workspaces/libs/cels/src/cels.cpp` (REST enabled at line 511-514, NO FlecsStats import)
- CELS project definition: `/home/cachy/workspaces/libs/cels/.planning/PROJECT.md`

**MEDIUM confidence (web research, verified with multiple sources):**
- ncurses panel architecture: [TLDP Panel Library HOWTO](https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/panels.html)
- ncurses event loop patterns: [Event Loops and NCurses](https://linuxjedi.co.uk/event-loops-and-ncurses/), [The Ncurses Feedback Loop](https://hoop.dev/blog/the-ncurses-feedback-loop/)
- Model-View-Controller for TUI: [The Elm Architecture | Ratatui](https://ratatui.rs/concepts/application-patterns/the-elm-architecture/)
- cJSON library: [GitHub - DaveGamble/cJSON](https://github.com/DaveGamble/cJSON)
- yyjson comparison: [GitHub - ibireme/yyjson](https://github.com/ibireme/yyjson)
- C HTTP client via sockets: [GitHub - langhai/http-client-c](https://github.com/langhai/http-client-c)
