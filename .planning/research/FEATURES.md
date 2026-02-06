# Feature Landscape

**Domain:** TUI debugger/inspector for ECS framework (CELS/flecs)
**Researched:** 2026-02-05
**Overall confidence:** HIGH (grounded in flecs source code, existing TUI tools, and Unity ECS precedent)

## Data Source: Flecs REST API Endpoints

Before categorizing features, it is critical to understand what data is actually available from the flecs REST API, since the TUI is a read-only consumer of this data.

**Verified endpoints (from flecs REST source code):**

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/entity/<path>` | GET | Retrieve entity with tags, pairs, component values |
| `/component/<path>` | GET | Retrieve single component value from entity |
| `/query?expr=<expr>` | GET | Execute arbitrary query, return matching entities |
| `/world` | GET | Retrieve all serializable entities in the world |
| `/stats/<category>` | GET | World/pipeline/system statistics with time-series data |
| `/components` | GET | List all registered component types |
| `/queries` | GET | List all registered queries |
| `/tables` | GET | List all archetype tables |
| `/commands/capture` | GET | Start/stop command capture |
| `/commands/frame/<n>` | GET | Get commands from specific frame |

**Entity endpoint options (control what gets serialized):**
- `entity_id` -- numeric entity id
- `values` -- component values (requires reflection data)
- `type_info` -- component schema/type information
- `inherited` -- inherited components from prefabs
- `full_paths` -- full tag/pair/component paths
- `matches` -- matched queries for this entity
- `refs` -- relationship back references

**Query endpoint options:**
- `table` -- serialize full table (all components, not just matched)
- `type_info` -- component schemas
- `field_info` -- query field metadata
- `query_info` -- query term details
- `query_plan` -- query execution plan
- `query_profile` -- profiling info

**Stats data available (from `stats.h`, HIGH confidence):**
- World stats: entity count, component count, table count, frame count, FPS, delta time, frame time, system time, merge time, emit time, rematch time, memory allocation stats, HTTP request stats
- System stats: time_spent per system, matched entity/table counts
- Pipeline stats: system execution order, sync point timing, active system count, rebuild count
- Memory stats: detailed breakdown by subsystem (entities, tables, queries, components, allocators)
- Time-series: 60-datapoint windows at frame/second/minute/hour/day/week granularity

**What the REST API does NOT provide directly:**
- CELS-specific state values (State/SetState/GetState) -- these are CELS abstractions, not flecs primitives. They ARE stored as flecs components and can be queried, but require CELS-aware interpretation.
- System-to-phase mapping labels -- systems have names but their CELS phase grouping is a CELS concept built on top of flecs pipelines
- Change history/diffing -- the API gives snapshots, not diffs. The TUI must compute deltas client-side.
- Log streaming -- no log endpoint; logging is a CELS-level concept, not flecs

**Confidence:** HIGH -- verified by reading flecs source code directly (`rest.c`, `stats.h`)

---

## Table Stakes

Features users expect from a TUI debugger. Missing any of these and the tool feels broken or pointless. These are what make it "minimally useful" -- the bar below which a developer would just use the flecs web explorer instead.

| Feature | Why Expected | Complexity | Data Source | Notes |
|---------|--------------|------------|-------------|-------|
| **Connection status indicator** | User must know if they are seeing live data or stale data | Low | HTTP response codes / timeout | Header bar shows Connected/Disconnected/Reconnecting. Like k9s cluster status. |
| **Tab/view navigation** | Multiple data views need organized access | Low | N/A (UI chrome) | F-keys or number keys for tab switching. Every major TUI (lazygit, k9s, btop) uses panel/tab navigation. |
| **Entity list** | Core ECS primitive -- users need to see what exists | Medium | `GET /query?expr=*` or `GET /world` | Flat list first, tree later. Show entity name + component tags. Like Unity Entity Debugger's entity list. |
| **Component value inspection** | The whole point of a debugger is seeing values | Medium | `GET /entity/<path>?values=true&type_info=true` | Select entity, see its component data as key-value pairs. Requires reflection data registered in flecs. |
| **System list** | Users need to see what runs each frame | Medium | `GET /query?expr=flecs.system.System` + pipeline stats | List system names. Unity Entity Debugger shows systems with per-system time. |
| **Frame timing display** | Need to know if the app is running, how fast | Low | `GET /stats/world` (fps, delta_time, frame_time) | Status bar or dedicated area. Like htop's CPU usage header. |
| **Entity count summary** | Quick health check of the world | Low | `GET /stats/world` (entities.count) | Part of overview/header. btop shows process count similarly. |
| **Keyboard-driven navigation** | TUI users expect keyboard-first interaction | Medium | N/A (UI interaction) | j/k or arrow keys, Enter to expand, Esc to go back. Lazygit's pattern: panel focus + item selection. |
| **Auto-refresh/polling** | Debug data must be live, not manually refreshed | Medium | Periodic HTTP polling | Configurable interval (default ~500ms). Like htop's refresh rate. |
| **Graceful disconnect handling** | App may crash/restart, debugger should survive | Low | HTTP error handling | Auto-reconnect with backoff. Show last-known data greyed out. |

### Why these are table stakes specifically

The flecs web explorer already provides entity browsing, component inspection, query execution, and statistics visualization in a browser. If the TUI cannot at minimum show entities with their component values and systems with timing data, there is zero reason to use it over opening `http://localhost:27750` in a browser. The table stakes are the features that, combined with the terminal advantage (SSH, keyboard-driven, no browser needed, separate window), justify the tool's existence.

