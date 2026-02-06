# Roadmap: CELS Debug TUI

## Overview

CELS Debug is built foundation-first: prove the end-to-end data pipeline (HTTP GET -> JSON parse -> ncurses render) in Phase 1, then layer the tab vtable framework (Phase 2), entity/component inspection (Phase 3), system/pipeline views (Phase 4), and finally CELS-specific state/performance tabs with polish (Phase 5). Each phase delivers a usable tool -- after Phase 1 you have a connected status monitor, after Phase 3 you have a functional entity debugger, after Phase 5 you have the complete inspector.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 01: Foundation (Build, Connect, Render)** - CMake project, HTTP pipeline, ncurses shell, basic stats display
- [x] **Phase 02: Tab System and Overview** - Tab vtable framework, tab bar navigation, Overview dashboard
- [x] **Phase 03: Entities and Components** - Entity list, component inspection, component registry tab
- [x] **Phase 03.1: Redesign Navigation — ECS Tabs** (INSERTED) - Restructure tabs to read ECS (Overview, ECS, Performance, State), CELS-C tree inside ECS tab
- [x] **Phase 04: Systems and Pipeline** - System list, phase grouping, pipeline visualization
- [x] **Phase 05: State, Performance, and Polish** - Tab restructure (CELS + Systems), State section, Performance waterfall, navigation polish, auto-reconnect

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
  2. Cursor selection previews component data instantly; Enter toggles expand/collapse on tree nodes
  3. Selected entity displays all component names and their values as key-value pairs
  4. Component values render correctly for nested objects, arrays, and null values
  5. Components tab lists all registered component types from the component registry
  6. Entity list handles large counts without freezing (virtual scrolling data structures in place)
**Plans**: 4 plans

Plans:
- [x] 03-01-PLAN.md -- Data pipeline: entity/component data model, JSON parsers, main loop polling
- [x] 03-02-PLAN.md -- Reusable UI modules: scroll, split panel, JSON renderer, tree view
- [x] 03-03-PLAN.md -- Entities tab: interactive tree view + component inspector
- [x] 03-04-PLAN.md -- Components tab: component registry list + entity drill-down

### Phase 03.1: Redesign Navigation — ECS Tabs (INSERTED)
**Goal**: Restructure the debugger's tab and navigation architecture so the top-level tabs read as ECS concepts (Overview, ECS, Performance, State), with the ECS tab containing the CELS-C tree view (Compositions, Entities, Lifecycles, Systems, Components) as collapsible sub-sections
**Depends on**: Phase 03
**Success Criteria** (what must be TRUE):
  1. Top-level tabs are: Overview, ECS, Performance, State (4 tabs, not 5+)
  2. ECS tab contains the CELS-C tree view with collapsible section headers (C-E-L-S-C)
  3. Components section within CELS-C is fully functional (component type list, entity drill-down)
  4. Section headers are navigable — cursor lands on them, Enter toggles collapse, all start collapsed
  5. First letter of each section name is bold (C-E-L-S-C reads vertically as the paradigm)
  6. Arrow keys / j/k navigation works correctly through headers and entity items
  7. Inspector panel shows entity detail when an entity is selected from any section
**Plans**: 1 plan

Plans:
- [x] 03.1-01-PLAN.md -- Create tab_ecs (merged entity+component), update wiring to 4-tab layout

**Details:**
Current state: Phase 03 built entity/component tabs as separate views. User testing revealed the need to:
- Merge component browsing into the entity tree (not a separate tab)
- Group entities by CELS paradigm sections (Compositions, Entities, Lifecycles, Systems, Components)
- Reduce top-level tabs to match ECS concepts: Overview (stats), ECS (the CELS-C browser), Performance, State
- Make section headers navigable and collapsible (start closed)
- Bold first letter of each section spelling CELS-C vertically

### Phase 04: Systems and Pipeline
**Goal**: Users can see all registered systems grouped by execution phase with enabled/disabled status, with pipeline visualization and system detail inspector
**Depends on**: Phase 03
**Requirements**: F4
**Success Criteria** (what must be TRUE):
  1. Systems section in CELS-C tree displays all registered systems from /query endpoint
  2. Systems are grouped by Flecs execution phase (OnLoad, OnUpdate, OnStore, etc.)
  3. Each system shows name, color-coded phase tag, enabled/disabled status, and match count
  4. Pipeline visualization shows phase execution ordering with timing data
  5. System detail inspector shows metadata and approximate matched entity list
  6. Cross-navigation from matched entities to Entities section
