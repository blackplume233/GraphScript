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
- Autosave must reuse the same backend-owned edit paths. Graph edits should
  execute CLI commands first, then debounce `save` only when returned state is
  dirty and has a `file_path`. Source edits should debounce diagnostics plus
  `applySourcePatch`/`applySource` before any `save`; never write browser text
  directly to disk.
- Unsaved Source diagnostics with relative imports should derive `base_dir` by
  stripping the relative import path from a loaded import's `normalized_path`.
  Do not use the declaration file's parent directory as the resolver root for
  `import "tests\\fixtures\\x.d.gs"`; that resolves to
  `tests/fixtures/tests/fixtures/x.d.gs` and produces false
  `Import file not found` diagnostics.
- Saved Source diagnostics may still need an explicit `base_dir` when a source
  file imports a short declaration name, such as `import "ue_core.d.gs"`, while
  the session loaded the declaration from an include root such as
  `presets/ue_core.d.gs`. In that case, match source import paths against
  loaded declaration `source_file` values from `declared_types`, `types`, pins,
  fields, and schemas, then pass the inferred declaration root as `base_dir`.
  Do not rely on `source_path` alone when the declaration root differs from the
  `.gs` file's parent directory.
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
- Good: typing in Source auto-applies after diagnostics pass; diagnostics errors
  keep the buffer local and visible instead of forcing a broken session update.
- Base: filter text or current panel tab stays local because it is purely UI.
- Bad: browser mutates a node pin list locally without backend command support.
- Bad: browser autosave bypasses `/api/source_patch`, `/api/source`, or
  `/api/exec save` and writes a file from local component state.
