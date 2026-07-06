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
| `deep_chain.gs` | 3-level nested/subgraph projection chain |
| `multi_graph_file.gs` | Multiple graphs with cross-references |
| `*.gs` (others) | Domain-specific test scenarios |

### Adding a New Test Fixture

1. Create `tests/fixtures/your_fixture.gs`
2. Ensure graph-facing references respect the current [Graph Domain](./graph-domain.md)
   source-binding and diagnostic rules.
3. Add projection/integration test in `test_integration_deep.cpp` (stress test list)
4. Add round-trip test if the fixture exercises new syntax
5. Run full test suite

---

## Extending GraphScript

### Adding a New DSL Feature

Follow this checklist in order:

1. **Grammar / parser** — Update tree-sitter asset grammar and `asset::Parser` lowering.
2. **Document model** — Add or extend CST/AST facade, source ranges, and document operations.
3. **Semantic/domain layer** — Add binding, projection, or lint rules when the syntax affects graph-facing behavior.
4. **Patch/format path** — Add formatter or patch support while preserving unrelated comments, blank lines, and local formatting.
5. **EditSession / API** — Add editing operations if applicable.
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
export declare object MyNewNode {
    @flow.input
    target: AActor;

    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;

    @flow.pin(kind = "exec", direction = "out")
    success: Exec;

    @flow.pin(kind = "exec", direction = "out")
    failed: Exec;

    @flow.input
    enabled: bool = true;

    @flow.pin(kind = "data", direction = "out")
    result: bool;
}
```

### Adding a New Schema (Host Side)

```gs
export declare schema MyDomainGraph: FlowGraphSchema {
    max_exec_fan_out = 1;
    allow_exec_fan_in = true;
    strict_type_match = true;
}
```

Then use it:

Use the current `.gs` syntax documented in
`docs/syntax/current/serialization-syntax.md`. Do not copy grammar examples from
stable design specs; current and draft syntax examples belong under
`docs/syntax/`.

---

## Patterns

### Pattern: Result<T, E> for Error Handling

All fallible operations return `Result<T, std::string>` instead of throwing exceptions or returning error codes.

```cpp
// Function that can fail
Result<asset::FlowGraph, std::string> project_asset_graph(const asset::Module& module,
                                                          const std::string& graph_name);

// Void operation that can fail
Result<void, std::string> add_node(const std::string& type, const std::string& name);

// Caller
auto result = asset::FlowGraphProjector::project(parsed.module, "MainGraph");
if (result.is_err()) {
    std::cerr << result.error() << std::endl;
    return;
}
auto& graph = result.value();
```

### Pattern: Source-Bound Edit Transactions

Editing operations should know which source-bound item they target before
mutation. The implementation may use snapshots internally, but the product
contract is an edit transaction that can explain the source range and resulting
patch:

```cpp
EditTransaction tx;
tx.operation = "set-property";
tx.target = SourceBinding{/* file, node range, value range */};
tx.patches = rewrite.set_property_value(tx.target, "75");
```

## Scenario: Source-Bound Programmatic CRUD

### 1. Scope / Trigger

Any programmatic create/update/delete/move API that changes graph, flow,
parameter, node, edge, metadata, or source-visible domain state must compile to a
source-bound document edit. This applies even when the public API is intentionally
simple, such as `steps.push(...)`, `nodes.erase(...)`, or `edge.reconnect(...)`.

### 2. Signatures

Public APIs may look collection-like:

```cpp
Result<EditResult, std::string> push_step(FlowRef flow, StepSpec step);
Result<EditResult, std::string> delete_step(StepId step_id);
Result<EditResult, std::string> set_node_property(NodeId node, std::string key, ValueSpec value);
Result<EditResult, std::string> move_node(NodeId node, EditorPosition position);
```

Internally they must lower to semantic operations and document patches:

```cpp
struct SemanticEditOp {
    EditKind kind;
    SourceAnchor target;
    std::optional<SourceAnchor> insertion_anchor;
    EditPayload payload;
};

