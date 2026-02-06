# Phase 03: Entities and Components - Research

**Researched:** 2026-02-06
**Domain:** Flecs REST API entity/component introspection, ncurses split-panel TUI, tree rendering, virtual scrolling
**Confidence:** HIGH

## Summary

Phase 03 requires fetching entity data and component details from the flecs REST API, rendering an interactive entity tree with parent-child hierarchy, displaying component key-value pairs in a side-by-side inspector, and listing all registered component types. The research focused on six areas: flecs REST API endpoints, tree rendering, virtual scrolling, JSON value rendering, split-panel layout, and codebase integration points.

The flecs REST API provides all necessary endpoints. The `/query` endpoint with `table=true` returns entities with all their components in the same format as the `/entity` endpoint. The `/entity/<path>` endpoint retrieves a single entity with all its components. The `/components` endpoint returns all registered component types with entity counts. There is no pagination support in the REST API, so all entities are returned in a single response -- virtual scrolling must happen client-side.

**Primary recommendation:** Use a two-endpoint strategy: `/query` with a broad filter and `entity_id=true` for the entity list, and `/entity/<path>?entity_id=true` for the selected entity's component detail. Use the dedicated `/components` endpoint for the Components tab. Build all tree, scroll, and split-panel logic as reusable utilities since they will be needed by later phases.

## Flecs REST API Findings

### Endpoint 1: GET /query -- Entity List

**Confidence: HIGH** (verified from flecs source code at `build/_deps/flecs-src/docs/FlecsRemoteApi.md` and `build/_deps/flecs-src/distr/flecs.c`)

**URL:** `GET /query?expr=<query_expression>&<options>`

**Key options for entity listing:**

| Option | Type | Purpose |
|--------|------|---------|
| `expr` | string | Query expression (URL-encoded) |
| `entity_id` | bool | Include numeric entity ID in results |
| `values` | bool | Serialize component values (default true) |
| `table` | bool | Serialize ALL components per entity (not just query fields) |
| `fields` | bool | Include query field data |
| `type_info` | bool | Include component type schemas |
| `try` | bool | Don't throw HTTP error on failure |

**Strategy for entity list polling:**
```
GET /query?expr=!ChildOf(self|up,flecs),!Module(self|up)&entity_id=true&values=false&table=true&try=true
```

This mirrors the `/world` endpoint's internal query (which excludes flecs builtin and module entities) but with `values=false` to keep the response lightweight. The `table=true` option gives us all component/tag names per entity without the heavy value data.

**Response format with `table=true`:**
```json
{
  "results": [
    {
      "parent": "ParentName",
      "name": "EntityName",
      "id": 775,
      "tags": ["TagA", "TagB"],
      "pairs": {
        "flecs.core.ChildOf": "ParentName",
        "flecs.core.IsA": "PrefabName"
      },
      "components": {
        "Position": null,
        "Velocity": null,
        "Mass": null
      }
    }
  ]
}
```

When `values=false`, component values are serialized as `null`. Component NAMES are still present as keys in the `"components"` object. Tags appear in `"tags"` array. Pairs appear in `"pairs"` object.

**Response format with `table=true` and `values=true`:**
```json
{
  "results": [
    {
      "parent": "Earth.shipyard",
      "name": "USS Enterprise",
      "id": 775,
      "tags": ["Broken", "FtlEnabled"],
      "pairs": {"flecs.core.IsA": "Freighter"},
      "components": {
        "Position": {"x": 10, "y": 20},
        "Velocity": {"x": 1, "y": 1}
      }
    }
  ]
}
```

**Important behaviors:**
- `"parent"` field contains the dot-separated path of the parent entity
- `"name"` field is the entity's own name (NOT a full path)
- `"id"` field is the numeric entity ID (only with `entity_id=true`)
- Anonymous entities have no `"name"` field (or empty name) -- they can be identified by having only an `"id"`
- Components without reflection data have `null` values even when `values=true`
- The `"results"` array is flat -- parent-child hierarchy must be reconstructed from the `"parent"` field

### Endpoint 2: GET /entity -- Single Entity Detail

**Confidence: HIGH** (verified from flecs REST API docs in source tree)

**URL:** `GET /entity/<path>?<options>`

Path uses `/` as separator (e.g., `/entity/Sun/Earth` for entity `Sun.Earth`).

**Key options:**

| Option | Type | Purpose |
|--------|------|---------|
| `entity_id` | bool | Include numeric entity ID |
| `values` | bool | Serialize component values (default true) |
| `type_info` | bool | Include component type schemas |
| `inherited` | bool | Include components inherited from prefabs |
| `try` | bool | Suppress 404 errors |

**Strategy for component inspection:**
```
GET /entity/<path>?entity_id=true&type_info=true&try=true
```

**Response format:**
```json
{
  "parent": "Sun",
  "name": "Earth",
  "id": 775,
  "type_info": {
    "Mass": {
      "value": ["float", {"unit": "flecs.units.Mass.KiloGrams", "symbol": "kg"}]
    },
    "Position": 0
  },
  "tags": ["Planet"],
  "pairs": {"flecs.core.IsA": "HabitablePlanet"},
  "components": {
    "Mass": {"value": "5.9722e24"},
    "Position": null,
    "(flecs.doc.Description,flecs.doc.Color)": {"value": "#6b93d6"}
  }
}
```

**Key details:**
- Components with reflection data have their values serialized as JSON objects
- Components WITHOUT reflection data have `null` values
- `type_info` value of `0` means no schema available
- Pair components use `(First,Second)` syntax as key name
- `try=true` prevents 404 error if entity was deleted between polls

