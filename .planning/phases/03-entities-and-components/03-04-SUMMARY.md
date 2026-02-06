---
phase: 03-entities-and-components
plan: 04
subsystem: components-tab
tags: [ncurses, components, split-panel, scroll, entity-filtering, qsort]
dependency-graph:
  requires: [03-01-data-pipeline, 03-02-ui-utilities, 03-03-entities-tab]
  provides: [components-tab, component-registry-browsing, component-entity-drill-down]
  affects: [04-01-systems-tab]
tech-stack:
  added: []
  patterns: [component-centric-view, entity-filtering-by-component, alphabetical-sort-on-draw]
key-files:
  created:
    - tools/cels-debug/src/tabs/tab_components.h
    - tools/cels-debug/src/tabs/tab_components.c
  modified:
    - tools/cels-debug/src/tab_system.c
    - tools/cels-debug/CMakeLists.txt
decisions:
  - Sort component list alphabetically in draw (not parser) -- keeps parser simple, O(n log n) on ~100 items is negligible
  - Entity filtering done fresh each draw call -- no caching needed for <5K entities
  - Right scroll resets to top when left panel selection changes
metrics:
  duration: ~2 minutes
  completed: 2026-02-06
---

# Phase 03 Plan 04: Components Tab Summary

Split-panel Components tab with alphabetically sorted component type list (left) and filtered entity drill-down (right), completing Phase 03.

## One-Liner

Components tab: qsort-sorted registry in left panel with entity counts and size info, right panel filters entity_list by selected component name using strcmp scan, wired into tab_defs[2] with ENDPOINT_COMPONENTS | ENDPOINT_QUERY.

## What Was Done

### Task 1: Implement the Components tab

**tab_components.h** -- Standard tab vtable declarations (init/fini/draw/input) following the tab_entities.h pattern.

**tab_components.c** -- Full 260-line implementation with components_state_t private state:

- `components_state_t`: holds split_panel_t, left and right scroll_state_t, and panel_created flag for deferred window creation.
- `tab_components_init()`: Allocates state, resets both scroll states, defers panel creation.
- `tab_components_fini()`: Destroys panel windows, frees state struct.
- `tab_components_draw()`: Creates/resizes split panel on first draw or terminal resize. Left panel sorts component_registry alphabetically via qsort with strcmp comparator, renders visible items with entity count and optional type size right-aligned in A_DIM, cursor row in A_REVERSE. Right panel filters entity_list for entities having selected component (strcmp scan over component_names arrays), renders matching entities with name (or #id for anonymous) and full_path. Shows contextual messages: "Waiting for data...", "No components", "No entities", "Select a component", "Waiting for entity data...".
- `tab_components_input()`: Left panel: j/k, arrows, Page Up/Down, g/G navigate component list, each selection change resets right scroll. Right panel: j/k, arrows, Page Up/Down, g/G navigate filtered entity list. Left/right arrows switch panel focus via split_panel_handle_focus.

Helper function:
- `compare_components()`: qsort comparator for alphabetical component name ordering with NULL safety.

**Commit:** 3fc8c8b

### Task 2: Wire Components tab into tab system and build

- Added `#include "tabs/tab_components.h"` to tab_system.c
- Replaced tab_defs[2] (Components) from placeholder functions to tab_components_init/fini/draw/input
- Updated endpoint bitmask to ENDPOINT_COMPONENTS | ENDPOINT_QUERY (needs entity_list for right panel filtering)
- Added `src/tabs/tab_components.c` to CMakeLists.txt add_executable source list
- Full build compiles and links cleanly

**Commit:** 158c696

## Decisions Made

1. **Sort in draw, not parser**: Component list sorted alphabetically each draw via qsort. Since component_registry is replaced atomically on each poll, sorting in draw keeps the parser simple. O(n log n) on ~100 components is negligible.
2. **No filter caching**: Entity filtering done fresh each draw call with malloc/free per frame. For <5K entities and ~100 components, this is fast enough (<1ms). Avoids stale cache invalidation complexity.
3. **Right scroll reset on left selection change**: When the user selects a different component type, the right panel scroll cursor resets to 0. This prevents the cursor from being out of bounds when the filtered list changes size.

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

1. `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build` succeeds -- PASS
2. tab_system.c includes tab_components.h and has real vtable entry at tab_defs[2] -- PASS
3. CMakeLists.txt includes tab_components.c -- PASS
4. Tab 3 shows split panel with "Components" and "Entities" titled borders -- PASS
5. Left panel renders sorted component types with entity counts -- PASS
6. Right panel filters entities by selected component -- PASS
7. j/k scrolls both panels, left/right switches focus -- PASS
8. All Phase 03 success criteria met:
   - Entities tab shows scrollable entity list from /query (criterion 1) -- from Plan 03-03
   - j/k and arrow keys navigate, Enter selects (criterion 2) -- from Plan 03-03
   - Selected entity shows component key-value pairs (criterion 3) -- from Plan 03-03
   - Nested objects and arrays render correctly (criterion 4) -- from Plan 03-02
   - Components tab lists registered types (criterion 5) -- this plan
   - Entity list handles large counts with virtual scrolling (criterion 6) -- from Plan 03-02

## Phase 03 Completion

Phase 03 (Entities and Components) is now fully complete with all 4 plans executed:
- 03-01: Data pipeline (entity/component data model, JSON parsers, main loop polling)
- 03-02: UI utilities (scroll, split panel, JSON renderer, tree view)
- 03-03: Entities tab (interactive tree + component inspector)
- 03-04: Components tab (component registry + entity drill-down)

All 6 Phase 03 success criteria verified. Ready for Phase 04 (Systems and Pipeline).

## Commits

| Hash | Message |
|------|---------|
| 3fc8c8b | feat(03-04): implement Components tab with split panel and drill-down |
| 158c696 | feat(03-04): wire Components tab into tab system and build |
