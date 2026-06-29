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
│  Parse · Compile · EditGraph · EditSession · RuntimeGraph · Emitter │
│  (parse/ compile/ edit/ runtime/ emit/ core/ registry/)             │
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
  Lexer         → vector<Token>         (parse/lexer.h)
    │
    ▼
  Parser        → ModuleNode (AST)      (parse/parser.h)
    │
    ▼
  Compiler      → Module (IR)           (compile/compiler.h)
    │               │
    │               ├── Module.graphs[]    → Graph objects
    │               ├── Module.imports[]   → ImportDecl
    │               └── Module.top_level_lets[] → LetDecl
    │
    ▼
  Environment   ← types, nodes, schemas registered
```

### Graph → Text (Round-trip)

```
Module/Graph
    │
    ▼
  Emitter       → .gs source text       (emit/emitter.h)
    │
    ▼
  Re-parse + Re-compile → structurally equivalent Module
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
  RuntimeGraph.bake(edit_graph)
    │
    ▼
  RuntimeGraph  (flat arrays, integer indices, immutable)
```

---

## Core Data Structures

### Module (compile/compiler.h)

The top-level compilation result. Contains everything in one `.gs` file.

| Field | Type | Description |
|-------|------|-------------|
| `file_path` | `string` | Source file path |
| `imports` | `vector<ImportDecl>` | `import` declarations |
| `top_level_lets` | `vector<LetDecl>` | `let` declarations |
| `graphs` | `vector<Graph>` | Compiled graphs |

### ImportDecl / LetDecl (compile/compiler.h)

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

### RuntimeGraph (runtime/runtime_graph.h)

Baked, immutable, cache-friendly graph for runtime consumption.

| Feature | Implementation |
|---------|---------------|
| Nodes | `vector<RNode>` (flat array) |
| Pins | `vector<RPin>` (flat array) |
| Flow edges | `vector<RFlowEdge>` (integer indices) |
| Data edges | `vector<RDataEdge>` (integer indices) |

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

All three are populated by the Compiler when processing `declare type`, `declare Node`, and `declare Schema` statements in `.d.gs` files.

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

| Subcommand | Description |
|------------|-------------|
| `parse` | Tokenize and parse, print AST summary |
| `compile` | Parse + compile, print Module summary |
| `validate` | Compile + schema validate |
| `emit` | Compile + emit back to .gs text |
| `bake` | Compile + EditGraph.build + RuntimeGraph.bake |
| `info` | Show environment (types, nodes, schemas) |
| `schema` | Show registered schemas |
| `edit` | Interactive REPL editor |
| `serve` | Web GUI editor (HTTP server + LiteGraph.js) |
| `diagram` | Generate Mermaid markdown diagram |

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