### Endpoint 3: GET /components -- Component Registry

**Confidence: HIGH** (verified from flecs source code `flecs_rest_get_components`)

**URL:** `GET /components`

This is a dedicated endpoint that iterates ALL component records in the world.

**Response format (JSON array):**
```json
[
  {
    "name": "Position",
    "tables": [42, 55, 78],
    "entity_count": 150,
    "entity_size": 200,
    "type": {
      "size": 8,
      "alignment": 4,
      "ctor": true,
      "dtor": false,
      "copy": true,
      "move": true,
      "move_ctor": true,
      "copy_ctor": true,
      "on_add": false,
      "on_set": false,
      "on_remove": false,
      "on_replace": false
    },
    "memory": { ... },
    "traits": { ... }
  }
]
```

**Key fields:**
- `name`: Component name (string)
- `entity_count`: Number of entities with this component (int) -- exactly what the Components tab needs
- `entity_size`: Allocated capacity for entities
- `type`: Present only for components with type_info (not tags)
- `tables`: Array of table IDs (internal flecs storage, useful for debugging)

**This is the endpoint for the Components tab left panel.** No need to construct a custom query for `EcsComponent`.

### Endpoint 4: GET /world -- Full World Dump

**Confidence: HIGH** (verified from source)

**URL:** `GET /world`

Equivalent to `/query?expr=!ChildOf(self|up,flecs),!Module(self|up),?Prefab,?Disabled&table=true`. Returns ALL serializable entities excluding flecs internals. Response is the same format as `/query` with `table=true`.

**Usage note:** This endpoint dumps EVERYTHING including component values. For the entity list poll, use `/query` with `values=false` instead -- much lighter payload.

### No Pagination Support

**Confidence: HIGH** (verified: no offset/limit params in REST API docs or source)

The flecs REST API has NO pagination parameters. All matching entities are returned in a single response. This means:
- The full entity list arrives in one HTTP response
- Virtual scrolling must be implemented client-side on the parsed data
- For very large worlds (10K+ entities), the JSON payload could be substantial
- The `values=false` optimization is critical for keeping the list-polling response manageable

### Data Fetching Strategy (Two-Step)

Per user's decision in CONTEXT.md:

1. **Entity list poll** (every POLL_INTERVAL_MS):
   ```
   GET /query?expr=!ChildOf(self|up,flecs),!Module(self|up)&entity_id=true&values=false&table=true&try=true
   ```
   Light response: names, IDs, parent paths, component names (no values).

2. **Selected entity detail** (every POLL_INTERVAL_MS, only when entity selected):
   ```
   GET /entity/<path>?entity_id=true&try=true
   ```
   Full component values for the selected entity only.

3. **Component registry** (for Components tab, every POLL_INTERVAL_MS when tab active):
   ```
   GET /components
   ```

4. **Entities with specific component** (for Components tab right panel):
   ```
   GET /query?expr=<ComponentName>&entity_id=true&values=false&try=true
   ```

### URL Construction

Entity paths use `/` separator in URLs. For entity `Sun.Earth`, the URL path is `/entity/Sun/Earth`. The path component must be URL-encoded if it contains special characters.

For the query expression, use URL encoding: commas become `%2C`, spaces become `%20`, parentheses `%28`/`%29`, pipe `%7C`, exclamation `%21`, etc.

## Tree Rendering Approach

**Confidence: HIGH** (straightforward data structure and rendering)

### Data Structure for Entity Tree

The flecs `/query` response gives us a flat list with `"parent"` and `"name"` fields. We must build the tree client-side.

```c
typedef struct entity_node {
    char *name;            /* Entity name (just the leaf name) */
    char *full_path;       /* Full path for REST API lookups (e.g., "Sun/Earth") */
    uint64_t id;           /* Numeric entity ID */

    /* Component names (from the lightweight list poll) */
    char **component_names;
    int component_count;

    /* Tags */
    char **tags;
    int tag_count;

    /* Tree structure */
    struct entity_node *parent;
    struct entity_node **children;
    int child_count;
    int child_capacity;

    /* UI state */
    bool expanded;         /* Whether children are visible */
    bool is_anonymous;     /* Entity has no name (only numeric ID) */
    int depth;             /* Nesting level for indentation */
} entity_node_t;
```

### Building the Tree from Flat Results

```c
/* Algorithm:
 * 1. Parse all results into entity_node_t array
 * 2. Build a hash map: full_path -> entity_node_t*
 * 3. For each node, look up parent in hash map
 * 4. Attach as child; nodes without parent are roots
 * 5. Root nodes are top-level entities
 */
```

The `"parent"` field from flecs uses dot-separated paths (e.g., `"Earth.shipyard"`). When constructing the full path for a node: if parent is `"Earth.shipyard"` and name is `"USS Enterprise"`, the full path is `"Earth.shipyard.USS Enterprise"` for internal tracking and `"Earth/shipyard/USS Enterprise"` for REST API URLs.

### Flattened Visible List for Rendering

To render the tree with virtual scrolling, maintain a "flattened visible" array:

```c
typedef struct tree_view {
    entity_node_t **all_nodes;     /* All nodes (flat ownership array) */
    int total_count;

    entity_node_t **visible;       /* Flattened visible nodes (expanded tree) */
    int visible_count;

    int cursor;                    /* Currently highlighted row index into visible[] */
    int scroll_offset;             /* First visible row in the window */

    bool show_anonymous;           /* Toggle for 'f' key */
} tree_view_t;
```

