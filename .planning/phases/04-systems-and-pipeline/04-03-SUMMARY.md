---
phase: 04-systems-and-pipeline
plan: 03
subsystem: inspector
tags: [ncurses, pipeline-viz, enrichment, inspector, box-drawing, phase-order]

# Dependency graph
requires:
  - phase: 04-systems-and-pipeline
    plan: 01
    provides: system_info_t, system_registry_t, CP_PHASE_* color pairs, pipeline stats polling
  - phase: 04-systems-and-pipeline
    plan: 02
    provides: tree_view phase sub-headers, display_row_t.phase_group, tree_view_set_phases()
provides:
  - enrich_systems_with_pipeline() wiring pipeline stats into entity nodes
  - Pipeline visualization inspector (vertical flow with box-drawing connectors)
  - Systems summary inspector (total/enabled/disabled + phase distribution)
  - PHASE_ORDER canonical execution order array in tab_ecs.c
  - phase_color_pair() in tab_ecs.c for inspector rendering
affects: [04-04 (perf tab may reuse pipeline viz patterns)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Inspector dispatch checks cur_row header type before entity-based branches"
    - "Pipeline viz uses UTF-8 box-drawing connectors between phases"
    - "Enrichment matches systems by leaf name (O(n*m) but sufficient for <100 systems)"

key-files:
  created: []
  modified:
    - tools/cels-debug/src/tabs/tab_ecs.c

key-decisions:
  - "PHASE_ORDER and phase_color_pair() duplicated in tab_ecs.c with sync comments (avoids header changes)"
  - "Inspector dispatch uses cur_row (display_row_t*) to detect headers before checking entity selection"
  - "Pipeline viz aggregates per-phase timing by cross-referencing system_registry with entity classification"

patterns-established:
  - "Inspector branch dispatch order: header checks first, then entity-class checks, then fallbacks"
  - "Phase timing aggregation pattern: iterate system_registry, find matching entity node, check class_detail for phase"

# Metrics
duration: 2min
completed: 2026-02-06
---

# Phase 04 Plan 03: System Enrichment and Inspector Branches Summary

**enrich_systems_with_pipeline() wiring plus pipeline viz and systems summary inspector branches with color-coded phase rendering**

## Performance

- **Duration:** ~2 min
- **Started:** 2026-02-06T21:25:24Z
- **Completed:** 2026-02-06T21:27:31Z
- **Tasks:** 2
- **Files modified:** 1

## Accomplishments
- Added PHASE_ORDER canonical Flecs execution order array for sorting phases
- Added phase_color_pair() duplicate in tab_ecs.c for inspector rendering (sync comments referencing tree_view.c)
- Implemented enrich_systems_with_pipeline() that builds sorted phase list from entity classification, passes to tree_view_set_phases(), and enriches entity nodes with match count and disabled status from pipeline stats
- Wired enrichment into tab_ecs_draw() after classify + annotate, before tree rebuild
- Implemented draw_pipeline_viz() showing vertical pipeline flow with box-drawing connectors, color-coded phases, system counts, per-phase timing, and highlighted selected phase
- Implemented draw_systems_summary() showing total/enabled/disabled counts and color-coded phase distribution
- Modified inspector dispatch to check cur_row header type (phase sub-header vs section header) before falling through to entity-based branches

## Task Commits

Each task was committed atomically:

1. **Task 1: System enrichment function and wiring into draw cycle** - `74c9ef5` (feat)
2. **Task 2: Inspector branches for pipeline visualization and systems summary** - `6320f16` (feat)

## Files Created/Modified
- `tools/cels-debug/src/tabs/tab_ecs.c` - Added PHASE_ORDER, phase_color_pair(), enrich_systems_with_pipeline(), draw_pipeline_viz(), draw_systems_summary(), inspector dispatch with cur_row header detection

## Decisions Made
- PHASE_ORDER and phase_color_pair() are duplicated in tab_ecs.c rather than extracted to a shared header. Both have sync comments pointing to tree_view.c counterpart. This keeps the change to a single file.
- Inspector dispatch uses a display_row_t* (cur_row) to check for header/sub-header selection before the existing entity-class-based branches. This preserves backward compatibility with all existing inspector branches.
- Pipeline viz aggregates per-phase timing by cross-referencing system_registry entries with entity nodes (matched by name), then checking the entity's class_detail for phase membership.

## Deviations from Plan

None -- plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Systems section is now fully functional: enriched entity nodes, phase sub-headers, pipeline viz inspector, and systems summary inspector
- Ready for Plan 04 (Performance tab with frame time graphs and system profiling)
- Pipeline viz rendering patterns (box-drawing connectors, phase timing aggregation) can be reused in Performance tab

---
*Phase: 04-systems-and-pipeline*
*Completed: 2026-02-06*
