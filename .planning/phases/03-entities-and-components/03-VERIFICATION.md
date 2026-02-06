---
phase: 03-entities-and-components
verified: 2026-02-06T10:15:00Z
status: passed
score: 6/6 must-haves verified
human_verification:
  - test: "Launch cels-debug connected to a running CELS app, switch to Entities tab (key 2), verify entity tree populates and cursor selection previews components in real time"
    expected: "Left panel shows entity hierarchy with tree lines, right panel shows component key-value pairs for the cursor entity"
    why_human: "Requires live ECS application and visual confirmation of ncurses rendering"
  - test: "Navigate entity tree with j/k, press Enter on a parent node to collapse/expand, press f to toggle anonymous entities"
    expected: "Tree collapses/expands subtrees, anonymous entities appear/disappear, cursor stays on correct entity"
    why_human: "Interactive keyboard behavior and visual state transitions cannot be verified programmatically"
  - test: "Switch to Components tab (key 3), verify component types are listed alphabetically with entity counts, select a component and see matching entities in right panel"
    expected: "Left panel shows sorted component names with counts, right panel filters to entities having that component"
    why_human: "Requires live data and visual confirmation"
  - test: "With many entities (100+), verify scrolling is smooth and no freezing occurs on either tab"
    expected: "Virtual scrolling renders only visible rows, no perceptible lag"
    why_human: "Performance feel requires human judgment with live data"
---

# Phase 03: Entities and Components Verification Report

**Phase Goal:** Users can browse all entities, select one, and inspect its component names and values as key-value pairs
**Verified:** 2026-02-06T10:15:00Z
**Status:** PASSED
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Entities tab shows a scrollable list of entities fetched from /query endpoint | VERIFIED | `main.c:100-115` polls `/query` when `ENDPOINT_QUERY` bit set; `tab_entities.c:214-227` calls `tree_view_rebuild_visible()` + `tree_view_render()` with `entity_list` from `app_state`. `tab_system.c:12-14` wires Entities tab with `ENDPOINT_QUERY \| ENDPOINT_ENTITY`. |
| 2 | Cursor selection previews component data instantly; Enter toggles expand/collapse on tree nodes | VERIFIED | `tab_entities.c:417-460` handles j/k/arrows with `scroll_move()` + `sync_selected_path()` which updates `selected_entity_path` triggering detail fetch on next poll. Enter at line 450-454 calls `tree_view_toggle_expand()`. `tree_view.c:118-128` implements expand/collapse toggle with rebuild. |
| 3 | Selected entity displays all component names and their values as key-value pairs | VERIFIED | `tab_entities.c:245-401` renders right panel: iterates `entity_detail->components` via `yyjson_obj_foreach`, calls `json_render_component()` for each component. Also renders Tags and Pairs sections. `json_render.c:100-120` implements collapsible component header + value rendering. |
| 4 | Component values render correctly for nested objects, arrays, and null values | VERIFIED | `json_render.c:5-98` recursively handles all yyjson types: null (A_DIM, line 10-14), bool/int/real (CP_JSON_NUMBER, lines 17-36), string (CP_JSON_STRING, lines 38-45), objects (recursive at indent+2, lines 47-72), arrays (recursive with [idx]: prefix, lines 74-95). |
| 5 | Components tab lists all registered component types from the component registry | VERIFIED | `tab_components.c:85-148` renders sorted component list from `component_registry`. `main.c:147-159` polls `/components` when `ENDPOINT_COMPONENTS` bit set. `tab_system.c:15-17` wires Components tab with `ENDPOINT_COMPONENTS \| ENDPOINT_QUERY`. qsort at line 89 sorts alphabetically. |
| 6 | Entity list handles large counts without freezing (virtual scrolling data structures in place) | VERIFIED | `scroll.h` defines `scroll_state_t` with `total_items`, `visible_rows`, `cursor`, `scroll_offset`. `tree_view.c:141-251` renders only visible rows (`scroll_offset` to `scroll_offset + max_rows`). `tab_entities.c` uses `scroll_page()` for PageUp/PageDown. `tab_components.c` uses same pattern for both panels. |

