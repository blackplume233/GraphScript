# JSON 超集 DocumentNode 语法草稿

> 状态：目标语法替代草稿。
>
> 目的：评估 GraphScript 是否可以采用尽量接近 JSON/JSON5 的
> DocumentNode 源码形态。本文不是当前解析器的实现契约。

---

## 状态与范围

本文描述 `.gs` 和 `.d.gs` 的另一条目标创作语法路线：以 JSON/JSON5 object
tree 作为表层语法基础，用约定字段表达 `DocumentNode`。

本文只讨论语法形状，包括：

- 以 JSON object 表示 DocumentNode。
- command 在 JSON 结构中的表达。
- declaration、graph、node、event、property 的结构化写法。
- canonical JSON-compatible 写法与非 canonical 语法糖的边界。

不在本文范围内：

- 解析器实现。
- 运行时中间表示设计。
- Web 编辑器 JSON 合约。
- 具体 graph validator 行为。
- 项目稳定设计理念。

稳定设计理念以这些 spec 为准，本文不重新定义：

- `docs/spec/index.md`
- `docs/spec/architecture.md`
- `docs/spec/graph-domain.md`

---

## 语法设计思路与方向

方向是：

```text
JSON/JSON5-compatible object tree + DocumentNode 约定字段
```

JSON5 是 JSON 的扩展，面向更易手写维护的配置文件，支持注释、尾逗号、
未加引号的对象 key、单引号字符串等能力。本文借鉴 JSON5 的 human-friendly
配置文件方向，但把 GraphScript 自己的语义限制在 `DocumentNode` 约定字段
中。

核心目标是：

- 合法 JSON 文件应天然可以成为合法 GraphScript 输入。
- GraphScript 源码应尽量保持 object / array / scalar 的 JSON 数据模型。
- `kind`、`type`、`id`、`children`、`commands` 等字段表达 DocumentNode。
- 能被 JSON Pointer、object path、stable id 和普通结构化 patch 直接定位。

这条路线的主要代价是：命令和类型化语法糖必须结构化。越接近严格 JSON，
越不能使用 `connect(a, b)`、`printer: PrintString` 这类 DSL 形态。

---

## 核心心智模型

每个有身份的对象都是一个 `DocumentNode`：

```text
DocumentNode {
    kind?: string | symbol
    type?: string | symbol
    id?: string
    props?: object
    children?: object | array
    commands?: Command[]
    meta?: object
}
```

为了接近 JSON object tree，本文允许两种组织形式：

1. **内联字段形式**：节点的 properties 直接写在同一个 object 中。
2. **显式 children 形式**：子节点放在 `children` 字段中。

内联字段形式更短：

```json5
{
  printer: {
    kind: "node",
    type: "PrintString",
    message: "Hello",
  },
}
```

显式 children 形式更规整：

```json5
{
  printer: {
    kind: "node",
    type: "PrintString",
    props: {
      message: "Hello",
    },
  },
}
```

草稿推荐：**canonical 使用显式字段，允许简单场景使用内联 property，但
formatter 可选择规整成显式字段。**

---

## 语法总览

最小 graph：

```json5
{
  HelloWorld: {
    kind: "graph",

    children: {
      message: {
        kind: "input",
        type: "FString",
      },

      printer: {
        kind: "node",
        type: "PrintString",
        props: {
          message: "Hello",
          editor: { pos: [100, 200] },
        },
      },

      OnStart: {
        kind: "event",
        commands: [
          { op: "connect", from: "context.start", to: "printer.enter" },
          { op: "bind", source: "message", target: "printer.message" },
        ],
      },
    },
  },
}
```

同一个节点的简化内联 property 写法：

```json5
{
  printer: {
    kind: "node",
    type: "PrintString",
    message: "Hello",
    editor: { pos: [100, 200] },
  },
}
```

---

## 完整复杂配置示例

