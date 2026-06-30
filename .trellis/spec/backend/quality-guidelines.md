# Quality Guidelines

Backend/core changes must preserve GraphScript's core invariants: text and graph
isomorphism, domain-agnostic engine behavior, explicit errors, and CLI-first
editing.

## Build And Test Gates

Use the existing CMake build and GoogleTest suite:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
.\build\Release\gs_tests.exe
```

All tests are expected to pass before committing. `docs/spec/development-guide.md`
currently documents 179 tests across lexer, parser, compiler, emitter,
EditGraph, EditSession, RuntimeGraph, integration, and support suites.

## Required Tests By Change Type

| Change type | Required coverage |
| --- | --- |
| Lexer token change | Lexer tests for tokenization and edge cases |
| Parser grammar change | Parser tests plus invalid-input cases |
| Compiler/IR change | Compiler tests and integration fixture coverage |
| Emitter change | Round-trip test that reparses/recompiles emitted text |
| EditSession mutation | EditSession tests, undo/redo behavior when applicable |
| RuntimeGraph bake change | RuntimeGraph tests for baked array shape and edge mapping |
| CLI command change | CLI dispatch/help updates and replay-relevant tests |
| Scope rule change | Compiler and EditSession tests for event/function references |
| Fixture addition | Integration compile test and round-trip test for syntax features |

## Extension Checklist For New DSL Syntax

Follow this order unless a task proves a narrower scope:

1. Lexer token model.
2. AST node shape.
3. Parser rule.
4. Compiler lowering into Module/core data.
5. Emitter support.
6. EditSession operation if editable through CLI/GUI.
7. Unit tests for each touched layer.
8. Fixture exercising the feature.
9. Round-trip test.

## Required Patterns

- Public API functions need doc comments, and comments must be updated when
  behavior changes.
- Every `EditSession` method that mutates `module_` must call `push_undo()`
  before mutation.
- Scope validation should use positive allowed-name sets rather than
  blacklisting forbidden references.
- CLI and GUI editing operations must go through replayable command semantics.
- Search for existing helpers before adding new utilities or constants.

## Forbidden Patterns

- Do not put domain-specific rules into core engine code. Use schema
  declarations and schema properties instead.
- Do not update parser behavior without checking emitter and round-trip tests.
- Do not allow GUI-only mutations that cannot be replayed as CLI commands.
- Do not reference module-level `let` values from graph logic blocks.
- Do not let functions reference graph node instances; functions are limited to
  `context` and graph parameters.
- Do not create fixtures with dangling references.

## Common Mistakes

### `Result<void>::ok({})`

Use `Result<void, std::string>::ok()` with no argument.

### Parser Change Without Emitter Change

If new syntax parses but does not emit, round-trip behavior is broken. Add or
update emitter support in the same task.

### Asset Emitter Produces Syntax The Grammar Cannot Parse

`EditSession::emit()` and `emit_active()` must output tree-sitter asset syntax
that `EditSession::load_source()` can parse and project again. Do not serialize
metadata forms that the current asset grammar cannot represent. For example,
top-level `import` declarations currently do not accept prefix attributes, so
import annotations may remain in session state/JSON but must not be emitted as
`@Attr import "x.d.gs";` source until the grammar and parser support it.

When converting asset `const name = new Type { ... }` into the legacy `Module`
adapter, preserve body properties in `LetDecl::initializer_fields`; otherwise a
later edit that clears the source cache can silently save an empty const body.
Add regression tests that perform `emit -> load_source` after the edit.

### CLI Diagram Accidentally Reuses Legacy Emitter

The `diagram` editor command is a debug/view output, not a reason to keep the
legacy source emitter in the CLI. Prefer `asset::Parser` plus
`asset::FlowGraphProjector` for current `.gs` source, and route Mermaid output
through `debug::emit_mermaid_flow_graph_diagram()`. If projection is temporarily
unavailable during migration, an explicit warning plus a session-graph fallback
is acceptable, but `cli/editor.cpp` must not include `graphscript/emit/emitter.h`.

### Running Server During Rebuild

On Windows, a running `gs serve` can lock `gs.exe` and cause `LNK1104`. Stop the
server process before rebuilding.

### Fixture References Outside Scope

Before adding a fixture, verify every flow and data reference resolves to
`context`, a parameter, or a node instance allowed in the current block. See
`docs/spec/scope-rules.md`.
