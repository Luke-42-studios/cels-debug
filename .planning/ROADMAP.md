# Roadmap: CELS Debug TUI

## Overview

CELS Debug is built foundation-first: prove the end-to-end data pipeline (HTTP GET -> JSON parse -> ncurses render) in Phase 1, then layer the tab vtable framework (Phase 2), entity/component inspection (Phase 3), system/pipeline views (Phase 4), and finally CELS-specific state/performance tabs with polish (Phase 5). Each phase delivers a usable tool -- after Phase 1 you have a connected status monitor, after Phase 3 you have a functional entity debugger, after Phase 5 you have the complete inspector.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 01: Foundation (Build, Connect, Render)** - CMake project, HTTP pipeline, ncurses shell, basic stats display
- [x] **Phase 02: Tab System and Overview** - Tab vtable framework, tab bar navigation, Overview dashboard
- [ ] **Phase 03: Entities and Components** - Entity list, component inspection, component registry tab
- [ ] **Phase 04: Systems and Pipeline** - System list, phase grouping, pipeline visualization
- [ ] **Phase 05: State, Performance, and Polish** - State tab, performance metrics, auto-reconnect, navigation polish

## Phase Details

### Phase 01: Foundation (Build, Connect, Render)
**Goal**: A single-screen TUI connects to localhost:27750, polls /stats/world, and displays "Connected | FPS: 60 | Entities: 42" with graceful disconnect handling
**Depends on**: Nothing (first phase)
**Requirements**: F1, F5, F7 (partial), F8 (partial), T1, T2, T3, T4, T5, T6, T7, T8, T9 (partial), T10
**Success Criteria** (what must be TRUE):
  1. CMake project builds with ncursesw, libcurl, and yyjson dependencies resolved
  2. TUI launches, displays header/content/footer layout, and exits cleanly on q or Ctrl+C
  3. HTTP client polls localhost:27750/stats/world and parses JSON response into data model structs
  4. Header shows connection status: Connected, Disconnected, or Reconnecting
  5. Content area displays entity count and FPS from /stats/world response
  6. Terminal is never left in corrupted state (signal handlers + atexit(endwin) work on SIGINT/SIGTERM/SIGSEGV)
  7. Prerequisite met: ECS_IMPORT(FlecsStats) added behind #ifdef CELS_DEBUG in CELS runtime
**Plans**: 3 plans

Plans:
- [x] 01-01-PLAN.md -- CMake build system + CELS_DEBUG option + FlecsStats prerequisite
- [x] 01-02-PLAN.md -- HTTP client + JSON parser + data model (data pipeline)
- [x] 01-03-PLAN.md -- ncurses TUI shell + main event loop (user-facing TUI)

### Phase 02: Tab System and Overview
**Goal**: Users navigate between 6 tabs via keyboard, and the Overview tab shows a live dashboard with entity count, system count, FPS, and frame time
**Depends on**: Phase 01
**Requirements**: F2, F6 (partial), T9
**Success Criteria** (what must be TRUE):
  1. Tab bar renders 6 tab labels (Overview, Entities, Components, Systems, State, Performance) with active tab highlighted
  2. User switches tabs with 1-6 number keys and TAB cycles forward
  3. Tab vtable dispatches init/fini/draw/handle_input per tab; each tab declares required_endpoints bitmask
  4. Only the active tab's required endpoints are polled (smart polling)
  5. Overview tab displays dashboard: entity count, system count, FPS, frame time
**Plans**: 2 plans

Plans:
- [x] 02-01-PLAN.md -- Tab vtable framework, tab bar UI, keyboard navigation, 6 placeholder tabs, smart polling
- [x] 02-02-PLAN.md -- Overview tab implementation with live dashboard

### Phase 03: Entities and Components
**Goal**: Users can browse all entities, select one, and inspect its component names and values as key-value pairs
**Depends on**: Phase 02
**Requirements**: F3, F6 (partial)
**Success Criteria** (what must be TRUE):
  1. Entities tab shows a scrollable list of entities fetched from /query endpoint
  2. User navigates entity list with j/k or arrow keys and selects with Enter
  3. Selected entity displays all component names and their values as key-value pairs
  4. Component values render correctly for nested objects, arrays, and null values
  5. Components tab lists all registered component types from the component registry
  6. Entity list handles large counts without freezing (virtual scrolling data structures in place)