When expand/collapse state changes, rebuild the `visible[]` array by DFS traversal of roots, including children only for expanded nodes and respecting the `show_anonymous` filter.

### Unicode Box Drawing Characters

Per user decision: use Unicode box drawing for tree lines.

```c
/* UTF-8 encoded box drawing characters */
#define TREE_VERT    "\xe2\x94\x82"   /* U+2502  vertical line */
#define TREE_BRANCH  "\xe2\x94\x9c"   /* U+251C  branch (has sibling below) */
#define TREE_LAST    "\xe2\x94\x94"   /* U+2514  last child (corner) */
#define TREE_HORIZ   "\xe2\x94\x80"   /* U+2500  horizontal line */

/* Combined prefixes:
 *   "...  "  = TREE_VERT + "   " (for ancestor with more siblings)
 *   "     "  = "    " (for ancestor that was last child)
 *   "......"  = TREE_BRANCH + TREE_HORIZ + TREE_HORIZ + " " (for non-last child)
 *   "......  " = TREE_LAST + TREE_HORIZ + TREE_HORIZ + " " (for last child)
 */
```

Rendering a tree row:

```
depth 0:  EntityName                    [Position, Velocity, +2 more]  #775
depth 1:  +-- ChildEntity               [Mass]                         #776
depth 1:  +-- AnotherChild              [Health, Armor]                #777
depth 2:      +-- GrandChild            [Position]                     #778
depth 2:      +-- LastGrandChild         []                            #779
```

Where `+--` represents the Unicode box drawing characters. Each depth level adds 4 characters of indentation.

### Expand/Collapse State Tracking

- Each `entity_node_t` has an `expanded` bool
- Enter key toggles `expanded` on the currently selected node
- After toggling, rebuild the `visible[]` array
- Preserve `cursor` position by tracking which node it pointed to, then finding that node's new index in the rebuilt visible array

### Anonymous Entity Filtering

- Anonymous entities are those from flecs with no `"name"` in the query response
- Detect by: `name` field is NULL/empty, only `id` is present
- Display as `#<id>` (e.g., `#1045`)
- Hidden by default; toggled with `f` key per user decision
- When hidden, also hide their subtrees

## Virtual Scrolling Approach

**Confidence: HIGH** (standard pattern)

### Why Not ncurses Pads

While ncurses pads (`newpad()`) support larger-than-screen content, they have problems:
- Pads require `prefresh()`/`pnoutrefresh()` with region coordinates (complex)
- Cannot mix pads and windows easily in the batch `wnoutrefresh()`/`doupdate()` pattern the codebase already uses
- No real performance gain since we still need the data structure

### Client-Side Virtual Scrolling

The standard approach for large lists in TUI:

```c
typedef struct scroll_state {
    int total_items;      /* Total number of items in the list */
    int visible_rows;     /* Number of rows visible in the window */
    int cursor;           /* Currently selected item index [0..total_items-1] */
    int scroll_offset;    /* First visible item index */
} scroll_state_t;
```

**Scrolling logic:**
```c
static void scroll_ensure_visible(scroll_state_t *s) {
    /* Cursor above visible area -- scroll up */
    if (s->cursor < s->scroll_offset) {
        s->scroll_offset = s->cursor;
    }
    /* Cursor below visible area -- scroll down */
    if (s->cursor >= s->scroll_offset + s->visible_rows) {
        s->scroll_offset = s->cursor - s->visible_rows + 1;
    }
    /* Clamp scroll offset */
    int max_offset = s->total_items - s->visible_rows;
    if (max_offset < 0) max_offset = 0;
    if (s->scroll_offset > max_offset) s->scroll_offset = max_offset;
    if (s->scroll_offset < 0) s->scroll_offset = 0;
}

static void scroll_move(scroll_state_t *s, int delta) {
    s->cursor += delta;
    if (s->cursor < 0) s->cursor = 0;
    if (s->cursor >= s->total_items) s->cursor = s->total_items - 1;
    scroll_ensure_visible(s);
}
```

**Rendering loop:**
```c
for (int row = 0; row < visible_rows; row++) {
    int item_idx = scroll_offset + row;
    if (item_idx >= total_items) break;
    render_item(win, row, items[item_idx], item_idx == cursor);
}
```

Only the visible rows are rendered. For 10K entities with a 30-row window, only 30 items are rendered per frame. The data structure holding all entity nodes is in memory, but rendering is O(visible_rows).

### Performance Budget

- JSON parsing with yyjson: ~1ms per 1000 entities (yyjson is extremely fast)
- Tree rebuild: O(n) where n is total entities
- Visible list rebuild: O(n) DFS traversal
- Render: O(visible_rows) -- constant relative to total count

For up to ~5000 user entities, this should be responsive within the 500ms poll interval.

## JSON Value Rendering with yyjson

**Confidence: HIGH** (verified from yyjson source headers and existing codebase usage)

### Recursive Value Renderer

Component values from the `/entity` endpoint are arbitrary JSON. Need a recursive renderer for the component inspector panel.

**Key yyjson APIs needed:**

```c
/* Type checking */
bool yyjson_is_obj(yyjson_val *val);
bool yyjson_is_arr(yyjson_val *val);
bool yyjson_is_str(yyjson_val *val);
bool yyjson_is_num(yyjson_val *val);
bool yyjson_is_null(yyjson_val *val);
bool yyjson_is_bool(yyjson_val *val);

/* Value extraction */
const char *yyjson_get_str(yyjson_val *val);
double yyjson_get_real(yyjson_val *val);
int64_t yyjson_get_int(yyjson_val *val);
uint64_t yyjson_get_uint(yyjson_val *val);
bool yyjson_get_bool(yyjson_val *val);

/* Foreach macros (C99 compatible) */
size_t idx, max;
yyjson_val *key, *val;
yyjson_obj_foreach(obj, idx, max, key, val) { ... }

size_t idx, max;
yyjson_val *val;
yyjson_arr_foreach(arr, idx, max, val) { ... }

/* Size queries */
size_t yyjson_arr_size(yyjson_val *arr);
size_t yyjson_obj_size(yyjson_val *obj);
```