**Score:** 6/6 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/data_model.h` | entity_node_t, entity_detail_t, component_info_t, entity_list_t, component_registry_t types | VERIFIED | 97 lines. All 5 types declared with correct fields. Includes yyjson.h for entity_detail_t doc ownership. |
| `src/data_model.c` | Allocation and free functions for all new data types | VERIFIED | 107 lines. create/free for entity_node_t, entity_list_t, entity_detail_t, component_registry_t. entity_node_add_child with capacity doubling. Proper yyjson_doc_free in entity_detail_free. |
| `src/json_parser.h` | json_parse_entity_list, json_parse_entity_detail, json_parse_component_registry declarations | VERIFIED | 33 lines. All 3 functions declared with proper documentation. |
| `src/json_parser.c` | Parse implementations for /query, /entity, /components responses | VERIFIED | 405 lines. json_parse_entity_list builds parent-child tree with dot-to-slash path conversion (lines 63-288). json_parse_entity_detail preserves yyjson_doc ownership (lines 292-340). json_parse_component_registry handles root-is-array format (lines 344-405). |
| `src/scroll.h` | scroll_state_t type, scroll_move, scroll_ensure_visible, scroll_reset | VERIFIED | 30 lines. Type + 6 function declarations. |
| `src/scroll.c` | Scroll state implementation with clamping | VERIFIED | 58 lines. All functions implemented with correct boundary clamping. Edge case handled: total_items <= 0 returns early. |
| `src/split_panel.h` | split_panel_t type, create/destroy/resize/draw_borders/refresh | VERIFIED | 40 lines. All 7 functions declared. |
| `src/split_panel.c` | Split panel window lifecycle and border rendering | VERIFIED | 85 lines. 40/60 split with newwin. A_BOLD/A_DIM borders with CP_PANEL_ACTIVE/INACTIVE. Focus switching with KEY_LEFT/KEY_RIGHT. Uses wnoutrefresh (not wrefresh). |
| `src/json_render.h` | json_render_value and json_render_component declarations | VERIFIED | 33 lines. Both functions declared with documentation. |
| `src/json_render.c` | Handles obj, arr, str, num, bool, null with color pairs and indentation | VERIFIED | 120 lines. Recursive renderer covering all yyjson types with correct color pairs. json_render_component adds collapsible header. |
| `src/tree_view.h` | tree_view_t type, rebuild_visible, toggle_expand, toggle_anonymous | VERIFIED | 46 lines. Type with prev_selected_id for cursor preservation. 7 function declarations. |
| `src/tree_view.c` | DFS flattening, anonymous filter, expand/collapse, tree line rendering | VERIFIED | 251 lines. DFS via dfs_collect(). Cursor preservation by entity ID. Unicode box drawing (TREE_VERT/BRANCH/LAST/HORIZ). Right-aligned component names. Expand/collapse indicator. |
| `src/tabs/tab_entities.h` | tab_entities_init/fini/draw/input declarations | VERIFIED | 11 lines. Standard vtable declarations. |
| `src/tabs/tab_entities.c` | Full Entities tab with split panel, tree, and inspector | VERIFIED | 502 lines. entities_state_t with split_panel, tree_view, inspector_scroll, comp_expanded. Full draw with left panel tree + right panel inspector. Full input with j/k/Enter/f/g/G/PgUp/PgDn. Auto-select first entity. Collapsible component groups. |
| `src/tabs/tab_components.h` | tab_components_init/fini/draw/input declarations | VERIFIED | 11 lines. Standard vtable declarations. |
| `src/tabs/tab_components.c` | Full Components tab with split panel and drill-down | VERIFIED | 344 lines. qsort alphabetical sort. Left panel renders component types with entity counts + size. Right panel filters entities by selected component via strcmp scan. Both panels scrollable. |
| `src/tab_system.c` | Entities and Components tabs wired into tab_defs | VERIFIED | Lines 11-17: tab_entities_init/fini/draw/input at index 1, tab_components_init/fini/draw/input at index 2. Correct endpoint bitmasks. |
| `src/tui.h` | app_state_t with entity_list, entity_detail, component_registry, color pairs 7-15 | VERIFIED | 52 lines. All Phase 03 fields in app_state_t. CP_TREE_LINE through CP_CURSOR defined. |
| `src/tui.c` | Color pair init, setlocale, footer message | VERIFIED | 198 lines. setlocale(LC_ALL,"") at line 61 before initscr. init_pair for pairs 7-15 at lines 92-100. Footer message rendering at lines 175-183. |
| `src/main.c` | Conditional polling for ENDPOINT_QUERY, ENDPOINT_ENTITY, ENDPOINT_COMPONENTS | VERIFIED | 184 lines. Three conditional poll blocks at lines 99-159. Proper cleanup at lines 173-179. 404 handling clears selected_entity_path. |
| `CMakeLists.txt` | All new source files in add_executable | VERIFIED | Lines 32-39: scroll.c, split_panel.c, json_render.c, tree_view.c, tab_entities.c, tab_components.c all present. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `main.c` | `json_parse_entity_list` | Conditional poll when ENDPOINT_QUERY bit set | WIRED | Line 100: `if ((needed & ENDPOINT_QUERY) ...)`, line 108: `json_parse_entity_list(...)`, line 111: stored in `app_state.entity_list` |
| `main.c` | `json_parse_entity_detail` | Conditional poll when ENDPOINT_ENTITY bit set + entity selected | WIRED | Line 118: `if ((needed & ENDPOINT_ENTITY) && app_state.selected_entity_path ...)`, line 127: `json_parse_entity_detail(...)`, line 130: stored in `app_state.entity_detail` |
| `main.c` | `json_parse_component_registry` | Conditional poll when ENDPOINT_COMPONENTS bit set | WIRED | Line 147: `if ((needed & ENDPOINT_COMPONENTS) ...)`, line 152: `json_parse_component_registry(...)`, line 155: stored in `app_state.component_registry` |
| `tab_entities.c` | `app_state.entity_list` | tree_view_rebuild_visible called with entity_list | WIRED | Line 215: `tree_view_rebuild_visible(&es->tree, state->entity_list)` |
| `tab_entities.c` | `app_state.entity_detail` | json_render_component renders entity_detail->components | WIRED | Line 247: `strcmp(state->entity_detail->path, sel->full_path)`, line 274+287: `json_render_component(...)` with component values from detail |
| `tab_entities.c` | `app_state.selected_entity_path` | Sets path when cursor moves | WIRED | `sync_selected_path()` at line 147-158: `state->selected_entity_path = sel ? strdup(sel->full_path) : NULL` |
| `tab_system.c` | `tab_entities_init` | tab_defs[1] vtable entry | WIRED | Line 13: `tab_entities_init, tab_entities_fini, tab_entities_draw, tab_entities_input` |
| `tab_system.c` | `tab_components_init` | tab_defs[2] vtable entry | WIRED | Line 16: `tab_components_init, tab_components_fini, tab_components_draw, tab_components_input` |
| `tab_components.c` | `app_state.component_registry` | Reads component list for left panel | WIRED | Line 85: `if (state->component_registry && state->component_registry->count > 0)`, lines 88-135 render from registry |
| `tab_components.c` | `app_state.entity_list` | Filters entity_list for entities having selected component | WIRED | Lines 159-176: iterates `elist->nodes[i]->component_names[c]` comparing with `sel_name` via strcmp |
| `tree_view.h` | `scroll.h` | tree_view_t embeds scroll_state_t | WIRED | tree_view.h line 18: `scroll_state_t scroll;` field in tree_view_t |
| `split_panel.c` | ncurses newwin/delwin | Window lifecycle for left/right panels | WIRED | Lines 9-10: `newwin(...)`, lines 18-26: `delwin(...)` |

### Requirements Coverage

| Requirement | Status | Blocking Issue |
|-------------|--------|----------------|
| F3: Entity list with component inspection | SATISFIED | All supporting truths (1-4, 6) verified |
| F6: Keyboard-driven navigation (Phase 03 portion) | SATISFIED | j/k, arrows, Enter, f, g/G, PgUp/PgDn all implemented in both tabs |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `tab_system.c` | 7 | Comment says "Overview is real, rest are placeholder" (stale comment) | Info | No impact -- Entities and Components tabs are wired with real implementations at indices 1 and 2. Only Systems/State/Performance remain placeholder (Phase 04-05). |
| `tab_entities.c` | 286 | `(void)skip;` -- computed value unused | Info | Simple approach taken for scroll: renders full component rather than partial row skip. Functional, minor optimization opportunity. |

### Human Verification Required

### 1. Live Entity Browsing

**Test:** Launch cels-debug connected to a running CELS app, switch to Entities tab (key 2), verify entity tree populates with real data.
**Expected:** Left panel shows entity hierarchy with Unicode tree lines, expand/collapse indicators, entity names, and right-aligned component name hints. Right panel shows component key-value pairs for the cursor entity.
**Why human:** Requires a running ECS application and visual confirmation of ncurses rendering quality.

### 2. Interactive Tree Navigation

**Test:** Navigate with j/k, press Enter on parent nodes, press f to toggle anonymous entities.
**Expected:** Tree collapses/expands subtrees smoothly, anonymous entities toggle on/off, cursor preserves position on the same entity after operations.
**Why human:** Interactive keyboard behavior and visual state transitions require real-time interaction.

### 3. Components Tab with Drill-Down

**Test:** Switch to Components tab (key 3), browse sorted component types, select one and verify matching entities appear in right panel.
**Expected:** Component types sorted alphabetically with entity counts and byte sizes. Right panel shows only entities that have the selected component.
**Why human:** Requires live data from the flecs REST API.

### 4. Scroll Performance

**Test:** With 100+ entities, rapidly scroll through entity tree and component inspector.
**Expected:** No visible lag, no freeze, smooth cursor movement.
**Why human:** Performance perception requires human judgment under real conditions.

### Gaps Summary

No gaps found. All 6 Phase 03 success criteria are met through verified artifacts and wiring:

1. **Data pipeline complete:** Three JSON parsers (entity list, entity detail, component registry) are implemented and integrated into the main loop's conditional polling system. Each endpoint is polled only when the active tab's bitmask requests it.

2. **UI utility modules functional:** scroll_state_t, split_panel_t, json_render, and tree_view are self-contained modules with real implementations (not stubs). Total: 514 lines across 8 source files.

3. **Entities tab fully wired:** 502-line implementation with split panel, interactive tree view (left), and component inspector (right). Handles expand/collapse, anonymous toggle, cursor-driven detail fetching, collapsible component groups with JSON rendering. Wired into tab_defs[1].

4. **Components tab fully wired:** 344-line implementation with split panel, sorted component list (left), and filtered entity drill-down (right). Wired into tab_defs[2] with correct endpoint bitmask.

5. **Build clean:** `cmake --build build` succeeds with zero warnings. All 14 source files (8 new + 6 modified) compile and link correctly.

---

_Verified: 2026-02-06T10:15:00Z_
_Verifier: Claude (gsd-verifier)_
