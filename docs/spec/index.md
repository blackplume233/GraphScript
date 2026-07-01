# GraphScript Spec

> Product spec entry for GraphScript.

GraphScript's highest goal is not "a graph DSL". The goal is a shared authoring
format where humans and AI can work on the same game asset without fighting each
other:

- AI reads, lints, diagnoses, and patches serialization text directly.
- Humans edit graph views and asset views with visual tools.
- Both editing paths converge on the same source text and preserve comments,
  blank lines, local formatting, and source identity.

Everything else in this spec serves that goal.

---

## Current Principles

1. **Human + AI collaboration first**: text and graph editing are two views over
   the same asset, not separate sources of truth.
2. **Text is the canonical source**: `.gs` and `.d.gs` files must be readable,
   lintable, diagnosable, patchable, and suitable for LSP-style tooling.
3. **Graph is a domain projection**: graph, node, entry, pin, edge, flow/link,
   and runtime concepts are interpreted above the serialization layer.
4. **Lossless document model**: CST/source ranges/trivia are part of the editing
   contract. Blank lines, comments, and local formatting must survive unrelated
   edits.
5. **Minimal source patching**: visual edits should map to the smallest stable
   text range whenever possible, not to whole-file re-emission.
6. **Layered implementation**: serialization syntax, serialization document
   library, Graph domain, and Web editor remain separate responsibilities.

---

## Spec Map

`docs/spec/` stores repository output: product architecture, domain specs, and
development documents. `docs/syntax/` stores syntax design artifacts.
`.trellis/spec/` stores Agent harness guidance, including design review
standards and work checklists.

### Current Normative Specs

| Document | Purpose |
| --- | --- |
| [Architecture](./architecture.md) | Current product vision, layer boundaries, and edit pipeline. |
| [Current Graph Domain](./graph-domain.md) | Current Graph/FlowGraph projection contract and graph-specific syntax mapping. |
| [Development Guide](./development-guide.md) | Build, test, extension, and implementation workflow guidance. |

### Syntax Workspace

All syntax design material lives under [docs/syntax/](../syntax/):

| Directory | Meaning |
| --- | --- |
| [docs/syntax/current/](../syntax/current/) | Current syntax shape and decisions. |
| [docs/syntax/design/](../syntax/design/) | Active long-form syntax drafts. |
| [docs/syntax/archive/](../syntax/archive/) | Historical syntax references retained for migration evidence. |

### Migration Records

| Document | Purpose |
| --- | --- |
| [Legacy Graph DSL Feature Inventory](./migration/legacy-graph-dsl-feature-inventory.md) | Archive of old DSL capabilities and migration test intent. |

---

## Command Surface

Current CLI work should keep the source-first path visible:

| Task | Command / File |
| --- | --- |
| Parse `.gs/.d.gs` asset syntax | `gs parse -i file.gs` |
| Lint `.gs/.d.gs` asset syntax | `gs lint -i file.gs` |
| Project graph domain view | `gs project -i file.gs -I import.d.gs --graph Execute` |
| Apply source patch | `gs patch -i file.gs --op <op> ...` |
| Interactive editor | `gs edit [-I import.d.gs]` |
| Web GUI editor | `gs serve [-I import.d.gs] [-p 8080]` |
| Run tests | `./build/Release/gs_tests.exe` |

All `.gs` and `.d.gs` fixtures live in `tests/fixtures/`.