### Rendering Strategy

Per user decision: nested objects and arrays always rendered as indented tree (no inline shorthand).

```c
/* Recursive renderer for JSON values in the component inspector.
 *
 * render_json_value(win, val, row, indent, max_rows)
 * Returns: number of rows consumed
 */
static int render_json_value(WINDOW *win, yyjson_val *val,
                             int row, int indent, int max_row, int col_width) {
    if (row >= max_row) return 0;

    if (yyjson_is_null(val)) {
        mvwprintw(win, row, indent, "null");
        return 1;
    }
    if (yyjson_is_bool(val)) {
        mvwprintw(win, row, indent, "%s", yyjson_get_bool(val) ? "true" : "false");
        return 1;
    }
    if (yyjson_is_int(val)) {
        mvwprintw(win, row, indent, "%lld", (long long)yyjson_get_int(val));
        return 1;
    }
    if (yyjson_is_real(val)) {
        mvwprintw(win, row, indent, "%.4g", yyjson_get_real(val));
        return 1;
    }
    if (yyjson_is_str(val)) {
        mvwprintw(win, row, indent, "\"%.*s\"", col_width - indent - 2, yyjson_get_str(val));
        return 1;
    }
    if (yyjson_is_obj(val)) {
        int rows_used = 0;
        size_t idx, max;
        yyjson_val *key, *child;
        yyjson_obj_foreach(val, idx, max, key, child) {
            if (row + rows_used >= max_row) break;
            mvwprintw(win, row + rows_used, indent, "%s:", yyjson_get_str(key));
            if (yyjson_is_obj(child) || yyjson_is_arr(child)) {
                rows_used++;
                rows_used += render_json_value(win, child, row + rows_used, indent + 2, max_row, col_width);
            } else {
                /* Render simple value on same line after key */
                wprintw(win, " ");
                rows_used += render_json_value(win, child, row + rows_used, getcurx(win), max_row, col_width);
            }
        }
        return rows_used;
    }
    if (yyjson_is_arr(val)) {
        int rows_used = 0;
        size_t idx, max;
        yyjson_val *elem;
        yyjson_arr_foreach(val, idx, max, elem) {
            if (row + rows_used >= max_row) break;
            mvwprintw(win, row + rows_used, indent, "[%zu]:", idx);
            if (yyjson_is_obj(elem) || yyjson_is_arr(elem)) {
                rows_used++;
                rows_used += render_json_value(win, elem, row + rows_used, indent + 2, max_row, col_width);
            } else {
                wprintw(win, " ");
                rows_used += render_json_value(win, elem, row + rows_used, getcurx(win), max_row, col_width);
            }
        }
        return rows_used;
    }
    return 0;
}
```

### Component Inspector Layout

Per user decision: components grouped by component name as header, fields indented underneath.

```
Position                          [collapsible header]
  x: 10.0
  y: 20.5
Velocity                          [collapsible header]
  x: 1.0
  y: 2.0
Inventory                         [collapsible header]
  items:
    [0]:
      name: "Sword"
      damage: 50
    [1]:
      name: "Shield"
      defense: 30
```

Each component group needs its own `expanded` state (default: expanded). The component inspector is itself a scrollable list of rows.

### Storing Raw JSON for Live Update

Since component values update each poll cycle, store the raw yyjson_doc per selected entity:

```c
typedef struct entity_detail {
    char *path;             /* Entity path for REST lookup */
    uint64_t id;            /* Numeric entity ID */
    yyjson_doc *doc;        /* Parsed JSON response (owns all values) */
    yyjson_val *components; /* Pointer into doc: the "components" object */
    yyjson_val *tags;       /* Pointer into doc: the "tags" array */
    yyjson_val *pairs;      /* Pointer into doc: the "pairs" object */
    int64_t timestamp_ms;   /* When fetched */
} entity_detail_t;
```

On each poll, free the old `doc` and parse the new response. All `yyjson_val*` pointers remain valid for the lifetime of `doc`.

## Split-Panel ncurses Layout

**Confidence: HIGH** (straightforward, consistent with existing newwin pattern)

### Approach: Two Sub-Windows from Content Area

The existing codebase uses `newwin()` for the content area (`win_content`, at row 2, height LINES-3). For the split panel, create two child windows inside the content area dimensions.

**Per the existing pattern (plain `newwin()`, not subwin/derwin):**

```c
typedef struct split_panel {
    WINDOW *left;
    WINDOW *right;
    int left_width;     /* Columns for left panel (40% of COLS) */
    int right_width;    /* Columns for right panel (60% of COLS) */
    int height;         /* Rows available (LINES - 3) */
    int focus;          /* 0 = left panel, 1 = right panel */
} split_panel_t;
```

**Creation:**
```c
void split_panel_create(split_panel_t *sp, int height, int width, int start_row) {
    sp->height = height;
    sp->left_width = width * 40 / 100;
    sp->right_width = width - sp->left_width;
    sp->left = newwin(height, sp->left_width, start_row, 0);
    sp->right = newwin(height, sp->right_width, start_row, sp->left_width);
    sp->focus = 0;
}
```

