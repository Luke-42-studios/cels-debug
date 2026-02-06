# Phase 05: State, Performance, and Polish - Research

**Researched:** 2026-02-06
**Domain:** Tab restructuring, State section (CELS tab), Performance tab (waterfall viz), navigation polish, auto-reconnect
**Confidence:** HIGH (codebase analysis, existing patterns, architecture verified)

## Summary

Phase 05 is the final phase of cels-debug v0.1. It involves five workstreams: (1) tab restructuring -- renaming ECS to CELS, extracting Systems into its own top-level tab, and removing the separate State tab placeholder; (2) implementing the State section inside the CELS tab as the "S" in CELS-C; (3) building the Performance tab with a full-width waterfall/timeline visualization; (4) polishing navigation with direct 1-4 tab keys, Esc back-navigation stack, and a context-sensitive footer hint bar; (5) improving auto-reconnect with silent retry and configurable refresh intervals.

The most significant technical finding is that **CEL_State values are C-level static variables, NOT Flecs components**. They are not exposed via the Flecs REST API. The `state_storage` map in `CELS_Context` stores binary blobs keyed by ID without type information or field names. This means the State section **cannot display live state values from the running application** without adding a new debug endpoint to CELS itself. The State section must either: (a) show state *entities* (the lifecycle entities named `*Lifecycle`, which ARE in the entity list), or (b) be deferred until CELS adds a `/debug/state` REST endpoint. The CONTEXT.md decision says "State section displays State() component values" -- this requires a new CELS-side endpoint. Given this is out of scope for cels-debug (which is read-only over existing REST), the pragmatic approach is to show state-related entities from the entity query (which already exist as Lifecycle classification) and document the limitation.

**UPDATE on State approach:** Re-reading the CONTEXT.md more carefully, the user decided State replaces a separate tab and lives as a section inside CELS tab. The existing entity classification already has `ENTITY_CLASS_LIFECYCLE` which captures `*Lifecycle` entities. The State section can repurpose this to show lifecycle/state entities with their component data in the split-panel inspector. This provides meaningful value without a new CELS endpoint. The "highlight flash on value change" can work by comparing component values between poll cycles for selected state entities.

**Primary recommendation:** Implement in 4 plans: (1) Tab restructure + Systems tab extraction, (2) State section + change highlighting, (3) Performance tab waterfall, (4) Navigation polish + reconnect.

## Standard Stack

No new libraries needed. Phase 05 uses only existing dependencies.

### Core (existing, unchanged)
| Library | Version | Purpose | Status |
|---------|---------|---------|--------|
| ncurses/ncursesw | 6.5+ | TUI rendering | System package, already in use |
| libcurl | 8.x | HTTP polling | System package, already in use |
| yyjson | 0.10+ | JSON parsing | FetchContent, already in use |

### Supporting (existing, unchanged)
| Module | Purpose | Status |
|--------|---------|--------|
| split_panel.c | Two-panel layout with focus | Shared by CELS + new Systems tab |
| tree_view.c | CELS-C tree with sections | Extended for State section |
| scroll.c | Virtual scroll state | Reused across tabs |
| json_render.c | Component value display | Reused in State inspector |

### No New Dependencies
Phase 05 is pure application logic. All visualization (bars, waterfall) is built with ncurses drawing primitives (waddch, ACS_HLINE, A_BOLD, A_REVERSE, etc.).

## Architecture Patterns

### Current Tab Layout (4 tabs)
```
tab_system.c tab_defs[4]:
  [0] "Overview"    -> tab_overview  (ENDPOINT_STATS_WORLD | ENDPOINT_QUERY)
  [1] "ECS"         -> tab_ecs       (ENDPOINT_QUERY | ENDPOINT_ENTITY | ENDPOINT_COMPONENTS | ENDPOINT_STATS_PIPELINE)
  [2] "Performance" -> placeholder
  [3] "State"       -> placeholder
```

### Target Tab Layout (4 tabs)
```
tab_system.c tab_defs[4]:
  [0] "Overview"    -> tab_overview    (ENDPOINT_STATS_WORLD | ENDPOINT_QUERY)
  [1] "CELS"        -> tab_cels        (ENDPOINT_QUERY | ENDPOINT_ENTITY | ENDPOINT_COMPONENTS)
  [2] "Systems"     -> tab_systems     (ENDPOINT_QUERY | ENDPOINT_ENTITY | ENDPOINT_STATS_PIPELINE)
  [3] "Performance" -> tab_performance (ENDPOINT_STATS_WORLD | ENDPOINT_STATS_PIPELINE)
```

