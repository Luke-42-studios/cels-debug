# Phase 04: Systems and Pipeline - Research

**Researched:** 2026-02-06
**Domain:** Flecs REST API for system data, pipeline statistics, ncurses tree rendering with phase grouping, cross-navigation
**Confidence:** HIGH

## Summary

Phase 04 populates the Systems section of the CELS-C tree in the ECS tab with live system data from the Flecs REST API. Systems must be grouped by execution phase (OnLoad, OnUpdate, etc.), with each system showing name, phase tag, enabled/disabled status, and entity match count. The inspector panel must show either pipeline visualization (when a phase header is selected), summary stats (when the "Systems" CELS-C header is selected), or full system metadata (when a system is selected). Cross-navigation from matched entities in the system inspector to the Entities section completes the feature.

The critical finding from research is that the required data comes from TWO separate Flecs REST API sources that must be combined: (1) the existing `/query` endpoint already fetches system entities with their tags (which encode the phase via `flecs.pipeline.*` tags and system status via `flecs.system.System` tag), and (2) the `/stats/pipeline` endpoint provides per-system timing, match counts, and disabled status with the JSON structure `{"name":"...", "disabled":true/false, "matched_table_count":{...}, "matched_entity_count":{...}}`. The existing entity classification code in `tab_ecs.c` already identifies systems and extracts their pipeline phase from tags -- this is the foundation to build on.

**Primary recommendation:** Add a new `system_registry_t` data model that stores parsed system data (name, phase, enabled, match count, timing) from `/stats/pipeline`, poll it alongside existing endpoints, and enrich the Systems section of the CELS-C tree with this data. The tree_view module needs sub-header rows for phase groups within the Systems section. The inspector rendering needs three new branch paths (system detail, pipeline overview, systems summary).

## Standard Stack

This phase uses no new libraries or dependencies. All work is within the existing codebase.

### Core (Existing, No Changes Needed)
| Module | File | Purpose | Why No Change |
|--------|------|---------|---------------|
| http_client | http_client.h/c | HTTP GET with 200ms timeout | Already handles all endpoints |
| yyjson | (FetchContent) | JSON parsing | Already used by json_parser |
| ncurses | (system) | TUI rendering | Already used by all tabs |
| scroll | scroll.h/c | Virtual scrolling | Already used by tree and inspector |
| split_panel | split_panel.h/c | Left/right panel layout | Already used by ECS tab |
| json_render | json_render.h/c | JSON value rendering in inspector | Reused for system metadata display |

### Core (Existing, Modifications Needed)
| Module | File | Change Required | Reason |
|--------|------|-----------------|--------|
| data_model | data_model.h/c | Add `system_info_t` and `system_registry_t` structs | New data model for parsed pipeline stats |
| json_parser | json_parser.h/c | Add `json_parse_pipeline_stats()` function | Parse `/stats/pipeline` response |
| tab_ecs | tabs/tab_ecs.c | Add system detail inspector, pipeline viz, cross-nav | Three new inspector branches |
| tree_view | tree_view.h/c | Add phase sub-header rows within Systems section | Phase grouping display |
| main.c | main.c | Add `/stats/pipeline` polling when ENDPOINT_STATS_PIPELINE needed | New endpoint polling |
| tab_system.c | tab_system.c | Update ECS tab endpoints to include ENDPOINT_STATS_PIPELINE | ECS tab now needs pipeline stats |
| tui.h | tui.h | Add `system_registry_t*` to app_state_t, add phase color pairs | New data + new colors |

### No New Dependencies
This phase requires zero new libraries. The existing stack (libcurl + yyjson + ncurses) handles everything.

## Architecture Patterns

### Data Flow for System Information

Two REST endpoints provide complementary data:

```
/query (existing)                    /stats/pipeline (new)
  |                                    |
  v                                    v
entity_list_t                        system_registry_t
  - entity_node_t[]                    - system_info_t[]
  - has tags: flecs.system.System      - name (dot-separated path)
  - has tags: flecs.pipeline.OnUpdate  - disabled: bool
  - classified as ENTITY_CLASS_SYSTEM  - matched_entity_count (gauge)
  - class_detail: "OnUpdate"           - matched_table_count (gauge)
                                       - time_spent (gauge)
  |                                    |
  +-------- MERGE IN tab_ecs.c --------+
  |
  v
enriched tree_view display
  - system name + [OnUpdate] tag + (42 entities) + timing
  - disabled systems dimmed
```

