# GraphScript Development Guide

> Build, test, extend patterns, and anti-patterns for working on GraphScript.

---

## Build System

### Prerequisites

- CMake 3.14+
- C++17 compiler (MSVC 19.14+, GCC 7+, Clang 5+)
- No external dependencies (googletest and cpp-httplib are fetched automatically)

### Build Commands

```bash
cd GraphScript

# Configure (first time)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all targets
cmake --build build --config Release

# Build specific target
cmake --build build --config Release --target gs_tests
cmake --build build --config Release --target gs
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `GS_BUILD_TESTS` | ON | Build test executable |
| `GS_BUILD_CLI` | ON | Build CLI executable |

### Targets

| Target | Output | Description |
|--------|--------|-------------|
| `graphscript_core` | Static library | Core DSL engine |
| `gs` | Executable | CLI tool + web server |
| `gs_tests` | Executable | All unit + integration tests |

---

## Test Suite

### Running Tests

```bash
./build/Release/gs_tests.exe                           # All tests
./build/Release/gs_tests.exe --gtest_filter="Asset*"    # Specific suite
```

### Test Organization (179 tests)

| File | Suite | Count | What it tests |
|------|-------|-------|---------------|
| `test_asset_lexical_surface.cpp` | AssetLexicalSurface | 5 | Asset syntax lexical surface coverage |
| `test_asset_parser.cpp` | AssetParser | 7 | Asset CST/module parsing |
| `test_asset_projection.cpp` | AssetProjection | 6 | Asset declarations and graph projection |
| `test_asset_source_emission.cpp` | AssetSourceEmission | 7 | Asset source emission and reparse |
| `test_result.cpp` | Result | 6 | Result<T,E> monad |
| `test_node_registry.cpp` | NodeDefinition, NodeRegistry | 9 | Node type management |
| `test_connection_policy.cpp` | ConnectionPolicy | 4 | Schema policy values |
| `test_schema_registry.cpp` | SchemaRegistry | 5 | Schema registration |
| `test_slotmap.cpp` | Handle, SlotMap | 12 | Generational handle system |
| `test_editgraph.cpp` | EditGraph | 18 | Mutable graph operations |
| `test_edit_session.cpp` | EditSession | 25 | Full editing workflow |
| `test_graph_runtime_ir.cpp` | GraphRuntimeIR | 10 | Asset projection bake pipeline |
| `test_validator.cpp` | Diagnostic, Validator | 2 | Validation framework |
| `test_integration_deep.cpp` | DeepCycle | 23 | End-to-end fixture tests |

### Test Fixtures

Location: `tests/fixtures/`

| File | Purpose |
|------|---------|
| `ue_core.d.gs` | Core type and node declarations (PrintString, Delay, etc.) |
| `htn_nodes.d.gs` | HTN domain nodes + HTNGraph schema |
| `task_nodes.d.gs` | Task domain nodes + TaskGraph schema |
| `levelscript_nodes.d.gs` | LevelScript domain nodes + schema |
| `mixed_declarations.d.gs` | Multi-domain declarations (Cinematic, Ability, etc.) |
| `minimal.gs` | Simplest valid graph |
| `round_trip.gs` | Asset emission → reparse equivalence |
| `all_features.gs` | Exercises every language feature |
| `deep_chain.gs` | 3-level Graph-as-Node chain |
| `multi_graph_file.gs` | Multiple graphs with cross-references |
| `*.gs` (others) | Domain-specific test scenarios |

### Adding a New Test Fixture

1. Create `tests/fixtures/your_fixture.gs`
2. Ensure all references respect [Scope Rules](./scope-rules.md)
3. Add projection/integration test in `test_integration_deep.cpp` (stress test list)
4. Add round-trip test if the fixture exercises new syntax
5. Run full test suite

---

## Extending GraphScript

### Adding a New DSL Feature

Follow this checklist in order:

1. **Grammar / parser** — Update tree-sitter asset grammar and `asset::Parser` lowering.
2. **Asset model** — Add or extend `asset::Module` / Block / Property / Command / Expr structures.
3. **Projection / lint** — Add semantic projection or lint rules for graph-facing behavior.
4. **Source emission / patch** — Add formatter, source emission, or patch support when text output changes.
5. **EditSession** — Add editing operations if applicable.
6. **Tests** — Add unit tests for each layer + round-trip test.
7. **Fixtures** — Add `.gs` fixture exercising the feature.

### Adding a New CLI Command

1. **CLIEditor** — Add `cmd_xxx()` method (`cli/editor.h`, `cli/editor.cpp`)
2. **Dispatch** — Add to `execute()` dispatch table
3. **Help** — Update `cmd_help()` output
4. **Web API** — If GUI-accessible, ensure `state_to_json()` exports relevant state

### Adding a New Node Type (Host Side)

No core code changes needed! Create or extend a `.d.gs` file:

```gs
declare Node MyNewNode {
    exec in enter;
    exec out success;
    exec out failed;
    data in target : AActor;
    data out result : bool;
}
```

### Adding a New Schema (Host Side)

```gs
declare Schema MyDomainGraph {
    max_exec_fan_out = 1;
    allow_exec_fan_in = true;
    strict_type_match = true;
}
```

Then use it: `Graph MyGraph : MyDomainGraph { ... }`

---

## Patterns

### Pattern: Result<T, E> for Error Handling

All fallible operations return `Result<T, std::string>` instead of throwing exceptions or returning error codes.

```cpp
// Function that can fail
Result<Module, std::string> compile(const ModuleNode& ast);

