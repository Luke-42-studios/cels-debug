# CELS Debug v0.1 — Requirements

## MVP Scope

v0.1 delivers **read-only TUI inspection** of a running CELS/flecs application. Must-have features only; differentiators deferred to v0.2+.

## Must-Have Features (v0.1)

| # | Feature | Acceptance Criteria |
|---|---------|-------------------|
| F1 | Connection status indicator | Header shows Connected/Disconnected/Reconnecting; auto-reconnect on disconnect |
| F2 | Tab navigation (6 tabs) | Overview, Entities, Components, Systems, State, Performance; switch via 1-6 keys and TAB |
| F3 | Entity list with component inspection | Scrollable entity list; select entity to see component names and values as key-value pairs |
| F4 | System list | All registered systems displayed; ideally grouped by phase |
| F5 | Frame timing display | FPS and delta time shown in status bar (requires FlecsStats) |
| F6 | Keyboard-driven navigation | j/k or arrow keys for scrolling, Enter to select, Esc to back, q to quit |
| F7 | Auto-refresh polling | Configurable interval (default 500ms); polls only active tab's endpoints |
| F8 | Graceful disconnect handling | No crash on lost connection; display disconnect state; auto-reconnect attempts |

## Deferred to v0.2+ (Should-Have)

- Change highlighting (color changed values between polls)
- CELS State tab with change tracking
- System execution grouped by CELS phase
- Entity parent-child tree view
- Per-system execution time
- Filter/search in entity and system lists
- Runtime poll rate toggle

## Deferred Indefinitely (Post-MVP)

- Live editing of component values
- Watch expressions
- ASCII graphs/sparklines
- Log streaming
- Breakpoints/pause/step
- Mouse support, config files, color themes, query REPL
- Custom developer tabs

## Technical Requirements

| # | Requirement | Details |
|---|-------------|---------|
| T1 | C99 language | Consistent with CELS public API |
| T2 | CMake 3.21+ build | Integrates with CELS build system; `option(CELS_BUILD_TOOLS OFF)` |
| T3 | ncursesw 6.5+ | System package; wide-char support for UTF-8 |
| T4 | libcurl (easy interface) | System package; `CURLOPT_TIMEOUT_MS=200` for non-blocking localhost |
| T5 | yyjson 0.10+ | FetchContent; immutable doc API for read-only JSON parsing |
| T6 | FlecsStats debug-build only | `#ifdef CELS_DEBUG` in CELS runtime; zero overhead in release builds |
| T7 | Single-threaded MVC loop | No threads; ncurses + libcurl from main thread only |
| T8 | Snapshot data model | Each poll produces new snapshot; previous freed atomically |
| T9 | Tab vtable pattern | Each tab: init/fini/draw/handle_input + required_endpoints bitmask |
| T10 | Signal handlers | SIGINT/SIGTERM/SIGSEGV/SIGABRT call endwin(); atexit(endwin) safety net |

## Architecture Overview

```
main.c  →  app_state  →  tui  →  tab_system  →  tabs/
                ↓                                  ↑
           http_client  →  json_parser  →  data_model
```

- **http_client**: libcurl GET with timeout, connection state machine
- **json_parser**: yyjson parse into typed C structs
- **data_model**: Central store for entities, components, systems, stats
- **tui**: ncurses init, window layout (header + tab bar + content + footer)
- **tab_system**: Registry with vtable dispatch
- **tabs/**: 6 implementations (overview, entities, components, systems, state, performance)

## Build Phases (from research)

1. **Foundation** — HTTP pipeline + ncurses shell + signal handlers
2. **Tab System + Overview** — vtable framework + Overview dashboard
3. **Entities + Components** — Core ECS debugging value
4. **Systems + Pipeline** — System list, phase grouping
5. **State + Performance + Polish** — CELS-specific tabs, metrics

## Prerequisite

Add `ECS_IMPORT(world, FlecsStats)` to CELS runtime behind `#ifdef CELS_DEBUG`. This unblocks Performance tab and Overview stats. Must be done before or during Phase 1.

---
*Created: 2026-02-05*