### Pattern 1: Phase Sub-Headers in Tree View

The Systems section in the CELS-C tree currently shows systems as flat root-level entities. Phase 04 adds a grouping layer: within the Systems section, systems are organized under phase sub-headers (OnUpdate, OnValidate, etc.).

**Approach:** Extend `display_row_t` to support a third row type: phase sub-headers within a section. These behave like section headers (collapsible, navigable) but are nested one level deeper within the Systems section.

```
v Systems (12) ─────────────────────
  v OnLoad (2)
      InputSystem [OnLoad] (15)
      ResourceLoader [OnLoad] (3)
  v OnUpdate (6)
      MovementSystem [OnUpdate] (42)
      CollisionSystem [OnUpdate] (28)
      ...
  v OnStore (4)
      RenderSystem [OnStore] (120)
      ...
```

**Implementation options:**

Option A (recommended): Add a `phase_group_idx` field to `display_row_t` and a flag distinguishing section headers from phase sub-headers. The tree_view_rebuild_visible function groups ENTITY_CLASS_SYSTEM nodes by their class_detail (which already contains the phase name) and inserts sub-header rows. Phase sub-headers use the same collapse mechanic as section headers.

Option B: Implement phase grouping purely in the entity tree (add synthetic parent nodes for each phase). Rejected: This conflicts with the entity_list ownership model -- nodes are owned by entity_list_t and must not contain synthetic entries.

### Pattern 2: Inspector Context Branches

The inspector already branches on entity class (component type vs entity detail). Phase 04 adds three more contexts:

```
Inspector renders based on selection:
  1. System entity selected     -> Full system metadata + matched entity list
  2. Phase sub-header selected  -> Pipeline visualization (vertical flow)
  3. "Systems" header selected  -> Summary stats
  4. Non-system entity selected -> Existing entity detail (unchanged)
  5. Component type selected    -> Existing component browser (unchanged)
```

### Pattern 3: Cross-Navigation

When viewing a system's matched entities in the inspector (right panel), pressing Enter on an entity should navigate to that entity in the Entities section of the CELS-C tree.

**Implementation:** Set the tree cursor to the target entity's row index, uncollapse the Entities section if needed, update `selected_entity_path`, and shift focus back to the left panel. This requires searching `tree_view.rows[]` for the entity by ID/path and adjusting scroll position.

### Recommended Project Structure Changes

```
src/
  data_model.h/c    -- ADD system_info_t, system_registry_t
  json_parser.h/c   -- ADD json_parse_pipeline_stats()
  tree_view.h/c     -- ADD phase sub-header support
  tui.h             -- ADD system_registry_t* to app_state, phase colors
  main.c            -- ADD /stats/pipeline polling
  tab_system.c      -- UPDATE ECS tab endpoint mask
  tabs/
    tab_ecs.c       -- ADD system inspector, pipeline viz, cross-nav
```

### Anti-Patterns to Avoid
- **Separate Systems tab:** The CONTEXT.md establishes that systems live in the CELS-C tree within the ECS tab. Do NOT create a separate tab.
- **Fetching system data per-system:** Do NOT make individual `/entity/<path>` calls for each system. The `/stats/pipeline` endpoint returns all system data in one call.
- **Caching stale stats:** Pipeline stats should be refreshed each poll cycle. Do NOT keep old stats across polls.
- **Modifying entity_list_t for phase grouping:** Do NOT inject synthetic phase-group entities into the entity list. Phase grouping is a display concern, handled in tree_view.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Phase execution order | Custom ordering logic | Static const array of canonical phase names | Flecs phases have a fixed order; just hardcode it |
| System timing extraction | Custom gauge parser | Extract last element from 60-point circular buffer (existing pattern in `extract_latest_gauge`) | World stats already does this pattern |
| Entity match count formatting | Custom number formatting | Simple `snprintf` with `%d` | Match counts are integers, no special formatting needed |
| Pipeline flow diagram | Complex drawing library | Manual ncurses box-drawing characters (existing pattern in tree_view.c) | Vertical flow is just a few UTF-8 line chars |
| Focus management | Custom focus tracking | Existing `split_panel_handle_focus` | Already handles left/right panel focus |

## Common Pitfalls