**Plans**: 4 plans

Plans:
- [ ] 03-01-PLAN.md -- Data pipeline: entity/component data model, JSON parsers, main loop polling
- [ ] 03-02-PLAN.md -- Reusable UI modules: scroll, split panel, JSON renderer, tree view
- [ ] 03-03-PLAN.md -- Entities tab: interactive tree view + component inspector
- [ ] 03-04-PLAN.md -- Components tab: component registry list + entity drill-down

### Phase 04: Systems and Pipeline
**Goal**: Users can see all registered systems grouped by execution phase with enabled/disabled status
**Depends on**: Phase 03
**Requirements**: F4
**Success Criteria** (what must be TRUE):
  1. Systems tab displays all registered systems from /query endpoint
  2. Systems are grouped by phase (Phase_Input, Phase_Update, Phase_Render, etc.)
  3. Each system shows name, phase, and enabled/disabled status
  4. Pipeline visualization shows phase execution ordering
**Plans**: TBD

Plans:
- [ ] 04-01: TBD

### Phase 05: State, Performance, and Polish
**Goal**: CELS-specific State and Performance tabs complete the inspector, with polished auto-reconnect and configurable refresh
**Depends on**: Phase 04
**Requirements**: F1 (refinement), F5 (refinement), F6 (complete), F7 (complete), F8 (complete)
**Success Criteria** (what must be TRUE):
  1. State tab displays CELS State() component values
  2. Performance tab shows frame timing details and per-system metrics from /stats/pipeline
  3. Auto-refresh interval is configurable (default 500ms), polling only active tab endpoints
  4. Graceful disconnect with automatic reconnect attempts and clear status transitions
  5. Keyboard navigation is consistent across all tabs: j/k scroll, Enter select, Esc back, q quit
**Plans**: TBD

Plans:
- [ ] 05-01: TBD
- [ ] 05-02: TBD

## Traceability

### Feature Requirements

| Requirement | Phase | Status |
|-------------|-------|--------|
| F1: Connection status indicator | Phase 01 (initial), Phase 05 (refinement) | Pending |
| F2: Tab navigation (6 tabs) | Phase 02 | Complete |
| F3: Entity list with component inspection | Phase 03 | Pending |
| F4: System list | Phase 04 | Pending |
| F5: Frame timing display | Phase 01 (initial), Phase 05 (refinement) | Pending |
| F6: Keyboard-driven navigation | Phase 02 (framework), Phase 03 (entity nav), Phase 05 (polish) | Pending |
| F7: Auto-refresh polling | Phase 01 (basic), Phase 05 (configurable) | Pending |
| F8: Graceful disconnect handling | Phase 01 (basic), Phase 05 (refinement) | Pending |

### Technical Requirements

| Requirement | Phase | Status |
|-------------|-------|--------|
| T1: C99 language | Phase 01 | Pending |
| T2: CMake 3.21+ build | Phase 01 | Pending |
| T3: ncursesw 6.5+ | Phase 01 | Pending |
| T4: libcurl (easy interface) | Phase 01 | Pending |
| T5: yyjson 0.10+ | Phase 01 | Pending |
| T6: FlecsStats debug-build only | Phase 01 | Pending |
| T7: Single-threaded MVC loop | Phase 01 | Pending |
| T8: Snapshot data model | Phase 01 | Pending |
| T9: Tab vtable pattern | Phase 02 | Complete |
| T10: Signal handlers | Phase 01 | Pending |

**Coverage: 8/8 feature requirements mapped, 10/10 technical requirements mapped.**

## Progress

**Execution Order:**
Phases execute in numeric order: 01 -> 02 -> 03 -> 04 -> 05

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 01. Foundation | 3/3 | Complete | 2026-02-05 |
| 02. Tab System and Overview | 2/2 | Complete | 2026-02-06 |
| 03. Entities and Components | 0/4 | Not started | - |
| 04. Systems and Pipeline | 0/? | Not started | - |
| 05. State, Performance, Polish | 0/? | Not started | - |

---
*Created: 2026-02-05*
*Updated: 2026-02-06 (Phase 03 planned: 4 plans in 3 waves)*
