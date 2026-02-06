---
phase: 02-tab-system-and-overview
plan: 01
subsystem: tab-system
tags: [vtable, ncurses, tabs, bitmask, smart-polling]

dependency-graph:
  requires: [01-foundation]
  provides: [tab-vtable-framework, tab-bar-ui, endpoint-bitmask, app-state-struct]
  affects: [02-02-overview-tab, 03-entities, 04-systems, 05-state-performance]

tech-stack:
  added: []
  patterns: [C99-vtable-dispatch, bitmask-endpoint-filtering, 4-window-ncurses-layout]

key-files:
  created:
    - tools/cels-debug/src/tab_system.h
    - tools/cels-debug/src/tab_system.c
    - tools/cels-debug/src/tabs/tab_placeholder.h
    - tools/cels-debug/src/tabs/tab_placeholder.c
  modified:
    - tools/cels-debug/src/tui.h
    - tools/cels-debug/src/tui.c
    - tools/cels-debug/src/main.c
    - tools/cels-debug/CMakeLists.txt

decisions:
  - id: 02-01-01
    decision: "void* for app_state in vtable signatures"
    rationale: "Avoids circular includes between tab_system.h and tui.h; tab .c files cast internally"
  - id: 02-01-02
    decision: "app_state_t defined in tui.h"
    rationale: "tui.h already includes data_model.h and http_client.h which provide the member types"
  - id: 02-01-03
    decision: "Always poll /stats/world regardless of bitmask, only parse snapshot if ENDPOINT_STATS_WORLD needed"
    rationale: "Keeps connection state accurate on all tabs; /stats/world is cheap (localhost, <1KB)"
  - id: 02-01-04
    decision: "All 6 tabs use placeholder in this plan"
    rationale: "Overview tab implementation deferred to Plan 02-02 per research recommendation"

metrics:
  duration: ~2 minutes
  completed: 2026-02-06
---

# Phase 02 Plan 01: Tab Vtable Framework and Tab Bar UI Summary

**One-liner:** C99 vtable tab system with 6-endpoint bitmask, 4-window ncurses layout, and keyboard-driven tab navigation (1-6 + TAB)

## What Was Done

### Task 1: Create tab_system module and placeholder tab
Created 4 new files implementing the tab vtable framework:
- `tab_system.h`: Complete public API with `endpoint_t` bitmask enum (6 endpoints), `tab_def_t` vtable struct (name, required_endpoints, 4 function pointers), `tab_t` instance struct, `tab_system_t` container with `TAB_COUNT=6`, and 7 public functions
- `tab_system.c`: Static const `tab_defs[6]` array registering Overview, Entities, Components, Systems, State, Performance with per-tab endpoint requirements; all implementation functions
- `tabs/tab_placeholder.h/.c`: Shared placeholder rendering "Not implemented yet" centered with tab name in A_DIM below

### Task 2: Refactor tui.c, main.c, and CMakeLists.txt for tab integration
- **CMakeLists.txt**: Added `tab_system.c` and `tabs/tab_placeholder.c` to build
- **tui.h**: Added `app_state_t` struct (snapshot + conn_state), included `tab_system.h`, changed `tui_render` signature to accept `(const tab_system_t*, const app_state_t*)`
- **tui.c**: Inserted `win_tabbar` window at row 1, shifted content to row 2 (LINES-3 height). Tab bar renders " N:Name " labels with `A_REVERSE|A_BOLD` for active tab. Content delegates to `tab_system_draw()`. Footer updated to "1-6:tabs  TAB:next  q:quit". Added CP_TAB_ACTIVE/CP_TAB_INACTIVE color pairs. Batch refresh includes all 4 windows.
- **main.c**: Input routing follows global -> tab switch -> per-tab order. Added `tab_system_t tabs` and `app_state_t app_state`. Smart polling always fetches /stats/world for connection health, only parses snapshot when `ENDPOINT_STATS_WORLD` is needed. Lifecycle: init order tui->http->tabs, fini order tabs->snapshot->http->tui.

## Decisions Made

| ID | Decision | Rationale |
|----|----------|-----------|
| 02-01-01 | void* for app_state in vtable signatures | Avoids circular includes; tab .c files cast internally |
| 02-01-02 | app_state_t defined in tui.h | tui.h already includes the needed type headers |
| 02-01-03 | Always poll /stats/world, conditionally parse | Connection health stays accurate; localhost is cheap |
| 02-01-04 | All 6 tabs use placeholder initially | Overview implementation is Plan 02-02 |

## Deviations from Plan

None -- plan executed exactly as written.

## Verification

- Build: `cmake --build build --target cels-debug` completes with 0 errors, 0 warnings
- All must_have artifacts present with required contents
- All key_links verified (tab_system_activate/next/handle_input in main.c, tab_system_draw in tui.c, tab_system_required_endpoints gating polling)
- 4-window layout confirmed (header row 0, tabbar row 1, content row 2..LINES-2, footer LINES-1)

## Commits

| Task | Commit | Description |
|------|--------|-------------|
| 1 | 8eb242b | feat(02-01): create tab_system module and placeholder tab |
| 2 | 3179b7a | feat(02-01): refactor tui.c, main.c, CMakeLists.txt for tab integration |

## Next Phase Readiness

Plan 02-02 (Overview tab implementation) can proceed immediately. It needs to:
1. Create `tabs/tab_overview.h/.c` with the dashboard rendering (entity count, FPS, frame time, system count)
2. Replace the Overview slot in `tab_defs[0]` from placeholder to tab_overview functions
3. Add `tab_overview.c` to CMakeLists.txt
