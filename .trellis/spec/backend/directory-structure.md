# Directory Structure

GraphScript is a standalone C++17 library plus CLI/web editor. The public API,
implementation, tests, and fixtures have intentionally mirrored ownership.

## Core Layout

```text
include/graphscript/     Public C++ API headers
  core/                  Graph, node, pin, connection, Result
  parse/                 Lexer, parser, AST, token model
  compile/               Compiler, Module, imports, let declarations
  emit/                  Module/graph to `.gs` text
  edit/                  EditGraph, EditSession, stable handles
  runtime/               Baked RuntimeGraph
  registry/              Type, node, schema registries and Environment
  schema/                ConnectionPolicy, GraphSchema, Validator

src/                     Implementations mirroring include/graphscript/
cli/                     `gs` executable, CLIEditor, HTTP server entry points
tests/                   GoogleTest suites
tests/fixtures/          `.gs` and `.d.gs` integration fixtures
docs/spec/               Product, language, and architecture specifications
web/                     Legacy/single-file LiteGraph.js browser UI
webapp/                  Vite/React/TypeScript graph editor
CMakeLists.txt           Build configuration
```

## Ownership Rules

- Headers in `include/graphscript/<area>/` define the API surface; matching
  implementations live under `src/<area>/`.
- Keep file names in `snake_case`: `edit_session.h` pairs with
  `edit_session.cpp`.
- Put CLI command dispatch and command implementations in `cli/editor.*`.
- Put reusable engine behavior in the core layer, not in CLI or frontend code.
- Put integration examples and regression assets in `tests/fixtures/`.
- Product/language design details belong in `docs/spec/`, while engineering
  implementation rules belong in `.trellis/spec/`.

## Naming Conventions

- Classes and structs use `PascalCase`: `EditSession`, `RuntimeGraph`.
- Functions and methods use `snake_case`: `add_node`, `compile_graph`.
- Data members use `snake_case_` with a trailing underscore.
- Test cases use `TEST(Suite, DescriptiveTestName)`.
- GraphScript source fixtures use `.gs`; declaration files use `.d.gs`.

## Layer Boundaries

- Core engine and schema framework must stay domain-agnostic. Do not hard-code
  names such as `HTN`, `Task`, `LevelScript`, `Cinematic`, or `Ability` into
  engine behavior.
- Domain rules are expressed by `.d.gs` declarations and schema fields.
- The CLI and GUI are editing surfaces over `EditSession`; they must not invent
  graph semantics that the compiler/EditSession cannot validate.
- `web/` and `webapp/` are frontend surfaces, but their backend contract is the
  C++ server and `EditSession::state_to_json()`.

## Examples To Follow

- `docs/spec/development-guide.md` contains the canonical extension checklist.
- `docs/spec/architecture.md` defines the data flow from `.gs` text to AST,
  Module, EditGraph, RuntimeGraph, and emitted text.
- `docs/spec/scope-rules.md` defines reference validity for event/function
  blocks and names the compiler/EditSession enforcement points.
