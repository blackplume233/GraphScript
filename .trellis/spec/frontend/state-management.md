# State Management

The backend `EditSession` is the source of truth for graph state. Browser state
is a projection used for interaction, rendering, diagnostics, and command logs.

## Source Of Truth

| State | Owner |
| --- | --- |
| Module, graphs, nodes, params, links | C++ `EditSession` |
| Validation and scope semantics | Compiler, Validator, EditSession |
| JSON editor snapshot | C++ server / `state_to_json()` |
| Visual layout and selected UI controls | Frontend |
| Command history/replay | CLI command log and UI command log |

## Command Replay Contract

Every graph edit exposed by the UI must be replayable as a command. This applies
to node creation, deletion, rename, links, flow edges, annotations, source patch
operations, and metadata movement.

If a UI feature cannot be expressed as a command:

1. Add or design the backend command/API contract first.
2. Define validation and error behavior.
3. Add backend and frontend tests.
4. Then wire the UI.

## State Refresh Pattern

- Fetch initial state from `/api/state`.
- Execute edits through `/api/exec` or shared API client helpers.
- Render from returned state when provided.
- Refetch state after operations whose response does not include a complete
  state snapshot.
- Keep UI-only selection, search text, panel open/closed state, and viewport
  position separate from backend graph truth.

## Error And Diagnostic State

- Preserve backend diagnostics and target metadata for highlighting.
- Do not convert structured diagnostics into plain strings too early.
- UI previews may warn before command execution, but backend validation decides
  whether the edit is accepted.

## Good/Base/Bad Cases

- Good: dragging or reconnecting an edge issues a replayable command and then
  redraws from backend state.
- Base: filter text or current panel tab stays local because it is purely UI.
- Bad: browser mutates a node pin list locally without backend command support.
