---
phase: 04-systems-and-pipeline
plan: 02
subsystem: tree-view
tags: [ncurses, tree-view, phase-grouping, display-row, color-pairs]

# Dependency graph
requires:
  - phase: 04-systems-and-pipeline
    plan: 01
    provides: system_info_t, system_registry_t, CP_PHASE_* color pairs, pipeline stats polling
  - phase: 03.1-redesign-navigation-ecs-tabs
    provides: CELS-C tree view with section headers, display_row_t, tree_view_t
provides:
  - display_row_t.phase_group field for phase sub-header rows
  - tree_view_t phase collapse state (phase_names, phase_system_counts, phase_collapsed)
  - tree_view_set_phases() API for setting phase grouping data
  - phase_color_pair() canonical phase-to-color mapping in tree_view.c
  - draw_phase_subheader() for rendering phase group headers
  - Phase-grouped system rendering in rebuild_visible
  - entity_node_t.system_match_count and .disabled fields
affects: [04-03 (inspector), 04-04 (perf tab)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Phase sub-headers use same collapsible row pattern as CELS-C section headers but with phase_group >= 0"
    - "Custom sentinel: phase_group == phase_count for systems with unknown phase names"
    - "Collapse state preserved across tree_view_set_phases calls by matching phase name strings"

key-files:
  created: []
  modified:
    - tools/cels-debug/src/data_model.h
    - tools/cels-debug/src/tree_view.h
    - tools/cels-debug/src/tree_view.c

key-decisions:
  - "Phase sub-header phase_group field: -1 for section/entity rows, 0..N-1 for phase indices, N for Custom group"
  - "Custom group (phase_group == phase_count) is not collapsible -- always shows remaining uncategorized systems"
  - "_POSIX_C_SOURCE 200809L added to tree_view.c for strdup availability in C99"

patterns-established:
  - "phase_color_pair() in tree_view.c is the canonical location for phase-to-color mapping"
  - "Phase sub-header rows: node=NULL, section_idx=ENTITY_CLASS_SYSTEM, phase_group=index"

# Metrics
duration: 3min
completed: 2026-02-06
---

# Phase 04 Plan 02: Tree View Phase Sub-headers Summary

**display_row_t phase_group field, phase collapse state, phase sub-header rendering, system rows with color-coded phase tags and match counts**

## Performance

- **Duration:** ~3 min
- **Started:** 2026-02-06T21:19:52Z
- **Completed:** 2026-02-06T21:23:23Z
- **Tasks:** 1
- **Files modified:** 3

## Accomplishments
- Extended display_row_t with phase_group field (-1 for section/entity rows, >=0 for phase sub-headers)
- Extended tree_view_t with phase collapse state arrays (phase_names, phase_system_counts, phase_collapsed, phase_count)
- Implemented tree_view_set_phases() with strdup'd ownership and collapse state preservation across calls
- Modified rebuild_visible to group ENTITY_CLASS_SYSTEM nodes under collapsible phase sub-headers
- Empty phases hidden automatically; unknown-phase systems collected under "Custom" sub-header
- Added phase_color_pair() canonical phase-to-color mapping (OnLoad through PostFrame + Custom)
- Added draw_phase_subheader() for indented, color-coded phase group rendering
- System entity rows now render [Phase] tag in phase color instead of CP_COMPONENT_HEADER
- System entity rows show (N) match count when system_match_count > 0
- Disabled system rows render with A_DIM applied to the entire row
- Added system_match_count and disabled fields to entity_node_t in data_model.h

## Task Commits

Each task was committed atomically:

1. **Task 1: data_model.h fields + tree_view.h structs + tree_view.c phase infrastructure** - `116670f` (feat)

## Files Created/Modified
- `tools/cels-debug/src/data_model.h` - Added system_match_count and disabled fields to entity_node_t
- `tools/cels-debug/src/tree_view.h` - Extended display_row_t with phase_group, added phase arrays to tree_view_t, declared tree_view_set_phases()
- `tools/cels-debug/src/tree_view.c` - Phase init/fini, set_phases, phase-grouped rebuild, phase sub-header toggle, phase_color_pair, draw_phase_subheader, system row rendering with phase colors + match counts + disabled dimming

## Decisions Made
- Phase sub-header rows use sentinel values: phase_group == -1 for section headers and entity rows, 0..N-1 for known phases, N for Custom group
- Custom group (unknown phases) is not collapsible to keep all uncategorized systems visible
- _POSIX_C_SOURCE 200809L added for strdup in C99 (matches pattern from Phase 03-01 json_parser.c)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] _POSIX_C_SOURCE for strdup in C99**
- **Found during:** Task 1 build verification
- **Issue:** strdup requires _POSIX_C_SOURCE 200809L in C99 mode
- **Fix:** Added `#define _POSIX_C_SOURCE 200809L` before includes in tree_view.c
- **Files modified:** tools/cels-debug/src/tree_view.c
- **Commit:** 116670f

## Issues Encountered
None beyond the expected _POSIX_C_SOURCE requirement.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- tree_view_set_phases() is ready to be called by tab_ecs.c (Plan 03) before each rebuild
- entity_node_t.system_match_count and .disabled are zero-initialized by calloc, ready for enrichment in Plan 03
- phase_color_pair() is available for inspector panel rendering in Plan 03
- All infrastructure is in place for Plan 03 to wire up enrichment data and implement the inspector branches

---
*Phase: 04-systems-and-pipeline*
*Completed: 2026-02-06*