**Important: The tab's `draw` function receives `win_content` but should NOT draw into it directly.** Instead, the tab creates its own sub-windows at init time, manages them, and calls `wnoutrefresh()` on each. The parent `win_content` can be skipped for tabs that manage their own windows.

**Alternative approach (simpler):** Since `tui_render()` already calls `wnoutrefresh(win_content)`, the tab could draw directly into `win_content` using column offsets (no separate windows). This avoids window lifecycle management but loses independent scrolling/clipping.

**Recommended approach:** Create dedicated windows in `tab_init`, destroy in `tab_fini`, and resize on `KEY_RESIZE`. The tab's draw function erases and draws its own windows, then calls `wnoutrefresh()` on them. The tab_system draw should call the tab's draw function which handles its own refresh. This requires a small change: the tab draw function should be allowed to manage its own windows rather than being forced to draw into the passed `win_content`.

### Border Rendering

Per user decision: active panel has bold border, inactive has dim border.

```c
void draw_panel_border(WINDOW *win, bool active, const char *title) {
    if (active) {
        wattron(win, A_BOLD);
        box(win, 0, 0);
        wattroff(win, A_BOLD);
    } else {
        wattron(win, A_DIM);
        box(win, 0, 0);
        wattroff(win, A_DIM);
    }
    if (title) {
        mvwprintw(win, 0, 2, " %s ", title);
    }
}
```

### Focus Switching

Left/Right arrow keys switch panel focus. Per user decision: these are dedicated to focus switching, not tree navigation.

```c
/* In handle_input: */
case KEY_LEFT:
    sp->focus = 0;  /* Left panel */
    return true;
case KEY_RIGHT:
    sp->focus = 1;  /* Right panel */
    return true;
```

## Existing Codebase Patterns and Integration Points

**Confidence: HIGH** (read directly from source)

### Architecture Pattern

```
main.c loop:
  1. getch() -> handle global keys -> tab_system_handle_input()
  2. Timer-based poll -> http_get() -> parse JSON -> update app_state
  3. tui_render() -> tab_system_draw() -> active tab's draw()
```

### Tab VTable Pattern

Each tab implements 4 functions:
- `init(tab_t *self)` -- allocate per-tab state in `self->state`
- `fini(tab_t *self)` -- free per-tab state
- `draw(const tab_t *self, WINDOW *win, const void *app_state)` -- render
- `handle_input(tab_t *self, int ch, void *app_state)` -- process keys

Tab definition includes `required_endpoints` bitmask for smart polling.

### Current Endpoint Bitmask

Already defined in `tab_system.h`:
```c
ENDPOINT_QUERY    = (1u << 2),  /* /query?expr=... */
ENDPOINT_ENTITY   = (1u << 3),  /* /entity/<path> */
ENDPOINT_COMPONENTS = (1u << 4), /* /components */
```

The Entities tab is already registered with `ENDPOINT_QUERY` and the Components tab with `ENDPOINT_COMPONENTS`. Phase 03 needs to extend the main loop to actually poll these endpoints when those bitmask bits are set.

### Current app_state_t

```c
typedef struct app_state {
    world_snapshot_t   *snapshot;    /* Stats data */
    connection_state_t  conn_state;  /* Connection health */
} app_state_t;
```

Phase 03 needs to extend this with entity list data, selected entity detail, and component registry data. The `TODO` comment in `main.c` line 98-99 explicitly calls out adding conditional polling for `ENDPOINT_QUERY` etc.

### HTTP Client

Single `CURL *curl` handle with 200ms timeout. The `http_get()` function takes a URL string and returns `http_response_t` with status code and body buffer.

**Key integration point:** The main loop currently only fetches one URL per poll cycle (`/stats/world`). Phase 03 needs to add conditional fetching of additional URLs based on the `required_endpoints` bitmask.

```c
/* Pseudocode for extended polling: */
uint32_t needed = tab_system_required_endpoints(&tabs);

/* Always poll stats for connection health */
http_response_t stats_resp = http_get(curl, stats_url);
app_state.conn_state = connection_state_update(app_state.conn_state, stats_resp.status);

if (needed & ENDPOINT_STATS_WORLD) {
    /* Parse and store world stats... (existing code) */
}

if (needed & ENDPOINT_QUERY) {
    http_response_t query_resp = http_get(curl, entity_list_url);
    if (query_resp.status == 200) {
        /* Parse entity list, rebuild tree */
    }
    http_response_free(&query_resp);
}

if (app_state.selected_entity_path && (needed & ENDPOINT_ENTITY)) {
    char url[512];
    snprintf(url, sizeof(url), "http://localhost:27750/entity/%s?entity_id=true&try=true",
             app_state.selected_entity_path);
    http_response_t entity_resp = http_get(curl, url);
    if (entity_resp.status == 200) {
        /* Parse entity detail */
    }
    http_response_free(&entity_resp);
}

if (needed & ENDPOINT_COMPONENTS) {
    http_response_t comp_resp = http_get(curl, "http://localhost:27750/components");
    if (comp_resp.status == 200) {
        /* Parse component registry */
    }
    http_response_free(&comp_resp);
}
```

**Timing concern:** Each `http_get()` blocks for up to 200ms. Multiple fetches per poll cycle could take up to 600ms (3 endpoints). Mitigations:
- Localhost latency is sub-1ms typically
- Timeout is a ceiling, not typical duration
- Only fetch endpoints the active tab needs
- Could stagger: entity list every poll, entity detail every poll, component registry less frequently

### JSON Parser Integration

