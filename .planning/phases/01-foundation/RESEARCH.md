# Phase 01: Foundation (Build, Connect, Render) - Research

**Researched:** 2026-02-05
**Domain:** CMake build system, ncurses TUI, libcurl HTTP, yyjson JSON, flecs REST API
**Confidence:** HIGH

## Summary

Phase 01 establishes the complete data pipeline: CMake build, ncurses TUI shell, HTTP polling via libcurl, JSON parsing via yyjson, and display of live world stats from the flecs REST API. The research covers eight domains: CMake integration with the parent CELS project, FlecsStats prerequisite in the CELS runtime, the exact JSON response format from `/stats/world`, ncurses initialization and signal handling, libcurl easy interface patterns, yyjson read-only document API, main loop design, and source file structure.

The most critical finding is that **without `ECS_IMPORT(world, FlecsStats)` in the CELS runtime, the `/stats/world` endpoint will crash with a null pointer dereference**. The flecs REST handler dereferences the stats pointer without a null check. This makes the FlecsStats import a hard prerequisite -- not just a "nice to have." It must be the first task in this phase.

All dependencies are verified and available: ncursesw and libcurl as system packages, yyjson via FetchContent. The parent CELS CMakeLists.txt uses CMake 3.21+ with FetchContent for flecs, providing a clear pattern to follow.

**Primary recommendation:** Start with the FlecsStats import (one-line change gated by `#ifdef CELS_DEBUG`), then build the CMake project, then implement the data pipeline bottom-up: HTTP client, JSON parser, data model, TUI shell, main loop.

## Standard Stack