---

## Differentiators

Features that make this TUI better than just using the flecs web explorer in a browser. These are the reasons a developer would choose the TUI. Not expected, but valued -- and some are the actual selling points.

| Feature | Value Proposition | Complexity | Data Source | Notes |
|---------|-------------------|------------|-------------|-------|
| **Entity parent-child tree view** | Hierarchical browsing of the entity graph -- more natural than flat lists for game scenes | High | `GET /query?expr=(ChildOf, $p)` to discover hierarchy, then entity lookups | Collapsible tree like a file browser. The flecs explorer has this but TUI tree navigation is faster with keyboard. |
| **Change highlighting** | See what changed since last poll -- values that changed glow/flash/color differently | Medium | Client-side diff of previous vs current values | This is something the web explorer does NOT do. Killer feature for real-time debugging. Inspired by how btop highlights changing values. |
| **State tab (CELS-specific)** | Purpose-built view for CELS State() values with change tracking | Medium | `GET /query` for state components + client-side history | CELS-specific value add. The web explorer knows nothing about CELS state semantics. |
| **System execution by phase** | Systems grouped by CELS phase (Phase_Input, Phase_Physics, Phase_Render) with execution order | Medium | Pipeline stats + query for system entities with phase relationships | CELS-specific grouping. The explorer shows systems but not with CELS phase semantics. |
| **Per-system execution time** | See which systems are slow, identify bottlenecks | Medium | `GET /stats/` with system stats (requires FlecsStats import + system time measurement) | System time measurement adds overhead, document this trade-off. |
| **Overview dashboard** | Single-screen summary: entity count, system count, FPS, frame time, connection uptime | Low | Aggregate from multiple stats endpoints | Like btop's header region or k9s's pulse view. Quick health check without switching tabs. |
| **Filter/search in lists** | Type to filter entities by name or component | Medium | Client-side filtering of cached data | Every good TUI has `/` for search. lazygit, k9s, htop all support filtering. |
| **SSH/remote debugging** | Debug a headless server or remote machine via SSH | Free | Works inherently (TUI over SSH) | This is the primary differentiator vs browser-based explorer. No browser needed, no port forwarding for GUI. |
| **Separate window from game** | Game window stays clean, debug info in another terminal | Free | Works inherently (separate process) | Key selling point documented in existing CELS debug research. |
| **Configurable poll rate** | Adjust update frequency to balance responsiveness vs overhead | Low | Command-line arg or runtime toggle | The web explorer polls at its own rate. TUI users want control. |
| **Component type registry view** | Browse all registered component types and their schemas | Medium | `GET /components` + type_info | See what types exist without looking at code. |
| **Vim-style keybindings** | j/k/h/l navigation, / for search, g/G for top/bottom | Low | N/A (input handling) | Terminal users expect vim keys. lazygit does this well. |

### What makes this better than the flecs explorer

1. **Terminal native** -- works over SSH, in tmux splits, on headless servers. No browser or GUI needed.
2. **CELS-aware** -- understands State, Compositions, phase grouping. The explorer is generic flecs, this is purpose-built for CELS.
3. **Change detection** -- highlighting changed values between polls is a feature the web explorer lacks.
4. **Keyboard speed** -- navigating a TUI with vim keys is faster than clicking through a web UI for experienced developers.
5. **Resource efficiency** -- no browser tab consuming 200MB+ of RAM for debug tooling.

---

## Anti-Features

