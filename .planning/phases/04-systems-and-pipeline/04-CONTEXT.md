# Phase 04: Systems and Pipeline - Context

**Gathered:** 2026-02-06
**Status:** Ready for planning

<domain>
## Phase Boundary

Populate the Systems section of the CELS-C tree with live system data from the Flecs REST API. Systems are grouped by execution phase with enabled/disabled status. Pipeline visualization shows phase execution ordering with timing data. Cross-navigation from system inspector to entity tree. State tab, Performance tab, and auto-reconnect are separate phases.

</domain>

<decisions>
## Implementation Decisions

### System grouping & tree structure
- Systems grouped by execution phase under the CELS-C "Systems" header (sub-headers like OnUpdate, OnValidate, etc.)
- Phase sub-headers are collapsible (Enter toggles), start expanded so all systems are visible
- Phase groups ordered by Flecs execution order (OnLoad -> PostLoad -> PreUpdate -> OnUpdate -> OnValidate -> PreStore -> OnStore -> PostFrame)
- Empty phase groups are hidden — only show phases that have registered systems

### Pipeline visualization
- Lives in the inspector panel (right side) — shown when cursor is on a phase group header
- Vertical flow style with │ and ↓ connectors, phases listed top-to-bottom in execution order
- System counts shown inline per phase
- Timing data per phase shown if available from /stats/pipeline (e.g., "OnUpdate: 2.1ms")
- Currently selected phase group is highlighted in the pipeline view
- When cursor is on "Systems" CELS-C header (not inside), inspector shows summary stats instead of pipeline

### System detail & status display
- Each system in the tree shows: name, phase tag, and enabled/disabled status
- Phase tags are color-coded — each execution phase gets a distinct color
- Disabled systems shown in dimmed text, enabled in normal color
- Entity match count shown per system in the tree (e.g., "MovementSystem [OnUpdate] (42)")

### Inspector panel behavior
- **System selected:** Full metadata (phase, enabled, match count, query/filter expression, component access list) plus scrollable list of matched entities
- **Matched entity interaction:** Enter on a matched entity cross-navigates to that entity in the Entities section of the CELS-C tree
- **Phase group header selected:** Pipeline overview with the selected phase highlighted
- **"Systems" CELS-C header selected:** Summary stats — total system count, enabled/disabled counts, phase distribution

### Claude's Discretion
- Exact color assignments for phase tags (within the existing CP_* color pair system)
- Pipeline flow diagram character art and spacing
- How to fetch system data from Flecs REST API (which endpoints, query format)
- How cross-navigation between Systems and Entities sections is implemented internally
- Handling of systems with no query/filter expression

</decisions>

<specifics>
## Specific Ideas

- Pipeline visualization should feel like a vertical flow diagram — phases connected by lines/arrows, not just a list
- Cross-navigation from matched entities in system inspector to the Entities section ties the ECS tab together as a unified browser
- Phase sub-headers reuse the same collapsible pattern as CELS-C top-level headers (consistent UX from Phase 03.1)

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 04-systems-and-pipeline*
*Context gathered: 2026-02-06*
