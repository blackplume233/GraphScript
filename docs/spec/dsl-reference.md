# GraphScript DSL Reference

> Syntax and semantics for `.gs` (graph script) and `.d.gs` (declaration) files.

---

## File Types

| Extension | Purpose | Contains |
|-----------|---------|----------|
| `.gs` | Graph definition | imports, lets, Graph blocks |
| `.d.gs` | Declaration (interface-only) | `declare type`, `declare Node`, `declare Schema` |

`.d.gs` files are the host's way of injecting type/node/schema knowledge into the GraphScript environment. They are parsed but do not produce executable graphs.

---

## Module Structure

A `.gs` file compiles to a `Module` with this top-level structure:

```
import "path.d.gs";
import "other.gs";

let name = Type("constructor_arg");

Graph GraphName : OptionalSchemaBase {
    // ... graph body ...
}
```

### import

```gs
import "ue_core.d.gs";
import "htn_nodes.d.gs";
```

Loads another file's declarations into the environment. The Compiler processes imports to register types/nodes/schemas.

### let

```gs
let spawn_point = FVector("0,0,100");
let actor_path = SoftObjectPath("/Game/Maps/Level");
```

Module-level constant declarations. **Cannot** be referenced inside graph logic blocks (events/functions) — they exist at module scope only.

---

## Graph Definition

```gs
Graph MyGraph : OptionalSchemaBase {
    // Parameters
    in health : int;
    in name : FString = "default";
    out alive : bool;
    var temp_buffer : float;

    // Node instances
    PrintString logger{};
    Delay timer{};

    // Events (entry points)
    event OnStart { ... }
    event OnDamage { ... }

    // Functions (self-contained, no node access)
    function Calculate { ... }

    // Editor metadata
    generate { ... }
}
```

### Parameters

```gs
in <name> : <type>;               // Input parameter
in <name> : <type> = <default>;   // Input with default value
out <name> : <type>;              // Output parameter
var <name> : <type>;              // Local variable (excluded from NodeDefinition derivation)
```

Directions: `in` | `out` | `var`

### Node Instances

```gs
PrintString logger{};
Delay timer{};
MySubGraph sub{};
```

Format: `<TypeName> <instanceName>{<optional_initializer>};`

The type must exist in `NodeRegistry` (either native from `.d.gs` or derived from a compiled Graph).

### Event Blocks

Events are entry points that wire up graph-level nodes:

```gs
event OnStart {
    // Flow connections (exec wiring)
    context.start(logger.enter);
    logger.exit(timer.enter);

    // Data links
    link logger.message = name;
    link timer.duration = temp_buffer;
}
```

**Scope**: Events can reference `context`, parameter names, and node instance names.

### Function Blocks

Functions are self-contained logic blocks — **cannot reference graph-level node instances**:

```gs
function CalculateScore {
    context.start(context.done);
    link context.result = score;      // 'score' must be a parameter
}
```

**Scope**: Functions can only reference `context` and parameter names. See [Scope Rules](./scope-rules.md).

### Annotations (C# Attribute Style)

Annotations attach metadata (position, comments, UI hints) to elements using C#-style `[Name(args)]` prefix syntax. They are placed on the line **immediately before** the decorated element.

**Supported targets:** Graph definitions, node instances, parameters.

```gs
// Graph-level annotation
[Comment("title", "My graph description")]
Graph MyGraph {
    // Parameter annotation
    [Tooltip("Player health")]
    in health : int;

    // Node instance annotation (positional args)
    [Position(X = 100, Y = 200)]
    PrintString logger{};

    // Multiple annotations on one element
    [Position(X = 300, Y = 200), Color("blue")]
    Delay timer{};
}
```

**Argument styles:**

| Style | Example | AnnotationArg |
|-------|---------|---------------|
| Positional | `[Comment("text")]` | `{ name: "", value: "text" }` |
| Named | `[Position(X = 100)]` | `{ name: "X", value: "100" }` |
| Mixed | `[Foo("a", Key = 42)]` | Both styles in one annotation |

**Combining annotations:** Multiple annotations in one bracket set use commas:
```gs
[Position(X = 100, Y = 200), Comment("note", "helper node")]
PrintString logger{};
```

### Generate Block (Legacy)

The `generate` block is retained for backward compatibility but new code should use annotations:

```gs
generate {
    Comment title = "deprecated - use [Comment()] instead";
    position:logger.x(100);
    position:logger.y(200);
}
```

---

## Flow Statements

Connect exec pins between nodes:

```gs
context.start(logger.enter);     // context → logger
logger.exit(timer.enter);         // logger → timer
timer.completed(logger.enter);    // timer → logger (loop)
```

Format: `<from_node>.<from_pin>(<to_node>.<to_pin>);`

Special node `context` has pins: `start`, `done`, and other context-defined pins.

---

## Link Statements

Wire data between pins:

```gs
link logger.message = name;           // bare param (no dot = parameter reference)
link logger.message = health;         // parameter reference
link timer.duration = temp_buffer;    // var parameter
link sub.value = context.result;      // context pin
```

Format: `link <target_node>.<target_pin> = <source>;`

Source can be:
- `<param_name>` — bare parameter reference (source_pin empty)
- `<node>.<pin>` — node instance pin reference

---

## Declaration Files (.d.gs)

### declare type

```gs
declare type int;                    // non-constructible
declare type FString constructible;  // can be used in let/initializer
declare type AActor;
```

### declare Node

```gs
declare Node PrintString {
    exec in enter;
    exec out exit;
    data in message : FString;
}

declare Node Delay {
    exec in enter;
    exec out completed;
    data in duration : float;
}
```

Pin declarations:
- `exec in <name>;` — Exec input pin
- `exec out <name>;` — Exec output pin
- `data in <name> : <type>;` — Data input pin
- `data out <name> : <type>;` — Data output pin

### declare Schema

```gs
declare Schema HTNGraph {
    max_exec_fan_out = unlimited;
    allow_exec_fan_in = false;
}

declare Schema TaskGraph {
    max_exec_fan_out = 1;
    allow_exec_fan_in = true;
}

declare Schema LevelScriptGraph {
    max_exec_fan_out = 1;
    allow_exec_fan_in = true;
    strict_type_match = false;
}
```

Schema properties:

| Property | Values | Default | Description |
|----------|--------|---------|-------------|
| `max_exec_fan_out` | integer or `unlimited` | 1 | Max outgoing exec connections per pin |
| `allow_exec_fan_in` | `true` / `false` | true | Allow multiple exec inputs to one pin |
| `strict_type_match` | `true` / `false` | false | Require exact type match on data links |
| `allowed_node_tags` | string array | [] | Filter available node types |
| `required_events` | string array | [] | Events that must exist |

---

## Tokens and Literals

### Keywords
`import`, `let`, `declare`, `type`, `constructible`, `Node`, `Schema`, `Graph`, `in`, `out`, `var`, `event`, `function`, `generate`, `link`, `exec`, `data`, `Comment`, `position`, `true`, `false`

### Annotation Brackets
`[` and `]` delimit annotation lists. Inside annotations, `(` `)` delimit arguments, `=` separates named arg keys from values, and `,` separates multiple arguments or annotations.

### Literals
- **Integer**: `42`, `-1`, `100`
- **Float**: `1.0`, `-3.14`, `0.5`
- **String**: `"hello"`, `"/Game/Maps/Level"`
- **Boolean**: `true`, `false`

### Identifiers
Start with letter or `_`, followed by letters, digits, or `_`. Examples: `MyGraph`, `health`, `spawn_point`

### Comments
```gs
// Line comment

/* Block
   comment */
```