Things to deliberately NOT build in v0.1. Each is either premature, risky, or contrary to the tool's purpose.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Live editing of component values** | Adds significant complexity (PUT requests, input validation, undo). Risk of corrupting running application state. Prove read-only value first. | Read-only inspection only. Plan editing for v0.2 after read-only is validated. |
| **Custom watch expressions** | Requires expression parsing, evaluation engine, variable tracking. Scope creep. | Let users browse to any entity/component manually. Watch expressions are a v0.2+ feature. |
| **ASCII graphs/sparklines** | Charting in terminal is fiddly, hard to get right, and not the core value proposition for v0.1. | Show numeric values with change deltas. Add graphs in a later version when the data pipeline is solid. |
| **Log streaming** | Flecs REST API has no log endpoint. Would require CELS-side log capture and a custom endpoint or separate protocol. Significant server-side work. | Use existing stdout debug output. Plan log integration for when embedded mode is built. |
| **Breakpoint/pause/step** | Requires bidirectional protocol and CELS runtime modifications. The REST API has no pause mechanism. Massive scope increase. | Read-only observation only. Pause/step requires embedded mode (v0.3+). |
| **Embedded mode (in-process)** | Requires threading, shared memory, tight coupling to CELS runtime. Should prove HTTP approach first. | HTTP polling only for v0.1. Embedded mode is a separate future milestone. |
| **Custom developer tabs** | Extensibility architecture is premature before the core tabs work. | Build the fixed 6 tabs well. Design tab system to be extensible but don't expose the extension API yet. |
| **Mouse support** | ncurses mouse support is platform-inconsistent and adds complexity to input handling. Keyboard-first is the right approach. | Keyboard-only in v0.1. Mouse is a nice-to-have later. |
| **Multi-world support** | CELS currently uses a single flecs world. Multi-world adds complexity for no current user need. | Connect to one world at localhost:27750. |
| **Configuration file** | Premature. Don't know what users want to configure yet. | Command-line arguments only (host, port, poll rate). |
| **Color themes/skinning** | Cosmetic complexity that doesn't add debug value. | Use a sensible default color scheme with ncurses color pairs. |
| **Query builder/REPL** | The web explorer already has a query REPL. Duplicating it in TUI is wasteful for v0.1. | Show pre-defined views. Advanced users can use the web explorer for ad-hoc queries. |

---

## Feature Dependencies

Understanding what must be built before what. This directly informs phase ordering.

```
Connection Layer (HTTP client + JSON parsing)
    |
    +-- Entity List (needs query endpoint)
    |       |
    |       +-- Entity Tree View (needs entity hierarchy queries)
    |       |
    |       +-- Component Value Inspection (needs entity detail endpoint)
    |               |
    |               +-- Change Highlighting (needs previous-value cache)
    |
    +-- System List (needs query + pipeline info)
    |       |
    |       +-- Phase Grouping (needs CELS phase knowledge)
    |       |
    |       +-- Per-System Timing (needs FlecsStats import)
    |
    +-- Stats/Performance (needs stats endpoint)
    |       |
    |       +-- Overview Dashboard (aggregates stats)
    |       |
    |       +-- Frame Timing Display (subset of stats)
    |
    +-- State Tab (needs CELS state component queries)
            |
            +-- State Change History (needs client-side history buffer)

Tab System + ncurses Layout
    |
    +-- All views above render into tabs
    |
    +-- Filter/Search (overlays on any list view)
```

Key dependency insight: The connection layer (HTTP + JSON) is the foundation everything depends on. The tab/layout system is the rendering foundation. These two must be built first, in parallel if possible.

---

## MVP Recommendation

For v0.1, prioritize in this order:

### Must Have (ship-blocking)

1. **Connection layer** -- HTTP client polling flecs REST API at configurable interval
2. **ncurses TUI shell** -- tab bar, status bar, content area, keyboard handling
3. **Overview tab** -- dashboard with entity count, FPS, frame time, connection status
4. **Entities tab** -- flat entity list with component names, select-to-inspect component values
5. **Systems tab** -- system list (ideally grouped by phase)
6. **Connection status indicator** -- visible in header/footer at all times

### Should Have (significant value, build if time allows)

7. **Components tab** -- component type registry showing registered types
8. **Performance tab** -- frame time, system time, entity count from stats endpoint
9. **Entity parent-child tree** -- tree view instead of flat list
10. **Change highlighting** -- color changed values differently
11. **Filter/search** -- `/` to filter entity and system lists

### Defer to Post-MVP