### Pitfall 1: /stats/pipeline Response Format
**What goes wrong:** The `/stats/pipeline` response is NOT a simple array of systems. It contains systems grouped by sync points, with sync point stats interleaved between system groups.
**Why it happens:** Flecs pipelines have merge/sync points between groups of systems. The JSON serialization reflects this structure.
**How to avoid:** Parse the response as alternating system-entries and sync-point-entries. System entries have a `"name"` field; sync points have `"system_count"` and `"multi_threaded"` fields. Filter for system entries only.
**Warning signs:** Parser crashes or returns wrong data on sync point JSON objects.

### Pitfall 2: System Name Path Format
**What goes wrong:** System names from `/stats/pipeline` use dot-separated paths (e.g., `"MyApp.Systems.Movement"`), while entity paths in the entity tree use slash-separated paths.
**Why it happens:** Flecs internally uses dot-separated paths. The existing codebase converts dots to slashes for REST API URLs.
**How to avoid:** When matching system names from pipeline stats to entities in the tree, either normalize both to the same format or do a leaf-name match (extract the last segment after the final dot/slash).
**Warning signs:** Systems in the tree don't get enriched with timing/match data despite both data sources being present.

### Pitfall 3: Phase Name Extraction
**What goes wrong:** Assuming phase names are always from the builtin set. CELS defines custom phases like `OnRender` (which maps to `OnStore` internally) and `PostFrame`.
**Why it happens:** CELS wraps Flecs phases with its own enum and may define custom phases via `CEL_DefinePhase`.
**How to avoid:** The phase name comes from the `flecs.pipeline.*` tag on the entity. Use whatever phase name the tag provides. The canonical ordering only applies to known builtin phases; unknown phases should be placed at the end.
**Warning signs:** Custom CELS phases don't appear, or appear in wrong order.

### Pitfall 4: Empty Phase Groups
**What goes wrong:** Showing phase group headers for phases that have no systems.
**Why it happens:** Using the full list of builtin phases instead of filtering to only phases with registered systems.
**How to avoid:** CONTEXT.md explicitly says "Empty phase groups are hidden -- only show phases that have registered systems." Collect the set of phases that actually have systems, then only create sub-headers for those.
**Warning signs:** Lots of empty expandable headers in the Systems section.

### Pitfall 5: Gauge Data Extraction
**What goes wrong:** Reading the wrong value from the 60-element circular buffer in stats responses.
**Why it happens:** Flecs stats use circular buffers with 60 data points. The latest value is at the end of the array, but the `t` field indicates the current write position.
**How to avoid:** Use the same pattern as `extract_latest_gauge` in `json_parser.c`: read the last element of the `"avg"` array. The `/stats/pipeline` system stats use the `ECS_GAUGE_APPEND` macro which writes `"avg"`, `"min"`, and `"max"` arrays.
**Warning signs:** Timing data shows 0 or stale values.

### Pitfall 6: Cross-Navigation Stale References
**What goes wrong:** Cross-navigating to an entity that no longer exists in the entity list (it was removed between polls).
**Why it happens:** Entity data and system data are polled independently and may be slightly out of sync.
**How to avoid:** After navigating, verify the target entity exists in the current entity_list. If not found, show a footer message ("Entity not found") and don't change cursor position.
**Warning signs:** Cursor jumps to wrong entity or crashes on NULL dereference.

## Code Examples

### Example 1: system_info_t Data Model

```c
// In data_model.h

// Single system info (from /stats/pipeline response)
typedef struct system_info {
    char *name;              // leaf name (e.g., "MovementSystem")
    char *full_path;         // dot-separated path (e.g., "MyApp.Systems.Movement")
    char *phase;             // phase name (e.g., "OnUpdate") -- from entity tag
    bool disabled;           // from pipeline stats
    int matched_entity_count; // latest gauge value
    int matched_table_count;  // latest gauge value
    double time_spent_ms;    // latest gauge value, converted to ms
} system_info_t;

// All systems from one /stats/pipeline poll
typedef struct system_registry {
    system_info_t *systems;
    int count;

    // Phase ordering for display
    char **phase_names;      // unique phase names in execution order
    int phase_count;
} system_registry_t;
```

### Example 2: Pipeline Stats JSON Structure

The `/stats/pipeline` response has this structure (confirmed from Flecs REST source code):

