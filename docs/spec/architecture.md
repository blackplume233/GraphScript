# GraphScript Architecture

> Current product architecture for human + AI collaborative game asset editing.

GraphScript is an AI Native game asset format and authoring stack. The system is
designed around one canonical source text that can be edited by AI agents,
linted like a script, projected into graph/domain views, and patched from visual
editor operations without erasing the user's text.

---

## Highest Goal

Humans and AI should collaborate organically on the same asset:

- AI works directly on serialization text with diagnostics, source ranges, and
  structured patches.
- Humans work through graph and asset editors.
- Both paths preserve one canonical source file, including comments, blank lines,
  local formatting, and stable identities.

This is the reason for the CST/AST/document model, Graph projection, LSP-facing
diagnostics, and minimal source patching rules.

---

## Implementation Layers

```text
┌─────────────────────────────────────────────────────────────────────┐
│  Web Editor                                                         │
│  Human graph/asset editing surface. Issues replayable operations.   │
├─────────────────────────────────────────────────────────────────────┤
│  Graph Domain                                                       │
│  Interprets serialization facts as graph, node, pin, edge, entry.   │
├─────────────────────────────────────────────────────────────────────┤
│  Serialization Document Library                                     │
│  CST, typed AST facade, semantic model, diagnostics, rewrite ops.   │
├─────────────────────────────────────────────────────────────────────┤
│  Serialization Syntax                                               │
│  .gs/.d.gs source grammar: file, import, scope, object, property.   │
└─────────────────────────────────────────────────────────────────────┘
```

### 1. Serialization Syntax

The base language describes game asset facts:

- files and imports
- scopes/assets/fragments
- objects and properties
- values and references
- attributes/metadata
- declarations and schemas

It does not make `Graph`, `Node`, `Pin`, `Edge`, HTN task, table row, or dialogue
branch fundamental parser concepts. Those belong to domain projection layers.

Current syntax shape: [docs/syntax/current/serialization-syntax.md](../syntax/current/serialization-syntax.md).

### 2. Serialization Document Library

The document library is the editing foundation. It must provide:

- lossless CST with tokens, trivia, comments, blank lines, source ranges, missing
  nodes, and error nodes
- typed AST wrappers over the CST
- semantic model and partial binding
- machine-usable diagnostics and quick fixes
- document operations and rewrite planning
- minimal `TextPatch` output

The library is what lets AI patch text directly and lets graph edits preserve
the surrounding source instead of re-emitting whole files.

### 3. Graph Domain

The Graph domain consumes serialization facts and projects them into an authoring
model:

- graph scopes
- node candidates
- entries
- pins
- edges
- graph diagnostics
- graph edit operations that lower back to document operations

Graph domain rules are described by declarations, schemas, attributes, binders,
and projection providers. They must not leak back into base parser concepts.

The graph authoring model should stay associated with the document model through
stable source bindings or document anchors. It should not depend on parser
internals or own the CST directly. A graph item may cache domain data for fast
editor interaction, but editable graph items need a way to resolve back to a
source anchor exposed by the serialization document library. This association is
what makes precise patches possible without serializing the whole graph back to
text.

Current Graph contract: [graph-domain.md](./graph-domain.md).

### 4. Web Editor

The Web editor is the human visual editing surface. It should:

- render from backend/domain state, not invent its own graph semantics
- send replayable edit operations to the backend
- display diagnostics and source ranges from the document/domain model
- treat source text as canonical even when showing graph-first workflows

---

## Source-First Pipeline

```text
.gs/.d.gs source text
  -> Serialization parser
  -> Lossless CST + Syntax diagnostics
  -> Typed AST facade
  -> Semantic model + asset lint
  -> Domain projections
  -> Graph/Web/CLI authoring views
```

Invalid or incomplete text should still produce partial structure whenever
possible. A broken property should not make the whole graph disappear; it should
produce a partial model plus diagnostics and invalid domain items.

---

## Visual Edit To Text Patch

```text
Human graph edit
  -> Graph domain operation
  -> Source binding / document anchor lookup
  -> Serialization document operation
  -> Minimal TextPatch
  -> Reparse + relint + reproject
```

Required behavior:

- Patch the smallest stable source range.
- Preserve unrelated text byte-for-byte.
- Preserve comments, blank lines, and local formatting.
- Reparse after patching and surface any new diagnostics.
- Fall back to inserting a new fragment when a precise patch anchor is missing,
  rather than rewriting the whole document.

The editor may keep an interactive graph model in memory, but that model should
be source-bound through document-library anchors. It should store or reference
enough stable binding data to answer: "which source range should this visual edit
patch?" If the answer is unknown, the operation is degraded and should create an
explicit fragment or diagnostic instead of pretending the graph can be safely
serialized wholesale.

---

## AI Edit To Graph Refresh

```text
AI text patch
  -> Reparse
  -> Syntax/semantic/domain diagnostics
  -> Projection delta
  -> Web editor refresh
```

AI-facing diagnostics should identify source ranges, expected shapes, candidate
symbols, and executable quick fixes. Natural-language advice can be layered on
top, but the core diagnostic contract should be structured.

---

## Dependency Direction

Allowed:

```text
Serialization Syntax
  -> Serialization Document Library
  -> Graph Domain
  -> Web Editor
```

Forbidden:

```text
Serialization Syntax -> Graph Domain
Serialization Syntax -> Web Editor
Serialization Document Library -> Web Editor
```

The base parser must stay reusable for graph, table, dialogue, quest, level, and
other game asset domains.

---

## Current CLI Surface

| Subcommand | Purpose |
| --- | --- |
| `parse` | Parse `.gs/.d.gs` and report syntax structure/diagnostics. |
| `lint` | Run syntax, semantic, and domain diagnostics where available. |
| `project` | Project a domain view such as Graph/FlowGraph. |
| `patch` | Apply a document-aware source patch. |
| `edit` | Run an interactive source/domain editor. |
| `serve` | Run the web editor server. |

Legacy graph-only commands and old hand-written DSL behavior are archive or
migration material, not the current architectural target.
