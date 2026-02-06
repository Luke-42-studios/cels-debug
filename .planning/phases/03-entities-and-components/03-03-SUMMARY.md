---
phase: 03-entities-and-components
plan: 03
subsystem: entities-tab
tags: [ncurses, entities, tree-view, split-panel, component-inspector, keyboard-navigation]
dependency-graph:
  requires: [03-01-data-pipeline, 03-02-ui-utilities]
  provides: [entities-tab, entity-browsing, component-inspection]
  affects: [03-04-components-tab]
tech-stack:
  added: []
  patterns: [split-panel-tab, inspector-with-virtual-scroll, auto-select-first-entity]
key-files:
  created:
    - tools/cels-debug/src/tabs/tab_entities.h
    - tools/cels-debug/src/tabs/tab_entities.c
  modified:
    - tools/cels-debug/src/tab_system.c
    - tools/cels-debug/CMakeLists.txt
decisions: []
metrics:
  duration: ~2 minutes
  completed: 2026-02-06
---

# Phase 03 Plan 03: Entities Tab Summary

Split-panel Entities tab with interactive entity tree (left) and collapsible component inspector (right), replacing the placeholder tab in the tab system.

## One-Liner

Full Entities tab: tree_view in left panel with j/k/Enter/f navigation, component inspector in right panel with collapsible groups rendering tags/pairs/components via json_render, wired into tab_defs[1].

## What Was Done

### Task 1: Implement the Entities tab

**tab_entities.h** -- Standard tab vtable declarations (init/fini/draw/input) following the tab_overview.h pattern.

**tab_entities.c** -- Full 310-line implementation with entities_state_t private state:

- `entities_state_t`: holds split_panel_t, tree_view_t, inspector scroll_state_t, comp_expanded bool array for collapsible component groups, and panel_created flag for deferred window creation.
- `tab_entities_init()`: Allocates state, initializes tree_view and scroll, defers panel creation to first draw (dimensions unknown at init).
- `tab_entities_fini()`: Destroys panel windows, frees tree_view, comp_expanded array, and state struct.
- `tab_entities_draw()`: Creates/resizes split panel on first draw or terminal resize. Left panel renders entity tree via tree_view_rebuild_visible + tree_view_render, or "Waiting for data..." if no entity_list. Right panel renders component inspector with collapsible component groups (via json_render_component), Tags section, and Pairs section, all with virtual scrolling. Auto-selects first entity on initial data load. Shows "Loading...", "Select an entity", or full inspector based on state.
- `tab_entities_input()`: Left panel: j/k, arrows, Page Up/Down, g/G, Enter (expand/collapse), f (anonymous toggle) -- all update selected_entity_path and clear stale detail. Right panel: j/k scroll inspector, Enter toggles component group expand/collapse via cursor_to_group_index mapping.

Helper functions:
- `count_inspector_rows()`: Estimates total inspector rows for scroll total calculation
- `ensure_comp_expanded()`: Dynamically grows expand/collapse bool array as component count changes
- `count_groups()`: Counts component + tags + pairs groups for expand array sizing
- `cursor_to_group_index()`: Maps inspector scroll cursor row to group index for expand toggle
- `sync_selected_path()`: Updates app_state.selected_entity_path from tree cursor, clears stale detail

**Commit:** bb73799

### Task 2: Wire Entities tab into tab system and build

- Added `#include "tabs/tab_entities.h"` to tab_system.c
- Replaced tab_defs[1] (Entities) from placeholder functions to tab_entities_init/fini/draw/input
- Added `src/tabs/tab_entities.c` to CMakeLists.txt add_executable source list
- Full build compiles and links cleanly

**Commit:** 51c4f56

## Decisions Made

No new architectural decisions needed. All patterns established in Plans 03-01 and 03-02 applied directly.

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

1. `cmake --build build` succeeds -- PASS
2. tab_system.c includes tab_entities.h and has real vtable entry -- PASS
3. CMakeLists.txt includes tab_entities.c -- PASS
4. Tab implements split panel with entity tree (left) and inspector (right)
5. j/k/arrows/Enter/f keyboard handling implemented for both panels
6. Component groups collapsible with Enter in right panel
7. Auto-select first entity on initial data load
8. Inspector virtual scrolling with scroll_state_t

## Next Phase Readiness

Plan 03-04 (Components tab) can proceed immediately. All utility modules and the Entities tab pattern are established. The Components tab will follow the same split-panel + scroll pattern.

## Commits

| Hash | Message |
|------|---------|
| bb73799 | feat(03-03): implement Entities tab with split panel, tree, and inspector |
| 51c4f56 | feat(03-03): wire Entities tab into tab system and CMake build |
