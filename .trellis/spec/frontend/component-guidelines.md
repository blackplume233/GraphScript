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
- The graph canvas should follow the MothCocoon FlowGraph/Unreal-style editor
  language: dark low-contrast grid, compact dark nodes, colored title strips,
  exec pins as directional triangles, data pins as small diamonds, and smooth
  Bezier wires without arrowheads.
- Canvas layering must keep comment boxes behind editable graph elements, wire
  paths above node backgrounds so endpoints remain visible at pins, selected or
  diagnostic wires above ordinary wires, and transient overlays such as context
  menus above the graph. Keep pins visually stronger than wires. Avoid
  persistent overlays like minimaps that cover editable graph content.
- Selection and active wire feedback should use the FlowGraph yellow/orange
  highlight family rather than the application primary blue, so node selection,
  multi-select outlines, reconnect affordances, and highlighted wires read as a
  single editor interaction system.

## Dockable Workbench Components

- Docked panels under `webapp/src/workbench/` must render the latest React
  state through a dynamic boundary such as Context. Do not let Dockview panel
  component factories close over first-render `ReactNode` values; that freezes
  panels like Canvas at the initial backend state.
- Persist dock layout as UI-only state. Layout JSON in `localStorage` must not
  become graph truth and must be recoverable by falling back to the default
  workbench layout.
- The default workbench layout should open Source on the left as an active
  authoring panel, with Palette available as a sibling tab. Diagnostics,
  Graph Text, and Console can remain auxiliary bottom tabs.
- Panel activation is part of interoperation. Source-range actions should bring
  Source forward; command-log assertions and command entry should bring Console
  forward; diagnostics assertions should bring Diagnostics forward.
- Tests for docked panels must interact with tabs explicitly when a panel may be
  inactive. Hidden Dockview panels may be unmounted, so visible text selectors
  should not assume all panels exist in the DOM at once.

## Source Editor Components

- Source editing may use Monaco, but source authority remains the backend
  diagnostics/source APIs. Monaco should call existing `onSourceChange`,
  `onApplySource`, `onRevertSource`, and source-range focus callbacks rather
  than inventing a browser-only source model.
- Source should default to Monaco edit mode and auto-sync from backend
  `/api/emit` when there are no local source edits. Auto-sync must not overwrite
  local edits, synced patch/snapshot results, or import resolver metadata.
- Source range focus must translate backend `{ line, column }` ranges into
  Monaco selections and reveal the range in view. Preview mode must continue to
  highlight backend-provided ranges without guessing semantic targets.
- E2E tests that edit Monaco should use an explicit test bridge or Monaco-aware
  keyboard interaction instead of textarea-only `fill()` assumptions.

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