Current `json_parser.c` only parses `/stats/world`. Phase 03 adds new parse functions:
- `json_parse_entity_list()` -- parse `/query` response into entity tree
- `json_parse_entity_detail()` -- parse `/entity` response into detail struct
- `json_parse_component_registry()` -- parse `/components` response

### Color Pairs

Currently defined: CP_CONNECTED(1), CP_DISCONNECTED(2), CP_RECONNECTING(3), CP_LABEL(4), CP_TAB_ACTIVE(5), CP_TAB_INACTIVE(6). New pairs needed:
- CP_TREE_LINE (7): Dim color for box drawing characters
- CP_ENTITY_NAME (8): Entity name color
- CP_COMPONENT_HEADER (9): Component name headers
- CP_JSON_KEY (10): JSON key names in inspector
- CP_JSON_STRING (11): String values
- CP_JSON_NUMBER (12): Numeric values
- CP_PANEL_BORDER_ACTIVE (13): Active panel border
- CP_PANEL_BORDER_INACTIVE (14): Inactive panel border
- CP_CURSOR (15): Selected row highlight

### File Structure

New files needed:
```
src/tabs/tab_entities.h     -- Entities tab header
src/tabs/tab_entities.c     -- Entities tab implementation
src/tabs/tab_components.h   -- Components tab header
src/tabs/tab_components.c   -- Components tab implementation
```

Shared utilities (used by both tabs and future tabs):
```
src/split_panel.h           -- Split panel layout and focus management
src/split_panel.c
src/tree_view.h             -- Tree data structure, expand/collapse, virtual scrolling
src/tree_view.c
src/scroll.h                -- Generic scroll state management
src/scroll.c
src/json_render.h           -- Recursive JSON value renderer for ncurses
src/json_render.c
```

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON parsing | Custom tokenizer | yyjson (already in project) | Fast, correct, handles all JSON edge cases |
| URL encoding | Custom encoder | `curl_easy_escape()` | libcurl already linked, handles all edge cases |
| HTTP requests | Custom sockets | libcurl (already in project) | Handles timeouts, connection reuse, error codes |
| Unicode output | Manual byte encoding | Define UTF-8 byte constants as string macros | Wide-char ncurses handles display width |
| Tree data structure | Ad-hoc linked list | Dedicated tree_view module with proper alloc/free | Reusable for Systems tab (Phase 04) hierarchy |

## Common Pitfalls

### Pitfall 1: Entity Path Separator Mismatch
**What goes wrong:** The `/query` response uses `.` as path separator in `"parent"` field (e.g., `"Earth.shipyard"`), but the `/entity` REST endpoint uses `/` as URL path separator (e.g., `/entity/Earth/shipyard`).
**Why it happens:** Flecs uses `.` internally for entity paths but `/` for URL routing.
**How to avoid:** When constructing the URL for `/entity/<path>`, replace all `.` with `/` in the parent+name path. Store both formats or convert on the fly.
**Warning signs:** 404 errors from the `/entity` endpoint.

### Pitfall 2: Anonymous Entity Handling
**What goes wrong:** Anonymous entities appear in query results with no `"name"` field. If the code assumes every entity has a name, it will crash or display empty rows.
**Why it happens:** Flecs creates many anonymous entities for internal bookkeeping (relationships, type entities, etc.).
**How to avoid:** Check for missing `"name"` field. Display as `#<id>`. Filter them by default (user decision: hidden by default, toggle with `f`).
**Warning signs:** Empty tree rows, crash on NULL name dereference.

### Pitfall 3: Entity Disappears Between Polls
**What goes wrong:** User selects an entity, but by the next poll it has been destroyed. The `/entity/<path>` request returns 404.
**Why it happens:** Entity is destroyed in the running game between poll cycles.
**How to avoid:** Use `try=true` parameter. Check for 404 response. Show brief footer notification "Entity X removed" per user decision. Clear the inspector panel.
**Warning signs:** Error messages in footer, stale data in inspector.

### Pitfall 4: Window Lifecycle on Resize
**What goes wrong:** Tab creates sub-windows (left/right panels) at init. Terminal resize doesn't update them, causing rendering corruption.
**Why it happens:** `tui_resize()` destroys and recreates `win_content`, but the tab's own windows are not touched.
**How to avoid:** Tabs with custom windows must handle `KEY_RESIZE` in their `handle_input` function (or expose a resize callback). Destroy and recreate sub-windows with new dimensions.
**Warning signs:** Panels render at wrong position, text appears outside expected area after resize.

### Pitfall 5: Multiple HTTP Requests Per Poll Cycle
**What goes wrong:** Fetching entity list + entity detail + stats in one poll cycle takes 600ms+ (3x 200ms timeout ceiling), making the UI unresponsive.
**Why it happens:** `http_get()` is blocking and uses a 200ms timeout.
**How to avoid:** On localhost, actual response time is sub-1ms. The 200ms is only hit on failure/disconnect. In connected state, multiple requests are fine. Only the connection-health poll is always needed; others are conditional on active tab.
**Warning signs:** UI freezes when all requests fail simultaneously.

### Pitfall 6: ncurses Wide Character Width
**What goes wrong:** Unicode box drawing characters (3 bytes each in UTF-8) might be treated as 3 columns instead of 1 column by ncurses, misaligning text.
**Why it happens:** If ncursesw is not properly linked, or if `setlocale(LC_ALL, "")` is not called.
**How to avoid:** The project already uses `ncursesw` (wide-char ncurses) in CMakeLists.txt. Ensure `setlocale(LC_ALL, "")` is called at startup (check if `tui_init` does this).
**Warning signs:** Tree lines appear too wide, text misaligned after Unicode characters.

