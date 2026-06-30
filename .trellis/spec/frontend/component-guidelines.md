# Component Guidelines

GraphScript frontend components are editor controls over backend graph state.
They should make commands easier to issue, not duplicate compiler/EditSession
authority in the browser.

## React Webapp Components

- Keep presentational UI primitives under `webapp/src/components/`.
- Keep graph-specific panels under `webapp/src/panels/`.
- Keep graph canvas behavior under `webapp/src/canvas/`.
- Prefer typed props that receive backend-derived state from `GSState` and
  related API types.
- Do not make components own permanent graph truth; persist graph edits through
  backend command/API calls.

## Canvas Components

- React Flow nodes and edges must reflect backend state and stable identifiers.
- Port compatibility previews must match backend validation rules.
- Source range and diagnostic highlighting must use backend-provided ranges and
  targets, not guessed string offsets.
- Node rendering metadata should be treated as part of the state contract and
  covered by visual tests when changed.

## Legacy LiteGraph UI

`web/index.html` is intentionally compact and imperative. When touching it:

- Keep GUI actions mapped to CLI command strings.
- Register node types from backend `types` state.
- Rebuild the canvas from backend state after command execution.
- Escape user-visible HTML through existing escaping helpers.

## Command Surface Pattern

UI commands should follow this shape:

1. Gather user input.
2. Quote or encode command arguments safely.
3. Execute through `/api/exec` or the shared client helper.
4. Apply returned backend state.
5. Append command output to the command log.

Do not mutate graph state locally and hope the backend catches up later.

## Accessibility And Usability

- Keep command errors visible near the action that caused them.
- Preserve keyboard-usable controls for command entry and common editor actions.
- Do not hide diagnostics in console-only output.
- Large graph interactions should avoid layout shifts that make node selection
  or edge reconnection unreliable.

## Common Mistakes

### GUI-Only Editing

If a user can perform an edit in the GUI but the command log cannot replay it,
the implementation violates CLI-first design.

### Stale Local State

After command execution, render from returned backend state or refetch state.
Do not assume local optimistic edits are authoritative.

### Duplicated Validation

Frontend validation may provide previews, but the compiler/EditSession remains
authoritative. Keep preview rules aligned with backend errors and tests.