// Void operation that can fail
Result<void, std::string> add_node(const std::string& type, const std::string& name);

// Caller
auto result = compiler.compile(*ast);
if (result.is_err()) {
    std::cerr << result.error() << std::endl;
    return;
}
auto& module = result.value();
```

### Pattern: Snapshot-Based Undo/Redo

`EditSession` captures full `Module` snapshots before each mutation. Simple and reliable:

```cpp
void EditSession::push_undo(const std::string& description) {
    undo_stack_.push_back({module_, description});
    redo_stack_.clear();
}
```

### Pattern: Scope Validation via Positive Checking

Validate references by building allowed-name sets and checking membership, rather than blacklisting specific forbidden patterns:

```cpp
auto check_ref = [&](const std::string& ref, bool allow_nodes) -> std::string {
    if (param_names.count(ref)) return "";              // always allowed
    if (allow_nodes && node_names.count(ref)) return ""; // event-only
    return "error: unknown reference '" + ref + "'";     // everything else rejected
};
```

---

## Anti-Patterns

### Don't: Domain Names in Core Code

```cpp
// ✗ Never do this in core engine
if (graph.base_type == "HTNGraph") { /* HTN-specific logic */ }

// ✓ Use schema properties instead
if (schema && schema->connection_policy.max_exec_fan_out == -1) { /* generic */ }
```

### Don't: Skip Round-Trip Testing

Every new syntactic feature MUST have a test that:
1. Creates the feature (via text or EditSession)
2. Emits to text
3. Re-parses and re-compiles
4. Verifies structural equivalence

### Don't: Mutate Module Without push_undo

Every `EditSession` method that modifies `module_` must call `push_undo()` first:

```cpp
Result<void, std::string> EditSession::add_node(...) {
    // ... validation ...
    push_undo("add node ...");  // MUST be before mutation
    module_.graphs[active_].node_instances.push_back(...);
    return Result<void, std::string>::ok();
}
```

### Don't: Reference Module-Level `let` in Graph Logic

`let` declarations are module-scope constants. Graph logic blocks (events/functions) cannot reference them. Use graph parameters instead.

### Don't: Reference Nodes in Functions

Functions are self-contained. They can only use `context` and graph parameters. Use events for node wiring.

---

## Common Mistakes

### Mistake: Forgetting to Update Source Emission After Parser Change

**Symptom**: New syntax parses and projects, but round-trip test fails — emitted or patched text doesn't contain the new feature.

**Fix**: Every parser/model change that affects persisted syntax must update asset source emission or patch support.

### Mistake: `Result<void, std::string>::ok({})` Instead of `::ok()`

**Symptom**: Compile error C2860 — `void` cannot be used with `std::variant`.

**Fix**: Use `Result<void, std::string>::ok()` (no argument) for the void specialization.

### Mistake: Adding Fixture with Dangling References

**Symptom**: Stress test `StressTest_AllFixturesParseCompile` fails with "unknown reference" error.

**Fix**: Before committing a fixture, verify all flow/data-assignment references resolve to `context`, a parameter, or a node instance within the graph. See [Scope Rules](./scope-rules.md).

### Mistake: Not Killing `gs serve` Before Rebuilding

**Symptom**: `LNK1104: cannot open gs.exe` during build.

**Fix**: Stop the running server first:
```powershell
Get-Process gs -ErrorAction SilentlyContinue | Stop-Process -Force
```

---

## Code Organization

```
GraphScript/
├── include/graphscript/    # Public headers (the API surface)
│   ├── core/               # Fundamental types: Graph, Node, Pin, Connection, Result
│   ├── edit/               # EditGraph (SlotMap), EditSession, Handle
│   ├── asset/              # Tree-sitter asset parser, linter, projector, patcher
│   ├── graph/              # GraphRuntimeIR (baked graph runtime IR)
│   ├── registry/           # Environment (TypeRegistry, NodeRegistry, SchemaRegistry)
│   └── schema/             # ConnectionPolicy, GraphSchema, Validator
├── src/                    # Implementation files (mirror include/ structure)
├── cli/                    # CLI tool: main, editor (REPL), server (HTTP)
├── web/                    # Web frontend: index.html (LiteGraph.js)
├── tests/                  # GoogleTest suites + fixture files
│   └── fixtures/           # .gs and .d.gs test data
├── docs/                   # Design documents
└── CMakeLists.txt          # Build configuration
```

### Naming Conventions

- **Headers**: `snake_case.h` in nested namespace dirs
- **Sources**: `snake_case.cpp` matching header structure
- **Classes**: `PascalCase` (`EditSession`, `GraphRuntimeIR`)
- **Functions**: `snake_case` (`add_node`, `compile_graph`)
- **Members**: `snake_case_` with trailing underscore
- **Test names**: `TEST(Suite, DescriptiveTestName)` in PascalCase
