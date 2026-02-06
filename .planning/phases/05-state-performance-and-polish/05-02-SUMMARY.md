---
phase: 05-state-performance-and-polish
plan: 02
subsystem: cels-tab-state-section
tags: [state, entity-classification, change-highlighting, cels-c-paradigm]
dependency-graph:
  requires: [05-01]
  provides: [entity-class-state, state-section, change-flash]
  affects: [05-03, 05-04]
tech-stack:
  added: []
  patterns: [name-heuristic-classification, json-diff-flash]
key-files:
  created: []
  modified:
    - tools/cels-debug/src/data_model.h
    - tools/cels-debug/src/tree_view.c
    - tools/cels-debug/src/tabs/tab_cels.c
decisions:
  - id: 05-02-01
    description: "ENTITY_CLASS_STATE at position 3, ENTITY_CLASS_SYSTEM moved to position 5"
    rationale: "Sections 0-4 must spell C-E-L-S-C. SYSTEM is internal-only (used by Systems tab), so it goes at the end."
  - id: 05-02-02
    description: "name_ends_with_state() heuristic for State classification"
    rationale: "CEL_State values are C-level static variables not exposed via Flecs REST API. Name suffix matching is the best available v0.1 approach."
  - id: 05-02-03
    description: "yyjson_val_write() serialization for change detection"
    rationale: "Serializing the components JSON to string and comparing with previous poll is simple and correct. No per-field diffing needed for the flash effect."
  - id: 05-02-04
    description: "Flash applies A_BOLD | COLOR_PAIR(CP_RECONNECTING) to entire inspector"
    rationale: "Simpler than per-field highlighting. CP_RECONNECTING (yellow) is already defined and stands out without adding new color pairs."
metrics:
  duration: "~2 minutes"
  completed: "2026-02-06"
---

# Phase 05 Plan 02: State Section in CELS Tab Summary

Add ENTITY_CLASS_STATE to complete the CELS-C acronym (Compositions, Entities, Lifecycles, State, Components). State entities detected via name_ends_with_state heuristic. Inspector flashes bold/yellow for 2 seconds on component value changes.

## What Was Done

### Task 1: Update entity_class_t enum and section names
- Inserted `ENTITY_CLASS_STATE` at position 3 in the `entity_class_t` enum
- Shifted `ENTITY_CLASS_COMPONENT` to position 4, `ENTITY_CLASS_SYSTEM` to position 5
- Updated enum comment: "sections 0-4 spell CELS-C, index 5 is internal (Systems tab)"
- Updated `tree_view.c` `section_names` array to include "State" at the correct index
- Verified all references use symbolic names (no hardcoded numeric indices)
- Verified `tab_systems.c` compiles without changes (uses `ENTITY_CLASS_SYSTEM` symbolically)

### Task 2: State classification and change highlighting in CELS tab
- Added `name_ends_with_state()` helper (case-sensitive suffix match on "State")
- Inserted State classification in `classify_node()` between Components and Lifecycles checks
- Priority order: Systems > Components > State > Lifecycles > Entities > Compositions
- Added change-highlighting fields to `cels_state_t`: `prev_entity_json`, `prev_entity_path`, `flash_expire_ms`
- Added `now_ms()` helper using `CLOCK_MONOTONIC`
- Flash detection: serialize components via `yyjson_val_write()`, compare with previous serialization
- Flash rendering: `A_BOLD | COLOR_PAIR(CP_RECONNECTING)` on entire inspector content for 2 seconds
- Freed new fields in `tab_cels_fini`

## Decisions Made

| ID | Decision | Rationale |
|----|----------|-----------|
| 05-02-01 | ENTITY_CLASS_STATE at position 3 | Sections 0-4 spell C-E-L-S-C; SYSTEM is internal-only |
| 05-02-02 | name_ends_with_state() heuristic | Best available v0.1 approach since CEL_State is not in Flecs REST API |
| 05-02-03 | yyjson_val_write() for change detection | Simple string comparison, no per-field diffing needed |
| 05-02-04 | CP_RECONNECTING (yellow) flash on entire inspector | Reuses existing color pair, simple and visible |

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

1. Build: 0 errors, 0 warnings (full rebuild)
2. Enum: ENTITY_CLASS_STATE at position 3, ENTITY_CLASS_SYSTEM at position 5
3. Section names: Compositions, Entities, Lifecycles, State, Components, Systems (6 entries)
4. tab_cels.c: State classification via name_ends_with_state() heuristic
5. tab_cels.c: Flash fields (flash_expire_ms, prev_entity_json, prev_entity_path)
6. tab_systems.c: compiles without changes (symbolic ENTITY_CLASS_SYSTEM references)

## Commits

| Task | Commit | Description |
|------|--------|-------------|
| 1 | 4444e62 | feat(05-02): add ENTITY_CLASS_STATE enum value and update section names |
| 2 | f40911d | feat(05-02): add State classification and change highlighting to CELS tab |

## Next Phase Readiness

Plan 05-03 (Performance tab) can proceed. The CELS tab now displays the complete CELS-C paradigm with 5 sections. The entity classification system supports all 6 enum values (5 visible sections + 1 internal Systems tab class).
