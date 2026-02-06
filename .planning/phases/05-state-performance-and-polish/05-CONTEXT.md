# Phase 05: State, Performance, and Polish - Context

**Gathered:** 2026-02-06
**Status:** Ready for planning

<domain>
## Phase Boundary

Complete the debugger by: (1) restructuring tabs from 4 to 4 with new layout (Overview, CELS, Systems, Performance), (2) implementing State as a section inside the CELS tab (the S in CELS-C), (3) building the Performance tab with pipeline waterfall visualization, and (4) polishing auto-reconnect, configurable refresh, and keyboard navigation consistency. This is the final phase of v0.1.

</domain>

<decisions>
## Implementation Decisions

### Tab restructure (ECS → CELS + Systems)
- Rename ECS tab to **CELS**
- Extract Systems section from CELS tab into its own **top-level Systems tab**
- Systems tab shows ALL systems (framework + app), keeps current grouped-by-phase layout with detail inspector and cross-navigation — same as current implementation, just promoted to top-level
- CELS tab sections become: **Compositions, Entities, Lifecycles, State, Components** (CELS-C acronym preserved)
- State section replaces the previously planned separate State tab — State() values live inside CELS tab as the S section
- Final top-level tabs: **Overview, CELS, Systems, Performance** (4 tabs)
- Tab switching via number keys **1-4** (direct access)

### State section (inside CELS tab)
- Split-panel layout: state list on left, selected state's full value + previous value on right
- Each state entry shows: name + current value + last previous value
- **Highlight flash** on value change (~2 seconds bold/color flash on the changed row)
- History depth: just the last change (current value vs previous value)

### Performance tab
- **Full-width timeline/waterfall** view (no split panel, no inspector)
- Systems grouped by execution phase (OnLoad, OnUpdate, OnStore, etc.) with phase headers
- **Proportional bars + numbers**: bar width shows relative time, actual ms value displayed next to each bar
- Data sourced from /stats/pipeline (frame timing, per-system metrics)

### Navigation polish
- Tab switching: number keys 1-4 for direct access
- **Esc goes back** after cross-navigation jumps (returns to previous tab/position)
- **Always-visible footer hint bar**: context-sensitive keybinding hints (like htop), e.g., `←→ tabs | jk scroll | Enter select | q quit`
- j/k scroll, Enter select, Esc back, q quit — consistent across all tabs

### Reconnect & refresh
- **Silent auto-retry** on disconnect — no user action needed
- Header shows status transitions: Connected → Disconnected → Reconnecting → Connected
- Auto-refresh interval configurable (default 500ms), polling only active tab endpoints

### Claude's Discretion
- Exact flash duration and color for state change highlight
- How to structure the waterfall bar rendering within terminal width constraints
- Auto-reconnect retry interval and backoff strategy
- Internal architecture for back-navigation stack (how deep, when to clear)
- How to handle the CELS tab's Systems section removal gracefully in code

</decisions>

<specifics>
## Specific Ideas

- The CELS-C acronym was important enough to restructure — bold first letter of each section name should continue spelling C-E-L-S-C vertically
- Systems tab is essentially the current systems view extracted as-is — minimal new code, mostly wiring
- Performance waterfall is the primary new visualization — grouped by phase like Systems tab but with proportional timing bars

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 05-state-performance-and-polish*
*Context gathered: 2026-02-06*
