# GraphScript Specification

> Domain-agnostic DSL engine for text↔graph isomorphic editing and parsing.

---

## Context

GraphScript is a **standalone C++17 library + CLI**. It provides a full pipeline for defining, editing, and serializing node-based graphs via a textual DSL (`.gs` / `.d.gs` files) and a visual editor (web or CLI).

**Repo**: `git@github.com:blackplume233/GraphScript.git`

---

## Spec Index

| Document | Description | Status |
|----------|-------------|--------|
| [Architecture](./architecture.md) | Data model, compilation pipeline, layer boundaries | Filled |
| [DSL Reference](./dsl-reference.md) | `.gs` and `.d.gs` syntax, file types, grammar | Filled |
| [Scope Rules](./scope-rules.md) | Block-level scope isolation for flow/link references | Filled |
| [AI Native 资产格式](./ai-native-asset-format.md) | Roslyn-like 文本资产、通用 AST、领域投影和 lint 的目标模型草案 | Draft |
| [AI Native 资产语法](./ai-native-syntax.md) | 通用 scope/object/property/call 语法草案与可逆作者子集 | Draft |
| [Development Guide](./development-guide.md) | Build, test, extend, common patterns and anti-patterns | Filled |

---

## Quick Start for Agents

### Build & Test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/gs_tests.exe          # 179 tests
```

### Key Entry Points

| Task | Command / File |
|------|---------------|
| Parse `.gs` file | `gs parse <file>` |
| Compile + validate | `gs compile <file>` |
| Interactive editor | `gs edit [-I import.d.gs]` |
| Web GUI editor | `gs serve [-I import.d.gs] [-p 8080]` |
| Generate Mermaid diagram | `gs diagram <file>` |
| Run tests | `./build/Release/gs_tests.exe` |

### Test Fixture Location

All `.gs` and `.d.gs` test fixtures: `tests/fixtures/`

---

## Design Axioms

1. **Text ↔ Graph Isomorphism**: Any `.gs` file can be compiled to an in-memory graph and emitted back to structurally equivalent text. Round-trip fidelity is mandatory.
2. **Domain Agnostic**: Core engine has zero knowledge of HTN, Task, LevelScript, etc. Business rules are injected via `.d.gs` Schema declarations.
3. **Graph-as-Node**: Every `Graph` automatically derives a `NodeDefinition`, enabling composable sub-graphs.
4. **Scope Isolation**: Functions can only reference `context` and parameters; events can reference `context`, parameters, and node instances. No cross-scope or dangling references allowed.
5. **CLI-First**: Every editing operation has a CLI command. GUI maps to CLI commands 1:1.

---

## Architecture Overview (Quick)

```
.gs text  ──[Lexer→Parser]──▶  AST  ──[Compiler]──▶  Module
                                                         │
                                          ┌──────────────┼──────────────┐
                                          ▼              ▼              ▼
                                    EditSession     EditGraph       Emitter
                                    (CLI/GUI)     (SlotMap-based)   (→ .gs text)
                                                       │
                                                       ▼
                                                  RuntimeGraph
                                                  (Baked, flat arrays)
```

See [Architecture](./architecture.md) for full details.
