# Current Graph Domain

> Current contract for graph-specific meaning above the serialization layer.

The Graph domain is a projection over serialized asset facts. It gives humans a
visual graph editing model while keeping source text canonical for AI, lint, LSP,
and precise patches.

---

## Boundary

The Graph domain may define:

- graph authoring models
- graph entries
- node authoring models
- pin definitions and references
- edges and connection policies
- graph diagnostics
- graph edit operations
- graph runtime or bake inputs

The Graph domain must not require the base parser or base serialization AST to
contain graph-specific nodes.

---

## Source Mapping

| Serialization source | Graph interpretation |
| --- | --- |
| `scope graph Execute: AbilityGraph` | Graph domain root named `Execute` with schema `AbilityGraph`. |
| nested `scope entry Start` | Graph entry named `Start`. |
| `const apply = new ApplyDamage { ... }` | Candidate graph node `apply` of type `ApplyDamage`. |
| `property editor.pos: [x, y]` or equivalent metadata | Node/editor metadata. |
| object field declarations with `@flow.pin(...)` | FlowGraph pin definitions. |
| restricted command call `a.out.connect(b.in)` | Candidate graph edge. |

Every projected graph item should retain a source binding:

```text
Graph -> source scope range
Entry -> source entry scope range
Node -> source const/object range
Node property -> source property range
Pin definition -> declaration field range
Edge -> source command call range
Diagnostic -> best source range plus related ranges
```

If a graph item has no precise source binding, it must be marked synthetic or
invalid so editors and AI tools know how cautiously to patch it.

---

## Source-Bound Authoring Model

The editor-facing graph model should not be a detached graph DTO that later gets
serialized back to text. It should be a source-bound projection that talks to the
serialization document library through stable anchors:

```text
Document model
  -> typed AST facade
  -> semantic model
  -> graph projection with SourceBinding anchors
  -> editor graph state
```

Graph nodes, pins, edges, entries, and editable metadata should either carry a
`SourceBinding` directly or reference a domain item that can resolve to one. This
does not mean the Graph domain owns or directly manipulates CST internals. The
CST stays inside the serialization document layer; the Graph domain consumes
public binding/anchor handles from that layer.

Useful binding data includes:

- source file identity
- primary syntax anchor kind exposed by the document layer
- primary source range
- value range when editing a property value
- insertion anchor for adding related syntax
- contribution ranges for fragment-based assets
- stable semantic/domain id when available

This is not "runtime graph depends on CST" and not "Graph domain violates
layering by owning parser details". Runtime/baked graph data can stay separate.
The association belongs in the authoring graph model used by the editor,
diagnostics, quick fixes, and AI repair loop.

Without this association, graph edits must guess how to serialize back to text,
which usually leads to whole-file emission and destroys comments or local
formatting. With document anchors, a visual edit can become a document operation
against a known source range.

---

## Edit Contract

Graph edit operations should lower to serialization document operations:

| Graph edit | Preferred source operation |
| --- | --- |
| add node | insert `const alias = new Type { ... }` in the graph scope |
| delete node | remove the source object and related source-bound edges when safe |
| rename node | patch `const_declaration.name` and references |
| move node | patch editor metadata property |
| set node property | patch the corresponding property value |
| connect pins | insert restricted command call |
| disconnect edge | remove the source command call |
| add entry | insert nested `scope entry Name { ... }` |

When the preferred patch anchor is missing, the system should insert a new
fragment or contribution rather than rewrite the whole file.

---

## Graph Diagnostics

Graph diagnostics are domain diagnostics over projected graph facts, not parser
errors. Examples:

- unknown node type
- unknown pin
- pin direction mismatch
- type mismatch
- fan-in/fan-out policy violation
- missing required entry
- disconnected required flow

Diagnostics must include source ranges so AI can repair text and humans can see
the issue in both text and graph views.

---

## Web Editor Responsibilities

The Web editor should render the graph domain model and issue graph operations.
It should not invent semantics that the backend cannot parse, bind, project,
diagnose, and patch.

Expected flow:

```text
UI action
  -> backend graph operation
  -> document operation / TextPatch
  -> reparse + relint + reproject
  -> refreshed UI state
```

This keeps visual graph editing and AI text editing in the same collaboration
loop.
