# Hook Guidelines

This file applies to the React/TypeScript `webapp/`. The legacy `web/` editor
does not use React hooks.

## Hook Boundaries

- Use hooks for UI lifecycle, local interaction state, memoized projections, and
  event handlers.
- Do not use hooks to create an alternate graph model independent of backend
  `GSState`.
- Keep network calls in shared API helpers or clearly named app-level effects.
- Keep command execution paths explicit so tests can replay user actions.

## State Effects

Effects that fetch or refresh backend state must handle:

- loading state,
- command/API errors,
- stale responses after rapid user actions,
- cleanup for timers or subscriptions.

When a command changes graph state, prefer updating from the command response or
refetching `/api/state` over locally patching a partial graph.

## Derived Data

Use memoization for expensive derived canvas data, but derive it from typed
backend state. Examples:

- node/edge lists for React Flow,
- diagnostic highlight maps,
- source range lookup tables,
- palette filters.

Derived data must be invalidated when the underlying state version or relevant
state fields change.

## Do Not

- Do not bury API contract assumptions inside anonymous effects.
- Do not keep persistent graph truth in component-local state.
- Do not let hook ordering depend on selected graph/block data.
- Do not ignore command failures because a preview looked valid.
