---
phase: 04-systems-and-pipeline
verified: 2026-02-06T22:00:00Z
status: passed
score: 6/6 must-haves verified
must_haves:
  truths:
    - "Systems section in CELS-C tree displays all registered systems from /query endpoint"
    - "Systems are grouped by Flecs execution phase (OnLoad, OnUpdate, OnStore, etc.)"
    - "Each system shows name, color-coded phase tag, enabled/disabled status, and match count"
    - "Pipeline visualization shows phase execution ordering with timing data"
    - "System detail inspector shows metadata and approximate matched entity list"
    - "Cross-navigation from matched entities to Entities section"
  artifacts:
    - path: "tools/cels-debug/src/data_model.h"
      provides: "system_info_t, system_registry_t, entity_node_t.system_match_count and .disabled"
    - path: "tools/cels-debug/src/data_model.c"
      provides: "system_registry lifecycle (create/free)"
    - path: "tools/cels-debug/src/json_parser.h"
      provides: "json_parse_pipeline_stats declaration"
    - path: "tools/cels-debug/src/json_parser.c"
      provides: "Pipeline stats JSON parser with extract_pipeline_gauge helper"
    - path: "tools/cels-debug/src/tui.h"
      provides: "CP_PHASE_* color pair defines (16-26), system_registry_t* in app_state_t"
    - path: "tools/cels-debug/src/tui.c"
      provides: "Phase color pair init_pair calls"
    - path: "tools/cels-debug/src/tree_view.h"
      provides: "display_row_t.phase_group, tree_view_t phase arrays, tree_view_set_phases()"
    - path: "tools/cels-debug/src/tree_view.c"
      provides: "Phase sub-header rendering, phase-grouped rebuild, phase_color_pair(), draw_phase_subheader()"
    - path: "tools/cels-debug/src/tabs/tab_ecs.c"
      provides: "enrich_systems_with_pipeline(), draw_pipeline_viz(), draw_systems_summary(), draw_system_detail(), build_system_matches(), cross_navigate_to_entity()"
    - path: "tools/cels-debug/src/tab_system.c"
      provides: "ECS tab endpoint mask includes ENDPOINT_STATS_PIPELINE"
    - path: "tools/cels-debug/src/main.c"
      provides: "Pipeline stats polling block and system_registry_free cleanup"
  key_links:
    - from: "main.c"
      to: "json_parse_pipeline_stats"
      via: "polling loop calls parser"
    - from: "tab_system.c"
      to: "ENDPOINT_STATS_PIPELINE"
      via: "ECS tab requests pipeline endpoint"
    - from: "tab_ecs.c"
      to: "enrich_systems_with_pipeline"
      via: "called in tab_ecs_draw before tree rebuild"
    - from: "tab_ecs.c"
      to: "tree_view_set_phases"
      via: "enrichment calls set_phases with sorted phase data"
    - from: "tab_ecs.c"
      to: "cross_navigate_to_entity"
      via: "Enter on matched entity in system inspector"
human_verification:
  - test: "Run cels-debug with a CELS app, navigate to ECS tab, expand Systems section"
    expected: "Phase sub-headers (OnUpdate, OnStore, etc.) appear with system counts; systems listed under each phase with color-coded [Phase] tags"
    why_human: "Visual rendering, color correctness, and layout cannot be verified programmatically"
  - test: "Select a system entity in the tree"
    expected: "Inspector shows Phase (color-coded), Status (Enabled/Disabled), Matched entities count, Timing, Component Access list, and approximate Matched Entities list"
    why_human: "Inspector content rendering and layout need visual confirmation"
  - test: "In system inspector, switch focus to right panel, scroll to a matched entity, press Enter"
    expected: "Cursor jumps to that entity in the Entities section (left panel), Entities section auto-expands if collapsed"
    why_human: "Cross-navigation behavior requires interactive testing"
  - test: "Select a phase sub-header (e.g. OnUpdate)"
    expected: "Inspector shows Pipeline Execution Order with vertical flow, box-drawing connectors, per-phase timing, and selected phase highlighted"
    why_human: "Pipeline visualization uses Unicode box-drawing chars and colors that need visual verification"
---

# Phase 04: Systems and Pipeline Verification Report