```json5
{
  imports: [
    "ue_core.d.gs",
    "ability_nodes.d.gs",
  ],

  declarations: {
    FString: { kind: "type" },
    bool: { kind: "type" },
    float: { kind: "type" },
    Exec: { kind: "type" },

    ApplyDamage: {
      kind: "node",
      fields: {
        enter: { kind: "input", type: "Exec" },
        exit: { kind: "output", type: "Exec" },
        target: { kind: "input", type: "Actor" },
        amount: { kind: "input", type: "float", default: 0.0 },
        critical: { kind: "output", type: "bool" },
      },
    },

    PrintString: {
      kind: "node",
      fields: {
        enter: { kind: "input", type: "Exec" },
        exit: { kind: "output", type: "Exec" },
        message: { kind: "input", type: "FString", default: "" },
      },
    },

    AbilityGraph: {
      kind: "schema",
      props: {
        max_exec_fan_out: 1,
        allow_exec_fan_in: true,
        strict_type_match: true,
      },
    },
  },

  assets: {
    Fireball: {
      kind: "graph",
      type: "AbilityGraph",
      id: "ability.fireball",

      children: {
        target: { kind: "input", type: "Actor" },
        damage: { kind: "input", type: "float", default: 50.0 },
        success: { kind: "output", type: "bool" },

        apply: {
          kind: "node",
          type: "ApplyDamage",
          meta: {
            Position: { X: 100, Y: 120 },
          },
          props: {
            amount: { ref: "damage" },
          },
        },

        log: {
          kind: "node",
          type: "PrintString",
          meta: {
            Position: { X: 380, Y: 120 },
          },
          props: {
            message: "Fireball applied",
          },
        },

        Start: {
          kind: "event",
          commands: [
            { op: "connect", from: "context.start", to: "apply.enter" },
            { op: "connect", from: "apply.exit", to: "log.enter" },
            { op: "bind", source: "target", target: "apply.target" },
            { op: "bind", source: "apply.critical", target: "success" },
          ],
        },

        DebugLog: {
          kind: "function",
          commands: [
            { op: "connect", from: "context.start", to: "log.enter" },
            { op: "bind", source: "debug fireball", target: "log.message" },
          ],
        },
      },
    },
  },
}
```

这个示例刻意使用结构化 command object，保持 JSON-compatible 的数据模型。
如果允许 call expression 语法糖，它应被视为非 canonical 输入形式。

---

## Command 表达

Command 是 JSON 路线的核心难点。本文记录四种可选表达，并推荐 canonical
使用 object command。

### 方案 A：Command Object

```json5
commands: [
  { op: "connect", from: "context.start", to: "apply.enter" },
  { op: "bind", source: "target", target: "apply.target" },
]
```

优点：

- 字段名明确。
- 适合 diagnostics。
- 适合 schema validation。
- 适合局部 patch。

缺点：

- 最啰嗦。
- 人类手写 graph 时噪声较大。

这是本文推荐的 canonical command 形式。

### 方案 B：Command Tuple

```json5
commands: [
  ["connect", "context.start", "apply.enter"],
  ["bind", "target", "apply.target"],
]
```

优点：

- 比 object 短。
- 仍保持 JSON-compatible。

缺点：

- 参数靠位置解释。
- 诊断信息和可读性弱于 object。

可作为压缩输入或 debug snapshot，不建议作为 formatter 默认输出。

### 方案 C：Command String

```json5
commands: [
  "connect(context.start, apply.enter)",
  "bind(target, apply.target)",
]
```

优点：

- 最像脚本。
- 人类输入快。

缺点：

- 字符串内部需要二次解析。
- source binding 变差。
- 局部 patch 不够结构化。

不建议进入 canonical 子集。

### 方案 D：CallExpression 扩展

```json5
commands: [
  connect(context.start, apply.enter),
  bind(target, apply.target),
]
```

优点：

- 书写体验最好。
- 接近现有 graph DSL。

缺点：

- 不再是严格 JSON/JSON5-compatible。
- parser、formatter 和 schema 都必须自定义。

如果采用，应明确标成 `JSON-inspired DSL`，而不是 JSON 超集路线。

---

## 每种语法的详细解释

### Import

```json5
{
  imports: [
    "ue_core.d.gs",
    "ability_nodes.d.gs",
  ],
}
```

Import 是普通数组字段。这样最容易被 JSON 工具读取，也容易做路径级 patch。

### Declaration

