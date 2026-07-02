# 面向节点的语法草稿

> 状态：目标语法草稿。
>
> 目的：记录当前借鉴 KDL、面向节点的 GraphScript 语法方向。
> 本文不是当前解析器的实现契约。

---

## 状态与范围

本文描述 `.gs` 和 `.d.gs` 文件的一种目标创作语法。它属于设计材料；只有
后续实现任务更新解析器、投影层、格式化器、测试样例和测试后，才会成为
当前实现语法。

本文只讨论语法形状，包括：

- 图创作源码的表层写法。
- 声明文件的表层写法。
- 文档节点的语法形状。
- canonical 写法与兼容写法。

不在本文范围内：

- 解析器实现。
- 运行时中间表示设计。
- Web 编辑器 JSON 合约。
- 图状资产之外所有领域的最终语义。
- 项目稳定设计理念。

稳定设计理念以这些 spec 为准，本文不重新定义：

- `docs/spec/index.md`
- `docs/spec/architecture.md`
- `docs/spec/graph-domain.md`

如果本文语法方向与稳定 spec 存在张力，应记录为开放问题或实现风险，而不是
在本文中改写稳定原则。

---

## 语法设计思路与方向

方向是：

```text
类 KDL 文档模型 + GraphScript 类型化表层语法
```

GraphScript 应该是一种面向游戏资产的节点化文档语言。表层语法保持紧凑、
类型化；底层文档形状应自然渲染成类似 DOM 的树。

本文借鉴 KDL 的这些思路：

- 文档是一棵节点树。
- 每个节点有名称。
- 节点可以有属性。
- 节点可以有子节点。
- 面向节点的语法适合配置、数据交换和存储。

本文不采用 KDL 的完整表层语法。GraphScript 保留显式类型标注、分号结尾的
声明/属性/命令，以及更适合图领域的命令写法。

---

## 核心心智模型

目标草稿里不再有 `scope` 概念。

基础语法只有 `DocumentNode`：

```text
DocumentNode {
    name: Identifier
    type?: TypeRef
    kind?: Identifier
    attributes: Attribute[]
    properties: Property[]
    commands: Command[]
    children: DocumentNode[]
}
```

`graph`、`node`、`event`、`function`、`input`、`output`、`schema`、`type`
等词都是节点 kind，不是解析器拥有的专用 AST 家族。

文档模型里的 node 与图领域里的 node 必须区分：

- `DocumentNode`：语法和文档树节点。
- `GraphNode`：图领域投影出来的图节点。

---

## 语法总览

Canonical 图创作写法：

```gs
import "ue_core.d.gs";

HelloWorld graph {
    message: FString input;

    printer: PrintString {
        message: "Hello";
        editor.pos: [100, 200];
    }

    OnStart event {
        connect(context.start, printer.enter);
        bind(message, printer.message);
    }
}
```

需要消歧时，可以显式写出 kind：

```gs
printer: PrintString node {
    message: "Hello";
}
```

在 `graph` 内，带 type 但省略 kind 的子节点块默认解释为 `node`。

---

## 完整复杂配置示例

```gs
import "ue_core.d.gs";
import "ability_nodes.d.gs";

export FString type;
export bool type;
export float type;
export Exec type;

export ApplyDamage node {
    enter: Exec input;
    exit: Exec output;
    target: Actor input;
    amount: float input = 0.0;
    critical: bool output;
}

export PrintString node {
    enter: Exec input;
    exit: Exec output;
    message: FString input = "";
}

export AbilityGraph schema {
    max_exec_fan_out: 1;
    allow_exec_fan_in: true;
    strict_type_match: true;
}

@id("ability.fireball")
Fireball: AbilityGraph graph {
    target: Actor input;
    damage: float input = 50.0;
    success: bool output;

    @Position(X = 100, Y = 120)
    apply: ApplyDamage {
        amount: damage;
    }

    @Position(X = 380, Y = 120)
    log: PrintString {
        message: "Fireball applied";
    }

    Start event {
        connect(context.start, apply.enter);
        connect(apply.exit, log.enter);
        bind(target, apply.target);
        bind(apply.critical, success);
    }

    DebugLog function {
        connect(context.start, log.enter);
        bind("debug fireball", log.message);
    }
}
```

这个示例刻意把声明和图创作内容放在同一个文件里，用于展示完整语法形状。
真实项目可以把声明拆到 `.d.gs` 文件中。

---

## 结构化等价形式

创作语法：

```gs
apply: ApplyDamage {
    amount: 50;
}
```

结构化等价形式：

```js
{
  name: "apply",
  kind: "node",
  type: "ApplyDamage",
  properties: {
    amount: 50
  }
}
```

结构化形式不是 canonical 创作语法。它适合中间表示、调试转储、编辑器
状态、文档解释和测试快照。

---

## Grammar 形状

非正式 grammar：

```text
File        := Item*
Item        := Import | Export | NodeDecl | Property | Command | Empty

Import      := "import" String ";"
Export      := "export" NodeDecl

NodeDecl    := Attribute* Identifier TypeAnnotation? Kind? NodeTail
NodeTail    := BlockBody | Default? ";"

TypeAnnotation := ":" TypeRef
Kind           := Identifier
Default        := "=" Value
BlockBody      := "{" Item* "}"

Property    := Attribute* QualifiedName ":" Value ";"
Command     := Identifier "(" Arguments? ")" ";"
Attribute   := "@" QualifiedName AttributeArgs?
```