### Pattern 1: Tab Extraction (Systems from ECS)
**What:** The current tab_ecs.c contains both CELS-C tree navigation AND systems-specific logic (phase grouping, pipeline enrichment, system detail inspector, pipeline viz, systems summary, cross-navigation to entities). The Systems section must be extracted into its own tab_systems.c.

**Approach:**
1. Copy the Systems-related code from tab_ecs.c into new tabs/tab_systems.c
2. tab_systems uses the same split_panel + tree_view pattern but with Systems section only
3. The tree_view_t in tab_systems only shows System entities grouped by phase (reusing the existing phase sub-header mechanism)
4. The inspector in tab_systems shows system detail (reusing draw_system_detail and draw_pipeline_viz patterns)
5. Cross-navigation "Go to Entity" from system detail jumps to CELS tab (tab index 1)
6. tab_ecs.c becomes tab_cels.c -- removes ENTITY_CLASS_SYSTEM from its tree view and drops ENDPOINT_STATS_PIPELINE

**Key insight:** This is primarily a code *move*, not new code. The tree_view and display_row_t structures already support single-section views. The tab_systems tree_view would show only one section (Systems) with phase sub-headers.

### Pattern 2: State Section in CELS Tab
**What:** Add State as the "S" section in the CELS-C tree (between Lifecycles and Components).

**Architecture challenge:** CEL_State() creates `static` C variables, NOT Flecs components. The state_storage map in CELS_Context stores binary blobs (void*) keyed by integer ID with no type information or field names. There is no REST endpoint to read state values.

**Practical approach:**
- Add a new entity_class_t: reorder or repurpose to insert State between Lifecycle and System positions
- Actually, looking at the CONTEXT.md decision: CELS tab sections are "Compositions, Entities, Lifecycles, State, Components" (CELS-C). The current enum is: COMPOSITION(C), ENTITY(E), LIFECYCLE(L), SYSTEM(S), COMPONENT(C). Systems are being removed to their own tab. So the new enum needs: COMPOSITION(C), ENTITY(E), LIFECYCLE(L), STATE(S), COMPONENT(C).
- State entities can be identified by: entities with names matching `*State` or entities that have state-related tags/components. Alternatively, use a classification heuristic similar to Lifecycle detection.
- The split-panel inspector for a selected state entity shows its component values (from /entity/<path> detail), with change highlighting.

**Change highlighting approach:**
- Store the previous poll's entity_detail JSON values for the currently selected state entity
- On each poll, compare current vs previous values
- When a value differs, set a `flash_expire_ms` timestamp (CLOCK_MONOTONIC + 2000ms)
- During render, check if current time < flash_expire_ms and apply A_BOLD + color if still flashing

### Pattern 3: Performance Tab Waterfall
**What:** Full-width view (no split panel) showing systems grouped by phase with proportional timing bars.

**Data source:** Already parsed by `json_parse_pipeline_stats()` into `system_registry_t`. Each `system_info_t` has `time_spent_ms`, `phase` (filled by enrichment), and `name`.

**Rendering approach:**
```
Performance
----------------------------------------------------------
  OnLoad
    SystemA     |=====|              0.12ms
    SystemB     |==|                 0.05ms
  OnUpdate
    InputSystem |================|   0.45ms
    PhysicsSystem|==========|       0.28ms
  OnStore
    RenderSystem|========================|  0.72ms
----------------------------------------------------------
  Frame: 1.62ms | FPS: 60.1 | 12 systems
```

**Bar width calculation:**
1. Find max_time across all systems in the current frame
2. bar_width = (system_time / max_time) * available_columns
3. Available columns = terminal_width - name_indent - time_label_width - padding
4. Use ACS_HLINE or block characters for the bar, colored by phase

**Phase grouping:** Reuse PHASE_ORDER and phase_color_pair() from tab_ecs.c / tree_view.c. These are already duplicated in both files -- the Performance tab can reference the same constants.

### Pattern 4: Navigation Back-Stack
**What:** Esc returns to previous tab/position after cross-navigation jumps.