### Pitfall 7: yyjson Document Lifetime
**What goes wrong:** Accessing `yyjson_val*` pointers after `yyjson_doc_free()` causes use-after-free.
**Why it happens:** All values in a yyjson doc are allocated as part of the doc. Freeing the doc invalidates all value pointers.
**How to avoid:** Keep the `yyjson_doc*` alive as long as any `yyjson_val*` from it is being used. For the entity detail, replace the doc atomically: parse new, swap pointer, free old.
**Warning signs:** Garbage data in inspector, random crashes.

## Code Examples

### Example 1: Constructing Entity List Query URL

```c
/* Source: flecs REST API docs (build/_deps/flecs-src/docs/FlecsRemoteApi.md) */
static const char *ENTITY_LIST_URL =
    "http://localhost:27750/query"
    "?expr=!ChildOf(self%7Cup%2Cflecs)%2C!Module(self%7Cup)"
    "&entity_id=true"
    "&values=false"
    "&table=true"
    "&try=true";

/* URL-decoded query expression:
 *   !ChildOf(self|up,flecs), !Module(self|up)
 * This excludes flecs builtin entities and module entities.
 */
```

### Example 2: Constructing Entity Detail URL

```c
/* entity_path uses / separator (e.g., "Sun/Earth") */
static void build_entity_url(char *buf, size_t bufsize, const char *entity_path) {
    snprintf(buf, bufsize,
        "http://localhost:27750/entity/%s?entity_id=true&try=true",
        entity_path);
}
```

### Example 3: Parsing /query Response for Entity List

```c
/* Parse flat entity list from /query response */
static int parse_entity_results(const char *json, size_t len,
                                entity_node_t **out_nodes, int *out_count) {
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return -1;

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *results = yyjson_obj_get(root, "results");
    if (!results || !yyjson_is_arr(results)) {
        yyjson_doc_free(doc);
        return -1;
    }

    size_t count = yyjson_arr_size(results);
    entity_node_t *nodes = calloc(count, sizeof(entity_node_t));

    size_t idx, max;
    yyjson_val *entity;
    yyjson_arr_foreach(results, idx, max, entity) {
        yyjson_val *name_val = yyjson_obj_get(entity, "name");
        yyjson_val *parent_val = yyjson_obj_get(entity, "parent");
        yyjson_val *id_val = yyjson_obj_get(entity, "id");

        if (name_val && yyjson_is_str(name_val)) {
            nodes[idx].name = strdup(yyjson_get_str(name_val));
        }
        if (parent_val && yyjson_is_str(parent_val)) {
            /* Build full_path from parent + name */
            const char *parent = yyjson_get_str(parent_val);
            /* ... build path ... */
        }
        if (id_val) {
            nodes[idx].id = (uint64_t)yyjson_get_uint(id_val);
        }
        nodes[idx].is_anonymous = (nodes[idx].name == NULL);

        /* Extract component names from "components" object keys */
        yyjson_val *comps = yyjson_obj_get(entity, "components");
        if (comps && yyjson_is_obj(comps)) {
            size_t ci, cmax;
            yyjson_val *ckey, *cval;
            int comp_count = (int)yyjson_obj_size(comps);
            nodes[idx].component_names = calloc(comp_count, sizeof(char*));
            nodes[idx].component_count = comp_count;
            int c = 0;
            yyjson_obj_foreach(comps, ci, cmax, ckey, cval) {
                nodes[idx].component_names[c++] = strdup(yyjson_get_str(ckey));
            }
        }

        /* Extract tags */
        yyjson_val *tags = yyjson_obj_get(entity, "tags");
        if (tags && yyjson_is_arr(tags)) {
            /* ... similar pattern ... */
        }
    }

    *out_nodes = nodes;
    *out_count = (int)count;
    yyjson_doc_free(doc);  /* Safe: we strdup'd all strings */
    return 0;
}
```

### Example 4: Parsing /components Response

```c
/* Parse component registry from /components response */
typedef struct component_info {
    char *name;
    int entity_count;
    int size;
    int alignment;
    bool has_type_info;
} component_info_t;

static int parse_component_registry(const char *json, size_t len,
                                    component_info_t **out, int *out_count) {
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return -1;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_arr(root)) {
        yyjson_doc_free(doc);
        return -1;
    }

    size_t count = yyjson_arr_size(root);
    component_info_t *comps = calloc(count, sizeof(component_info_t));

    size_t idx, max;
    yyjson_val *comp;
    yyjson_arr_foreach(root, idx, max, comp) {
        yyjson_val *name = yyjson_obj_get(comp, "name");
        yyjson_val *ec = yyjson_obj_get(comp, "entity_count");
        yyjson_val *type = yyjson_obj_get(comp, "type");

        if (name) comps[idx].name = strdup(yyjson_get_str(name));
        if (ec) comps[idx].entity_count = (int)yyjson_get_int(ec);

        if (type && yyjson_is_obj(type)) {
            comps[idx].has_type_info = true;
            yyjson_val *sz = yyjson_obj_get(type, "size");
            yyjson_val *al = yyjson_obj_get(type, "alignment");
            if (sz) comps[idx].size = (int)yyjson_get_int(sz);
            if (al) comps[idx].alignment = (int)yyjson_get_int(al);
        }
    }

    *out = comps;
    *out_count = (int)count;
    yyjson_doc_free(doc);
    return 0;
}
```

### Example 5: Tree Row Rendering with Box Drawing

