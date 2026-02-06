# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-02-05)

**Core value:** Real-time visibility into a running CELS application -- entities, state, systems, and performance -- in a single terminal window.
**Current focus:** Phase 04 (Systems and Pipeline) -- in progress

## Current Position

Phase: 04 of 05 (Systems and Pipeline)
Plan: 2 of 4 in current phase
Status: In progress
Last activity: 2026-02-06 -- Completed 04-02-PLAN.md (tree view phase sub-headers)

Progress: [█████████.] 86%

## Performance Metrics

**Velocity:**
- Total plans completed: 12
- Phase 01: ~25 minutes total
- Phase 02 Plan 01: ~2 minutes
- Phase 02 Plan 02: ~2 minutes
- Phase 02 total: ~4 minutes
- Phase 03 Plan 01: ~4 minutes
- Phase 03 Plan 02: ~5 minutes
- Phase 03 Plan 03: ~2 minutes
- Phase 03 Plan 04: ~2 minutes
- Phase 03 total: ~13 minutes
- Phase 04 Plan 01: ~4 minutes
- Phase 04 Plan 02: ~3 minutes

**By Phase:**

| Phase | Plans | Status |
|-------|-------|--------|
| 01-foundation | 3/3 | Complete |
| 02-tab-system-and-overview | 2/2 | Complete |
| 03-entities-and-components | 4/4 | Complete |
| 03.1-redesign-navigation-ecs-tabs | 1/1 | Complete |
| 04-systems-and-pipeline | 2/4 | In progress |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Research]: libcurl easy interface with CURLOPT_TIMEOUT_MS=200 (not curl_multi or raw sockets)
- [Research]: yyjson over cJSON (cleaner FetchContent integration, immutable doc API matches read-only use case)
- [Research]: Plain newwin() over ncurses panels (windows do not overlap)
- [Research]: Single-threaded MVC loop (ncurses is not thread-safe; curl_easy sufficient for localhost)
- [Research]: FlecsStats behind #ifdef CELS_DEBUG (zero overhead in release builds)
- [Research]: Snapshot data model -- each poll produces new snapshot, previous freed atomically
- [01-01]: CELS_DEBUG and CELS_BUILD_TOOLS CMake options (both default OFF)
- [01-01]: FlecsStats import at line 515 of cels.cpp, after EcsRest config
- [01-02]: connection_state_t pure function for state machine transitions
- [01-02]: http_response_t owns body buffer, caller frees via http_response_free()
- [01-03]: timeout(100) for getch -- 10fps idle loop, good CPU/responsiveness balance
- [01-03]: assume_default_colors(-1, -1) for terminal theme compatibility
- [01-03]: run.sh script for build+run from terminal (bypasses VS Code terminal theme issues)
- [02-01]: void* for app_state in vtable signatures (avoids circular includes)
- [02-01]: app_state_t defined in tui.h (aggregates snapshot + conn_state)
- [02-01]: Always poll /stats/world for connection health, only parse if ENDPOINT_STATS_WORLD needed
- [02-01]: All 6 tabs use placeholder initially; Overview implementation in Plan 02-02
- [02-02]: CP_* defines moved from tui.c to tui.h (shared color pairs for all tab implementations)
- [03-01]: _POSIX_C_SOURCE 200809L in json_parser.c for strdup availability in C99
- [03-01]: O(n^2) parent lookup in entity list parser (acceptable for <5K entities)
- [03-01]: yyjson_doc ownership in entity_detail_t for zero-copy component value access
- [03-01]: 404 handling clears selected_entity_path and shows footer notification
- [03-02]: prev_selected_id field in tree_view_t for cursor preservation across entity_list rebuilds
- [03-04]: Sort component list alphabetically in draw (not parser) via qsort
- [03-04]: Entity filtering fresh each draw call, no caching needed for <5K entities
- [04-01]: extract_pipeline_gauge is separate static helper (pipeline metrics nested in array entries, not root)
- [04-01]: Phase field in system_info_t left NULL by parser -- enriched by tab_ecs classification in Plan 02
- [04-01]: Pipeline stats polling follows atomic swap pattern (same as component registry)
- [04-02]: Phase sub-header phase_group field: -1 for section/entity rows, 0..N-1 for phases, N for Custom
- [04-02]: Custom group (unknown phases) not collapsible -- keeps uncategorized systems visible
- [04-02]: phase_color_pair() in tree_view.c is canonical location for phase-to-color mapping

### Roadmap Evolution

- Phase 03.1 inserted after Phase 03: Redesign Navigation — ECS Tabs (URGENT)
  - Top-level tabs → Overview, ECS, Performance, State
  - ECS tab contains CELS-C tree view (Compositions, Entities, Lifecycles, Systems, Components)
  - Components becomes a section inside ECS, not a separate tab
  - Section headers navigable + collapsible, bold first letter spells CELS-C

### Pending Todos

None.

### Blockers/Concerns

None.

## Session Continuity

Last session: 2026-02-06
Stopped at: Completed 04-02-PLAN.md (tree view phase sub-headers)
Resume file: None

---
*Created: 2026-02-05*
*Updated: 2026-02-06 (Phase 04 Plan 02 complete -- tree view phase sub-headers)*
