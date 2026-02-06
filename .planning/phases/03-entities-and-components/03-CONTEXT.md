# Phase 03: Entities and Components - Context

**Gathered:** 2026-02-06
**Status:** Ready for planning

<domain>
## Phase Boundary

Browse all entities in a running CELS application, select one, and inspect its component names and values as key-value pairs. A separate Components tab lists all registered component types with entity counts and drill-down. Entity and component data fetched from flecs REST API.

</domain>

<decisions>
## Implementation Decisions

### Entity list layout
- Each entity row shows: name, numeric ID, and component names inline
- Component names truncated after 3-4 with "+N more" suffix when many
- Entity tree with parent-child hierarchy using Unicode box drawing characters (├── / └──)
- Tree nodes collapsible via Enter key (toggle expand/collapse)
- Creation order sorting (natural ECS order, not alphabetical)
- Anonymous entities hidden by default, toggled visible/hidden with `f` key

### Component inspector
- Components grouped by component name as header, fields indented underneath
- Component groups are collapsible (Enter to toggle)
- Nested objects and arrays always rendered as indented tree (no inline shorthand)
- Values live-update each poll cycle while entity is selected

### Navigation model
- Entities tab: side-by-side 40/60 split (left: entity tree, right: component inspector)
- Components tab: side-by-side 40/60 split (left: component types with entity counts, right: entities with selected component)
- Left/right arrows switch focus between panels
- Active panel has bold/highlighted border, inactive panel has dim border
- j/k or up/down arrows scroll within focused panel
- Entity inspector updates instantly as cursor moves through entity list (no Enter required to preview)
- Enter toggles expand/collapse on tree nodes (dedicated key — left/right reserved for panel focus)
- Consistent navigation conventions across both Entities and Components tabs

### Components tab
- Lists all registered component types queried from flecs (entities with EcsComponent)
- Each type shows entity count (how many entities have it)
- Selecting a component type shows list of entities with that component in right panel

### Data fetching
- Two-step fetch: poll entity list (names/IDs), then fetch full component data for selected entity only
- Entity list and component detail refresh at the same poll rate
- Component registry fetched by querying flecs for entities with EcsComponent tag
- Brief footer notification when a selected entity disappears between polls ("Entity X removed")

### Claude's Discretion
- Exact poll interval tuning
- Virtual scrolling implementation for large entity counts
- How to structure the flecs REST queries (exact endpoint paths and query parameters)
- Handling edge cases like entities with zero components

</decisions>

<specifics>
## Specific Ideas

- Pause/play mechanism that feeds back to the CELS runtime to pause/resume the game loop (noted for future phase — not Phase 03)
- Unicode box drawing for tree lines: ├──, └──, │ for clean modern look
- Same 40/60 split ratio and navigation conventions on both Entities and Components tabs for consistency

</specifics>

<deferred>
## Deferred Ideas

- Pause/play debug control with runtime feedback (pausing the actual CELS game loop from the debugger) — future phase, requires bidirectional communication with the runtime
- Entity search/filtering by name — could be valuable but is a new capability

</deferred>

---

*Phase: 03-entities-and-components*
*Context gathered: 2026-02-06*
