---
phase: 04-systems-and-pipeline
plan: 01
subsystem: data-pipeline
tags: [json, yyjson, ncurses, pipeline-stats, color-pairs, polling]

# Dependency graph
requires:
  - phase: 03-entities-and-components
    provides: data_model.h structs, json_parser.c patterns, polling loop in main.c
  - phase: 03.1-redesign-navigation-ecs-tabs
    provides: ECS tab with endpoint mask system
provides:
  - system_info_t and system_registry_t data model structs
  - json_parse_pipeline_stats() parser for /stats/pipeline endpoint
  - Phase color pairs (CP_PHASE_ONLOAD..CP_SYSTEM_DISABLED, 16..26)
  - Pipeline stats polling in main loop when ECS/Performance tab active
  - system_registry_t* field in app_state_t
affects: [04-02 (tree view), 04-03 (inspector), 04-04 (perf tab)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "extract_pipeline_gauge helper reuses gauge extraction pattern from extract_latest_gauge"
    - "System entries distinguished from sync points by presence of 'name' vs 'system_count' fields"
    - "Phase field left NULL in parser -- enriched later by tab_ecs classification"

key-files:
  created: []
  modified:
    - tools/cels-debug/src/data_model.h
    - tools/cels-debug/src/data_model.c
    - tools/cels-debug/src/json_parser.h
    - tools/cels-debug/src/json_parser.c
    - tools/cels-debug/src/tui.h
    - tools/cels-debug/src/tui.c
    - tools/cels-debug/src/tab_system.c
    - tools/cels-debug/src/main.c

key-decisions:
  - "Task 1 code was already committed in prior planning revision (a90d0b5) -- no duplicate commit needed"
  - "extract_pipeline_gauge is a separate static helper (not reusing extract_latest_gauge) because pipeline gauge objects are nested within array entries, not at the root level"

patterns-established:
  - "Pipeline stats polling follows same pattern as component registry: check endpoint mask, poll if connected, swap atomically"

# Metrics
duration: 4min
completed: 2026-02-06
---

# Phase 04 Plan 01: Pipeline Stats Data Pipeline Summary

**system_info_t/system_registry_t data model with JSON parser for /stats/pipeline, phase color pairs, and polling loop integration**

## Performance

- **Duration:** ~4 min
- **Started:** 2026-02-06T21:13:53Z
- **Completed:** 2026-02-06T21:17:38Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- Data model structs (system_info_t, system_registry_t) with lifecycle functions for pipeline stats
- JSON parser differentiates system entries from sync points, extracts name/disabled/timing/match counts
- Phase color pairs CP_PHASE_ONLOAD..CP_SYSTEM_DISABLED (16..26) initialized for system rendering
- Pipeline stats polling integrated into main loop, triggered by ECS tab endpoint mask

## Task Commits

Each task was committed atomically:

1. **Task 1: Data model + JSON parser for pipeline stats** - `a90d0b5` (pre-existing in HEAD)
2. **Task 2: Phase color pairs + app_state + polling + ECS endpoint mask** - `403a52d` (feat)

**Plan metadata:** [pending] (docs: complete plan)

## Files Created/Modified
- `tools/cels-debug/src/data_model.h` - Added system_info_t and system_registry_t structs with lifecycle declarations
- `tools/cels-debug/src/data_model.c` - Added system_registry_create/free implementations
- `tools/cels-debug/src/json_parser.h` - Added json_parse_pipeline_stats declaration
- `tools/cels-debug/src/json_parser.c` - Added extract_pipeline_gauge helper and json_parse_pipeline_stats implementation
- `tools/cels-debug/src/tui.h` - Added CP_PHASE_* and CP_SYSTEM_DISABLED defines (16..26), system_registry_t* in app_state_t
- `tools/cels-debug/src/tui.c` - Added init_pair calls for all phase color pairs
- `tools/cels-debug/src/tab_system.c` - ECS tab endpoint mask now includes ENDPOINT_STATS_PIPELINE
- `tools/cels-debug/src/main.c` - Added /stats/pipeline polling block and system_registry_free in cleanup

## Decisions Made
- Task 1 code (data model + JSON parser) was already present in HEAD from a prior planning commit (a90d0b5). No duplicate commit was created -- the code was verified identical to plan specification.
- extract_pipeline_gauge is a standalone static helper separate from extract_latest_gauge, since pipeline metrics are nested within array entries rather than at the document root level.

## Deviations from Plan

None -- plan executed exactly as written. Task 1 code was pre-existing but matched plan specification exactly.

## Issues Encountered
- Task 1 files (data_model.h/c, json_parser.h/c) had already been committed in a prior revision (a90d0b5). The edits produced identical content, so there was no diff to commit. Task 2 proceeded normally with a clean commit.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- system_registry_t* is populated in app_state every 500ms when ECS or Performance tab is active
- Phase color pairs are initialized and available for system rendering
- Ready for Plan 02 (tree view rendering with phase-based sections) and Plan 03 (inspector panel)
- Phase field in system_info_t is NULL -- Plan 02 will add enrichment logic in tab_ecs.c

---
*Phase: 04-systems-and-pipeline*
*Completed: 2026-02-06*
