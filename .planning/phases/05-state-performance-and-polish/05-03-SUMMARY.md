---
phase: 05-state-performance-and-polish
plan: 03
subsystem: performance-tab
tags: [performance, waterfall, timing, pipeline, visualization]
dependency-graph:
  requires: [05-01]
  provides: [performance-waterfall, phase-grouped-timing, frame-budget]
  affects: [05-04]
tech-stack:
  added: []
  patterns: [waterfall-visualization, self-contained-phase-detection, virtual-scroll]
key-files:
  created:
    - tools/cels-debug/src/tabs/tab_performance.c
    - tools/cels-debug/src/tabs/tab_performance.h
  modified:
    - tools/cels-debug/src/tab_system.c
    - tools/cels-debug/CMakeLists.txt
decisions:
  - id: 05-03-01
    description: "Self-contained phase detection via entity tags instead of relying on other tabs' classify step"
    rationale: "Performance tab may draw before CELS/Systems tab classifies entities. Self-contained extraction from flecs.pipeline.* tags ensures correctness regardless of tab draw order."
  - id: 05-03-02
    description: "ENDPOINT_QUERY added to Performance tab endpoint bitmask"
    rationale: "Phase detection requires entity_list with tags, which comes from /query endpoint. Without it, systems would all fall into Custom group."
  - id: 05-03-03
    description: "tab_placeholder.c removed from CMakeLists.txt build"
    rationale: "No tab_defs entry references placeholder anymore. Files remain on disk but are dead code."
metrics:
  duration: "~2 minutes"
  completed: "2026-02-06"
---

# Phase 05 Plan 03: Performance Tab Waterfall Summary

Full-width waterfall visualization of per-system execution timing, grouped by Flecs pipeline phases with proportional timing bars and frame budget percentage.

## What Was Done

### Task 1: Create Performance tab with waterfall visualization
- Created `tab_performance.h` with init/fini/draw/input function declarations
- Created `tab_performance.c` with full waterfall rendering implementation
- Self-contained phase detection: `has_tag_str()` + `extract_phase_from_tags()` scan entity tags directly
- Phase grouping: systems sorted into PHASE_ORDER groups + Custom fallback
- Proportional timing bars: bar width = (system_time / max_time) * available_cols, minimum 1 char for non-zero
- Phase headers show color-coded name, system count, and total phase time
- Summary line: total systems, total ms/frame, frame budget percentage
- FPS/frame time/system count header from world_snapshot
- Scroll support via scroll_state_t for systems lists exceeding screen height
- Input: j/k for line scroll, PgUp/PgDn for page scroll, g/G for top/bottom
- Full-width layout with wnoutrefresh (same pattern as tab_overview)

### Task 2: Wire Performance tab into tab system
- Replaced `#include "tabs/tab_placeholder.h"` with `#include "tabs/tab_performance.h"` in tab_system.c
- Updated tab_defs[3] from placeholder functions to tab_performance_* functions
- Added ENDPOINT_QUERY to Performance tab bitmask for entity tag access
- Removed `src/tabs/tab_placeholder.c` from CMakeLists.txt add_executable
- Added `src/tabs/tab_performance.c` to CMakeLists.txt add_executable
- Build succeeds with 0 errors

## Decisions Made

| ID | Decision | Rationale |
|----|----------|-----------|
| 05-03-01 | Self-contained phase detection from entity tags | Independence from other tabs' classify step |
| 05-03-02 | ENDPOINT_QUERY added to Performance tab | Needed for entity_list with tags for phase detection |
| 05-03-03 | tab_placeholder.c removed from build | No remaining references in tab_defs |

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

1. Build: 0 errors with cmake -DCELS_DEBUG=ON -DCELS_BUILD_TOOLS=ON
2. tab_performance.h declares all 4 lifecycle functions
3. tab_performance.c implements waterfall with ACS_HLINE bars, max_time scaling, bar_width proportional
4. tab_system.c references tab_performance_* (no placeholder references remain)
5. CMakeLists.txt includes tab_performance.c, excludes tab_placeholder.c

## Commits

| Task | Commit | Description |
|------|--------|-------------|
| 1 | 2efd053 | feat(05-03): create Performance tab with waterfall visualization |
| 2 | d45fe19 | feat(05-03): wire Performance tab, remove placeholder |

## Next Phase Readiness

Plan 05-04 (polish and final cleanup) can proceed. All 4 tabs are now fully implemented:
- Overview: world stats dashboard
- CELS: C-E-L-C tree view (State section pending Plan 05-02)
- Systems: phase-grouped list with detail inspector
- Performance: waterfall timing visualization

No placeholder tabs remain in the application.
