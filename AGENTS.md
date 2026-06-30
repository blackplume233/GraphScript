# GraphScript — AI Assistant Instructions

> Context and rules for AI assistants working within the GraphScript repository.

## Detailed Specs

Full specifications live in `docs/spec/` within this repo:

| Document | Description |
|----------|-------------|
| [docs/spec/index.md](docs/spec/index.md) | Overview, design axioms, quick start |
| [docs/spec/architecture.md](docs/spec/architecture.md) | Data model, pipeline, layer boundaries |
| [docs/spec/dsl-reference.md](docs/spec/dsl-reference.md) | `.gs` and `.d.gs` syntax reference |
| [docs/spec/scope-rules.md](docs/spec/scope-rules.md) | Block-level scope isolation rules |
| [docs/spec/development-guide.md](docs/spec/development-guide.md) | Build, test, extend patterns |

## Project Overview

GraphScript is a **domain-agnostic C++17 DSL engine** for text↔graph isomorphic editing. It provides:

- **Lexer → Parser → Compiler** pipeline (`.gs` text → in-memory `Module`)
- **Emitter** for round-trip text generation (`Module` → `.gs` text)
- **EditGraph** with generational handles (`SlotMap`) for mutable graph editing
- **RuntimeGraph** bake pipeline (flat arrays, immutable, cache-friendly)
- **Schema system** for domain-specific rules (injected via `.d.gs` declaration files)
- **CLI editor** (REPL) and **Web GUI** (LiteGraph.js + cpp-httplib)

## Build & Test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/gs_tests.exe          # 179 tests must pass
./build/Release/gs.exe edit           # Interactive CLI editor
./build/Release/gs.exe serve          # Web editor at localhost:8080
```

## Architecture (Quick Reference)

```
.gs text ──[Lexer→Parser]──▶ AST ──[Compiler]──▶ Module
                                                    │
                                     ┌──────────────┼───────────┐
                                     ▼              ▼           ▼
                               EditSession     EditGraph    Emitter → .gs
                               (CLI/GUI)     (SlotMap)
                                                    │
                                                    ▼
                                              RuntimeGraph (Baked)
```

### Key Directories

| Path | Purpose |
|------|---------|
| `include/graphscript/` | Public API headers |
| `src/` | Implementation (mirrors include/ structure) |
| `cli/` | CLI tool: main, editor (REPL), server (HTTP) |
| `web/` | Web frontend (LiteGraph.js single-page app) |
| `tests/` | GoogleTest suites |
| `tests/fixtures/` | `.gs` and `.d.gs` test data |

## Critical Rules

### 1. Domain Agnostic Core

Core engine code (`src/`, `include/`) must **never** contain domain-specific names (HTN, Task, LevelScript, Cinematic, etc.). All domain knowledge is injected via `.d.gs` Schema declarations.

### 2. Scope Isolation (ENFORCED)

| Block Type | Allowed References | Forbidden |
|------------|-------------------|-----------|
| **function** | `context`, parameter names | Node instances, `let` variables, undefined names |
| **event** | `context`, parameter names, node instance names | `let` variables, undefined names |

Enforced at: Compiler (compile-time), EditSession (`add_flow`/`add_link`), unit tests.

### 3. Annotation Syntax (C# Attribute Style)

Metadata (position, comments, tooltips) is attached via `[Name(args)]` prefix annotations, placed on the line before the target element:

```gs
[Comment("title", "Graph description")]
Graph MyGraph {
    [Position(X = 100, Y = 200)]
    PrintString logger{};
}
```

Supports: Graph definitions, node instances, parameters. Both positional and named arguments. Multiple annotations via comma separation: `[A(...), B(...)]`.

Data model: `Annotation { name, vector<AnnotationArg> }` where `AnnotationArg { name, value }` (name empty for positional args). Fields on `Graph`, `NodeInstance`, `GraphParameter` in IR; and `GraphNode`, `NodeInstanceNode`, `ParamDeclNode` in AST.

### 4. Round-Trip Fidelity

Any `.gs` text must survive: Parse → Compile → Emit → Re-parse → Re-compile → structurally equivalent Module. Every new syntax feature needs a round-trip test.

### 5. Result<T, E> Error Handling

All fallible operations return `Result<T, std::string>`. No exceptions. Use `Result<void, std::string>::ok()` (no argument) for void operations.

### 6. Function-Level Documentation

Every public function must have a doc-comment. Update comments when modifying behavior.

### 7. CLI-First Design

Every editing operation has a CLI command. GUI maps to CLI commands 1:1 for recordability and replay.

## File Types

| Extension | Purpose |
|-----------|---------|
| `.gs` | Graph definitions (imports, lets, Graph blocks) |
| `.d.gs` | Declarations (types, nodes, schemas — host interface) |

## Test Suite Structure

| Suite | Count | Coverage |
|-------|-------|----------|
| Lexer | 20 | Tokenization |
| Parser | 21 | AST construction |
| Compiler | 10 | AST → Module |
| Emitter | 8 | Round-trip text gen |
| EditGraph | 18 | Mutable graph ops |
| EditSession | 25 | Full editing workflow + scope rules |
| RuntimeGraph | 9 | Bake pipeline |
| DeepCycle | 23 | End-to-end fixture integration |
| Others | 45 | Result, SlotMap, registries, policies, validator |

**All 179 tests must pass before any commit.**

## Common Pitfalls

- `Result<void>::ok({})` → use `Result<void>::ok()` (void specialization)
- Adding fixture with dangling references → validate all flow/link refs exist in scope
- Forgetting to kill `gs serve` before rebuild → `LNK1104` linker error
- Editing Parser without updating Emitter → round-trip tests fail
- Adding domain names to core code → violates domain-agnostic principle
