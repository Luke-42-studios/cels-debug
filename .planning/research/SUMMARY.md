# Project Research Summary

**Project:** CELS Debug TUI
**Domain:** C99 TUI debugger polling flecs REST API for ECS world inspection
**Researched:** 2026-02-05
**Confidence:** HIGH

## Executive Summary

CELS Debug is a terminal-based debugger that connects to the flecs REST API at localhost:27750 to inspect entities, components, systems, state, and performance in a running CELS application. The expert pattern for this type of tool is a single-threaded MVC loop with non-blocking HTTP polling and ncurses rendering -- the same architecture used by k9s, btop, and lazygit. The stack is C99 with ncurses(w) for TUI, libcurl for HTTP, and yyjson for JSON parsing, all integrated via CMake with system packages for ncurses/curl and FetchContent for yyjson.

The recommended approach is to build foundation-first: prove the end-to-end data pipeline (HTTP GET -> JSON parse -> ncurses render) in a minimal prototype, then layer the tab system and individual views on top. The tab vtable pattern allows each view (Overview, Entities, Components, Systems, State, Performance) to be developed independently once the foundation is solid. The primary technical risk is blocking the UI thread with synchronous HTTP calls -- this must be solved at architecture time, not retrofitted. A secondary risk is that CELS does not currently import FlecsStats, which means the Performance tab and per-system timing data will return empty responses until that is added to the CELS runtime.

The tool's value proposition over the existing flecs web explorer is threefold: it works over SSH without a browser, it understands CELS-specific concepts (State, Compositions, phase grouping), and it can highlight changes between polls -- something the web explorer cannot do. These differentiators should be prioritized after table stakes are met.

## Key Findings

### Recommended Stack

The stack is C99 with three dependencies. All researchers agree on the language and build system. The HTTP client and JSON parser had conflicting recommendations that are resolved below.

**Core technologies:**

| Technology | Purpose | Why |
|------------|---------|-----|
| **C99 + CMake 3.21** | Language and build | Matches CELS conventions; FetchContent for deps |
| **ncursesw 6.5+** | TUI rendering | Only production-grade C TUI library with wide-char support; system package |
| **libcurl (easy interface)** | HTTP client | Structured error handling, timeout control, reconnection for free; system package |
| **yyjson 0.10+** | JSON parsing | Fastest C JSON lib, 2-file integration, immutable doc API matches read-only use case; FetchContent |

**Conflict resolution -- HTTP client (libcurl vs raw sockets):**

STACK.md recommends libcurl. ARCHITECTURE.md recommends raw POSIX sockets. PITFALLS.md recommends curl_multi (non-blocking libcurl).

**Decision: libcurl easy interface with short timeouts.** Rationale:
- Raw sockets save zero dependencies (libcurl is a system package, already installed) but require manual timeout management, reconnection state machines, and HTTP parsing
- libcurl provides `CURLOPT_TIMEOUT_MS` and `CURLOPT_CONNECTTIMEOUT_MS` which eliminate the blocking-UI pitfall (PITFALLS P1) without needing curl_multi complexity
- With `CURLOPT_TIMEOUT_MS` set to 100-200ms for localhost, the blocking window is negligible (sub-frame at 30fps TUI refresh)
- curl_multi is overkill for a single sequential connection -- it adds API complexity for no benefit at this polling rate
- If blocking becomes a problem in practice, curl_multi is a drop-in upgrade path from easy interface (same library, same linking)

**Conflict resolution -- JSON parser (yyjson vs cJSON):**

STACK.md recommends yyjson. ARCHITECTURE.md recommends cJSON.

