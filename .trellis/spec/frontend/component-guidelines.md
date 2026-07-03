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
- React Flow `Handle` elements should be the connection hit target. Render
  custom pin glyphs as pointer-events-none siblings or children that do not
  replace the handle's measured DOM box, and call `useUpdateNodeInternals()`
  when dynamic pins change so edge endpoints stay aligned.
- Selection and active wire feedback should use the FlowGraph yellow/orange
  highlight family rather than the application primary blue, so node selection,
  multi-select outlines, reconnect affordances, and highlighted wires read as a
  single editor interaction system.
- Match Blueprint-style wire editing shortcuts: Alt+click on an edge removes
  that connection through the same replayable CLI delete command as the visible
  edge delete button and Delete/Backspace.
- Graph interface facts that are not persisted nodes, such as `context.start`,
  `context.done`, `context.result`, and bare graph parameters used by
  `bind(message, node.pin)`, may be rendered as synthetic React Flow nodes, but
  synthetic node IDs must never leak into backend edit payloads. Convert visual
  IDs back to backend endpoints before creating, deleting, reconnecting, or
  logging edges.
- Render graph interface facts as semantic helper nodes, not as misleading
  grouped object nodes. Event blocks expose an `Event Entry` node with only
  `context.start`; event views must not show `context.done` or `context.result`.
  Function blocks expose separate `Function Entry` and `Function Return` nodes,
  where return owns `context.done` and `context.result`. Bare graph parameters
  render as one `Parameter Getter` node per parameter instead of a grouped
  `Graph Inputs` node.
- Synthetic graph interface nodes should remain draggable as UI-only layout
  helpers. Moving event/function entry, function return, or parameter getter
  nodes updates local canvas position only and must not issue `annotate node`,
  `remove_node`, or other backend commands. Keep them on the same node color
  system as ordinary graph nodes. Flow input exec pins should use the same
  filled exec glyph treatment as flow outputs, not a black or hollow block.
- Real graph nodes without `Position` annotations must still receive a stable,
  readable canvas fallback. Prefer deriving columns from the active
  event/function flow topology, use data links to place otherwise data-only
  nodes near their consumers, and put unused nodes in a separated fallback
  grid. Existing `Position` annotations remain authoritative, and dragging a
  real node should still persist through `annotate node <instance> Position ...`.
- Active event/function views should not render every graph node. Show only
  real nodes referenced by that block's flow or data links, plus the needed
  synthetic entry/return and parameter getter nodes. Nodes used only by other
  blocks should not appear as disconnected islands in the current block view.
- Real nodes remain graph-owned even when the canvas is filtered to one
  event/function block. If multiple blocks reference the same real node, render
  that same node in each referencing block with a visible shared marker; do not
  duplicate it into block-local copies, and do not delete the node just because
  the active block no longer references it. Edge rendering still remains
  block-local so another block's connections do not appear in the current view.
- Do not model cross-event reuse by reusing the same exec-output chain in
  multiple blocks. A real node may be shared as an exec target or data consumer,
  but if its output pin drives another node in more than one block the backend
  duplicate-connection/fan-out diagnostics will reject the graph. Use distinct
  node instances for re-entrant event chains, or add a backend-supported
  call-like abstraction before exposing that pattern in the canvas. Regression
  tests should assert both the rendered shared marker and zero backend
  diagnostics for the fixture.
- Parameter getters should render as compact Blueprint-style capsules near
  their data-link consumers, not as a large grouped input node or a distant
  full card. Per-bind duplicate getter capsules are acceptable UI-only helpers
  as long as their visual IDs map back to the same source parameter name for
  replayable commands.

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
- Dockview chrome that is visually decorative, such as watermarks in empty
  content containers, must not intercept pointer events over the Canvas. When a
  canvas E2E drag fails, inspect `document.elementFromPoint()` at the pin center
  before changing React Flow logic.

## Source Editor Components

- Source editing may use Monaco, but source authority remains the backend
  diagnostics/source APIs. Monaco should call existing `onSourceChange`,
  `onApplySource`, `onRevertSource`, and source-range focus callbacks rather
  than inventing a browser-only source model.
- Source should default to Monaco edit mode and auto-sync from backend
  `/api/emit` when there are no local source edits. Auto-sync must not overwrite
  local edits, synced patch/snapshot results, or import resolver metadata.
- External source updates must patch the Monaco model by minimal range edit
  rather than passing a fully controlled `value` that resets the whole buffer.
  Preserve scroll/selection where possible and suppress the resulting model
  change callback so backend-to-source sync is not misclassified as a manual
  edit.
- Background Source diagnostics and auto-apply must not switch Monaco into
  `readOnly` or otherwise steal focus/cursor from the user's active edit. Keep
  manual Apply/Revert operations visibly busy, but treat debounced background
  checks as non-blocking and ignore stale async results if the buffer changed
  while they were in flight.
- Source diagnostics resolver/import environment details belong in the
  Console/Output panel, not inside Source. Source may show compact sync state,
  pending patch state, and inline Monaco markers, but it must not render bulky
  resolver summaries, import trees, dry-run environment text, or replay plans
  above the editor because those blocks shift the user's cursor context.
- Ranged Source diagnostics must create Monaco markers/decorations from
  backend-provided `Diagnostic.range`. Errors should visibly mark the affected
  line in red and warnings in warning color without focusing, scrolling, or
  changing selection unless the user explicitly chooses a diagnostic location.
- Source range focus must translate backend `{ line, column }` ranges into
  Monaco selections and reveal the range in view. Preview mode must continue to
  highlight backend-provided ranges without guessing semantic targets.
- E2E tests that edit Monaco should use an explicit test bridge or Monaco-aware
  keyboard interaction instead of textarea-only `fill()` assumptions.
- Source quickfix actions may use `edit_range` with an empty `replacement` to
  delete invalid source. Treat the presence of `edit_range` as actionable even
  when `replacement === ""`; otherwise delete fixes for duplicate statements
  become invisible.

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

Graph-to-source synchronization must stay command-first: GUI graph operations
issue CLI commands through `/api/exec`; the CLI is responsible for patching
source-backed CST/text and reprojecting backend state. Do not add browser-only
semantic source patch endpoints for graph edits.

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

### Synthetic Canvas IDs In Backend Commands

If the canvas renders graph parameters or context pins as helper nodes, do not
send helper IDs such as `__graph_parameters__` or `__logic_context__:*` through
`/api/exec`. Commands must use source-domain endpoints like `message`,
`context.start`, and `printer.enter`.
