---
phase: 03-entities-and-components
plan: 02
subsystem: tui-utilities
tags: [ncurses, scroll, split-panel, json-render, tree-view, virtual-scrolling]
dependency-graph:
  requires: [01-foundation, 03-01-data-pipeline]
  provides: [scroll-state, split-panel-layout, json-renderer, tree-view]
  affects: [03-03-entities-tab, 03-04-components-tab, 04-systems]
tech-stack:
  added: []
  patterns: [virtual-scrolling, DFS-tree-flattening, recursive-json-rendering, split-panel-layout]
key-files:
  created:
    - tools/cels-debug/src/scroll.h
    - tools/cels-debug/src/scroll.c
    - tools/cels-debug/src/split_panel.h
    - tools/cels-debug/src/split_panel.c
    - tools/cels-debug/src/json_render.h
    - tools/cels-debug/src/json_render.c
    - tools/cels-debug/src/tree_view.h
    - tools/cels-debug/src/tree_view.c
  modified:
    - tools/cels-debug/CMakeLists.txt
    - tools/cels-debug/src/tui.h
    - tools/cels-debug/src/tui.c
decisions:
  - id: 03-02-01
    decision: "prev_selected_id field added to tree_view_t for reliable cursor preservation across rebuilds"
    rationale: "Entity ID is the only stable identifier when entity_list is replaced on each poll"
metrics:
  duration: ~5 minutes
  completed: 2026-02-06
---

# Phase 03 Plan 02: UI Utility Modules Summary

Four reusable ncurses UI utility modules with scroll state, split-panel layout, recursive JSON renderer, and entity tree view with virtual scrolling.

## What Was Done

### Task 1: Create scroll, split_panel, and json_render modules

**scroll.h/c** -- Generic scroll state for any list with virtual scrolling. Tracks cursor position, scroll offset, and visible row count. All movement operations clamp to valid bounds and call `scroll_ensure_visible()` to maintain the invariant that the cursor is always within the visible window. Edge case: total_items <= 0 makes all operations no-ops.

**split_panel.h/c** -- Two-panel layout with 40/60 width split and focus tracking. Creates two ncurses windows side by side using `newwin()`. Active panel gets `A_BOLD` border with `CP_PANEL_ACTIVE`, inactive gets `A_DIM` with `CP_PANEL_INACTIVE`. Focus switches with KEY_LEFT/KEY_RIGHT. Resize destroys and recreates both windows while preserving focus state.

**json_render.h/c** -- Recursive yyjson value renderer for ncurses. Handles null (A_DIM), bool/number (CP_JSON_NUMBER), string (CP_JSON_STRING), object keys (CP_JSON_KEY), and nested objects/arrays with indentation. The `json_render_component()` convenience function renders a collapsible component header with expand/collapse indicator followed by the component's JSON value.

**tui.h/tui.c** -- Added CP_TREE_LINE through CP_CURSOR color pair defines (7-15) and their init_pair() calls. Added `setlocale(LC_ALL, "")` before `initscr()` for Unicode box drawing character support.

**Status:** scroll.h/c, split_panel.h/c, json_render.h/c committed in a51e48e (parallel execution). tui.h/tui.c color pairs committed in same batch.

### Task 2: Create tree_view module and update CMakeLists.txt

**tree_view.h/c** -- Entity tree with virtual scrolling. Does NOT own entity data (holds pointers into entity_list_t). DFS traversal from roots builds a flattened visible array respecting expand/collapse state and anonymous entity filter. Key features:
- `tree_view_rebuild_visible()`: DFS flattening with cursor preservation by entity ID
- `tree_view_toggle_expand()`: Toggle expand on cursor node, rebuild visible
- `tree_view_toggle_anonymous()`: Flip show_anonymous filter, rebuild visible
- `tree_view_render()`: Renders visible portion with Unicode box drawing characters (TREE_VERT, TREE_BRANCH, TREE_LAST, TREE_HORIZ), expand indicators, entity names, and right-aligned component names
- Tree line rendering uses `ancestor_has_next_sibling()` for correct vertical continuation lines

**CMakeLists.txt** -- Added scroll.c, split_panel.c, json_render.c, tree_view.c to add_executable() after existing source files and before tab files.

**Status:** Committed as acf545d.

## Decisions Made

| ID | Decision | Rationale |
|----|----------|-----------|
| 03-02-01 | Added `prev_selected_id` field to tree_view_t | Entity ID is the only stable identifier when entity_list is replaced on each poll; needed for reliable cursor preservation across rebuilds |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Color pair defines and setlocale needed before Task 1 files could compile**

- **Found during:** Task 1
- **Issue:** split_panel.c references CP_PANEL_ACTIVE/CP_PANEL_INACTIVE and json_render.c references CP_JSON_KEY/CP_JSON_STRING/CP_JSON_NUMBER, but these color pairs were not yet defined in tui.h (they're part of Plan 03-01 Task 3)
- **Fix:** Added CP_TREE_LINE through CP_CURSOR defines to tui.h and corresponding init_pair() calls to tui.c. Added setlocale(LC_ALL,"") for Unicode support.
- **Files modified:** tools/cels-debug/src/tui.h, tools/cels-debug/src/tui.c
- **Commit:** a51e48e (batched with Task 1 files)

**Note:** The data_model.h/c entity types (entity_node_t, entity_list_t) that tree_view depends on were already present from Plan 03-01 execution (commits c3a1fb9, 76d6f71).

## Verification

1. `cmake -B build && cmake --build build` succeeds with zero warnings -- PASS
2. scroll.h declares scroll_state_t and 6 scroll functions -- PASS (7 matches)
3. split_panel.h declares split_panel_t with create/destroy/resize/refresh -- PASS (8 matches)
4. json_render.h declares json_render_value and json_render_component -- PASS (2 matches)
5. tree_view.h declares tree_view_t with rebuild_visible, toggle_expand, toggle_anonymous, render -- PASS (9 matches)
6. CMakeLists.txt lists all 4 new .c files -- PASS (4 matches)
7. No wrefresh() calls in any module (all use wnoutrefresh pattern) -- PASS

## Next Phase Readiness

All four utility modules are ready for consumption by:
- **Plan 03-03 (Entities tab):** Will use tree_view, split_panel, scroll, json_render
- **Plan 03-04 (Components tab):** Will use split_panel, scroll, json_render
- **Phase 04 (Systems tab):** Will reuse scroll and split_panel
