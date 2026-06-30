# GraphScript Architecture

> Data model, compilation pipeline, and layer boundaries.

---

## Layer Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                          Domain Layer                               │
│  .d.gs files: declare type, declare Node, declare Schema            │
│  (htn_nodes.d.gs, task_nodes.d.gs, levelscript_nodes.d.gs, ...)    │
├─────────────────────────────────────────────────────────────────────┤
│                       Schema Framework                              │
│  ConnectionPolicy · GraphSchema · SchemaRegistry · Validator        │
│  (schema/)  — defines rule containers, no concrete rules            │
├─────────────────────────────────────────────────────────────────────┤
│                        Core Engine                                  │
│  Asset Parser · Projection · EditSession · GraphRuntimeIR           │
│  (asset/ edit/ graph/ core/ registry/)                              │
├─────────────────────────────────────────────────────────────────────┤
│                       CLI / Web Frontend                            │
│  gs CLI · CLIEditor · WebServer · LiteGraph.js UI                   │
│  (cli/ web/)                                                        │
└─────────────────────────────────────────────────────────────────────┘
```

**Invariant**: Core Engine and Schema Framework code must **never** contain domain-specific names (HTN, Task, LevelScript, Cinematic, etc.).

---

## Compilation Pipeline

### Text → Graph

```
Source (.gs)
    │
    ▼
  tree-sitter asset parser
    │
    ▼
  asset::Module                    (asset/language.h)
    │               │
    │               ├── imports / declarations
    │               ├── block/property/command/expr items
    │               └── graph blocks
    │
    ▼
  asset lint + FlowGraphProjector
    │
    ▼
  FlowGraph / EditSession adapter / Environment declarations
```

### Graph → Text (Round-trip)

```
asset source
    │
    ▼
  source patch / asset source emission
    │
    ▼
  Re-parse + re-project → structurally equivalent asset graph
```

### Graph → Editor

```
Graph (from Module)
    │
    ▼
  EditGraph.build(graph, env)
    │
    ▼
  EditGraph     (SlotMap<EditNode>, SlotMap<EditConnection>)
    │               Stable generational handles
    │               Schema-aware connection validation
    │
    ▼
  FlowGraphProjector.project(module)
    │
    ▼
  GraphRuntimeIR.bake(flow_graph)
    │
    ▼
  GraphRuntimeIR  (flat arrays, integer indices, immutable)
```

---

## Core Data Structures

### Module (core/module.h)

The top-level compilation result. Contains everything in one `.gs` file.

| Field | Type | Description |
|-------|------|-------------|
| `file_path` | `string` | Source file path |
| `imports` | `vector<ImportDecl>` | `import` declarations |
| `top_level_lets` | `vector<LetDecl>` | `let` declarations |
| `graphs` | `vector<Graph>` | Compiled graphs |

### ImportDecl / LetDecl (core/module.h)

Top-level declarations retain source traceability and prefix metadata.

| Field | Type | Description |
|-------|------|-------------|
| `annotations` | `vector<Annotation>` | Prefix metadata annotations |
| `source_range` | `SourceRange` | Source span of the declaration |
| `name_range` | `SourceRange` | `let` binding name span |
| `type_name_range` | `SourceRange` | `let` constructible type reference span |

### Graph (core/graph.h)

A single graph definition, the primary unit of editing.

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Graph name |
| `base_type` | `optional<string>` | Schema type (e.g. "HTNGraph") |
| `parameters` | `vector<GraphParameter>` | in/out/var params |
| `node_instances` | `vector<NodeInstance>` | Instantiated nodes |
| `events` | `vector<Event>` | Event logic blocks |
| `functions` | `vector<Function>` | Function logic blocks |
| `generate` | `optional<GenerateBlock>` | Editor metadata |

### LogicBlock (base of Event/Function)

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Block name |
| `annotations` | `vector<Annotation>` | Prefix metadata annotations |
| `flow_connections` | `vector<FlowConnection>` | Exec flow edges |
| `data_links` | `vector<DataLink>` | Data wiring |

### FlowConnection / DataLink (core/connection.h)

```cpp
struct FlowConnection {
    PinAddress from;  // {node_instance, pin_name}
    PinAddress to;
    vector<Annotation> annotations;
};

