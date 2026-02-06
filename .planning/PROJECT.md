# CELS Debug

## What This Is

CELS Debug is a terminal-based (TUI) debugger that connects to a running CELS application via the flecs REST API (localhost:27750) to provide real-time inspection of entities, components, systems, state, and performance. It gives developers full visibility into their ECS world without leaving the terminal.

## Core Value

Real-time visibility into a running CELS application — entities, state, systems, and performance — in a single terminal window.

## Current Milestone: v0.1 Debug Inspector

**Goal:** Read-only TUI inspector with tab-based views for inspecting a running CELS/flecs application over REST API

**Target features:**
- Tab-based TUI with ncurses (Overview, Entities, Components, Systems, State, Performance)
- HTTP polling against flecs REST API (localhost:27750)
- Entity tree with parent-child hierarchy
- Component value inspection
- System execution visibility (phases, ordering)
- State change tracking
- Frame timing / performance metrics
- Extensible tab system for future custom developer tabs

## Requirements

### Validated

(None yet — ship to validate)

### Active

- [ ] ncurses-based TUI with tab navigation
- [ ] HTTP client for flecs REST API polling
- [ ] Overview tab: dashboard summary of entity count, active systems, frame timing
- [ ] Entities tab: entity tree with parent-child relationships, expandable component views
- [ ] Components tab: component type registry, which entities have which components
- [ ] Systems tab: system list grouped by phase, execution order
- [ ] State tab: CELS state values with change highlighting
- [ ] Performance tab: frame timing, system execution times, entity count over time
- [ ] Configurable polling interval
- [ ] Connection status indicator (connected/disconnected/reconnecting)

### Out of Scope

- Live editing of component values — read-only for v0.1, future milestone
- Custom developer tabs — extensibility architecture planned but not in v0.1
- Embedded mode (in-process) — start with standalone HTTP polling, add later
- State change triggers — no modifying state from the debugger in v0.1
- GUI version — terminal only

## Context

**Parent project:** CELS (libs/cels/) — declarative ECS framework with flecs backend
**Data source:** flecs REST explorer API at localhost:27750
**flecs REST API:** Provides entity queries, component inspection, system info, pipeline data
**Existing debug output:** CELS already prints entity create/destroy and state changes to stdout via FakeEngine observers

**Architecture:**
- Standalone C executable using ncurses for rendering
- HTTP client polls flecs REST API endpoints
- Future: embedded mode with direct memory access to CELS_Context
- Tab system designed for extensibility (custom developer tabs in future versions)

## Constraints

- **Language**: C99 (consistent with CELS public API)
- **TUI Library**: ncurses
- **Connection**: HTTP to flecs REST API (localhost:27750)
- **Platform**: Linux first (ncurses available everywhere)
- **Build System**: CMake (integrates with CELS build)

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| ncurses for TUI | Battle-tested, widely available, low-level control | — Pending |
| HTTP polling first | Simple, works now, no CELS changes needed | — Pending |
| Tab-based layout | Clean separation of concerns, extensible for custom tabs | — Pending |
| Read-only v0.1 | Simpler to build, lower risk, prove value before adding editing | — Pending |
| Inside CELS repo | Tightly coupled to CELS concepts, shares build system | — Pending |

---
*Last updated: 2026-02-05 — Initial project setup for v0.1*