消歧规则：

- `name: Type kind;` 是无 body 的 DocumentNode。
- `name: Type kind { ... }` 是带 children 的 DocumentNode。
- `name: value;` 是 property。
- `command(args);` 是 command。
- 在 graph body 中，`name: Type { ... }` 推断为 `kind = node`。

---

## 每种语法的详细解释

### Import

```gs
import "ue_core.d.gs";
```

Import 把声明文件引入当前文档环境。

### DocumentNode

完整 node 形式：

```gs
name: Type kind {
    // items
}
```

无 body 的 node 形式：

```gs
name: Type kind;
```

各位置含义：

- `name`：当前文档节点的本地名称。
- `Type`：类型、schema 或契约。
- `kind`：结构角色。
- body：嵌套文档项目。

### Graph

```gs
Fireball: AbilityGraph graph {
}
```

`graph` 是 `DocumentNode` kind。`AbilityGraph` 是图 schema/type。

领域层可以推断 schema 时，可以省略 type：

```gs
HelloWorld graph {
}
```

### Parameter

```gs
target: Actor input;
success: bool output;
temp: float var;
```

Graph parameter 是无 body 的 `DocumentNode`。kind 为 `input`、`output` 或
`var`。

旧写法不是本文 canonical：

```gs
@graph.input
param target: Actor;
```

### Graph Node

Graph 内的 canonical 简写：

```gs
apply: ApplyDamage {
    amount: 50;
}
```

显式 kind：

```gs
apply: ApplyDamage node {
    amount: 50;
}
```

在 graph body 内，typed child block 省略 kind 时解释为 `kind = node`。

### Event 与 Function

```gs
Start event {
    connect(context.start, apply.enter);
}

DebugLog function {
    connect(context.start, log.enter);
}
```

Event 和 function 都是由图领域解释的 `DocumentNode` kind。

### Property

```gs
amount: 50;
editor.pos: [100, 200];
message: "Hello";
```

Property 使用 `:` 和静态值。除非语法上下文要求节点声明，否则 property
不创建 `DocumentNode`。

### Command

```gs
connect(context.start, apply.enter);
bind(target, apply.target);
```

Canonical 图命令：

```text
connect(from, to)
bind(source, target)
```

Member-call connection syntax 不是 canonical：

```gs
context.start.connect(apply.enter);
```

### Attribute

```gs
@id("ability.fireball")
Fireball: AbilityGraph graph {
}

@Position(X = 100, Y = 120)
apply: ApplyDamage {
}
```

Attribute 附着到后续 `DocumentNode`、property 或 command。具体允许位置由
grammar 和领域层共同决定。

### Value

本文草稿支持的 value 形式：

```gs
name: "text";
amount: 50;
ratio: 0.5;
enabled: true;
target: ref "enemy.current";
pos: [100, 200];
config: {
    retries: 3;
};
```

Value 保持静态。可逆创作子集不包含 loop、branch、lambda、spread、
computed property、import-time execution 或一般 operator expression。

### Declaration

Declaration file 也使用同一套 `DocumentNode` 语法：

```gs
export FString type;

export PrintString node {
    enter: Exec input;
    exit: Exec output;
    message: FString input = "";
}

export AbilityGraph schema {
    max_exec_fan_out: 1;
}
```

`PrintString node` 声明一个 node type。`printer: PrintString` 实例化一个
graph-local `DocumentNode`，其 projected graph kind 为 `node`。

---

## Formatter 与 Canonicalization 规则

Formatter 应输出：

```gs
GraphName: GraphSchema graph { ... }
paramName: Type input;
nodeName: NodeType { ... }
EventName event { ... }
connect(from, to);
bind(source, target);
```

Formatter 不应输出：

```gs
scope graph Name: Schema { ... }
const nodeName = new NodeType { ... }
node nodeName { type NodeType; }
context.start.connect(node.enter);
```

这些形式未来可以作为迁移或兼容糖被解析器接受，但它们不是目标 canonical
syntax。

本节只规定该草稿的输出形状。源码保真、最小 patch、注释保留等稳定要求
不在本文重复定义，以 `docs/spec/` 为准。

---

## 实现状态

当前仓库实现还不等于本文目标草稿。

当前已经实现或已有样例覆盖：

- generic block declarations。
- directive statements。
- property declarations。
- call statements。
- attributes。
- import/export/declaration forms。
- 当前 fixtures 使用的 `graph`、`node`、`event`、`function`、`param`、
  `connect(...)`、`bind(...)` 形状

本文目标但尚未实现：

- `name: Type kind { ... }` 作为 canonical node header
- graph-local 省略 `node` kind 的推断
- `message: FString input;` 作为 canonical graph parameter syntax
- declaration files 改写为 `export Name kind { ... }`
- 格式化器按本文风格输出 canonical 语法。

---

## 开放问题

- 顶层 typed node 是否必须显式写 kind？
- graph schema 应优先使用 `GraphName: Schema graph`，还是
  `GraphName graph schema=Schema`？
- `input/output/var` 是 graph-specific kind，还是 node pin 也复用的通用
  declaration kind？
- Attribute 是否允许附着到 command？
- 类 JSON 结构化转储应标准化为 `.gs.json5` 调试输出、编辑器状态还是测试
  快照？
