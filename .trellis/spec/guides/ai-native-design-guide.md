# AI Native Design Guide

This is harness guidance for agents reviewing or changing GraphScript design.
It is not a syntax artifact.

## Highest Design Goal

GraphScript exists to let humans and AI collaborate organically on the same game
asset:

- AI should be able to inspect, lint, diagnose, and patch serialization text.
- Humans should be able to edit visual graph/domain views.
- Both paths must converge on the same source text without destroying comments,
  blank lines, local formatting, or stable source identity.

## Review Standard

Before accepting a design change, ask:

- Can AI inspect and modify the text safely with structured diagnostics?
- Can a human make the equivalent edit visually without destroying text layout?
- Can the system explain the change through source ranges, diagnostics, and
  replayable operations?
- Can incomplete or invalid text still produce enough structure for AI and
  visual tools to remain useful?

If a feature makes one editing path powerful by making the other opaque, it
needs redesign or should stay outside the reversible authoring subset.

## Syntax Design Expectations

When reviewing files under `docs/syntax/`, keep the syntax documents focused on
language shape:

- grammar constructs
- examples
- current syntax decisions
- draft alternatives
- archive/migration references

Do not push broad review standards or Agent workflow instructions into
`docs/syntax/`; keep those in `.trellis/spec/`.

### DSL Layer Terminology Boundary

Syntax drafts describe the DSL surface and its lossless `DocumentNode` model.
Graph and FlowGraph are target domain projections, not the current syntax layer.

Use current-layer terms such as:

- `DocumentNode`
- `DocumentObject`
- `DocumentEntry` / `Entry`
- `DocumentArray`
- `TypedObject`
- `Command`
- `Assignment`
- `Annotation`

Do not use `GraphNode`, `Pin`, `Edge`, or graph node/edge/pin language as DSL
grammar concepts. These terms are acceptable only when explicitly describing
how a DSL structure is projected into a Graph/FlowGraph domain view.

Do not make domain field names such as `event`, `flow`, `steps`, `on`, `param`,
`inputs`, `outputs`, `methods`, or `callbacks` grammar concepts. They may
appear as ordinary entries, but their meaning belongs to schema, linter, and
projection layers.

## Source Preservation Expectations

For parser, document model, patch, or formatter work:

- Preserve tokens and punctuation needed for round-trip.
- Preserve whitespace, comments, blank lines, and local formatting outside the
  edit target.
- Track source ranges for meaningful constructs.
- Represent missing or skipped syntax as recoverable diagnostics where possible.
- Prefer minimal `TextPatch` output over whole-file re-emission.

## Graph Editing Expectations

For graph/domain/editor work:

- Treat graph views as projections over source-bound asset facts.
- Use public document anchors or `SourceBinding`-style handles from the document
  layer; do not make Graph domain own parser internals.
- If a visual edit cannot resolve a source anchor, degrade with a diagnostic or
  explicit fragment insertion instead of silently serializing the whole graph.
- Keep runtime/baked graph data separate from authoring source bindings.