**Phase Goal:** Users can see all registered systems grouped by execution phase with enabled/disabled status, with pipeline visualization and system detail inspector
**Verified:** 2026-02-06T22:00:00Z
**Status:** passed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Systems section in CELS-C tree displays all registered systems from /query endpoint | VERIFIED | classify_node() in tab_ecs.c:91-119 classifies flecs.system.System entities as ENTITY_CLASS_SYSTEM; tree_view_rebuild_visible() includes Systems section; /query polled in main.c:100-115 |
| 2 | Systems are grouped by Flecs execution phase (OnLoad, OnUpdate, OnStore, etc.) | VERIFIED | enrich_systems_with_pipeline() in tab_ecs.c:174-248 builds sorted phase list; tree_view.c:200-258 groups ENTITY_CLASS_SYSTEM under phase sub-headers; PHASE_ORDER canonical array used for sorting |
| 3 | Each system shows name, color-coded phase tag, enabled/disabled status, and match count | VERIFIED | tree_view.c:518-539 renders [Phase] in phase color via phase_color_pair(); match count as (N) on line 534-537; disabled rows wrapped in A_DIM on lines 461-462, 583 |
| 4 | Pipeline visualization shows phase execution ordering with timing data | VERIFIED | draw_pipeline_viz() in tab_ecs.c:722-815 renders vertical flow with PIPE_VERT/PIPE_ARROW box-drawing, color-coded phases, system counts, per-phase timing aggregation, and total summary |
| 5 | System detail inspector shows metadata and approximate matched entity list | VERIFIED | draw_system_detail() in tab_ecs.c:445-648 renders phase, status, match count, timing, tables, full path, component access, and scrollable matched entities labeled "(approx)"; build_system_matches() on 383-441 uses component overlap |
| 6 | Cross-navigation from matched entities to Entities section | VERIFIED | cross_navigate_to_entity() in tab_ecs.c:666-718 uncollapses Entities/Compositions sections, moves cursor, updates selected_entity_path, switches focus; wired in input handler at tab_ecs.c:1363-1383; footer "Entity not found" on failure |

**Score:** 6/6 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `tools/cels-debug/src/data_model.h` | system_info_t, system_registry_t structs, entity_node_t.system_match_count and .disabled | VERIFIED | Lines 96-111: system_info_t with name/full_path/phase/disabled/match/timing. Lines 57-58: system_match_count and disabled on entity_node_t. 134 lines total. |
| `tools/cels-debug/src/data_model.c` | system_registry lifecycle | VERIFIED | Lines 112-125: system_registry_create() and system_registry_free() with proper cleanup of name, full_path, phase strings. 126 lines total. |
| `tools/cels-debug/src/json_parser.h` | json_parse_pipeline_stats declaration | VERIFIED | Lines 33-38: declaration with full doc comment. 41 lines total. |
| `tools/cels-debug/src/json_parser.c` | Pipeline stats parser | VERIFIED | Lines 342-439: extract_pipeline_gauge() helper + json_parse_pipeline_stats() with two-pass parse (count systems, then extract fields), sync point filtering, leaf name extraction, gauge metrics. 505 lines total. |
| `tools/cels-debug/src/tui.h` | CP_PHASE_* defines, system_registry_t* in app_state_t | VERIFIED | Lines 28-38: CP_PHASE_ONLOAD(16) through CP_SYSTEM_DISABLED(26). Line 48: system_registry_t* in app_state_t. 67 lines total. |
| `tools/cels-debug/src/tui.c` | Phase color pair init_pair calls | VERIFIED | Lines 102-113: init_pair for all 11 phase/system color pairs. 213 lines total. |
| `tools/cels-debug/src/tree_view.h` | display_row_t.phase_group, phase arrays in tree_view_t, tree_view_set_phases() | VERIFIED | Line 14: phase_group field. Lines 35-38: phase_names, phase_system_counts, phase_collapsed, phase_count. Lines 64-65: tree_view_set_phases() declaration. 71 lines total. |
| `tools/cels-debug/src/tree_view.c` | Phase sub-header rendering, phase grouping, phase_color_pair() | VERIFIED | Lines 105-152: tree_view_set_phases() with strdup ownership and collapse preservation. Lines 200-258: phase-grouped rebuild. Lines 291-315: toggle_expand handles phase sub-headers. Lines 363-376: phase_color_pair(). Lines 379-402: draw_phase_subheader(). Lines 518-539: system row rendering. 586 lines total. |
| `tools/cels-debug/src/tabs/tab_ecs.c` | System enrichment, inspectors, cross-navigation | VERIFIED | 1454 lines. enrich_systems_with_pipeline (174-248), find_system_info (363-373), build_system_matches (383-441), draw_system_detail (445-648), cross_navigate_to_entity (666-718), draw_pipeline_viz (722-815), draw_systems_summary (819-913), input handling (1340-1383). |
| `tools/cels-debug/src/tab_system.c` | ECS tab includes ENDPOINT_STATS_PIPELINE | VERIFIED | Line 11: `ENDPOINT_QUERY | ENDPOINT_ENTITY | ENDPOINT_COMPONENTS | ENDPOINT_STATS_PIPELINE`. 70 lines total. |
| `tools/cels-debug/src/main.c` | Pipeline stats polling + cleanup | VERIFIED | Lines 161-174: pipeline stats polling block. Line 193: system_registry_free in cleanup. 201 lines total. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| main.c | json_parse_pipeline_stats | polling loop calls parser | WIRED | Line 167: `json_parse_pipeline_stats(presp.body.data, presp.body.size)` called after HTTP 200 check |
| tab_system.c | ENDPOINT_STATS_PIPELINE | ECS tab endpoint mask | WIRED | Line 11: ECS tab def includes `ENDPOINT_STATS_PIPELINE` in bitmask |
| main.c | system_registry_free | cleanup on exit | WIRED | Line 193: `system_registry_free(app_state.system_registry)` in shutdown section |
| tab_ecs.c | enrich_systems_with_pipeline | called before tree rebuild | WIRED | Line 975: called after classify + annotate, before rebuild_visible |
| tab_ecs.c | tree_view_set_phases | enrichment calls set_phases | WIRED | Line 231: `tree_view_set_phases(tree, found_phases, found_counts, found_count)` inside enrich function |
| tab_ecs.c | cross_navigate_to_entity | Enter on matched entity | WIRED | Lines 1363-1383: builds matches, gets target from cursor, calls cross_navigate_to_entity |
| tab_ecs.c | system_registry_t | enrichment reads pipeline stats | WIRED | Lines 239-246: iterates reg->systems matching by name, sets system_match_count and disabled |
| tab_ecs.c | draw_pipeline_viz | phase sub-header inspector | WIRED | Line 1016: called when cur_row is system header with phase_group >= 0 |
| tab_ecs.c | draw_systems_summary | section header inspector | WIRED | Line 1019: called when cur_row is system section header (phase_group == -1) |
| tab_ecs.c | draw_system_detail | system entity inspector | WIRED | Line 1023: called when sel->entity_class == ENTITY_CLASS_SYSTEM |