The established libraries/tools for this domain:

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| ncursesw | 6.5+ | TUI rendering | Only production-grade C TUI library with wide-char/UTF-8 support; system package on all Linux distros |
| libcurl (easy) | 8.x | HTTP GET client | curl_easy with `CURLOPT_TIMEOUT_MS` provides non-blocking-enough localhost polling; system package |
| yyjson | 0.12.0 | JSON parsing | Fastest C JSON lib, immutable doc API perfect for read-only use, clean FetchContent integration |
| CMake | 3.21+ | Build system | Matches CELS parent project; FetchContent for yyjson, FindCurses/FindCURL for system packages |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| signal.h | POSIX | Signal handlers | SIGINT/SIGTERM/SIGSEGV cleanup for endwin() |
| time.h | POSIX | Timer for polling | `clock_gettime(CLOCK_MONOTONIC)` for 500ms poll interval |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| curl_easy | curl_multi | curl_multi is non-blocking but adds complexity; curl_easy with 200ms timeout is sufficient for localhost |
| yyjson | cJSON | cJSON has known CMake FetchContent issues (GitHub #816); yyjson API is cleaner for read-only |
| ncurses newwin() | ncurses panels | Panels add complexity for overlapping windows; our windows never overlap |

**Installation (system packages):**
```bash
# Arch Linux / CachyOS
sudo pacman -S ncurses curl

# Debian/Ubuntu
sudo apt install libncursesw5-dev libcurl4-openssl-dev
```

**FetchContent (yyjson) -- handled by CMake automatically.**

## Architecture Patterns

### Recommended Project Structure
```
tools/cels-debug/
├── CMakeLists.txt          # Tool-level build config
├── src/
│   ├── main.c              # Entry point, main loop, signal handlers
│   ├── http_client.h       # HTTP GET interface
│   ├── http_client.c       # libcurl implementation
│   ├── json_parser.h       # JSON-to-struct interface
│   ├── json_parser.c       # yyjson implementation
│   ├── data_model.h        # WorldSnapshot struct definitions
│   ├── data_model.c        # Snapshot alloc/free
│   ├── tui.h               # ncurses init/fini/render interface
│   └── tui.c               # ncurses implementation
└── .planning/              # Planning artifacts
```

### Pattern 1: Single-Threaded MVC Loop
**What:** Main loop polls input (ncurses), polls data on timer (HTTP), and renders (ncurses) -- all in one thread.
**When to use:** Always for Phase 01. ncurses is not thread-safe. libcurl easy is sufficient for localhost.
**Example:**
```c
// Source: Verified pattern from btop/k9s/lazygit architecture
while (g_running) {
    // 1. Input (non-blocking via ncurses timeout)
    int ch = getch();  // returns ERR if no input within timeout
    if (ch == 'q') break;

    // 2. Poll data on timer (500ms default)
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed_ms = (now.tv_sec - last_poll.tv_sec) * 1000.0
                      + (now.tv_nsec - last_poll.tv_nsec) / 1e6;
    if (elapsed_ms >= poll_interval_ms) {
        http_response_t resp = http_get(curl, url);
        if (resp.status == HTTP_OK) {
            world_snapshot_t *snap = json_parse_world_stats(resp.data, resp.size);
            if (snap) {
                world_snapshot_free(current_snapshot);
                current_snapshot = snap;
                connection_state = CONNECTED;
            }
        } else {
            connection_state = DISCONNECTED;
        }
        http_response_free(&resp);
        last_poll = now;
    }

    // 3. Render
    tui_render(current_snapshot, connection_state);
}
```

### Pattern 2: Snapshot Data Model
**What:** Each poll produces a new immutable snapshot struct. The previous snapshot is freed atomically after the new one is assigned. No partial updates.
**When to use:** Always. Prevents half-updated state during render.
**Example:**
```c
// Source: Established pattern for read-only data display tools
typedef struct world_snapshot {
    // From /stats/world response
    double entity_count;
    double fps;
    double frame_time_ms;
    double system_count;

    // Connection metadata
    int64_t timestamp_ms;
} world_snapshot_t;

world_snapshot_t *world_snapshot_create(void);
void world_snapshot_free(world_snapshot_t *snap);
```

### Pattern 3: Connection State Machine
**What:** Three-state connection model: CONNECTED, DISCONNECTED, RECONNECTING.
**When to use:** Always. Drives the header display and reconnect behavior.
**Example:**
```c
typedef enum {
    CONN_DISCONNECTED = 0,
    CONN_CONNECTED,
    CONN_RECONNECTING
} connection_state_t;
```

### Anti-Patterns to Avoid
- **wrefresh() per window:** Causes flicker. Use `wnoutrefresh()` on all windows, then `doupdate()` once per frame.
- **Blocking HTTP in main loop:** Never call `curl_easy_perform()` without `CURLOPT_TIMEOUT_MS`. Localhost may be down.
- **Threads for HTTP polling:** ncurses is not thread-safe. Single-threaded design eliminates this class of bugs.
- **Hardcoded terminal dimensions:** Always derive from `LINES`/`COLS` and handle `KEY_RESIZE`.

## Don't Hand-Roll

Problems that look simple but have existing solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON parsing | Custom parser | yyjson | JSON edge cases (escaping, unicode, numbers) are endless |
| HTTP GET | Raw sockets | libcurl | Timeout handling, connection pooling, error codes for free |
| Terminal I/O | Raw termios | ncurses | Handles terminal types, resize, keypad sequences, wide chars |
| Build system | Makefiles | CMake | FetchContent, FindCurses, FindCURL integrate deps automatically |
| Signal cleanup | Manual atexit | atexit(endwin) + signal handlers | Signal handlers alone miss some exit paths; atexit alone misses signals |

**Key insight:** Every one of these "simple" problems has a 10-line naive solution and a 200-line correct solution. The libraries handle the 200-line version.

## Common Pitfalls

### Pitfall 1: FlecsStats Not Imported (NULL Dereference)
**What goes wrong:** Without `ECS_IMPORT(world, FlecsStats)`, the `/stats/world` endpoint crashes the target application. The REST handler calls `ecs_get_pair(world, EcsWorld, EcsWorldStats, period)` which returns NULL, then dereferences `monitor_stats->stats` without null check.
**Why it happens:** FlecsStats is marked "optional" in flecs docs but the REST stats handler does not guard against its absence.
**How to avoid:** Add `ECS_IMPORT(world, FlecsStats)` to CELS `cels_init()` behind `#ifdef CELS_DEBUG`. This is a hard prerequisite, not optional.
**Warning signs:** Target app crashes when debugger connects and polls `/stats/world`.

### Pitfall 2: Terminal Left Corrupted on Crash
**What goes wrong:** If the program exits without calling `endwin()`, the terminal is left in raw/cbreak mode. User cannot see their typing.
**Why it happens:** SIGINT/SIGTERM/SIGSEGV bypass normal cleanup. `exit()` calls `atexit()` handlers but `_exit()` and signals do not.
**How to avoid:** Install signal handlers for SIGINT, SIGTERM, SIGSEGV, SIGABRT that call `endwin()` then `_exit()`. Also register `atexit(endwin)` as a safety net.
**Warning signs:** Running the program and pressing Ctrl+C leaves terminal broken.

### Pitfall 3: wrefresh() Flicker
**What goes wrong:** Calling `wrefresh()` on each window causes visible flicker because each call writes to the physical screen.
**Why it happens:** `wrefresh()` = `wnoutrefresh()` + `doupdate()`. Multiple `doupdate()` calls per frame causes partial screen updates.
**How to avoid:** Use `wnoutrefresh()` on every window, then call `doupdate()` exactly once at the end of the render function.
**Warning signs:** Screen flashes on each poll cycle.

### Pitfall 4: Blocking HTTP Freezes UI
**What goes wrong:** `curl_easy_perform()` blocks until response or TCP timeout (default 300 seconds). If the target app dies, the TUI freezes for 5 minutes.
**Why it happens:** Default libcurl has no per-request timeout for easy interface.
**How to avoid:** Set `CURLOPT_TIMEOUT_MS` to 200 and `CURLOPT_CONNECTTIMEOUT_MS` to 200. Localhost round-trip is sub-millisecond; 200ms caps worst-case block.
**Warning signs:** TUI becomes unresponsive when target app is killed.

### Pitfall 5: Polling Too Fast
**What goes wrong:** Polling at 60fps wastes CPU and generates unnecessary HTTP traffic.
**Why it happens:** Using ncurses timeout as the poll timer (e.g., `timeout(16)` = poll every 16ms).
**How to avoid:** Decouple poll timer from render timer. Use `clock_gettime(CLOCK_MONOTONIC)` for poll cadence (500ms) and `timeout(100)` for input responsiveness.
**Warning signs:** Target app shows high HTTP request count in stats.

### Pitfall 6: CMake FindCurses Returns ncurses Without Wide-Char
**What goes wrong:** `FindCurses` finds `/usr/lib/libncurses.so` which lacks wide-char support. UTF-8 characters render as garbage.
**Why it happens:** ncurses and ncursesw are separate libraries. `FindCurses` prefers ncurses.
**How to avoid:** Set `CURSES_NEED_WIDE TRUE` before `find_package(Curses)` or use `pkg_check_modules(NCURSESW REQUIRED ncursesw)`.
**Warning signs:** Unicode box-drawing characters display as question marks.

## Code Examples

Verified patterns from official sources:

### CMake Build Configuration
```cmake
# Source: Verified against parent CELS CMakeLists.txt conventions
cmake_minimum_required(VERSION 3.21)
project(cels-debug VERSION 0.1.0 LANGUAGES C)

# === Dependencies ===
include(FetchContent)

# yyjson - JSON parser (FetchContent, consistent with CELS flecs pattern)
FetchContent_Declare(
    yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG        0.12.0
)
FetchContent_MakeAvailable(yyjson)

# ncursesw - TUI (system package)
set(CURSES_NEED_WIDE TRUE)
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses REQUIRED)

# libcurl - HTTP client (system package)
find_package(CURL REQUIRED)

# === Executable ===
add_executable(cels-debug
    src/main.c
    src/http_client.c
    src/json_parser.c
    src/data_model.c
    src/tui.c
)

set_target_properties(cels-debug PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

target_include_directories(cels-debug PRIVATE
    ${CURSES_INCLUDE_DIRS}
)

target_link_libraries(cels-debug PRIVATE
    ${CURSES_LIBRARIES}
    CURL::libcurl
    yyjson
)
```

### Parent CELS CMakeLists.txt Integration
```cmake
# Source: Pattern from parent CELS CMakeLists.txt structure
# At the END of /home/cachy/workspaces/libs/cels/CMakeLists.txt:

# ============================================================================
# Tools
# ============================================================================
option(CELS_BUILD_TOOLS "Build CELS development tools" OFF)
if(CELS_BUILD_TOOLS)
    add_subdirectory(tools/cels-debug)
endif()
```

### FlecsStats Import in CELS Runtime
```cpp
// Source: Verified from flecs docs/FlecsRemoteApi.md and flecs stats.h
// Location: /home/cachy/workspaces/libs/cels/src/cels.cpp, in cels_init()
// Insert AFTER the EcsRest configuration (line 514) and BEFORE lifecycle system setup (line 517)

// cels_init() function, after EcsRest setup:
#ifdef CELS_DEBUG
        // Import FlecsStats for debug tooling (/stats/* REST endpoints)
        // Without this, the /stats/world endpoint will crash (null deref)
        ECS_IMPORT(g_context->ecs.c_ptr(), FlecsStats);
#endif
```

### CMake CELS_DEBUG Define
```cmake
# In parent CELS CMakeLists.txt, add a debug option:
option(CELS_DEBUG "Enable debug features (FlecsStats import)" OFF)
if(CELS_DEBUG)
    target_compile_definitions(cels PRIVATE CELS_DEBUG)
endif()

# When building with debug tools:
# cmake -DCELS_BUILD_TOOLS=ON -DCELS_DEBUG=ON ..
```

### ncurses Initialization Sequence
```c
// Source: ncurses man pages (curs_initscr, curs_inopts, curs_set)
// Verified against TLDP HOWTO and ncurses 6.5 docs

#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>

static void cleanup(void) {
    endwin();
}

static void signal_handler(int sig) {
    (void)sig;
    endwin();
    _exit(1);
}

void tui_init(void) {
    // Safety net for normal exits
    atexit(cleanup);

    // Signal handlers for abnormal exits
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);

    // Initialize ncurses
    initscr();              // Start curses mode
    cbreak();               // Disable line buffering
    noecho();               // Don't echo input
    keypad(stdscr, TRUE);   // Enable arrow keys, F-keys
    curs_set(0);            // Hide cursor
    timeout(100);           // getch() returns ERR after 100ms (non-blocking-ish)

    // Colors (if terminal supports them)
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN, -1);   // Connected
        init_pair(2, COLOR_RED, -1);     // Disconnected
        init_pair(3, COLOR_YELLOW, -1);  // Reconnecting
        init_pair(4, COLOR_CYAN, -1);    // Labels
    }
}

void tui_fini(void) {
    endwin();
}
```

### libcurl Easy Interface HTTP Client
```c
// Source: curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
// Source: curl.se/libcurl/c/CURLOPT_TIMEOUT_MS.html

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t size;
} http_buffer_t;

typedef struct {
    int status;          // HTTP status code or -1 on error
    http_buffer_t body;  // Response body (caller frees)
} http_response_t;

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    http_buffer_t *buf = (http_buffer_t *)userdata;

    char *new_data = realloc(buf->data, buf->size + total + 1);
    if (!new_data) return 0;  // Signal error to curl

    buf->data = new_data;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';

    return total;
}

CURL *http_client_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    // Configure for localhost polling
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 200L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 200L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // Thread-safe signal handling

    return curl;
}

http_response_t http_get(CURL *curl, const char *url) {
    http_response_t resp = {0};
    http_buffer_t buf = {NULL, 0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        resp.status = (int)http_code;
        resp.body = buf;
    } else {
        resp.status = -1;
        free(buf.data);
    }

    return resp;
}

void http_response_free(http_response_t *resp) {
    free(resp->body.data);
    resp->body.data = NULL;
    resp->body.size = 0;
}

void http_client_fini(CURL *curl) {
    if (curl) curl_easy_cleanup(curl);
    curl_global_cleanup();
}
```

### yyjson Parsing for /stats/world Response
```c
// Source: github.com/ibireme/yyjson API.md
// Verified against flecs rest.c stats serialization format

#include <yyjson.h>

// The /stats/world response has this structure per field:
// "entities.count": { "avg": [60 floats], "min": [...], "max": [...], "brief": "..." }
// We only need the LAST element of the "avg" array (most recent measurement).

static double extract_latest_gauge(yyjson_val *obj, const char *field) {
    yyjson_val *metric = yyjson_obj_get(obj, field);
    if (!metric) return 0.0;

    yyjson_val *avg = yyjson_obj_get(metric, "avg");
    if (!avg || !yyjson_is_arr(avg)) return 0.0;

    // Last element is the most recent (60-element circular buffer)
    size_t count = yyjson_arr_size(avg);
    if (count == 0) return 0.0;

    yyjson_val *last = yyjson_arr_get(avg, count - 1);
    if (!last) return 0.0;

    return yyjson_get_real(last);
}

world_snapshot_t *json_parse_world_stats(const char *json, size_t len) {
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return NULL;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return NULL;
    }

    world_snapshot_t *snap = world_snapshot_create();
    if (!snap) {
        yyjson_doc_free(doc);
        return NULL;
    }

    snap->entity_count   = extract_latest_gauge(root, "entities.count");
    snap->fps            = extract_latest_gauge(root, "performance.fps");
    snap->frame_time_ms  = extract_latest_gauge(root, "performance.frame_time") * 1000.0;
    snap->system_count   = extract_latest_gauge(root, "queries.system_count");

    yyjson_doc_free(doc);
    return snap;
}
```

### Main Loop Structure
```c
// Source: Synthesized from research -- standard TUI polling pattern

#include <time.h>

#define POLL_INTERVAL_MS 500
#define TUI_TIMEOUT_MS   100  // ncurses getch timeout

static volatile int g_running = 1;

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    const char *url = "http://localhost:27750/stats/world";

    // Initialize subsystems
    tui_init();
    CURL *curl = http_client_init();
    if (!curl) {
        tui_fini();
        fprintf(stderr, "Failed to initialize HTTP client\n");
        return 1;
    }

    world_snapshot_t *snapshot = NULL;
    connection_state_t conn_state = CONN_DISCONNECTED;

    struct timespec last_poll = {0, 0};

    // Main loop
    while (g_running) {
        // 1. Handle input
        int ch = getch();
        switch (ch) {
            case 'q':
            case 'Q':
                g_running = 0;
                break;
            case KEY_RESIZE:
                // Recalculate layout from LINES/COLS
                tui_resize();
                break;
            default:
                break;
        }

        // 2. Poll on timer
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed_ms = (now.tv_sec - last_poll.tv_sec) * 1000.0
                          + (now.tv_nsec - last_poll.tv_nsec) / 1e6;

        if (elapsed_ms >= POLL_INTERVAL_MS) {
            http_response_t resp = http_get(curl, url);
            if (resp.status == 200) {
                world_snapshot_t *new_snap = json_parse_world_stats(
                    resp.body.data, resp.body.size);
                if (new_snap) {
                    world_snapshot_free(snapshot);
                    snapshot = new_snap;
                    conn_state = CONN_CONNECTED;
                }
            } else {
                conn_state = (conn_state == CONN_CONNECTED)
                    ? CONN_RECONNECTING : CONN_DISCONNECTED;
            }
            http_response_free(&resp);
            last_poll = now;
        }

        // 3. Render
        tui_render(snapshot, conn_state);
    }

    // Cleanup
    world_snapshot_free(snapshot);
    http_client_fini(curl);
    tui_fini();

    return 0;
}
```

## Flecs REST API: /stats/world Response Format

**Confidence: HIGH** -- Verified directly from `build/_deps/flecs-src/src/addons/rest.c` lines 884-954.

The `/stats/world` endpoint returns a JSON object where each metric is a key with an object value containing `avg`, `min`, and `max` arrays. Each array has 60 elements (a circular buffer, `ECS_STAT_WINDOW = 60`). The most recent value is at the end of the array. A `brief` field provides a human-readable description.

### Key Fields for Phase 01
```json
{
    "entities.count":       { "avg": [60 floats], "min": [...], "max": [...], "brief": "Alive entity ids in the world" },
    "performance.fps":      { "avg": [60 floats], "min": [...], "max": [...], "brief": "Frames per second" },
    "performance.frame_time": { "avg": [60 floats], "min": [...], "max": [...], "brief": "Time spent in frame" },
    "queries.system_count": { "avg": [60 floats], "min": [...], "max": [...], "brief": "Systems in the world" }
}
```

### Full Field List (from rest.c)
**Entities:** `entities.count`, `entities.not_alive_count`
**Performance:** `performance.fps`, `performance.frame_time`, `performance.system_time`, `performance.emit_time`, `performance.merge_time`, `performance.rematch_time`
**Commands:** `commands.add_count`, `commands.remove_count`, `commands.delete_count`, `commands.clear_count`, `commands.set_count`, `commands.ensure_count`, `commands.modified_count`, `commands.other_count`, `commands.discard_count`, `commands.batched_entity_count`, `commands.batched_count`
**Frame:** `frame.merge_count`, `frame.pipeline_build_count`, `frame.systems_ran`, `frame.observers_ran`, `frame.event_emit_count`, `frame.rematch_count`
**Tables:** `tables.count`, `tables.empty_count`, `tables.create_count`, `tables.delete_count`
**Components:** `components.tag_count`, `components.component_count`, `components.pair_count`, `components.type_count`, `components.create_count`, `components.delete_count`
**Queries:** `queries.query_count`, `queries.observer_count`, `queries.system_count`
**Memory:** `memory.alloc_count`, `memory.realloc_count`, `memory.free_count`, `memory.outstanding_alloc_count`, `memory.block_alloc_count`, `memory.block_free_count`, `memory.block_outstanding_alloc_count`, `memory.stack_alloc_count`, `memory.stack_free_count`, `memory.stack_outstanding_alloc_count`
**HTTP:** `http.request_received_count`, `http.request_invalid_count`, `http.request_handled_ok_count`, `http.request_handled_error_count`, `http.request_not_handled_count`, `http.request_preflight_count`, `http.send_ok_count`, `http.send_error_count`

### Period Parameter
The endpoint accepts a `period` query parameter: `?period=1s` (default), `?period=1m`, `?period=1h`, `?period=1d`, `?period=1w`. Phase 01 uses the default 1s period.

### Without FlecsStats
If FlecsStats is not imported:
- `ecs_get_pair(world, EcsWorld, EcsWorldStats, period)` returns NULL
- `flecs_world_stats_to_json()` dereferences NULL pointer -- **crash**
- The REST server on the target app will segfault

## FlecsStats Prerequisite: Exact Implementation

**Confidence: HIGH** -- Verified from cels.cpp source and flecs source.

### Current State of cels_init()
Lines 505-514 of `src/cels.cpp`:
```cpp
CELS_Context* cels_init(void) {
    if (g_context == nullptr) {
        g_context = new CELS_Context();

        // Enable flecs REST explorer (available at http://localhost:27750)
        EcsRest rest_config = {};
        rest_config.port = 27750;
        ecs_set_id(g_context->ecs.c_ptr(), EcsWorld, ecs_id(EcsRest),
                   sizeof(EcsRest), &rest_config);
        // ... lifecycle system setup follows
```

### What to Add
Insert after line 514 (after EcsRest setup), before the lifecycle system setup:
```cpp
#ifdef CELS_DEBUG
        // Import FlecsStats module for debug tooling
        // Required for /stats/world and /stats/pipeline REST endpoints
        ECS_IMPORT(g_context->ecs.c_ptr(), FlecsStats);
#endif
```

### CELS_DEBUG Define
No `CELS_DEBUG` define currently exists in the CELS project. It must be created:
```cmake
# In CELS CMakeLists.txt
option(CELS_DEBUG "Enable debug features (FlecsStats import, debug REST endpoints)" OFF)
if(CELS_DEBUG)
    target_compile_definitions(cels PRIVATE CELS_DEBUG)
endif()
```

### ECS_IMPORT Macro Expansion
`ECS_IMPORT(world, FlecsStats)` expands to `ecs_import_c(world, FlecsStatsImport, "FlecsStats")`. This is a C macro that works with the raw `ecs_world_t*` pointer, which is what `g_context->ecs.c_ptr()` returns. Verified from `build/_deps/flecs-src/include/flecs/addons/module.h:119`.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| cJSON for JSON | yyjson 0.12.0 | 2024 | Cleaner FetchContent, immutable doc API, no known cmake issues |
| curl_multi for non-blocking | curl_easy with TIMEOUT_MS | Always valid for localhost | Simpler API, sufficient for sub-ms round trips |
| ncurses panels | Plain newwin() | N/A | Panels only needed for overlapping windows; we have non-overlapping layout |

**Deprecated/outdated:**
- Raw POSIX sockets for HTTP: libcurl handles timeouts, reconnection, error codes
- `napms()` for timing: `clock_gettime(CLOCK_MONOTONIC)` is more precise and drift-resistant

## Open Questions

Things that could not be fully resolved:

1. **Exact ncurses timeout value for best responsiveness**
   - What we know: `timeout(100)` means getch blocks up to 100ms. Lower = more responsive input, higher CPU. Higher = less responsive.
   - What's unclear: Whether 100ms feels sluggish for q-to-quit or if 50ms is better.
   - Recommendation: Start with `timeout(100)`. If input feels laggy, reduce to 50. This is trivially tunable.

2. **yyjson_get_real() for integer-formatted floats**
   - What we know: flecs serializes gauge values as floats. Some may be serialized as integers (e.g., `42` not `42.0`).
   - What's unclear: Whether `yyjson_get_real()` handles integer-typed JSON values or returns 0.
   - Recommendation: Use `yyjson_is_num()` check, then `yyjson_get_num()` which handles both int and real. Test with actual flecs output.

3. **FindCurses vs pkg-config for ncursesw**
   - What we know: CMake `FindCurses` with `CURSES_NEED_WIDE=TRUE` should work but behavior varies across distros.
   - What's unclear: Whether CachyOS/Arch provides the wide variant by default.
   - Recommendation: Try `FindCurses` first. If ncursesw not found, fall back to `pkg_check_modules(NCURSESW REQUIRED ncursesw)`.

## Sources

### Primary (HIGH confidence)
- `build/_deps/flecs-src/src/addons/rest.c` -- REST endpoint dispatcher, stats JSON serialization, verified `/stats/world` response format
- `build/_deps/flecs-src/include/flecs/addons/stats.h` -- `ecs_world_stats_t` struct, `ECS_STAT_WINDOW=60`, metric types
- `build/_deps/flecs-src/docs/FlecsRemoteApi.md` -- REST API documentation, `ECS_IMPORT(world, FlecsStats)` usage
- `build/_deps/flecs-src/include/flecs/addons/module.h` -- `ECS_IMPORT` macro definition
- `src/cels.cpp` -- CELS init, EcsRest configuration (lines 505-514), confirmed no FlecsStats import
- CELS `CMakeLists.txt` -- Build conventions (CMake 3.21, FetchContent for flecs, C99/C++17 standards)
- [yyjson API documentation](https://github.com/ibireme/yyjson/blob/master/doc/API.md) -- Read-only document API
- [libcurl easy interface](https://curl.se/libcurl/c/libcurl-easy.html) -- CURLOPT_TIMEOUT_MS, CURLOPT_WRITEFUNCTION
- [CURLOPT_WRITEFUNCTION](https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html) -- Write callback signature

### Secondary (MEDIUM confidence)
- [ncurses initialization HOWTO](https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/init.html) -- initscr/cbreak/noecho sequence
- [ncurses input options man page](https://invisible-island.net/ncurses/man/curs_inopts.3x.html) -- timeout(), nodelay() semantics
- [yyjson building and testing](https://ibireme.github.io/yyjson/doc/doxygen/html/building-and-testing.html) -- FetchContent integration

### Tertiary (LOW confidence)
- btop/k9s/lazygit architecture references -- Single-threaded TUI polling pattern (design pattern, not code)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- All libraries verified from source, versions confirmed, CMake integration patterns proven by parent project
- Architecture: HIGH -- Single-threaded MVC loop is the established pattern; data pipeline verified end-to-end from flecs REST source
- Pitfalls: HIGH -- FlecsStats null deref verified from source code; ncurses/libcurl pitfalls are extensively documented
- Flecs REST format: HIGH -- JSON structure read directly from flecs rest.c serialization code
- FlecsStats prerequisite: HIGH -- Import location, macro expansion, and CELS_DEBUG gating all verified from source

**Research date:** 2026-02-05
**Valid until:** 2026-04-05 (stable dependencies, unlikely to change)