```c
/* Render one entity tree row at the given window row */
static void render_tree_row(WINDOW *win, int row, entity_node_t *node,
                            bool selected, int panel_width) {
    if (selected) wattron(win, A_REVERSE);

    int col = 1;  /* Start after border */

    /* Draw tree indentation lines */
    for (int d = 0; d < node->depth; d++) {
        /* Check if ancestor at depth d has more siblings below */
        /* For simplicity: always draw vertical continuation or spaces */
        if (d < node->depth - 1) {
            /* Ancestor line: vertical or space depending on context */
            mvwprintw(win, row, col, TREE_VERT "   ");
        } else {
            /* Immediate parent connection */
            if (node_is_last_child(node)) {
                mvwprintw(win, row, col, TREE_LAST TREE_HORIZ TREE_HORIZ " ");
            } else {
                mvwprintw(win, row, col, TREE_BRANCH TREE_HORIZ TREE_HORIZ " ");
            }
        }
        col += 4;  /* Each depth level = 4 columns */
    }

    /* Expand/collapse indicator */
    if (node->child_count > 0) {
        wprintw(win, "%s ", node->expanded ? "v" : ">");
        col += 2;
    }

    /* Entity name or anonymous ID */
    if (node->is_anonymous) {
        wattron(win, A_DIM);
        wprintw(win, "#%llu", (unsigned long long)node->id);
        wattroff(win, A_DIM);
    } else {
        wprintw(win, "%s", node->name);
    }

    /* Component names inline (right-aligned, truncated) */
    /* ... */

    if (selected) wattroff(win, A_REVERSE);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| flecs-rest hub (v3) separate repo | Built-in REST in flecs v4+ | 2024 | REST is part of core, no external dependency |
| `/filter` and `/scope` endpoints | `/query` with expr language | Flecs v4 | Single unified query endpoint |
| No `/components` endpoint | Dedicated `/components` endpoint | Flecs v4 | No need to craft custom EcsComponent query |

## Open Questions

1. **setlocale() call missing?**
   - What we know: ncursesw is linked, but `tui_init()` does not call `setlocale(LC_ALL, "")`. Box drawing characters may not render correctly without it.
   - Recommendation: Add `setlocale(LC_ALL, "")` to `tui_init()` before `initscr()`.

2. **Exact query expression for entity list**
   - What we know: The `/world` endpoint uses `!ChildOf(self|up, flecs), !Module(self|up), ?Prefab, ?Disabled` to filter out builtin/module entities. This may exclude too many or too few entities for the debugger's purposes.
   - What's unclear: Whether the CELS app creates entities under module scopes that should still be visible. Whether prefabs and disabled entities should appear.
   - Recommendation: Start with the `/world` query expression. Test against the actual CELS demo app and adjust if user entities are missing. Could simplify to just exclude flecs scope: `!ChildOf(self|up, flecs)`.

3. **Tab draw function window management**
   - What we know: Current tab draw receives `WINDOW *win` (the content window). Phase 03 tabs need to create their own split panel windows.
   - What's unclear: Whether the tab should draw into the passed window or manage its own windows.
   - Recommendation: Tab manages its own windows (created in init, destroyed in fini). The passed `win_content` provides dimension info but is not drawn into. Tab calls `wnoutrefresh()` on its own windows. May need a resize callback or the tab checks dimensions on each draw.

4. **Multiple HTTP requests per poll cycle**
   - What we know: The current loop does one `http_get()` per poll. Phase 03 needs up to 3 (stats + entity list + entity detail).
   - What's unclear: Whether this will noticeably impact UI responsiveness.
   - Recommendation: On localhost, actual latency is sub-1ms. 3 requests should complete in <5ms total when connected. Only a problem on disconnect (each waits 200ms). Accept this risk for v0.1.

## Sources

### Primary (HIGH confidence)
- `/home/cachy/workspaces/libs/cels/build/_deps/flecs-src/docs/FlecsRemoteApi.md` -- Full REST API reference with all endpoints, options, and JSON response examples
- `/home/cachy/workspaces/libs/cels/build/_deps/flecs-src/distr/flecs.c` (lines 27808-27927, 28496-28586) -- REST server implementation showing all endpoints including `/components`
- `/home/cachy/workspaces/libs/cels/build/_deps/yyjson-src/src/yyjson.h` (lines 2036-2222) -- yyjson foreach macros and iteration API
- All existing cels-debug source files (read directly)

### Secondary (MEDIUM confidence)
- [Flecs Remote API docs](https://www.flecs.dev/flecs/md_docs_2FlecsRemoteApi.html) -- Official online documentation
- [yyjson API documentation](https://github.com/ibireme/yyjson/blob/master/doc/API.md) -- Immutable document API reference
- [ncurses window management](https://linux.die.net/man/3/derwin) -- subwin vs newwin guidance

### Tertiary (LOW confidence)
- WebSearch results for virtual scrolling patterns -- confirmed with standard practice
- WebSearch results for ncurses split panel -- confirmed with existing codebase pattern

## Metadata

**Confidence breakdown:**
- Flecs REST API: HIGH -- verified from source code in the build tree
- Tree rendering: HIGH -- standard data structures, no external dependency
- Virtual scrolling: HIGH -- standard pattern, no ncurses-specific concerns
- JSON rendering: HIGH -- yyjson API verified from headers
- Split panel layout: HIGH -- follows existing newwin() pattern
- Integration points: HIGH -- read directly from source code

**Research date:** 2026-02-06
**Valid until:** 2026-03-06 (stable -- flecs REST API unlikely to change within a minor version)