**Implementation:**
```c
typedef struct nav_entry {
    int tab_index;              // which tab was active
    int scroll_cursor;          // cursor position in that tab's tree
    char *selected_entity_path; // entity that was selected (if any)
} nav_entry_t;

// Fixed-depth stack (4-8 entries sufficient)
#define NAV_STACK_MAX 8
typedef struct nav_stack {
    nav_entry_t entries[NAV_STACK_MAX];
    int top;  // -1 = empty
} nav_stack_t;
```

**When to push:** Before any cross-navigation jump (e.g., system detail -> entity, component -> entity).
**When to pop:** On Esc key press, if stack is not empty -- restore tab + cursor.
**When to clear:** On explicit tab switch via number keys (1-4) or Tab key.

### Pattern 5: Auto-Reconnect State Machine
**Current state machine** (in http_client.c):
```
Status 200 -> CONNECTED
Was CONNECTED + fail -> RECONNECTING
Was DISCONNECTED/RECONNECTING + fail -> DISCONNECTED
```

**Problem:** The transition from RECONNECTING to DISCONNECTED happens immediately on the next failed poll. There is no retry behavior -- it drops to DISCONNECTED and stays there. The visual display shows "Disconnected" rather than continuing to show "Reconnecting..."

**Fix:** Change the state machine so that once CONNECTED, failures always go to RECONNECTING (not DISCONNECTED). The app continues polling normally (it already does -- the POLL_INTERVAL_MS timer runs regardless), so "auto-reconnect" is really just "don't give up displaying Reconnecting status." When a poll succeeds again, it transitions back to CONNECTED.

```c
connection_state_t connection_state_update(connection_state_t current, int http_status) {
    if (http_status == 200) {
        return CONN_CONNECTED;
    }
    // Once we've ever connected, always show Reconnecting on failure
    if (current == CONN_CONNECTED || current == CONN_RECONNECTING) {
        return CONN_RECONNECTING;
    }
    // Never connected yet -- stay disconnected
    return CONN_DISCONNECTED;
}
```

This is a one-line change. The current behavior transitions `RECONNECTING -> DISCONNECTED` on the second failed poll. The fix keeps it at `RECONNECTING` indefinitely until a successful response.

### Pattern 6: Configurable Refresh Interval
**Current:** `#define POLL_INTERVAL_MS 500` in main.c (hardcoded).

**Approach:** Move to `app_state_t` as a mutable field. Add command-line flag `-r <ms>` and/or runtime toggle key. The main loop already uses `POLL_INTERVAL_MS` in one place (`if (now - last_poll >= POLL_INTERVAL_MS)`), so replacing with `app_state.poll_interval_ms` is trivial.

### Anti-Patterns to Avoid
- **Do NOT create separate ncurses windows for each bar in the waterfall** -- use a single full-screen window (like tab_overview does) with careful row/col positioning.
- **Do NOT use napms() for flash effects** -- it blocks the event loop. Use timestamp-based checks in the render path.
- **Do NOT store full JSON strings for change detection** -- compare yyjson values structurally or store a hash of the serialized component values.
- **Do NOT add threading for reconnect** -- the existing single-threaded poll loop already retries on each interval.

### Project Structure Changes
```
src/
  tabs/
    tab_ecs.c      -> RENAME to tab_cels.c (or keep and rename internal refs)
    tab_ecs.h      -> RENAME to tab_cels.h
    tab_systems.c  -> NEW (extracted from tab_ecs.c Systems section)
    tab_systems.h  -> NEW
    tab_performance.c -> NEW (replaces placeholder)
    tab_performance.h -> NEW
    tab_overview.c -> UNCHANGED
    tab_overview.h -> UNCHANGED
    tab_entities.c -> DEAD CODE (superseded by tab_ecs.c in Phase 03.1)
    tab_entities.h -> DEAD CODE
    tab_components.c -> DEAD CODE
    tab_components.h -> DEAD CODE
    tab_placeholder.c -> CAN BE REMOVED (no more placeholders)
    tab_placeholder.h -> CAN BE REMOVED
```

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Phase ordering | Custom sort | Existing PHASE_ORDER[] constant | Already defined in tab_ecs.c and tree_view.c |
| Phase colors | New color scheme | Existing phase_color_pair() | Already defined in tree_view.c |
| Split panel layout | Custom window math | Existing split_panel.c | Already handles create/resize/focus/borders |
| Virtual scrolling | Custom scroll logic | Existing scroll.c | Already handles cursor/offset/page/ensure_visible |
| Tree with sections | Custom tree from scratch | Existing tree_view.c | Already has section headers, collapse, DFS traversal |
| JSON display | Custom pretty-printer | Existing json_render.c | Already renders components with colors |
| Entity classification | New heuristics | Existing classify_node() patterns | Already has tag/component/name checks in tab_ecs.c |