struct DataLink {
    PinAddress target;  // node.pin being written to
    DataSource source;  // node.pin or bare param name
    vector<Annotation> annotations;
};
```

### NodeDefinition (core/node.h)

Defines a node type's pins. Can be `is_native` (from `.d.gs`) or derived from a Graph.

Declaration-file node and pin definitions retain prefix metadata annotations for JSON traceability. `TypeInfo`, `NodeDefinition`, `PinDefinition`, `GraphSchema`, and `GraphSchemaField` can carry `vector<Annotation>` alongside source/name ranges.

### EditGraph (edit/edit_graph.h)

Mutable, editor-friendly graph using `SlotMap` for O(1) node/connection operations with stable handles.

| Feature | Implementation |
|---------|---------------|
| Node storage | `SlotMap<EditNode>` |
| Connection storage | `SlotMap<EditConnection>` |
| Handle stability | Generational `Handle` (index + generation) |
| Schema enforcement | `ConnectionPolicy` checked on `connect()` |

### GraphRuntimeIR (graph/runtime_ir.h)

Baked, immutable, cache-friendly graph for runtime consumption.

| Feature | Implementation |
|---------|---------------|
| Nodes | `vector<RuntimeIRNode>` (flat array) |
| Pins | `vector<RuntimeIRPin>` (flat array) |
| Flow edges | `vector<RuntimeIRFlowEdge>` (integer indices) |
| Data edges | `vector<RuntimeIRDataEdge>` (integer indices) |

### EditSession (edit/edit_session.h)

Stateful editing context wrapping a `Module`. Provides:
- Graph CRUD (new, delete, switch active)
- Node/param/event/function manipulation
- Flow/data-assignment operations with **scope validation**
- Snapshot-based undo/redo
- Command logging
- JSON state export for web UI
- File I/O (load/save/import)

---

## Environment & Registries

`Environment` (registry/environment.h) is the central context holding:

| Registry | Purpose |
|----------|---------|
| `TypeRegistry` | Type definitions (`int`, `float`, `AActor`, ...) with `constructible` flag |
| `NodeRegistry` | Node definitions (native from `.d.gs` + derived from Graph-as-Node) |
| `SchemaRegistry` | GraphSchema definitions with ConnectionPolicy |

All three are populated by the asset declaration loading path when processing `.d.gs` declaration files.

---

## Graph-as-Node

When a `Graph` is compiled, a `NodeDefinition` is automatically derived:
- `in` params → Data input pins
- `out` params → Data output pins
- `var` params → excluded
- Events → Exec input pins

This enables **composable sub-graphs**: `Graph A` can instantiate `Graph B` as a node.

---

## Schema System

Schemas define domain-specific rules without modifying core code:

```
declare Schema HTNGraph {
    max_exec_fan_out = unlimited;
    allow_exec_fan_in = false;
}
```

Schema declarations and individual schema fields can carry prefix annotations, which are preserved in registry metadata and exported through JSON state.

| Schema Property | Type | Effect |
|-----------------|------|--------|
| `max_exec_fan_out` | int or "unlimited" | Limits outgoing exec connections per pin |
| `allow_exec_fan_in` | bool | Whether multiple exec edges can target one input |
| `strict_type_match` | bool | Whether data connections require exact type match |
| `allowed_node_tags` | string[] | Filter available node types |
| `required_events` | string[] | Events that must exist in the graph |

---

## CLI Tool (`gs`)

### Scenario: tree-sitter asset CLI surface

#### 1. Scope / Trigger

- Trigger: migration to tree-sitter asset `.gs/.d.gs` syntax changes the supported CLI command surface.
- Scope: user-facing `gs` subcommands. Editor/server entry points use the tree-sitter asset path and may expose additional replay/debug commands only inside the interactive `edit` surface; those are not top-level CLI commands.

#### 2. Signatures

| Subcommand | Signature | Description |
|------------|-----------|-------------|
| `parse` | `gs parse -i file.gs` | Parse `.gs/.d.gs` asset syntax and print syntax summary. |
| `lint` | `gs lint -i file.gs [-I import.d.gs...]` | Parse and lint asset syntax, returning diagnostics JSON. |
| `project` | `gs project -i file.gs [-I import.d.gs...] --graph Name` | Project a graph block to FlowGraph JSON summary. |
| `patch` | `gs patch -i file.gs --op <op> [patch options]` | Apply a tree-sitter-aware source patch and write to stdout or `-o`. |
| `edit` | `gs edit [-i file.gs] [-I import.d.gs...]` | Start the interactive editor. |
| `serve` | `gs serve [-i file.gs] [-I import.d.gs...] [-p 8080]` | Start the web editor server. |

Supported patch ops: `add-import`, `add-node`, `add-block`, `add-attribute`, `connect`, `disconnect`, `rename-node`, `rename-block`, `set-property`.

#### 3. Contracts

- Current file suffixes are `.gs` and `.d.gs`.
- `parse/lint/project/patch` use the tree-sitter asset parser path.
- `project` consumes imports by merging asset declarations before graph projection.
- `patch` prints patched source to stdout unless `-o/--output` is supplied.
- Legacy command names are not accepted user commands: `compile`, `validate`, `emit`, `diagram`, `bake`, `info`, `schema`, and all `sc-*`.

#### 4. Validation & Error Matrix

| Condition | Expected behavior |
|-----------|-------------------|
| Unknown or legacy command | Exit non-zero with `Unknown command: <name>`. |
| Asset command without `-i` | Exit non-zero with `Error: -i <input_file> required`. |
| Missing graph in `project`/`patch` | Exit non-zero with projection or patch error. |
| Invalid source syntax | `parse/lint` include diagnostics and return non-zero. |
| Patch edit outside source range | Exit non-zero with patch error. |

#### 5. Good/Base/Bad Cases

- Good: `gs project -i ability.gs -I ability_core.d.gs --graph Execute` returns graph name, schema, node count, edge count, and diagnostics count.
- Base: `gs parse -i ability.d.gs` returns declaration counts and diagnostics count.
- Bad: `gs compile -i ability.gs` returns `Unknown command: compile`.

#### 6. Tests Required

- CLI help smoke asserts only `edit/serve/parse/lint/project/patch` are listed.
- Unknown-command smoke asserts removed legacy commands return `Unknown command`.
- Parse/lint/project/patch smoke tests use `.gs/.d.gs` fixtures.
- Full C++ test suite must pass after command-surface changes.

#### 7. Wrong vs Correct

Wrong:

```bash
gs compile -i ability.gs
gs sc-project -i ability.gs --graph Execute
```

Correct:

```bash
gs lint -i ability.gs
gs project -i ability.gs -I ability_core.d.gs --graph Execute
```

---

## Web Editor Architecture

```
Browser (LiteGraph.js)
    │
    │  HTTP REST API
    │
    ▼
WebServer (cpp-httplib)
    │
    │  std::mutex protected
    │
    ▼
EditSession
    │
    ▼
Module (source of truth)
```

Every GUI action maps to a CLI command string → sent to `/api/exec` → executed by `CLIEditor.execute()` → state returned as JSON → UI re-rendered.

Command log is maintained for full replay capability.
