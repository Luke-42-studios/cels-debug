---
phase: 05-state-performance-and-polish
plan: 01
subsystem: tab-structure
tags: [tabs, restructure, systems, cels, split-panel, scroll-state]
dependency-graph:
  requires: [04-systems-and-pipeline]
  provides: [tab-cels, tab-systems, pending-tab-navigation, 4-tab-layout]
  affects: [05-02, 05-03, 05-04]
tech-stack:
  added: []
  patterns: [flat-display-list, cross-tab-navigation]
key-files:
  created:
    - tools/cels-debug/src/tabs/tab_cels.c
    - tools/cels-debug/src/tabs/tab_cels.h
    - tools/cels-debug/src/tabs/tab_systems.c
    - tools/cels-debug/src/tabs/tab_systems.h
  modified:
    - tools/cels-debug/src/tab_system.c
    - tools/cels-debug/src/tui.h
    - tools/cels-debug/src/main.c
    - tools/cels-debug/CMakeLists.txt
decisions:
  - id: 05-01-01
    description: "scroll_state_t flat display list for Systems tab instead of tree_view_t"
    rationale: "Systems tab shows only systems grouped by phase -- a flat scrollable list is simpler and more appropriate than repurposing tree_view for a single section"
  - id: 05-01-02
    description: "hide_systems_from_tree() reclassifies ENTITY_CLASS_SYSTEM as ENTITY_CLASS_ENTITY in CELS tab"
    rationale: "Prevents Systems section header from appearing in CELS tab tree while keeping classify_node() classification logic intact for the Systems tab"
  - id: 05-01-03
    description: "pending_tab field in app_state_t for cross-tab navigation"
    rationale: "Tab switching must happen in main loop (after input dispatch returns), not inside tab input handler. pending_tab is a clean deferred mechanism."
  - id: 05-01-04
    description: "Endpoint bitmasks refined: CELS drops PIPELINE, Systems gets PIPELINE"
    rationale: "CELS tab no longer displays system timing data. Systems tab needs pipeline stats for enrichment."
metrics:
  duration: "~6 minutes"
  completed: "2026-02-06"
---

# Phase 05 Plan 01: Tab Restructure Summary

Rename ECS tab to CELS (stripping Systems section) and extract Systems into a standalone top-level tab using scroll_state_t flat display list with phase grouping, system detail inspector, and cross-tab navigation.

## What Was Done

### Task 1: Rename tab_ecs to tab_cels and strip Systems section
- Created `tab_cels.c/h` based on `tab_ecs.c/h`
- Renamed all `tab_ecs_*` functions to `tab_cels_*`, `ecs_state_t` to `cels_state_t`
- Removed all Systems-specific code: `PHASE_ORDER`, `phase_color_pair()`, `enrich_systems_with_pipeline()`, `find_system_info()`, `build_system_matches()`, `draw_system_detail()`, `draw_pipeline_viz()`, `draw_systems_summary()`
- Removed inspector branches for `ENTITY_CLASS_SYSTEM` entities and system section headers
- Removed input handler branches for system detail mode
- Added `hide_systems_from_tree()` to reclassify system entities as `ENTITY_CLASS_ENTITY`
- Changed split_panel label from "ECS" to "CELS"
- Kept `classify_node()` with full classification logic including system detection

### Task 2: Create tab_systems with scroll_state flat-list approach
- Created `tab_systems.c/h` as a standalone Systems tab
- Used `scroll_state_t` (not `tree_view_t`) with `display_entry_t` flat display list
- Phase-grouped rendering: canonical execution order, observers group, custom group
- System detail inspector: phase, status, timing, component access, approximate matched entities
- Pipeline visualization for phase headers with timing summation
- Systems summary overview with phase distribution
- Cross-navigation to CELS tab (index 1) via `pending_tab` mechanism
- Independent classification and enrichment from pipeline stats

### Task 3: Wire tabs, build system, and pending_tab cross-navigation
- Updated `tab_system.c`: replaced `tab_ecs` with `tab_cels`, added `tab_systems`, updated `tab_defs` array
- Updated `tui.h`: added `pending_tab` field to `app_state_t`
- Updated `main.c`: initialized `pending_tab = -1`, added pending_tab handling after input dispatch
- Updated `CMakeLists.txt`: replaced `tab_ecs.c` with `tab_cels.c`, added `tab_systems.c`
- Endpoint bitmasks: CELS=QUERY|ENTITY|COMPONENTS, Systems=QUERY|ENTITY|STATS_PIPELINE, Performance=STATS_WORLD|STATS_PIPELINE

## Decisions Made

| ID | Decision | Rationale |
|----|----------|-----------|
| 05-01-01 | scroll_state_t flat display list for Systems tab | Flat list simpler than tree_view for single-section display |
| 05-01-02 | hide_systems_from_tree() reclassifies systems as entities | Keeps classify_node() intact for Systems tab reuse |
| 05-01-03 | pending_tab field for cross-tab navigation | Clean deferred switching in main loop |
| 05-01-04 | CELS drops PIPELINE, Systems gets PIPELINE | Each tab polls only what it needs |

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

1. Build: 0 errors, 0 warnings
2. Application launches with tab bar: "1:Overview 2:CELS 3:Systems 4:Performance"
3. CELS tab: no Systems section (system entities reclassified to Entities)
4. Systems tab: phase-grouped flat list with detail inspector
5. Endpoint bitmasks: correct per-tab isolation
6. No crashes on launch/quit

## Commits

| Task | Commit | Description |
|------|--------|-------------|
| 1 | ed2b677 | feat(05-01): rename tab_ecs to tab_cels and strip Systems section |
| 2 | 2e945a1 | feat(05-01): create standalone Systems tab with flat-list approach |
| 3 | 270f620 | feat(05-01): wire 4-tab layout [Overview, CELS, Systems, Performance] |

## Next Phase Readiness

Plan 05-02 (State section in CELS tab) can proceed. The CELS tab tree now shows C-E-L-C sections. Plan 05-02 will add ENTITY_CLASS_STATE and the State section to complete the C-E-L-S-C paradigm.