**Key insight:** Phase 05 has very little genuinely new code. The tab restructure is mostly moving existing code. The Performance tab is the only truly new visualization. The State section extends existing patterns. The polish items are small changes to existing modules.

## Common Pitfalls

### Pitfall 1: Forgetting to Update Endpoint Bitmasks
**What goes wrong:** New tab_systems and tab_cels have wrong endpoint bitmasks, causing missing data or unnecessary polling.
**Why it happens:** CELS tab no longer needs ENDPOINT_STATS_PIPELINE (that moves to Systems). Systems tab needs it.
**How to avoid:** Verify endpoint bitmasks match each tab's data requirements:
- tab_cels: ENDPOINT_QUERY | ENDPOINT_ENTITY | ENDPOINT_COMPONENTS (no pipeline!)
- tab_systems: ENDPOINT_QUERY | ENDPOINT_ENTITY | ENDPOINT_STATS_PIPELINE
- tab_performance: ENDPOINT_STATS_WORLD | ENDPOINT_STATS_PIPELINE
**Warning signs:** Tab shows "Loading..." when it shouldn't, or polls endpoints it doesn't need.

### Pitfall 2: Duplicate Static Functions After Tab Split
**What goes wrong:** classify_node(), has_tag(), has_component_component(), etc. are currently duplicated in both tab_ecs.c AND tab_entities.c. Splitting tab_ecs into tab_cels + tab_systems creates a third copy.
**Why it happens:** These are `static` functions, not shared. The original tab_entities.c and tab_ecs.c both have copies.
**How to avoid:** Either accept the duplication (it works, these functions are small and stable) or extract shared classification logic into a shared header/source file. Given the project's pattern of static functions per-tab, duplication is acceptable but should have sync comments.
**Warning signs:** Classification behavior differs between tabs.

### Pitfall 3: State Section Entity Classification
**What goes wrong:** No entities get classified as State because the current classification heuristic doesn't have a State class.
**Why it happens:** CEL_State defines static variables, not entities. State entities are actually *Lifecycle* entities in the Flecs world.
**How to avoid:** The State section in CELS-C needs a new classification heuristic. Options:
  - Entities whose names end with "State" (matching the CEL_State naming convention)
  - Entities with specific state-related tags
  - Manual registration of state entity names
  The simplest: check for names ending in "State" similar to how `name_is_lifecycle()` checks for "Lifecycle".
**Warning signs:** State section shows (0) items.

### Pitfall 4: Performance Bar Width Integer Division
**What goes wrong:** Very small system times produce 0-width bars, making them invisible.
**Why it happens:** Integer division truncates to 0 for systems with tiny time_spent_ms relative to the max.
**How to avoid:** Ensure minimum bar width of 1 character for any non-zero time. Use: `bar_width = max(1, (int)((sys_time / max_time) * available_cols))`.
**Warning signs:** Some systems show no bar despite having non-zero time.

### Pitfall 5: Flash Expiry in Event Loop
**What goes wrong:** Flash highlight persists forever or never shows.
**Why it happens:** The event loop runs at 10fps (100ms timeout on getch). If flash_expire_ms is checked only at poll time (500ms), the flash won't look smooth.
**How to avoid:** Check flash expiry in the render path (tui_render or tab draw), not in the poll path. The render path runs every loop iteration (10fps), so 2-second flashes will expire correctly.
**Warning signs:** Flash either lasts too long or isn't visible at all.

