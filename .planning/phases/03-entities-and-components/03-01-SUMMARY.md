---
phase: 03-entities-and-components
plan: 01
subsystem: data-pipeline
tags: [data-model, json-parser, rest-api, yyjson, entity-tree]
dependency-graph:
  requires: [01-foundation, 02-tab-system-and-overview]
  provides: [entity-list-data, entity-detail-data, component-registry-data, conditional-polling]
  affects: [03-02, 03-03, 03-04]
tech-stack:
  added: []
  patterns: [dot-to-slash-path-conversion, parent-child-tree-from-flat-list, yyjson-doc-ownership]
key-files:
  created: []
  modified:
    - tools/cels-debug/src/data_model.h
    - tools/cels-debug/src/data_model.c
    - tools/cels-debug/src/json_parser.h
    - tools/cels-debug/src/json_parser.c
    - tools/cels-debug/src/tui.h
    - tools/cels-debug/src/tui.c
    - tools/cels-debug/src/main.c
    - tools/cels-debug/src/tab_system.c
decisions:
  - id: 03-01-01
    description: "_POSIX_C_SOURCE 200809L for strdup availability in C99"
    rationale: "C99 strict mode does not declare strdup without POSIX feature macro"
metrics:
  duration: ~4 minutes
  completed: 2026-02-06
---

# Phase 03 Plan 01: Entity/Component Data Pipeline Summary

Extended the data pipeline to fetch, parse, and store entity list, entity detail, and component registry data from flecs REST API endpoints. Provides the data foundation for the Entities and Components tabs.

## One-Liner

Data model with 5 new types (entity_node_t tree, entity_detail_t with yyjson_doc ownership, component_registry_t), 3 JSON parsers with dot-to-slash path conversion, and conditional main loop polling gated on tab endpoint bitmask.

## What Was Done

### Task 1: Extend data model with entity and component types
- Added `entity_node_t` with name, full_path, id, component_names, tags, parent/children tree links, expanded/is_anonymous UI state, and depth for indentation
- Added `entity_list_t` with flat ownership array and root node pointers
- Added `entity_detail_t` that owns a yyjson_doc for zero-copy component value access
- Added `component_info_t` and `component_registry_t` for /components endpoint
- Implemented create/free for all types; entity_node_add_child with capacity doubling
- Commit: `c3a1fb9`

### Task 2: Add JSON parsers for entity list, entity detail, and component registry
- `json_parse_entity_list`: parses /query response, builds parent-child tree from flat results using dot-to-slash path conversion. O(n^2) parent lookup acceptable for <5K entities
- `json_parse_entity_detail`: parses /entity/<path> response, preserves yyjson_doc lifetime. All yyjson_val* pointers valid while doc lives
- `json_parse_component_registry`: parses /components array root with type_info size extraction
- Added `_POSIX_C_SOURCE 200809L` for strdup in C99
- Commit: `76d6f71`

### Task 3: Extend app_state and main loop with conditional endpoint polling
- Extended app_state_t with entity_list, entity_detail, component_registry, selected_entity_path, footer_message with timed expiry
- Added conditional polling in main loop: /query when ENDPOINT_QUERY bit set, /entity/<path> when ENDPOINT_ENTITY and entity selected, /components when ENDPOINT_COMPONENTS
- Handles entity deletion (404/network error) by clearing detail and showing "Selected entity removed" footer notification for 3 seconds
- Added color pairs CP_TREE_LINE through CP_CURSOR (7-15) for Phase 03 UI
- Added setlocale(LC_ALL, "") before initscr() for Unicode box drawing support
- Updated Entities tab bitmask to ENDPOINT_QUERY | ENDPOINT_ENTITY
- Added cleanup frees for all new app_state fields
- Commit: `691dad5`

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| _POSIX_C_SOURCE 200809L in json_parser.c | C99 strict mode does not expose strdup without POSIX feature macro; needed for all string duplication in parsers |
| O(n^2) parent lookup in entity list parser | Simpler than hash table; acceptable for <5K entities per the research doc; can be optimized later if needed |
| yyjson_doc ownership in entity_detail_t | Zero-copy access to component values; doc freed only when detail is replaced or freed |
| 404 handling clears selected_entity_path | Prevents repeated 404 polls; user gets footer notification |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added _POSIX_C_SOURCE 200809L to json_parser.c**
- **Found during:** Task 2
- **Issue:** strdup not declared in C99 strict mode, causing implicit-function-declaration errors
- **Fix:** Added `#define _POSIX_C_SOURCE 200809L` before includes
- **Files modified:** tools/cels-debug/src/json_parser.c
- **Commit:** 76d6f71

**2. [Rule 3 - Blocking] Updated _POSIX_C_SOURCE in main.c from 199309L to 200809L**
- **Found during:** Task 3
- **Issue:** strdup needed for footer_message creation; 199309L does not expose strdup
- **Fix:** Changed to 200809L which covers all needed POSIX functions
- **Files modified:** tools/cels-debug/src/main.c
- **Commit:** 691dad5

## Verification Results

All 7 verification criteria passed:
1. cmake --build succeeds with no warnings
2. All 5 new data types declared in data_model.h with create/free in data_model.c
3. All 3 JSON parsers declared in json_parser.h and implemented in json_parser.c
4. main.c polling block includes 3 conditional fetches gated on endpoint bitmask
5. app_state_t has entity_list, entity_detail, component_registry, selected_entity_path
6. CP_TREE_LINE through CP_CURSOR (7-15) defined in tui.h and initialized in tui.c
7. setlocale(LC_ALL, "") called in tui_init() before initscr()

## Next Phase Readiness

Plans 02-04 of Phase 03 can proceed immediately:
- **03-02** (Entities tab UI): entity_list and entity_detail data available in app_state
- **03-03** (Components tab UI): component_registry data available in app_state
- **03-04** (Polish): All data types and color pairs ready

No blockers identified.

## Commits

| Hash | Message |
|------|---------|
| c3a1fb9 | feat(03-01): extend data model with entity and component types |
| 76d6f71 | feat(03-01): add JSON parsers for entity list, entity detail, and component registry |
| 691dad5 | feat(03-01): extend app_state and main loop with conditional endpoint polling |