12. State tab -- CELS-specific, needs more design work on how to query state components
13. Watch expressions -- needs expression parser
14. Log streaming -- needs server-side work
15. Pause/step/breakpoints -- needs embedded mode
16. Live editing -- needs validation and undo
17. ASCII graphs -- nice but not core value

---

## Lessons from Existing TUI Tools

### From htop/btop (system monitors)

- **Header region for summary stats** -- always visible, shows the most important numbers (CPU/RAM = entity count/FPS for us)
- **Toggle-able regions** -- btop lets you hide/show regions with number keys. Consider this for panes within a tab.
- **Color coding for severity** -- red for high CPU = red for slow systems or low FPS
- **Refresh rate matters** -- too fast wastes CPU on the debugger, too slow misses changes. Default 500ms is good.

### From lazygit (git TUI)

- **Panel-based layout** -- multiple panels visible simultaneously, focus follows keyboard
- **Single-key actions** -- no modifier keys needed for common operations. `Enter` to expand, `q` to quit, `?` for help.
- **Context-sensitive footer** -- bottom bar shows available actions for the current selection
- **Speed above all** -- TUI users chose the terminal because they want speed. Never make them wait.

### From k9s (Kubernetes TUI)

- **Resource views** -- each Kubernetes resource type gets its own view. Maps to our tabs.
- **Connection to remote API** -- k9s connects to a Kubernetes API server, just like we connect to flecs REST. Their connection handling (reconnect, timeout, status display) is directly applicable.
- **Column customization** -- users can configure what columns appear. Nice-to-have for entity list.
- **Pulse view** -- high-level cluster health overview. Maps to our Overview tab.

### From Unity Entity Debugger (ECS inspector)

- **System list with frame time** -- shows how much time each system takes per frame, with checkboxes to enable/disable systems
- **Entity filtering by component** -- select a component type, see only entities that have it
- **Component groups** -- shows which archetypes/tables entities belong to
- **World selector** -- choose which world to inspect (not needed for v0.1 since CELS uses one world)

### From flecs Explorer (web-based)

- **Entity tree browser** -- hierarchical entity view with expand/collapse
- **Component inspector** -- shows component values with type info
- **Query REPL** -- execute arbitrary queries (defer for TUI)
- **Statistics page** -- performance graphs (simplified version for TUI)
- **Script editor** -- edit flecs scripts live (not applicable to TUI v0.1)
- **Drag-to-change values** -- inspector value editing (defer, TUI equivalent would be inline editing)

**Key insight:** The flecs explorer is our competition AND our reference. Every feature we build must either (a) match what the explorer does but in a terminal, or (b) offer something the explorer cannot (SSH access, CELS-specific views, change highlighting, keyboard speed).

---

## Sources

### HIGH Confidence (verified from source code)
- Flecs REST API endpoints: verified from `build/_deps/flecs-src/src/addons/rest.c` (line 1992-2060)
- Flecs stats structures: verified from `build/_deps/flecs-src/include/flecs/addons/stats.h`
- Flecs REST API documentation: `build/_deps/flecs-src/docs/FlecsRemoteApi.md`
- CELS REST integration: verified in `src/cels.cpp` (line 509-514, EcsRest on port 27750)
- CELS debug infrastructure: verified in `src/cels.cpp` (debug observers, state change output)

### MEDIUM Confidence (official documentation + multiple sources)
- [Flecs Remote API docs](https://www.flecs.dev/flecs/md_docs_2FlecsRemoteApi.html) -- official REST API reference
- [Flecs Explorer](https://github.com/flecs-hub/explorer) -- web-based UI feature reference
- [Unity Entity Inspector](https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/editor-entity-inspector.html) -- ECS debugger feature reference
- [Flecs v4.0 announcement](https://ajmmertens.medium.com/flecs-v4-0-is-out-58e99e331888) -- explorer v4 features
- [Flecs Stats addon](https://www.flecs.dev/flecs/group__c__addons__stats.html) -- statistics API reference

### LOW Confidence (community/general patterns)
- [k9s](https://k9scli.io/) -- Kubernetes TUI design patterns
- [lazygit](https://github.com/jesseduffield/lazygit) -- git TUI design patterns
- [btop](https://terminaltrove.com/btop/) -- system monitor TUI patterns
- [awesome-tuis](https://github.com/rothgar/awesome-tuis) -- TUI tool catalog
- [Unity ECS Debugging](https://docs.unity3d.com/Packages/com.unity.entities@0.9/manual/ecs_debugging.html) -- ECS debugging feature patterns