```json
[
  {"name":"MyApp.Systems.InputHandler", "disabled":false,
   "matched_table_count":{"avg":[...60 floats...]},
   "matched_entity_count":{"avg":[...60 floats...]},
   "time_spent":{"avg":[...60 floats...]}},
  {"name":"MyApp.Systems.Movement", "disabled":false, ...},
  {"system_count":2, "multi_threaded":false, "immediate":false,
   "time_spent":{"avg":[...]}, "commands_enqueued":{"avg":[...]}},
  {"name":"MyApp.Systems.Render", "disabled":false, ...},
  {"system_count":1, "multi_threaded":false, ...}
]
```

Key observations:
- Top-level is a JSON array
- System entries have `"name"` field (dot-separated path)
- Sync point entries have `"system_count"` field (no `"name"`)
- Gauge values are 60-element circular buffers under `"avg"`
- `"disabled"` is a boolean
- Task systems (no query) omit `matched_table_count` and `matched_entity_count`

### Example 3: Parsing Pipeline Stats

```c
// In json_parser.c
// Pattern: same as extract_latest_gauge for world stats

static double extract_pipeline_gauge(yyjson_val *obj, const char *field) {
    yyjson_val *metric = yyjson_obj_get(obj, field);
    if (!metric) return 0.0;
    yyjson_val *avg = yyjson_obj_get(metric, "avg");
    if (!avg || !yyjson_is_arr(avg)) return 0.0;
    size_t count = yyjson_arr_size(avg);
    if (count == 0) return 0.0;
    yyjson_val *last = yyjson_arr_get(avg, count - 1);
    return (last && yyjson_is_num(last)) ? yyjson_get_num(last) : 0.0;
}

system_registry_t *json_parse_pipeline_stats(const char *json, size_t len) {
    // Parse JSON array
    // For each element:
    //   if has "name" field -> system entry, extract fields
    //   if has "system_count" field -> sync point, skip
    // Collect unique phase names in order they appear
    // (Phase comes from entity tags, not from this endpoint --
    //  must be merged later with entity_list classification)
}
```

### Example 4: Phase Sub-Header in Tree View

```c
// Extended display_row_t
typedef struct display_row {
    entity_node_t *node;    // Non-NULL = entity row, NULL = header
    int section_idx;        // Which CELS-C section
    int phase_group;        // -1 = section header, >=0 = phase sub-header index
} display_row_t;
```

### Example 5: Pipeline Visualization (Inspector Right Panel)

```
Pipeline Execution Order
========================

  OnLoad         2 systems   0.1ms
     |
     v
  OnUpdate       6 systems   2.1ms    <-- highlighted (selected)
     |
     v
  OnValidate     1 system    0.3ms
     |
     v
  OnStore        4 systems   1.2ms

  Total: 13 systems, 3.7ms/frame
```

Using box-drawing characters:
```c
#define PIPE_VERT  "\xe2\x94\x82"   // U+2502 vertical line
#define PIPE_ARROW "\xe2\x86\x93"   // U+2193 downward arrow
```

### Example 6: Phase Color Pair Assignments

```c
// In tui.h - new color pairs for phase tags
#define CP_PHASE_ONLOAD      16
#define CP_PHASE_POSTLOAD    17
#define CP_PHASE_PREUPDATE   18
#define CP_PHASE_ONUPDATE    19
#define CP_PHASE_ONVALIDATE  20
#define CP_PHASE_POSTUPDATE  21
#define CP_PHASE_PRESTORE    22
#define CP_PHASE_ONSTORE     23
#define CP_PHASE_POSTFRAME   24
#define CP_PHASE_CUSTOM      25  // fallback for custom phases
#define CP_SYSTEM_DISABLED   26  // dimmed text for disabled systems

// In tui.c - init_pair calls
init_pair(CP_PHASE_ONLOAD,     COLOR_BLUE,    -1);
init_pair(CP_PHASE_POSTLOAD,   COLOR_BLUE,    -1);
init_pair(CP_PHASE_PREUPDATE,  COLOR_CYAN,    -1);
init_pair(CP_PHASE_ONUPDATE,   COLOR_GREEN,   -1);
init_pair(CP_PHASE_ONVALIDATE, COLOR_YELLOW,  -1);
init_pair(CP_PHASE_POSTUPDATE, COLOR_YELLOW,  -1);
init_pair(CP_PHASE_PRESTORE,   COLOR_MAGENTA, -1);
init_pair(CP_PHASE_ONSTORE,    COLOR_RED,     -1);
init_pair(CP_PHASE_POSTFRAME,  COLOR_WHITE,   -1);
init_pair(CP_PHASE_CUSTOM,     COLOR_WHITE,   -1);
init_pair(CP_SYSTEM_DISABLED,  COLOR_WHITE,   -1);  // used with A_DIM
```