### Requirements Coverage

| Requirement | Status | Blocking Issue |
|-------------|--------|----------------|
| F4: System list | SATISFIED | All systems displayed, grouped by phase, with metadata |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| None found | - | - | - | - |

No TODO, FIXME, placeholder, or stub patterns were found in any Phase 04 modified files. All implementations are substantive with real logic.

### Human Verification Required

### 1. Visual System Tree Rendering
**Test:** Run cels-debug with a CELS app, navigate to ECS tab, expand Systems section
**Expected:** Phase sub-headers (OnUpdate, OnStore, etc.) appear with system counts; systems listed under each phase with color-coded [Phase] tags; disabled systems appear dimmed
**Why human:** Visual rendering, color correctness, and ncurses layout cannot be verified programmatically

### 2. System Detail Inspector
**Test:** Select a system entity in the tree
**Expected:** Inspector shows Phase (color-coded), Status (Enabled/Disabled), Matched entities count, Timing, Component Access list, and scrollable approximate Matched Entities list labeled "(approx)"
**Why human:** Inspector content rendering and layout need visual confirmation

### 3. Cross-Navigation
**Test:** In system inspector, switch focus to right panel, scroll to a matched entity, press Enter
**Expected:** Cursor jumps to that entity in the Entities section (left panel), Entities section auto-expands if collapsed
**Why human:** Cross-navigation behavior requires interactive testing

### 4. Pipeline Visualization
**Test:** Select a phase sub-header (e.g. OnUpdate)
**Expected:** Inspector shows Pipeline Execution Order with vertical flow, box-drawing connectors, per-phase timing, and selected phase highlighted in reverse video
**Why human:** Pipeline visualization uses Unicode box-drawing chars and colors that need visual verification

### Gaps Summary

No gaps found. All 6 observable truths are verified at all three levels (existence, substantive, wired). The data pipeline flows from HTTP polling (main.c) through JSON parsing (json_parser.c) to data model storage (data_model.h/c), enrichment (tab_ecs.c enrich_systems_with_pipeline), tree view grouping (tree_view.c phase sub-headers), and rendering (tree_view.c system rows + tab_ecs.c inspector branches). Cross-navigation is fully wired from input handling through to cursor repositioning and section uncollapsing. The build compiles cleanly with zero errors.

---
*Verified: 2026-02-06T22:00:00Z*
*Verifier: Claude (gsd-verifier)*