struct EditResult {
    DocumentSnapshot before;
    std::vector<TextPatch> patches;
    DocumentSnapshot after;
    SemanticDelta semantic_delta;
    std::vector<Diagnostic> diagnostics;
};
```

### 3. Contracts

- `DocumentSnapshot` is immutable for a completed operation.
- `LosslessDocument` owns token, trivia, comments, blank lines, source ranges,
  missing nodes, and error nodes.
- `SemanticModel`, graph model, and editor state are projections; they must not
  be persisted as a second source of truth.
- A public mutation API records a `SemanticEditOp`; it does not directly mutate
  persisted graph state.
- Applying an operation must produce one or more minimal `TextPatch` values.
- After patching, the implementation must reparse, relint, and reproject.
- The final `SemanticDelta` must match the requested operation before the edit is
  accepted.
- Unmodified source ranges must remain byte-for-byte unchanged.

### 4. Validation & Error Matrix

| Condition | Required behavior |
| --- | --- |
| Target has no source anchor | Return diagnostic; do not silently serialize the whole graph. |
| Duplicate semantic item target is ambiguous | Require stable id, source range, or item index. |
| Patch applies but reparse fails outside the edited range | Reject or surface blocking diagnostic. |
| Reparse succeeds but expected semantic delta is missing | Reject operation and report projection mismatch. |
| Formatter would need whole-file rewrite for a local edit | Use local fragment insertion or return diagnostic. |
| Public API mutates only in-memory graph state | Treat as implementation bug. |

### 5. Good/Base/Bad Cases

- Good: `steps.push(call("patrol.start"))` records `AddStepOp`, inserts
  `patrol.start()` into the source `steps` array, reparses, and verifies the new
  call fact exists.
- Base: `move_node(node, position)` patches only editor metadata and preserves
  all unrelated text.
- Bad: `graph.nodes.push(node)` updates editor JSON and later emits the whole
  graph file from memory.

### 6. Tests Required

Every editable CRUD feature needs tests that assert:

1. The public API produces a `TextPatch`.
2. The patch touches only the intended source range or insertion point.
3. Comments, blank lines, and unrelated formatting are preserved.
4. Reparse + relint + reproject happens after patch application.
5. The semantic delta contains the expected create/update/delete/move fact.
6. Ambiguous duplicate items require source identity instead of deleting by
   semantic equality alone.

### 7. Wrong vs Correct

#### Wrong

```cpp
graph.events["OnStart"].steps.push_back(CallStep{"patrol.start"});
save_graph_as_text(graph);
```

#### Correct

```cpp
auto result = edit_session.push_step(
    FlowRef{"events.OnStart"},
    StepSpec::call("patrol.start"));
if (result.is_err()) return result;

// push_step created a TextPatch, reparsed the source, and verified the new
// semantic call fact before committing the new document snapshot.
```

### Pattern: Graph Validation via Positive Checking

Validate references by using projected graph/domain facts and source bindings,
rather than blacklisting specific forbidden strings:

```cpp
auto check_edge = [&](const PinRef& from, const PinRef& to) -> Diagnostic {
    auto source = graph_model.resolve_pin(from);
    auto target = graph_model.resolve_pin(to);
    if (!source || !target) return diagnostic_with_source_range(from.range);
    return connection_policy.check(*source, *target);
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
1. Creates the feature via text or source-bound edit operation.
2. Applies a formatter or text patch.
3. Re-parses, re-lints, and re-projects.
4. Verifies structural equivalence and preservation of unrelated source text.

### Don't: Mutate Graph State Without A Source-Bound Operation

Every graph-facing edit must be expressible as a source-bound document
operation. A visual-only mutation that cannot map back to source violates the
collaboration model:

```cpp
Result<void, std::string> EditSession::move_node(...) {
    auto binding = graph_model.source_binding(node_id);
    if (!binding) return err("node has no source binding");
    return document_ops.set_property(binding->editor_position, new_position);
}
```

### Don't: Reference Module-Level `let` in Graph Logic

Legacy `let` declarations were module-level constants. Current asset syntax
should model graph inputs and serialized values through explicit document
structures, references, and schema/domain rules instead of reintroducing hidden
cross-boundary state.

### Don't: Reference Nodes in Functions

Function-like domain constructs must define their reference boundary in the
Graph domain or the relevant domain spec. Do not let a visual editor create
references that cannot be diagnosed and patched back to source.

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

**Fix**: Before committing a fixture, verify every graph edge or property
binding has a source-bound target that the Graph domain can project and diagnose.
See [Current Graph Domain](./graph-domain.md).

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
│   ├── core/               # Shared result, ranges, IDs, and low-level contracts
│   ├── edit/               # EditGraph (SlotMap), EditSession, Handle
│   ├── asset/              # Serialization parser, CST/AST facade, linter, patcher
│   ├── graph/              # Graph domain projection and runtime-facing models
│   ├── registry/           # Environment (TypeRegistry, NodeRegistry, SchemaRegistry)
│   └── schema/             # ConnectionPolicy, GraphSchema, Validator
├── src/                    # Implementation files (mirror include/ structure)
├── cli/                    # CLI tool: main, editor (REPL), server (HTTP)
├── web/                    # Web frontend: index.html (LiteGraph.js)
├── tests/                  # GoogleTest suites + fixture files
│   └── fixtures/           # .gs and .d.gs test data
├── docs/spec/              # Product, language, architecture, syntax workspace
└── CMakeLists.txt          # Build configuration
```

### Naming Conventions

- **Headers**: `snake_case.h` in nested namespace dirs
- **Sources**: `snake_case.cpp` matching header structure
- **Classes**: `PascalCase` (`EditSession`, `GraphRuntimeIR`)
- **Functions**: `snake_case` (`add_node`, `compile_graph`)
- **Members**: `snake_case_` with trailing underscore
- **Test names**: `TEST(Suite, DescriptiveTestName)` in PascalCase
