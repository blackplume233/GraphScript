# Current Serialization Syntax

> Current source-language shape for AI Native game asset serialization.

This document describes current syntax structure and syntax decisions. Design
review standards and implementation expectations live in `.trellis/spec/`.

---

## Purpose

The base language records static game asset facts:

- file and import declarations
- asset/scope boundaries
- fragments that contribute to logical assets
- typed objects
- named properties
- static values
- references
- attributes/metadata
- declarations and schemas

Graph, FlowGraph, table, dialogue, quest, HTN, and level-script concepts are
domain interpretations of those facts.

---

## Current Base Concepts

```text
source_file
import_declaration
scope_declaration
const_declaration
object_expression
property_declaration
value_expression
reference_expression
attribute
declaration
schema
syntax_error / missing node
```

These concepts are allowed in the base CST/AST facade.

These concepts are not base parser concepts:

```text
Graph
GraphNode
Entry
Pin
ExecPin
DataPin
Edge
Flow
Link
HTNTask
TableRow
DialogueBranch
```

Domain layers may use these names in projection models, diagnostics, UI state,
and runtime IR.

---

## Current Authoring Shape

```ts
import "ability_core.d.gs";

@id("ability.fireball")
scope asset Fireball: Ability {
    name: "Fireball";

    const damage = new DamageEffect {
        amount: 50;
        type: DamageType.Fire;
    }

    scope graph Execute: AbilityGraph {
        const apply = new ApplyDamage {
            effect: damage;
            editor.pos: [100, 100];
        }

        scope entry Start {
            context.start.connect(apply.enter);
        }
    }
}
```

Important decisions:

- `scope` is the generic boundary syntax.
- `asset`, `graph`, `entry`, `table`, and similar words are scope kind
  identifiers interpreted by semantic/domain layers.
- `const alias = new Type { ... }` creates a typed serialized object.
- `property: value` records static serialized data.
- `.connect(...)` is a restricted command call, not arbitrary runtime execution.
- Attributes are the primary extension mechanism for stable identity, editor
  metadata, flow metadata, table metadata, and deprecation.

---

## Current Decisions

### Stable Identity

Use declaration-prefix attributes as the current default:

```ts
@id("01J2FIREBALLAPPLY")
const apply = new ApplyDamage {
    amount: 50;
}
```

Older suffix examples such as `const apply @id(...) = ...` are design-history
material unless explicitly reintroduced. Tools may support migration from suffix
forms, but new current syntax should prefer prefix attributes.

### Object Fields And Pins

FlowGraph pins are expressed as object fields plus attributes in `.d.gs`:

```ts
export declare object ApplyDamage {
    @flow.input
    amount: float = 0.0;

    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
}
```

The base parser sees attributes and fields. Graph/FlowGraph projection decides
which fields become pins.

### Directive Statements

Directive-like statements are a compatibility or schema-extension mechanism, not
the preferred core reversible authoring form. The core reversible subset should
prefer properties, attributes, restricted command calls, and declarations.

If a directive is retained, the parser should treat it as a generic directive,
not as a hard-coded graph primitive.

### Scope Kind Declarations

Scope kind declarations describe the allowed kind and its schema hooks:

```ts
export declare scope graph: FlowGraphScope {
    allows object any;
    allows scope entry;
    allows command connect;
}
```

Declarations that look like `declare scope graph AbilityGraph` are design-draft
material until the grammar assigns them a precise role.

---

## Reversible Authoring Subset

Allowed in the core reversible subset:

```text
scope
const = new Type { ... }
property: static value
ref "asset/path"
array literal
inline object literal
qualified reference
restricted command call
assignment when schema allows it
attribute
comment
```

Not allowed in the core reversible subset:

```text
if / for / while
lambda
user function
spread
computed property
dynamic type selection
arbitrary function calls in expressions
operator expressions such as a + b
import-time execution
```

Non-reversible generation layers may exist later, but they must not be confused
with the canonical human/AI collaborative source.

---

## Design Drafts

Long-form design history remains in:

- [AI Native Syntax Draft](../design/ai-native-syntax-draft.md)
- [AI Native Asset Format](../design/ai-native-asset-format.md)
