---
phase: 04-systems-and-pipeline
plan: 04
subsystem: inspector
tags: [ncurses, system-inspector, cross-navigation, component-overlap, entity-matching]

# Dependency graph
requires:
  - phase: 04-systems-and-pipeline
    plan: 01
    provides: system_info_t, system_registry_t, pipeline stats polling
  - phase: 04-systems-and-pipeline
    plan: 02
    provides: tree_view phase sub-headers, entity_node_t.system_match_count and .disabled
  - phase: 04-systems-and-pipeline
    plan: 03
    provides: enrich_systems_with_pipeline, pipeline viz, systems summary, phase_color_pair in tab_ecs.c
provides:
  - System detail inspector panel (metadata + component access + approximate matched entities)
  - cross_navigate_to_entity() for jumping from inspector to entity tree
  - find_system_info() helper for system_registry lookup
  - build_system_matches() approximate entity matching via component overlap
  - System inspector input handling (scroll + cross-navigate)
affects: [05-polish (may extend inspector with more detail)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Approximate entity matching by component overlap (not exact Flecs query) -- clearly labeled (approx)"
    - "Cross-navigation: uncollapse target section, rebuild visible, find by path, move cursor"
    - "Footer message with clock_gettime expiry for transient notifications"

key-files:
  created: []
  modified:
    - tools/cels-debug/src/tabs/tab_ecs.c

key-decisions:
  - "Approximate matching filters out flecs.* and Component-type components before overlap check"
  - "Cross-navigation tries Entities section first, then Compositions section"
  - "Footer message uses CLOCK_MONOTONIC with 3-second expiry for entity-not-found"
  - "System inspector branch inserted between header checks and component entity check in dispatch"

patterns-established:
  - "Cross-navigation pattern: uncollapse section -> rebuild -> find by path -> move cursor -> switch focus"
  - "Inspector input branches ordered: system -> component -> entity detail (matches draw dispatch)"

# Metrics
duration: 2min
completed: 2026-02-06
---

# Phase 04 Plan 04: System Detail Inspector and Cross-Navigation Summary

**System detail inspector with metadata/component access/approximate matched entities plus cross-navigation to entity tree via component overlap matching**

## Performance

- **Duration:** ~2 min
- **Started:** 2026-02-06T21:29:17Z
- **Completed:** 2026-02-06T21:31:41Z
- **Tasks:** 2
- **Files modified:** 1

## Accomplishments
- System detail inspector panel showing: name, phase (color-coded), status (enabled/disabled), match count, timing, table count, full path, component access list, and scrollable approximate matched entities
- Approximate entity matching via component overlap: filters flecs.* and Component-type components, finds entities sharing at least one component with the system's access pattern
- Cross-navigation from matched entity list to entity tree: uncollapses Entities (and Compositions) sections, moves cursor to target entity, switches focus to left panel
- Footer notification "Entity not found" with 3-second expiry when target entity is missing from tree
- Input handling for system inspector right panel: j/k scroll, PgUp/PgDn, g/G, Enter cross-navigates

## Task Commits

Each task was committed atomically:

1. **Task 1: System detail inspector rendering** - `ca36761` (feat)
2. **Task 2: Cross-navigation from system inspector to entity tree** - `539bd6e` (feat)

## Files Created/Modified
- `tools/cels-debug/src/tabs/tab_ecs.c` - Added find_system_info(), build_system_matches(), draw_system_detail(), cross_navigate_to_entity(), system inspector dispatch branch, system inspector input handling

## Decisions Made
- Approximate matching filters out `flecs.*` and `Component`-type components before overlap check, reducing false positives
- Cross-navigation tries Entities section first, then Compositions -- covers both leaf entities and parent nodes
- Footer message uses `CLOCK_MONOTONIC` with 3-second expiry for entity-not-found notification (matches existing footer pattern)
- System inspector branch inserted between header checks and component entity check in the inspector dispatch chain
- `#include <time.h>` added for clock_gettime (already had _POSIX_C_SOURCE 200809L)

## Deviations from Plan

None -- plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Phase 04 (Systems and Pipeline) is now complete with all 4 plans done
- Full system inspection workflow: select system -> see metadata -> browse approximate matches -> cross-navigate to entity
- Ready for Phase 05 (Polish and Performance)

---
*Phase: 04-systems-and-pipeline*
*Completed: 2026-02-06*