**Plans**: 4 plans

Plans:
- [x] 04-01-PLAN.md -- Data pipeline: system data model, pipeline stats parser, polling, phase colors
- [x] 04-02-PLAN.md -- Tree view phase sub-headers, data_model.h system fields, phase rendering
- [x] 04-03-PLAN.md -- System enrichment from pipeline stats, pipeline viz + summary inspector
- [x] 04-04-PLAN.md -- System detail inspector + cross-navigation

### Phase 05: State, Performance, and Polish
**Goal**: Restructure tabs to [Overview, CELS, Systems, Performance], complete the CELS-C paradigm with State section, add Performance waterfall, and polish navigation/reconnect/refresh
**Depends on**: Phase 04
**Requirements**: F1 (refinement), F5 (refinement), F6 (complete), F7 (complete), F8 (complete)
**Success Criteria** (what must be TRUE):
  1. Tab bar shows: Overview, CELS, Systems, Performance (4 tabs)
  2. CELS tab sections spell C-E-L-S-C: Compositions, Entities, Lifecycles, State, Components
  3. Performance tab shows per-system waterfall with proportional timing bars
  4. Auto-reconnect persists Reconnecting status (no premature Disconnected)
  5. Poll interval configurable via -r flag, Esc back-navigation, context-sensitive footer
**Plans**: 4 plans

Plans:
- [x] 05-01-PLAN.md -- Tab restructure: rename ECS to CELS, extract Systems to own tab
- [x] 05-02-PLAN.md -- State section in CELS tab with change highlighting
- [x] 05-03-PLAN.md -- Performance tab with waterfall visualization
- [x] 05-04-PLAN.md -- Navigation polish, auto-reconnect fix, configurable refresh

## Traceability

### Feature Requirements

| Requirement | Phase | Status |
|-------------|-------|--------|
| F1: Connection status indicator | Phase 01 (initial), Phase 05 (refinement) | Complete |
| F2: Tab navigation (6 tabs) | Phase 02 | Complete |
| F3: Entity list with component inspection | Phase 03 | Complete |
| F4: System list | Phase 04 | Complete |
| F5: Frame timing display | Phase 01 (initial), Phase 05 (refinement) | Complete |
| F6: Keyboard-driven navigation | Phase 02 (framework), Phase 03 (entity nav), Phase 05 (polish) | Complete |
| F7: Auto-refresh polling | Phase 01 (basic), Phase 05 (configurable) | Complete |
| F8: Graceful disconnect handling | Phase 01 (basic), Phase 05 (refinement) | Complete |

### Technical Requirements

| Requirement | Phase | Status |
|-------------|-------|--------|
| T1: C99 language | Phase 01 | Complete |
| T2: CMake 3.21+ build | Phase 01 | Complete |
| T3: ncursesw 6.5+ | Phase 01 | Complete |
| T4: libcurl (easy interface) | Phase 01 | Complete |
| T5: yyjson 0.10+ | Phase 01 | Complete |
| T6: FlecsStats debug-build only | Phase 01 | Complete |
| T7: Single-threaded MVC loop | Phase 01 | Complete |
| T8: Snapshot data model | Phase 01 | Complete |
| T9: Tab vtable pattern | Phase 02 | Complete |
| T10: Signal handlers | Phase 01 | Complete |

**Coverage: 8/8 feature requirements mapped, 10/10 technical requirements mapped.**

## Progress

**Execution Order:**
Phases execute in numeric order: 01 -> 02 -> 03 -> 03.1 -> 04 -> 05

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 01. Foundation | 3/3 | Complete | 2026-02-05 |
| 02. Tab System and Overview | 2/2 | Complete | 2026-02-06 |
| 03. Entities and Components | 4/4 | Complete | 2026-02-06 |
| 03.1 Redesign Navigation — ECS Tabs | 1/1 | Complete | 2026-02-06 |
| 04. Systems and Pipeline | 4/4 | Complete | 2026-02-06 |
| 05. State, Performance, Polish | 4/4 | Complete | 2026-02-06 |

---
*Created: 2026-02-05*
*Updated: 2026-02-06 (Phase 05 complete: all phases done, v0.1 milestone complete)*
