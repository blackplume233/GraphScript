# 当前序列化语法

> 当前 AI Native 游戏资产序列化源语言形状。

本文描述当前仓库已经使用的语法结构和语法决策。设计审查标准和实现期望位
于 `.trellis/spec/`。

本文是当前实现侧语法快照。目标语法实验单独记录在 `../design/`，尤其是
那些有意区别于当前解析器或格式化器的草稿。

---

## 目的

基础语言记录静态游戏资产事实：

- file 和 import declaration。
- asset/scope 边界。
- 贡献到逻辑资产的 fragment。
- typed object。
- named property。
- static value。
- reference。
- attribute/metadata。
- declaration 和 schema。

Graph、FlowGraph、table、dialogue、quest、HTN 和 level-script 概念都是这
些事实的领域解释。

---

## 当前基础概念

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

这些概念允许出现在基础 CST/AST facade 中。

这些概念不是基础 parser 概念：

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

领域层可以在 projection model、diagnostics、UI state 和 runtime IR 中使用
这些名称。

---

## 当前创作形状

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

重要决策：

- `scope` 是当前通用边界语法。
- `asset`、`graph`、`entry`、`table` 等词是 scope kind identifier，由
  semantic/domain 层解释。
- `const alias = new Type { ... }` 创建 typed serialized object。
- `property: value` 记录静态序列化数据。
- `.connect(...)` 是受限 command call，不是任意 runtime execution。
- Attribute 是 stable identity、editor metadata、flow metadata、table
  metadata 和 deprecation 的主要扩展机制。

---

## 当前决策

### Stable Identity

当前默认使用 declaration-prefix attribute：

```ts
@id("01J2FIREBALLAPPLY")
const apply = new ApplyDamage {
    amount: 50;
}
```

旧的 suffix 示例，例如 `const apply @id(...) = ...`，属于设计历史材料，除非
显式重新引入。工具可以支持从 suffix 形式迁移，但新的 current syntax 应优
先使用 prefix attribute。

### Object Fields And Pins

FlowGraph pin 在 `.d.gs` 中通过 object field 加 attribute 表达：

```ts
export declare object ApplyDamage {
    @flow.input
    amount: float = 0.0;

    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
}
```

基础 parser 只看到 attribute 和 field。Graph/FlowGraph projection 决定哪些
field 会成为 pin。

### Directive Statements

Directive-like statement 是兼容机制或 schema-extension 机制，不是当前偏好
的 core reversible authoring form。核心可逆子集应优先使用 property、
attribute、restricted command call 和 declaration。

如果保留 directive，parser 应把它视为 generic directive，而不是 hard-coded
graph primitive。

### Scope Kind Declarations

Scope kind declaration 描述允许的 kind 及其 schema hook：

```ts
export declare scope graph: FlowGraphScope {
    allows object any;
    allows scope entry;
    allows command connect;
}
```

形如 `declare scope graph AbilityGraph` 的 declaration 属于设计草稿材料，
直到 grammar 为它分配精确角色。

---

## 可逆创作子集

核心可逆子集允许：

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

核心可逆子集不允许：

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

后续可以存在非可逆 generation layer，但不能把它和 canonical human/AI
collaborative source 混淆。

---

## 设计草稿

长文设计历史保留在：

- [AI Native Syntax Draft](../design/ai-native-syntax-draft.md)
- [AI Native Asset Format](../design/ai-native-asset-format.md)
