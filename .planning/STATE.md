# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-02-05)

**Core value:** Real-time visibility into a running CELS application -- entities, state, systems, and performance -- in a single terminal window.
**Current focus:** Phase 02 - Tab System and Overview

## Current Position

Phase: 02 of 05 (Tab System and Overview)
Plan: 1 of 2 in current phase
Status: In progress
Last activity: 2026-02-06 -- Completed 02-01-PLAN.md (tab vtable framework + tab bar UI)

Progress: [███.......] 30%

## Performance Metrics

**Velocity:**
- Total plans completed: 4
- Phase 01: ~25 minutes total
- Phase 02 Plan 01: ~2 minutes

**By Phase:**

| Phase | Plans | Status |
|-------|-------|--------|
| 01-foundation | 3/3 | Complete |
| 02-tab-system-and-overview | 1/2 | In progress |

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

### Pending Todos

None.

### Blockers/Concerns

None.

## Session Continuity

Last session: 2026-02-06
Stopped at: Completed 02-01-PLAN.md
Resume file: None

---
*Created: 2026-02-05*
*Updated: 2026-02-06 (Plan 02-01 complete)*
