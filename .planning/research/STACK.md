# Technology Stack

**Project:** CELS Debug TUI
**Researched:** 2026-02-05
**Overall confidence:** HIGH

## Recommended Stack

### Core Framework

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| C99 | — | Language standard | Matches CELS public API convention; parent project already uses C99 for test targets and backends | HIGH |
| CMake | >= 3.21 | Build system | Parent CELS project uses CMake 3.21; FetchContent for dependencies | HIGH |

### TUI Rendering

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| ncurses (ncursesw) | 6.5+ | Terminal rendering | System package, wide-character support, battle-tested for TUI apps; already installed (6.5.20240427) | HIGH |
| ncurses panel library | 6.5+ | Window/tab management | Part of ncurses distribution; manages overlapping windows and z-ordering; perfect for tab-based layout | HIGH |

**Note on ncurses vs alternatives:** ncurses is the correct choice for this project. Alternatives like notcurses or FTXUI are C++ only. Termbox is too minimal (no panels, no menus, no forms). ncurses is the only production-grade C TUI library with panel support, and it is already a project constraint.

### HTTP Client

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| libcurl (easy interface) | 8.x | HTTP GET polling | Synchronous easy interface is simple for polling; handles connection lifecycle, timeouts, error reporting; system package available (8.18.0 installed); CMake has built-in FindCURL module | HIGH |

**Why libcurl over raw sockets:**

Raw POSIX sockets were considered for the localhost-only use case. However, libcurl wins for three reasons:

1. **Error handling** -- libcurl provides structured error codes (CURLE_COULDNT_CONNECT, CURLE_OPERATION_TIMEDOUT) and human-readable error strings via curl_easy_strerror(). Raw sockets require manual errno interpretation and partial-read handling.
2. **Timeout control** -- CURLOPT_TIMEOUT_MS and CURLOPT_CONNECTTIMEOUT_MS give precise non-blocking timeout behavior. With raw sockets you need select()/poll() loops and manual timer management.
3. **Reconnection** -- The debugger needs graceful disconnect/reconnect when the target app restarts. libcurl handles connection reuse and clean failure modes. Raw sockets require manual reconnection state machines.
4. **Future-proofing** -- If remote debugging (non-localhost) is ever needed, libcurl already supports it. Raw sockets would need a rewrite.

The overhead is negligible: libcurl is a system library (not bundled), and the easy interface adds zero complexity beyond what raw sockets would require.

**Why NOT libcurl multi interface:** The multi interface is for concurrent transfers. This debugger polls sequentially (one endpoint at a time, on a timer). The easy interface is simpler and sufficient.

### JSON Parser

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| yyjson | 0.10.0+ | JSON parsing | Fastest C JSON library; ANSI C compatible (works with C99); only 2 files (yyjson.h + yyjson.c); clean immutable + mutable document API; already installed on system (0.12.0); CMake FetchContent support | HIGH |

**Why yyjson over alternatives:**

