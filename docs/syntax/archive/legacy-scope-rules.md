# GraphScript Scope Rules

> Block-level scope isolation for flow and data-assignment references.

---

## Principle

Every reference in a flow or data-assignment statement must resolve to an entity that exists within the current block's scope. References that point outside the scope boundary are **compilation errors**.

---

## Scope Definitions

### Function Scope (Restrictive)

Functions are **self-contained** logic blocks. They cannot access graph-level state.

**Allowed references:**
- `context` — the implicit execution context
- Graph parameter names (`in`, `out`, `var` declarations)

**Forbidden:**
- Graph-level node instance names
- Module-level `let` variable names
- Any undefined name

```gs
Graph MyGraph {
    in health : int;
    out score : int;
    PrintString logger{};    // graph-level node

    function Calculate {
        context.start(context.done);        // ✓ context
        context.result = score;              // ✓ parameter

        // context.start(logger.enter);      // ✗ COMPILE ERROR: node instance
        // logger.message = health;          // ✗ COMPILE ERROR: node instance
    }
}
```

### Event Scope (Permissive)

Events are entry points that wire up the graph's node instances. They have broader access.

**Allowed references:**
- `context` — the implicit execution context
- Graph parameter names
- Graph-level node instance names

**Forbidden:**
- Module-level `let` variable names (cross-scope)
- Any undefined name (dangling reference)

```gs
let global_val = FVector("0,0,0");    // module scope

Graph MyGraph {
    in health : int;
    PrintString logger{};

    event OnStart {
        context.start(logger.enter);         // ✓ node instance
        logger.message = health;             // ✓ parameter

        // logger.message = global_val;      // ✗ COMPILE ERROR: module-level let
        // logger.message = ghost;           // ✗ COMPILE ERROR: undefined name
    }
}
```

---

## Enforcement Points

The scope rules are enforced at **three levels**:

### 1. Asset semantic projection / lint (authoritative)

Files: `src/asset/language.cpp` and the graph projection / edit adapter path.

After projecting each graph, semantic validation builds allowed-name sets and validates every flow/data-assignment reference:

```
param_names = {"context"} ∪ {p.name for p in graph.parameters}
node_names  = {ni.instance_name for ni in graph.node_instances}

For events:  allowed = param_names ∪ node_names
For functions: allowed = param_names only
```

Any reference not in the allowed set produces a `Result::err()` with a descriptive message.

### 2. EditSession (editing-time, interactive)

File: `src/edit/edit_session.cpp` — `check_block_scope()`

Called from `add_flow()` and `add_link()` before any mutation occurs. Prevents invalid references from ever entering the `Module` data.

### 3. Unit Tests (regression guard)

File: `tests/test_edit_session.cpp`

| Test | Verifies |
|------|----------|
| `FunctionCannotReferenceGraphNodes` | Function → node instance blocked; function → context/param allowed; event → node allowed |
| `EventCannotReferenceDanglingNames` | Event → undefined name blocked |

---

## Why Functions Are Restricted

Functions serve a different purpose than events:

| Aspect | Event | Function |
|--------|-------|----------|
| **Purpose** | Wire up graph nodes as entry points | Self-contained callable logic |
| **Node access** | Full access to graph nodes | No access — context + params only |
| **Analogy** | UE Blueprint Event Graph | UE Blueprint Function |
| **Composability** | Tied to specific graph instance | Portable, reusable |

This mirrors the UE Blueprint distinction where functions are pure parameter transformations and events are graph-level wirings.

---

## Why `let` Variables Are Not Accessible

Module-level `let` declarations (`let x = Type("...")`) exist at **module scope**, not graph scope. Allowing graph logic blocks to reference them would create cross-scope dependencies that:

1. Break graph portability (graph depends on specific module-level state)
2. Complicate round-trip semantics (emit must track external dependencies)
3. Violate the principle that a graph's behavior is fully defined by its parameters and nodes

If a graph needs a value, declare it as a parameter with a default:

```gs
// ✗ Don't: use module-level let in graph
let spawn_point = FVector("0,0,0");
Graph G {
    event OnStart { spawner.location = spawn_point; }  // COMPILE ERROR
}

// ✓ Do: declare as graph parameter
Graph G {
    in spawn_point : FVector;
    event OnStart { spawner.location = spawn_point; }  // OK
}
```

---

## Error Messages

| Violation | Error Message Format |
|-----------|---------------------|
| Function references node | `function 'X': cannot reference 'Y' (only context and parameters allowed)` |
| Event references undefined name | `event 'X': unknown reference 'Y' (must be context, a parameter, or a node instance)` |