**Decision: yyjson.** Rationale:
- cJSON has known CMake FetchContent issues (GitHub issue #816) -- exactly how this project integrates dependencies
- yyjson's immutable document API (`yyjson_doc`, `yyjson_val`) is a perfect match: parse once, walk tree, extract values, free -- no mutation needed
- yyjson is faster, though performance is not the bottleneck
- Both are 2-file integrations; yyjson's API is cleaner for the read-only use case
- yyjson single `yyjson_doc_free()` releases all memory in one call, simplifying the snapshot pattern recommended by PITFALLS.md (P11)

**What NOT to use:** pthreads (v0.1), ncurses menu/form libs, libev/libuv, SQLite, C++, event loop frameworks.

### Expected Features

**Must have (table stakes) -- what makes it minimally useful vs the flecs web explorer:**

1. Connection status indicator (Connected/Disconnected/Reconnecting in header)
2. Tab/view navigation (6 tabs: Overview, Entities, Components, Systems, State, Performance)
3. Entity list with component names, select-to-inspect component values
4. System list (ideally grouped by CELS phase)
5. Frame timing display (FPS, delta time in status bar)
6. Keyboard-driven navigation (j/k, arrow keys, Enter, Esc, q)
7. Auto-refresh polling at configurable interval (default 500ms)
8. Graceful disconnect handling with auto-reconnect

**Should have (differentiators) -- why use this over the web explorer:**

1. Change highlighting (color changed values between polls -- web explorer cannot do this)
2. CELS State tab (purpose-built view for State() values with change tracking)
3. System execution grouped by CELS phase
4. Entity parent-child tree view (keyboard navigation faster than clicking)
5. Per-system execution time (requires FlecsStats -- see critical finding below)
6. Filter/search in entity and system lists
7. Configurable poll rate (runtime toggle)

**Defer to post-MVP:**

- Live editing of component values (needs PUT, validation, undo)
- Watch expressions (needs expression parser)
- ASCII graphs/sparklines (fiddly, not core value)
- Log streaming (no flecs REST endpoint for logs)
- Breakpoints/pause/step (needs embedded mode + CELS runtime changes)
- Mouse support, config files, color themes, query REPL

### Architecture Approach

Single-threaded MVC loop. The main loop runs at ~30fps, handles non-blocking input via `timeout(0)` on getch, polls HTTP endpoints on a timer (500ms default), and renders the active tab. Data flows one direction: HTTP response -> JSON parse -> data model snapshot -> tab rendering. Tabs are views into a shared data model via a vtable interface. Each tab declares which endpoints it needs (bitmask), and only those endpoints are polled when that tab is active.

**Major components:**

1. **main.c** -- Entry point, argument parsing, main event loop orchestration
2. **http_client** -- libcurl-based HTTP GET with timeout, connection state machine, reconnection
3. **json_parser** -- yyjson response parsing into typed C structs (snapshot pattern)
4. **data_model** -- Central data store: entities, components, systems, stats; owns all data
5. **tui** -- ncurses init/fini, window layout (header, tab bar, content, footer), chrome rendering
6. **tab_system** -- Tab registry with vtable dispatch (init/fini/draw/handle_input per tab)
7. **tabs/** -- 6 tab implementations (overview, entities, components, systems, state, performance)
8. **app_state** -- Active tab, scroll positions, connection status, poll interval, running flag

**Key architectural patterns:**
- Snapshot-based data ownership: each poll produces a new snapshot that owns all strings; previous snapshot freed atomically
- Tab vtable with `required_endpoints` bitmask for smart polling
- Non-overlapping ncurses windows (no panels needed) with `wnoutrefresh()` + single `doupdate()`
- Layout derived from LINES/COLS, recalculated on KEY_RESIZE

### Critical Pitfalls

The top 5 pitfalls that can cause rewrites or unrecoverable UX problems:

1. **Blocking HTTP freezes UI (P1)** -- Set `CURLOPT_TIMEOUT_MS` to 100-200ms. Localhost latency is sub-millisecond so this caps worst-case block to 200ms on server death. Monitor and upgrade to curl_multi only if needed.
2. **Terminal left corrupted on crash (P4)** -- Install signal handlers for SIGINT/SIGTERM/SIGSEGV/SIGABRT that call `endwin()` + `_exit()`. Add `atexit(endwin)` as safety net. Must be done alongside ncurses init in phase 1.
3. **wrefresh() per window causes flicker (P2)** -- Use `wnoutrefresh()` on all windows, `doupdate()` exactly once per frame. Establish this pattern from first render and never break it.
4. **ncurses is not thread-safe (P3)** -- All ncurses calls from main thread only. Single-threaded architecture eliminates this by design.
5. **Polling too aggressively overloads target app (P7)** -- Default 500ms-1000ms, poll only active tab's endpoints, always poll /stats/world at low frequency for status bar.

### Critical Finding: FlecsStats Not Imported

**CELS currently does NOT call `ECS_IMPORT(world, FlecsStats)`.** This means:
- The `/stats/*` endpoints return empty/error responses
- The Performance tab, per-system timing, and world stats aggregation will not work
- The Overview tab's FPS/frame time display depends on this data

**Action required:** Add `ECS_IMPORT(world, FlecsStats)` to the CELS runtime (in `cels_init()` or equivalent). This is a one-line change but it must happen before the Performance tab and Overview stats can function. Without it, approximately 40% of the planned features are non-functional.

**Recommendation:** Make this a prerequisite task in Phase 1 or a standalone pre-phase. Document the overhead -- FlecsStats adds per-frame measurement cost which may be acceptable only in debug builds.

## Implications for Roadmap

### Phase 1: Foundation (Build, Connect, Render)

**Rationale:** Every subsequent phase depends on the data pipeline (HTTP -> JSON -> model) and the TUI shell (ncurses init, window layout, input loop). This must work end-to-end before any tab content is meaningful. All 5 critical pitfalls must be addressed here.

**Delivers:** A single-screen TUI that connects to localhost:27750, polls /stats/world, parses the response, and displays "Connected | FPS: 60 | Entities: 42" with graceful disconnect handling.

**Features addressed:** Connection status indicator, frame timing display, entity count summary, graceful disconnect handling, auto-refresh polling.

**Pitfalls addressed:** P1 (blocking HTTP), P2 (flicker), P3 (thread safety by design), P4 (terminal corruption), P8 (resize), P9 (UTF-8), P12 (window management), P14 (keypad), P15 (KEY_RESIZE), P16 (hardcoded dimensions).

**Prerequisite task:** Add `ECS_IMPORT(world, FlecsStats)` to CELS runtime (or document it as a requirement).

### Phase 2: Tab System and Overview

**Rationale:** The tab system is the rendering framework all views plug into. Building it second means it inherits the proven foundation from Phase 1. The Overview tab is the simplest tab (aggregates already-fetched stats) and validates the vtable pattern.

**Delivers:** 6-tab navigation shell with header/tab bar/content/footer. Overview tab shows dashboard with entity count, FPS, frame time, system count. Tab switching with 1-6 keys and TAB.

**Features addressed:** Tab/view navigation, keyboard-driven navigation, overview dashboard.

**Architecture component:** tab_system with vtable dispatch, app_state management.

### Phase 3: Entities and Components

**Rationale:** Entity inspection is the core value of an ECS debugger and the highest-value tab. Component type browsing is closely related (same data domain) and relatively simple to add alongside entities.

**Delivers:** Entity list with component names; select entity to see component values as key-value pairs. Component registry tab listing all registered types with schemas.

**Features addressed:** Entity list, component value inspection, component type registry view.

**Pitfalls addressed:** P6 (virtual scrolling -- design data structures for it even if initial render is naive), P18 (null/empty component values), P10 (defensive JSON parsing).

### Phase 4: Systems and Pipeline

**Rationale:** Systems tab completes the "understand the ECS world" story. Requires pipeline stats parsing which is more complex than entity queries. CELS phase grouping is the first CELS-specific feature.

**Delivers:** System list grouped by CELS phase (Phase_Input, Phase_Physics, Phase_Render). Per-system execution time. Pipeline sync point visualization.

**Features addressed:** System list, system execution by phase, per-system execution time.

**Depends on:** FlecsStats import for per-system timing data.

### Phase 5: State, Performance, and Polish

**Rationale:** State tab is the most CELS-specific feature and benefits from all prior infrastructure (data model, change detection patterns, tab system). Performance tab requires the most complex stats parsing. Polish items (filter/search, change highlighting) improve all existing tabs.

**Delivers:** CELS State tab with change highlighting. Performance tab with frame timing and per-system metrics. Filter/search across entity and system lists.

**Features addressed:** State tab, change highlighting, performance metrics, filter/search.

**Pitfalls addressed:** P7 (polling strategy refinement -- adaptive polling based on data change rate).

### Phase Ordering Rationale

- **Foundation first** because every researcher identified the data pipeline and event loop as the dependency root. All 5 critical pitfalls are Phase 1 concerns.
- **Tab system before content tabs** because the vtable pattern must be validated with a simple tab (Overview) before building complex tabs (Entities, Systems).
- **Entities before Systems** because entity inspection is the primary use case and entities are simpler to query than pipeline stats.
- **State and Performance last** because they are the most CELS-specific (State) and data-intensive (Performance), and both benefit from mature infrastructure.
- **Each phase delivers a usable tool** -- after Phase 1 you have a connected status monitor, after Phase 3 you have a functional entity debugger, after Phase 5 you have the complete tool.

### Research Flags

Phases likely needing deeper research during planning:
- **Phase 1:** ncurses event loop integration with libcurl timeouts -- the exact `timeout()` value and polling cadence needs prototyping to feel responsive without busy-waiting
- **Phase 3:** Flecs query syntax for filtering non-builtin entities -- the query `!ChildOf(self|up, flecs), !Module(self|up)` needs verification against the running CELS app
- **Phase 4:** CELS phase-to-system mapping -- how to discover which systems belong to which CELS phase via the REST API (this is a CELS concept, not a flecs primitive)
- **Phase 5:** State component discovery -- CELS state components need a query pattern to identify them among all components

Phases with standard patterns (skip deep research):
- **Phase 2:** Tab system vtable is a well-established C pattern; ncurses window layout is thoroughly documented
- **Phase 3 (Component tab):** `/components` endpoint is straightforward; rendering is a simple list

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | All dependencies verified on current system; versions confirmed; CMake integration tested |
| Features | HIGH | Grounded in flecs source code, existing TUI tools (k9s, btop, lazygit), and Unity ECS precedent |
| Architecture | HIGH | MVC + polling is the standard pattern for API-connected TUI tools; flecs REST endpoints verified from source |
| Pitfalls | HIGH | ncurses and libcurl pitfalls are extensively documented; flecs REST specifics verified from source |

**Overall confidence:** HIGH

### Gaps to Address

1. **FlecsStats import** -- Must be added to CELS runtime before Performance tab and Overview stats work. One-line change but needs a decision about debug-only vs. always-on. Validate the performance overhead.
2. **CELS state component discovery** -- How to identify CELS State() components via REST API queries. The State tab design depends on this. Needs investigation of what component names/tags CELS uses for state.
3. **CELS phase-to-system mapping** -- Systems have names but their CELS phase grouping is a CELS concept. Need to determine if phase relationships are queryable via the REST API or if a mapping must be hardcoded/configured.
4. **Component reflection coverage** -- Components without registered reflection metadata return null values. Need to audit which CELS components have full reflection data and which will show "(no data)" in the inspector.
5. **Large entity count behavior** -- The query endpoint defaults to 1000 entity limit. Need to decide pagination UX for worlds with >1000 entities. Virtual scrolling data structures should be designed from Phase 3 but pagination UX can be deferred.

## Key Decisions Needed Before Phase 1

| Decision | Options | Recommendation |
|----------|---------|----------------|
| HTTP blocking strategy | curl_easy with short timeout vs curl_multi | curl_easy + `CURLOPT_TIMEOUT_MS=200` -- simpler, sufficient for localhost |
| FlecsStats import | Always-on vs debug-build-only vs runtime toggle | Debug-build-only via `#ifdef CELS_DEBUG`; document overhead |
| ncurses panels | Use panel library vs plain newwin() | Plain newwin() -- windows do not overlap, panels add unnecessary complexity |
| Project location | `tools/cels-debug/` as subdirectory of CELS | Yes, with `option(CELS_BUILD_TOOLS OFF)` in parent CMakeLists.txt |
| JSON dependency strategy | FetchContent (portable) vs vendor/ (simpler) | FetchContent for yyjson -- consistent with CELS's approach to flecs |

## Sources

### PRIMARY (HIGH confidence -- verified from source code)

- Flecs REST API: `build/_deps/flecs-src/src/addons/rest.c` (endpoint dispatcher, stats serialization)
- Flecs stats structures: `build/_deps/flecs-src/include/flecs/addons/stats.h`
- Flecs REST documentation: `build/_deps/flecs-src/docs/FlecsRemoteApi.md`
- CELS REST integration: `src/cels.cpp` (line 509-514, EcsRest on port 27750, NO FlecsStats import)
- libcurl easy interface: https://curl.se/libcurl/c/libcurl-easy.html
- yyjson: https://github.com/ibireme/yyjson
- ncurses documentation: https://invisible-island.net/ncurses/
- CMake FindCURL/FindCurses modules

### SECONDARY (MEDIUM confidence -- official docs, multiple sources agree)

- Flecs Remote API docs: https://www.flecs.dev/flecs/md_docs_2FlecsRemoteApi.html
- Flecs Explorer (feature reference): https://github.com/flecs-hub/explorer
- curl_multi interface: https://curl.se/libcurl/c/libcurl-multi.html
- ncurses threading: https://man7.org/linux/man-pages/man3/curs_threads.3x.html
- ncurses resize: https://invisible-island.net/ncurses/man/resizeterm.3x.html

### TERTIARY (LOW confidence -- design patterns, community references)

- k9s TUI patterns: https://k9scli.io/
- lazygit TUI patterns: https://github.com/jesseduffield/lazygit
- btop system monitor patterns: https://github.com/aristocratos/btop
- Unity Entity Debugger: https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/editor-entity-inspector.html

---
*Research completed: 2026-02-05*
*Ready for roadmap: yes*