```json5
{
  declarations: {
    PrintString: {
      kind: "node",
      fields: {
        enter: { kind: "input", type: "Exec" },
        exit: { kind: "output", type: "Exec" },
        message: { kind: "input", type: "FString", default: "" },
      },
    },
  },
}
```

Declaration 是命名对象表。key 是 declaration name，value 是
`DocumentNode`。

### Graph

```json5
{
  assets: {
    Fireball: {
      kind: "graph",
      type: "AbilityGraph",
      children: {},
    },
  },
}
```

Graph 是 `kind = "graph"` 的 `DocumentNode`。

### Parameter

```json5
target: { kind: "input", type: "Actor" }
success: { kind: "output", type: "bool" }
temp: { kind: "var", type: "float" }
```

Parameter 是 graph `children` 中的 bodyless `DocumentNode`。

### Graph Node

```json5
apply: {
  kind: "node",
  type: "ApplyDamage",
  props: {
    amount: { ref: "damage" },
  },
}
```

`kind` 表示结构角色，`type` 表示节点类型。

### Event 与 Function

```json5
Start: {
  kind: "event",
  commands: [
    { op: "connect", from: "context.start", to: "apply.enter" },
  ],
}
```

Event 和 function 是不同 kind 的 `DocumentNode`，其命令放在 `commands`
数组中。

### Property

```json5
props: {
  amount: 50,
  message: "Hello",
  editor: { pos: [100, 200] },
}
```

Property 是普通 object member。需要区分子节点与 property 时，使用
`children` / `props` 显式分区。

### Reference

```json5
amount: { ref: "damage" }
target: { ref: "enemy.current" }
```

Reference 使用 `{ ref: "..." }`，避免把普通 string 和 symbolic reference
混淆。

### Metadata

```json5
meta: {
  Position: { X: 100, Y: 120 },
}
```

Metadata 是结构化 object，不使用 decorator 语法。

---

## 语法糖边界

如果坚持 JSON 超集路线，以下语法糖不能进入 canonical：

```gs
printer: PrintString
connect(context.start, printer.enter)
bind(message, printer.message)
@Position(X = 100, Y = 120)
```

它们可以作为另一条 `JSON-inspired DSL` 路线，或作为非 canonical 输入扩展，
但不能同时声称是严格 JSON-compatible canonical source。

本文推荐：

- canonical source：JSON5-compatible object / array / scalar。
- optional input sugar：必须能无损 lowering 到 canonical structure。
- formatter output：默认输出结构化 canonical form。

---

## Formatter 与 Canonicalization 规则

Formatter 应输出：

```json5
{
  assets: {
    GraphName: {
      kind: "graph",
      type: "GraphSchema",
      children: {
        nodeName: {
          kind: "node",
          type: "NodeType",
        },
      },
    },
  },
}
```

Formatter 不应输出：

```gs
GraphName: GraphSchema graph { ... }
nodeName: NodeType { ... }
connect(from, to)
```

这些属于 typed-surface 或 call-expression 路线，不属于严格 JSON 超集
canonical 形式。

---

## 实现状态

当前仓库实现还不等于本文目标草稿。

当前已经实现或已有样例覆盖：

- 当前 `.gs/.d.gs` parser 和 fixtures。
- 当前 graph projection 和 source diagnostics 的部分能力。
- JSON-like Web/API state 的部分结构化表达。

本文目标但尚未实现：

- JSON5-compatible `.gs` canonical source。
- `DocumentNode` 约定字段的 parser/binder。
- command object projection。
- declaration object table。
- JSON Pointer / object path / stable id patch 策略。
- formatter 输出 JSON-compatible canonical structure。

---

## 开放问题

- 文件顶层是否必须是 object？
- `imports`、`declarations`、`assets` 是否必须固定为顶层字段？
- `kind` / `type` 是否允许裸 identifier，还是必须 string？
- `props` 和 `children` 是否强制分区，还是允许内联 property？
- command canonical 是 object 还是 tuple？
- call expression 是否完全禁止，还是允许作为非 canonical sugar？
- reference 用 `{ ref: "..." }`，还是允许 bare string path？
- JSON5 是源语法目标，还是仅作为中间/debug 格式？