| Library | Verdict | Reason |
|---------|---------|--------|
| **yyjson** | **RECOMMENDED** | Fastest, 2-file integration, ANSI C, clean API, read-only and mutable modes |
| cJSON | Rejected | Slower, known CMake FetchContent header issues (GitHub issue #816), older API design |
| jsmn | Rejected | Token-based (no tree), requires manual value extraction, exponential parse time on large inputs |
| Jansson | Rejected | Heavier dependency, reference-counted API adds complexity, no performance advantage |
| json-c | Rejected | System library but heavier API, thread-safety concerns, overkill for this use case |

yyjson's immutable document API (`yyjson_doc`, `yyjson_val`) maps perfectly to our read-only inspection use case. Parse the response, walk the tree, extract values, free the document. No mutation needed.

### Dependency Strategy

| Dependency | Source | Rationale |
|------------|--------|-----------|
| ncurses + panel | **System package** (`find_package`) | Always available on Linux; too large to bundle; ncurses is a system-level dependency |
| libcurl | **System package** (`find_package`) | Always available on Linux; too large to bundle; CMake has built-in FindCURL |
| yyjson | **FetchContent** (bundled at build time) | Small (2 files); avoids requiring users to install yyjson system-wide; version-pinned for reproducibility |

## CMake Integration

### Finding System Dependencies

```cmake
# ncurses (with wide-character and panel support)
set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

# Panel library (not found by FindCurses, must be found manually)
find_library(PANEL_LIBRARY NAMES panelw panel)
if(NOT PANEL_LIBRARY)
    message(FATAL_ERROR "ncurses panel library not found. Install libncurses-dev or ncurses-devel.")
endif()

# libcurl
find_package(CURL REQUIRED)
```

### Fetching yyjson

```cmake
include(FetchContent)

FetchContent_Declare(
    yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG        0.10.0
)

# Disable yyjson tests and docs
set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YYJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(YYJSON_BUILD_MISC OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(yyjson)
```

### Linking the Target

```cmake
add_executable(cels-debug
    src/main.c
    src/tui.c
    src/http.c
    src/json_parse.c
    # ... additional source files
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
    ${PANEL_LIBRARY}
    CURL::libcurl
    yyjson
)
```

### Integration with Parent CELS CMakeLists.txt

The parent CELS project should add this as an optional subdirectory:

```cmake
# In CELS root CMakeLists.txt
option(CELS_BUILD_TOOLS "Build CELS development tools" OFF)

if(CELS_BUILD_TOOLS)
    add_subdirectory(tools/cels-debug)
endif()
```

This keeps the debugger opt-in and does not affect the core library build.

## Installation Requirements

### Arch Linux / CachyOS (current system)

```bash
# Already installed on this system:
# ncurses 6.5, libcurl 8.18.0, yyjson 0.12.0

# If needed:
sudo pacman -S ncurses curl
# yyjson is fetched via CMake FetchContent (no system install needed)
```

### Ubuntu / Debian

```bash
sudo apt install libncurses-dev libcurl4-openssl-dev
# yyjson is fetched via CMake FetchContent
```

### Fedora / RHEL

```bash
sudo dnf install ncurses-devel libcurl-devel
# yyjson is fetched via CMake FetchContent
```

## Alternatives Considered

### HTTP Client Alternatives

| Option | Considered | Why Rejected |
|--------|-----------|--------------|
| Raw POSIX sockets | Yes | No timeout management, no error abstraction, manual HTTP parsing, no reconnection handling |
| libhttp (markusfisch) | Yes | Tiny project, no CMake support, no community adoption, unclear maintenance |
| Serf (Apache) | Yes | Heavy APR dependency, designed for Apache modules not standalone tools |
| tiny-curl (wolfSSL) | Yes | Embedded-focused, not widely packaged, adds complexity vs standard libcurl |
| nng / nanomsg | No | Messaging library, not HTTP client; wrong abstraction level |

### JSON Parser Alternatives

| Option | Considered | Why Rejected |
|--------|-----------|--------------|
| cJSON | Yes | Known CMake integration issues; slower than yyjson; API is adequate but not as clean |
| jsmn | Yes | Token-only (no document tree); requires manual string comparison for keys; O(n^2) on large inputs |
| Jansson | Yes | Reference-counted memory model adds unnecessary complexity; no speed advantage |
| json-c | Yes | Larger API surface than needed; thread-safety model adds overhead |
| json.h (sheredom) | Briefly | Single-header but less battle-tested; limited community adoption |

### TUI Alternatives

| Option | Considered | Why Rejected |
|--------|-----------|--------------|
| Termbox/Termbox2 | Yes | No panel support, no menu/form widgets, too minimal for 6+ tab layout |
| notcurses | Yes | C++ only (requires C++ compiler), heavier, would conflict with C99 constraint |
| FTXUI | No | C++ only |
| Raw ANSI escape codes | No | Unmaintainable for complex multi-tab layout |

## Data Source: Flecs REST API Reference

The debugger connects to the flecs REST API. Key endpoints for v0.1:

| Endpoint | Method | Returns | Used For |
|----------|--------|---------|----------|
| `/entity/<path>` | GET | Entity with tags, pairs, components, values | Entity inspection, component values |
| `/query/?expr=<query>` | GET | Entities matching a flecs query expression | Entity listing, filtered views |
| `/world` | GET | All serializable world data | Overview dashboard, entity counts |
| `/component/<path>?component=<comp>` | GET | Single component value from an entity | Focused component inspection |

**Response format:** JSON. Example entity response:
```json
{
  "type": ["Name", "Position"],
  "entity": 330,
  "data": {
    "Name": { "value": "MyEntity" },
    "Position": { "x": 30.0, "y": 40.0 }
  }
}
```

**Connection details:**
- Default port: 27750 (configured in CELS `cels_init()`)
- Protocol: HTTP/1.1 (no TLS needed for localhost)
- Content-Type: application/json

## Architecture Implications

The stack choice drives these architectural boundaries:

```
+---------------------+
|   main.c            |  Entry point, argument parsing, main loop
+---------------------+
         |
    +----+----+
    |         |
+-------+  +--------+
| tui.c |  | http.c |   ncurses rendering / libcurl polling
+-------+  +--------+
    |         |
    |    +----------+
    |    | parse.c  |   yyjson response parsing
    |    +----------+
    |         |
    +----+----+
         |
+---------------------+
|   state/model       |  In-memory model of ECS world state
+---------------------+
```

- **tui.c** depends on ncurses + panel. Owns all rendering. Never touches HTTP.
- **http.c** depends on libcurl. Owns connection lifecycle, polling, raw response buffers.
- **parse.c** depends on yyjson. Transforms raw JSON into typed C structs.
- **state/model** is pure C structs with no library dependencies. Shared between layers.

This separation means each dependency is isolated to one module.

## What NOT to Use

| Technology | Why Not |
|------------|---------|
| **pthreads** (for v0.1) | Polling in the main loop with non-blocking curl is simpler. Threading adds complexity (shared state, races) without benefit for a single-connection debugger. Revisit only if UI becomes unresponsive. |
| **ncurses menu/form libraries** | Tab navigation is simpler than ncurses menus. Custom key handling gives more control. Forms are not needed for read-only v0.1. |
| **libev / libevent / libuv** | Event loop libraries are overkill for a single HTTP connection polled on a timer. ncurses already provides `timeout()` for non-blocking getch(). |
| **SQLite / any database** | No persistent storage needed. All data is transient (polled from REST API). |
| **Protocol Buffers / msgpack** | Flecs REST returns JSON. No binary protocol needed. |
| **C++ (any version)** | Project constraint is C99. The debugger is a standalone tool that should not require a C++ compiler. |

## Sources

- [FindCURL -- CMake Documentation](https://cmake.org/cmake/help/latest/module/FindCURL.html) -- HIGH confidence
- [FindCurses -- CMake Documentation](https://cmake.org/cmake/help/latest/module/FindCurses.html) -- HIGH confidence
- [yyjson GitHub](https://github.com/ibireme/yyjson) -- HIGH confidence
- [cJSON GitHub (FetchContent issue #816)](https://github.com/DaveGamble/cJSON/issues/816) -- MEDIUM confidence
- [Flecs Remote API Documentation](https://www.flecs.dev/flecs/md_docs_2FlecsRemoteApi.html) -- HIGH confidence
- [libcurl easy interface](https://curl.se/libcurl/c/libcurl-easy.html) -- HIGH confidence
- [libcurl simple example](https://curl.se/libcurl/c/simple.html) -- HIGH confidence
- [ncurses Panel Library](https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/panels.html) -- HIGH confidence
- [ncurses 6.6 announcement](https://invisible-island.net/ncurses/announce.html) -- HIGH confidence
- [Using libcurl with minimal dependencies](https://jyx.github.io/using-libcurl-with-minimal-dependencies.html) -- MEDIUM confidence
- [yyjson building and testing](https://ibireme.github.io/yyjson/doc/doxygen/html/building-and-testing.html) -- HIGH confidence
- [curl alternatives](https://curl.se/libcurl/competitors.html) -- MEDIUM confidence

## Verification Notes

### Verified on Current System (CachyOS)

```
ncurses:  6.5.20240427  (pkg-config ncursesw)
panel:    6.5.20240427  (pkg-config panelw)
libcurl:  8.18.0        (pkg-config libcurl, curl --version)
yyjson:   0.12.0        (pkg-config yyjson) -- system-installed but will FetchContent for portability
CMake:    FindCURL      confirmed working (cmake --find-package)
```

All three system dependencies (ncurses, panel, libcurl) are confirmed present and linkable. yyjson is also present as a system package but will be FetchContent-bundled for portability to systems without it.