### Example 7: Cross-Navigation Implementation

```c
// In tab_ecs.c - navigate from system inspector to entity in Entities section
static bool cross_navigate_to_entity(ecs_state_t *es, app_state_t *state,
                                      const char *entity_path) {
    // 1. Ensure Entities section is expanded
    es->tree.section_collapsed[ENTITY_CLASS_ENTITY] = false;

    // 2. Rebuild visible rows to include Entities section items
    tree_view_rebuild_visible(&es->tree, state->entity_list);

    // 3. Find the target entity in the display list
    for (int i = 0; i < es->tree.row_count; i++) {
        entity_node_t *node = es->tree.rows[i].node;
        if (node && node->full_path &&
            strcmp(node->full_path, entity_path) == 0) {
            es->tree.scroll.cursor = i;
            scroll_ensure_visible(&es->tree.scroll);
            // 4. Update selected path for detail polling
            free(state->selected_entity_path);
            state->selected_entity_path = strdup(entity_path);
            // 5. Switch focus to left panel
            es->panel.focus = 0;
            return true;
        }
    }
    // Entity not found in current list
    free(state->footer_message);
    state->footer_message = strdup("Entity not found");
    state->footer_message_expire = /* now + 3000ms */;
    return false;
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Separate Systems tab (Phase 02 design) | Systems section inside ECS tab | Phase 03.1 redesign | Systems are part of unified CELS-C tree, not a standalone tab |
| Flat system list | Phase-grouped system tree | Phase 04 (this phase) | Users see systems organized by execution phase |
| No system data beyond name | Full metadata from /stats/pipeline | Phase 04 (this phase) | Timing, match counts, disabled status now visible |

## Flecs REST API Reference

### Endpoints Used

| Endpoint | Purpose | Already Implemented | Phase 04 Change |
|----------|---------|--------------------|----|
| `/stats/world` | World overview stats | Yes (json_parse_world_stats) | None |
| `/query?expr=...` | Entity list with tags | Yes (json_parse_entity_list) | None -- systems already classified by tags |
| `/entity/<path>` | Entity detail | Yes (json_parse_entity_detail) | None |
| `/components` | Component registry | Yes (json_parse_component_registry) | None |
| `/stats/pipeline` | Pipeline + system stats | **NEW** | Add parser + polling |

### /stats/pipeline Response Details (HIGH confidence)

Source: Flecs REST source code analysis (`src/addons/rest.c`, lines 898-1048)

The response is a JSON array alternating between system entries and sync point entries:

**System entry fields:**
- `"name"` (string): Dot-separated entity path (e.g., `"flecs.pipeline.OnUpdate"`)
- `"disabled"` (bool): Whether system has EcsDisabled tag
- `"matched_table_count"` (gauge): 60-element circular buffer with `"avg"`, `"min"`, `"max"` arrays (omitted for task systems)
- `"matched_entity_count"` (gauge): Same structure (omitted for task systems)
- `"time_spent"` (gauge): System execution time

**Sync point entry fields:**
- `"system_count"` (int): Number of systems in the preceding group
- `"multi_threaded"` (bool)
- `"immediate"` (bool)
- `"time_spent"` (gauge): Sync point overhead
- `"commands_enqueued"` (gauge)

**Differentiator:** System entries have `"name"`, sync points have `"system_count"`. Use presence of `"name"` to distinguish.

### Flecs Builtin Pipeline Phase Order (HIGH confidence)

Source: Flecs Systems documentation + CELS source code (`include/cels/cels.h`)

Canonical execution order:
1. `OnStart` (special: only runs on first `ecs_progress()`)
2. `OnLoad`
3. `PostLoad`
4. `PreUpdate`
5. `OnUpdate`
6. `OnValidate`
7. `PostUpdate`
8. `PreStore`
9. `OnStore`
10. `PostFrame`

Note: In REST API responses, phase tags appear as `flecs.pipeline.OnLoad`, `flecs.pipeline.OnUpdate`, etc. The `extract_pipeline_phase` function in `tab_ecs.c` already strips the `flecs.pipeline.` prefix.

CELS also defines custom phases: `OnRender` (maps to `OnStore` internally), `PreFrame`. Custom phases defined via `CEL_DefinePhase` may appear with arbitrary names.

### System Entity Structure (HIGH confidence)

A system entity in Flecs has:
- Tag: `flecs.system.System` (identifies it as a system)
- Tag: `flecs.pipeline.OnUpdate` (or other phase -- identifies its execution phase)
- Relationship: `(DependsOn, flecs.pipeline.OnUpdate)` (phase ordering)
- Optional tag: `EcsDisabled` / `Disabled` (when disabled)

The existing `classify_node()` function in `tab_ecs.c` already checks for `flecs.system.System` and extracts the pipeline phase from `flecs.pipeline.*` tags. This classification continues to work -- Phase 04 enriches these nodes with data from `/stats/pipeline`.

## Open Questions

### 1. System Name Matching Strategy
**What we know:** `/stats/pipeline` provides dot-separated full paths (e.g., `"MyApp.Systems.Movement"`). The entity tree has slash-separated paths from `/query` (e.g., `"MyApp/Systems/Movement"`). Entity names (leaf only) are also available.
**What's unclear:** Whether leaf-name matching (just "Movement") is sufficient or if full-path matching is needed. In small CELS apps, leaf names are likely unique. In larger apps, there could be name collisions.
**Recommendation:** Match by leaf name first (fast, simple). If collisions are detected (multiple systems with same leaf name), fall back to full-path matching with dot-to-slash conversion. The existing `dots_to_slashes()` helper in json_parser.c can be reused.

### 2. Stats Polling Frequency
**What we know:** World stats and entity list are polled every 500ms (POLL_INTERVAL_MS). Pipeline stats are gauge data with 60-element circular buffers.
**What's unclear:** Whether 500ms is appropriate for pipeline stats. The data is historical (60 data points), so lower frequency might be fine.
**Recommendation:** Use the same 500ms interval. The HTTP request is small (localhost), and fresh timing data is more useful than stale data. This matches the existing polling pattern.

### 3. Phase Names from /stats/pipeline vs Entity Tags
**What we know:** System entities in the entity tree already have phase tags (from `/query`). The `/stats/pipeline` endpoint returns systems in pipeline execution order but does NOT include the phase name per-system -- it groups systems by sync points.
**What's unclear:** Whether sync point boundaries correspond exactly to phase boundaries.
**Recommendation:** Use the phase name from entity tags (already extracted by `extract_pipeline_phase`). Use `/stats/pipeline` for timing, match count, and disabled status only. Do NOT attempt to derive phase from sync point structure -- it's unreliable.

## Sources

### Primary (HIGH confidence)
- Flecs REST source code (`src/addons/rest.c`) -- endpoint routing, pipeline stats handler, system JSON serialization
- Flecs Stats header (`include/flecs/addons/stats.h`) -- `ecs_pipeline_stats_t`, `ecs_system_stats_t`, `ecs_sync_stats_t` structures
- Flecs Systems documentation (https://www.flecs.dev/flecs/md_docs_2Systems.html) -- phase ordering, DependsOn, system entity structure
- Flecs Remote API documentation (https://www.flecs.dev/flecs/md_docs_2FlecsRemoteApi.html) -- endpoint list, query parameters
- CELS source code (`include/cels/cels.h`, `src/cels.cpp`) -- CELS_Phase enum, phase-to-Flecs mapping
- cels-debug codebase -- all existing modules read in full

### Secondary (MEDIUM confidence)
- Flecs Quickstart (https://www.flecs.dev/flecs/md_docs_2Quickstart.html) -- phase ordering confirmation
- Flecs Pipeline API (https://www.flecs.dev/flecs/group__c__addons__pipeline.html) -- pipeline functions

### Tertiary (LOW confidence)
- None -- all critical claims verified against source code

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new dependencies, all existing patterns reused
- Architecture: HIGH -- direct code analysis of existing tree_view, tab_ecs, data_model modules
- Flecs API: HIGH -- verified against Flecs REST source code, not just documentation
- Pitfalls: HIGH -- identified from actual code analysis and API response structure
- Cross-navigation: MEDIUM -- implementation approach is clear but untested in this codebase

**Research date:** 2026-02-06
**Valid until:** 2026-03-06 (stable -- Flecs REST API changes infrequently)
