---
phase: 05-state-performance-and-polish
plan: 04
subsystem: ui
tags: [ncurses, navigation, auto-reconnect, cli-flags, state-machine]

# Dependency graph
requires:
  - phase: 05-01
    provides: 4-tab layout with pending_tab cross-navigation field
provides:
  - Navigation back-stack for cross-tab jumps (Esc returns to origin)
  - Context-sensitive footer hints per active tab
  - Fixed auto-reconnect state machine (Reconnecting persists on repeated failures)
  - Configurable poll interval via -r flag (100-5000ms)
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "nav_stack_t fixed-size stack for back-navigation (max 8 entries)"
    - "Context-sensitive footer hints via active tab index switch"
    - "CLI flag parsing with clamped range validation"

key-files:
  created: []
  modified:
    - "cels-debug/src/tui.h"
    - "cels-debug/src/tui.c"
    - "cels-debug/src/main.c"
    - "cels-debug/src/http_client.c"
    - "cels-debug/src/http_client.h"

key-decisions:
  - "Nav stack helpers are static in main.c (not exposed in tui.h) since only main loop uses them"
  - "Footer hints inline switch in tui_render (no separate function needed)"
  - "-r flag parsed before tui_init so poll_interval_ms is ready at main loop start"
  - "CONN_RECONNECTING || CONN_CONNECTED both transition to RECONNECTING on failure"

patterns-established:
  - "nav_push before pending_tab switch, nav_clear on direct tab switch"
  - "CLI flags parsed with clamped bounds (100-5000ms for -r)"

# Metrics
duration: 2min
completed: 2026-02-06
---

# Phase 05 Plan 04: Navigation, Reconnect, and Polish Summary

**Esc back-navigation stack for cross-tab jumps, context-sensitive footer hints, fixed auto-reconnect state machine, and configurable -r poll interval**

## Performance

- **Duration:** ~2 min
- **Started:** 2026-02-06T23:31:33Z
- **Completed:** 2026-02-06T23:33:57Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- Navigation back-stack: cross-tab jumps push origin, Esc pops back with entity cursor restore
- Context-sensitive footer: Overview shows minimal hints, CELS/Systems show full nav keys, Performance shows scroll-only
- Auto-reconnect fix: RECONNECTING state persists on repeated failures (no premature Disconnected)
- Configurable poll interval: `-r <ms>` flag with 100-5000ms range, 500ms default

## Task Commits

Each task was committed atomically:

1. **Task 1: Navigation back-stack and context-sensitive footer** - `f59b453` (feat)
2. **Task 2: Auto-reconnect fix and configurable poll interval** - `6c398c6` (fix)

## Files Created/Modified
- `cels-debug/src/tui.h` - Added nav_stack_t, nav_entry_t types; poll_interval_ms and nav_stack fields in app_state_t
- `cels-debug/src/tui.c` - Context-sensitive footer hints via active tab switch
- `cels-debug/src/main.c` - Nav stack helpers (push/pop/clear), Esc handling, pending_tab push, -r flag parsing, configurable poll interval
- `cels-debug/src/http_client.c` - Fixed connection_state_update: RECONNECTING stays RECONNECTING on failure
- `cels-debug/src/http_client.h` - Updated state machine transition comments

## Decisions Made
- [05-04]: Nav stack helpers static in main.c -- only the main loop pushes/pops, no need to expose in header
- [05-04]: Footer hints inline switch rather than separate function -- simpler, same result
- [05-04]: -r flag parsed with atoi + clamp (100-5000ms) -- no need for getopt for a single flag
- [05-04]: CONN_RECONNECTING || CONN_CONNECTED both -> RECONNECTING on failure (one-line fix to existing condition)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All 4 plans of Phase 05 are now complete
- Navigation, reconnect, and polish items finalize the v0.1 debugger experience
- Phase 05 complete: State, Performance, and Polish done

---
*Phase: 05-state-performance-and-polish*
*Completed: 2026-02-06*