### Pitfall 6: Cross-Tab Navigation Stack Corruption
**What goes wrong:** Esc navigates to wrong tab/position.
**Why it happens:** The nav stack stores absolute cursor indices that become invalid after entity_list rebuilds (which happen every poll).
**How to avoid:** Store entity IDs (uint64_t) in nav_entry instead of cursor indices. On pop, search for the entity by ID (same pattern as tree_view's prev_selected_id).
**Warning signs:** Esc jumps to wrong entity or crashes.

### Pitfall 7: Tab Rename Breaking Includes
**What goes wrong:** Compilation failure after renaming tab_ecs to tab_cels.
**Why it happens:** tab_system.c includes tab_ecs.h. CMakeLists.txt references tab_ecs.c.
**How to avoid:** Update ALL references: tab_system.c #include, CMakeLists.txt source list, and any cross-references in comments.
**Warning signs:** Build failures.

## Code Examples

### Waterfall Bar Rendering
```c
// Render a proportional timing bar for one system
static void draw_timing_bar(WINDOW *win, int row, int name_col, int bar_col,
                             int bar_max_width, int time_col,
                             const char *name, double time_ms, double max_time,
                             int phase_color) {
    // System name (indented, phase-colored)
    wattron(win, COLOR_PAIR(phase_color));
    mvwprintw(win, row, name_col, "%-20.*s", 20, name);
    wattroff(win, COLOR_PAIR(phase_color));

    // Proportional bar
    int bar_width = 0;
    if (max_time > 0.0 && time_ms > 0.0) {
        bar_width = (int)((time_ms / max_time) * bar_max_width);
        if (bar_width < 1) bar_width = 1;
    }

    wattron(win, COLOR_PAIR(phase_color) | A_BOLD);
    wmove(win, row, bar_col);
    for (int i = 0; i < bar_width; i++) {
        waddch(win, ACS_HLINE);  // or '=' or block char
    }
    wattroff(win, COLOR_PAIR(phase_color) | A_BOLD);

    // Time label
    wattron(win, COLOR_PAIR(CP_JSON_NUMBER));
    mvwprintw(win, row, time_col, "%.2fms", time_ms);
    wattroff(win, COLOR_PAIR(CP_JSON_NUMBER));
}
```

### State Change Detection
```c
// Store previous component values for change detection
typedef struct state_flash {
    char *entity_path;     // which entity's values we're tracking
    char *prev_json;       // serialized previous component values
    int64_t flash_expire;  // CLOCK_MONOTONIC ms when flash ends (0 = no flash)
} state_flash_t;

// Compare current entity detail with previous snapshot
static bool detect_state_change(state_flash_t *flash,
                                 const entity_detail_t *detail) {
    if (!detail || !detail->components) return false;

    // Serialize current components to string for comparison
    yyjson_write_err err;
    char *current_json = yyjson_val_write(detail->components, 0, NULL);
    if (!current_json) return false;

    bool changed = false;
    if (flash->prev_json && strcmp(flash->prev_json, current_json) != 0) {
        changed = true;
    }

    free(flash->prev_json);
    flash->prev_json = current_json;
    return changed;
}
```

### Navigation Back-Stack
```c
#define NAV_STACK_MAX 8

typedef struct nav_entry {
    int tab_index;
    uint64_t entity_id;  // entity ID for cursor restore (0 = none)
} nav_entry_t;

typedef struct nav_stack {
    nav_entry_t entries[NAV_STACK_MAX];
    int top;  // -1 = empty
} nav_stack_t;

static void nav_push(nav_stack_t *stack, int tab, uint64_t entity_id) {
    if (stack->top < NAV_STACK_MAX - 1) {
        stack->top++;
        stack->entries[stack->top].tab_index = tab;
        stack->entries[stack->top].entity_id = entity_id;
    }
}

static bool nav_pop(nav_stack_t *stack, nav_entry_t *out) {
    if (stack->top < 0) return false;
    *out = stack->entries[stack->top];
    stack->top--;
    return true;
}
```

### Improved Connection State Machine
```c
connection_state_t connection_state_update(connection_state_t current, int http_status) {
    if (http_status == 200) {
        return CONN_CONNECTED;
    }
    // Once connected, always show Reconnecting on failure (silent retry)
    if (current == CONN_CONNECTED || current == CONN_RECONNECTING) {
        return CONN_RECONNECTING;
    }
    // Never connected yet -- stay disconnected
    return CONN_DISCONNECTED;
}
```

### Footer Hint Bar (context-sensitive)
```c
// In tui_render, replace the hardcoded footer text:
static const char *get_footer_hints(const tab_system_t *tabs,
                                     const app_state_t *state) {
    // Context-sensitive hints based on active tab
    switch (tabs->active) {
    case 0:  // Overview
        return "1-4:tabs  q:quit";
    case 1:  // CELS
    case 2:  // Systems
        return "1-4:tabs  jk:scroll  Enter:expand  f:anon  Esc:back  q:quit";
    case 3:  // Performance
        return "1-4:tabs  jk:scroll  q:quit";
    default:
        return "1-4:tabs  q:quit";
    }
}
```

## State of the Art

| Old Approach (Phases 01-04) | New Approach (Phase 05) | Impact |
|----------------------------|------------------------|--------|
| Tab bar: "1:Overview 2:ECS 3:Performance 4:State" | "1:Overview 2:CELS 3:Systems 4:Performance" | Tab restructure |
| Systems inside ECS tab | Systems as own top-level tab | Code extraction |
| Hardcoded footer text | Context-sensitive hint bar | Better UX |
| RECONNECTING -> DISCONNECTED on 2nd fail | RECONNECTING persists until success | Silent retry |
| No back-navigation | Esc pops nav stack | Cross-tab continuity |
| No change highlighting | 2-second flash on value change | State awareness |

**No deprecated or outdated libraries** -- all deps are current.

## Open Questions

1. **State entity detection heuristic**
   - What we know: CEL_State(MenuState) creates static C vars. The entity world has entities like "MainMenuLifecycle" but NOT entities named "MenuState" (state is not registered as Flecs entities).
   - What's unclear: Whether State entities exist in the Flecs world at all. The _register() function just sets ID to 1, it does not create a Flecs entity.
   - Recommendation: During plan execution, poll a running CELS app and inspect the /query response to verify whether state entities appear. If they don't (likely), the State section shows Lifecycle entities instead (which ARE state-related -- lifecycles control state visibility). Document this as a v0.1 limitation. Alternatively, check if state_notify_change creates observable entities.

2. **ENTITY_CLASS_STATE enum insertion**
   - What we know: The current enum is COMPOSITION=0, ENTITY=1, LIFECYCLE=2, SYSTEM=3, COMPONENT=4. Systems must be removed (moved to own tab). State must be inserted at position 3 to spell C-E-L-S-C.
   - What's unclear: Whether renumbering the enum breaks serialized state or just requires a clean rebuild.
   - Recommendation: Since the enum is only used in-memory (not serialized), renumbering is safe. New enum: COMPOSITION=0, ENTITY=1, LIFECYCLE=2, STATE=3, COMPONENT=4. Total ENTITY_CLASS_COUNT drops from 5 to 5 (SYSTEM removed, STATE added).

3. **tab_entities.c and tab_components.c cleanup**
   - What we know: These files were superseded by tab_ecs.c in Phase 03.1 but still exist in the source tree.
   - What's unclear: Whether they're still compiled by CMakeLists.txt.
   - Recommendation: Check CMakeLists.txt and remove dead files if not referenced.

## Sources

### Primary (HIGH confidence)
- Codebase analysis of all 20+ source files in cels-debug/src/
- CELS framework source (cels.cpp, cels.h) for State architecture
- CONTEXT.md user decisions for Phase 05
- STATE.md accumulated decisions from Phases 01-04

### Secondary (MEDIUM confidence)
- ncurses documentation for drawing primitives (A_BOLD, A_REVERSE, ACS_HLINE, waddch)
- yyjson API for yyjson_val_write() serialization (used in change detection)

### Tertiary (LOW confidence)
- Web search for ncurses bar chart patterns (generic patterns, no specific library)

## Metadata

**Confidence breakdown:**
- Tab restructure: HIGH - direct codebase analysis, code is being moved not invented
- State section: MEDIUM - the entity classification heuristic needs runtime verification
- Performance tab: HIGH - data source (system_registry_t) is well understood, rendering is straightforward ncurses
- Navigation polish: HIGH - small changes to well-understood modules
- Auto-reconnect: HIGH - one-line state machine fix, verified by reading current logic
- Configurable refresh: HIGH - one #define replacement

**Research date:** 2026-02-06
**Valid until:** indefinite (codebase-specific research, no external dependency changes)
