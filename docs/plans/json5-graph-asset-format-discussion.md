# JSON5 图资产格式讨论稿

> 状态：讨论稿。
> 目的：记录“以 JSON5 为目标作者格式”的方案，同时把 CUE 作为重要参考技术纳入设计比较。
> 结论倾向：JSON5 更适合作为 GraphScript 的 canonical source；CUE 适合作为约束/默认值/验证模型参考，或作为后续高阶验证原型。

---

## 1. 背景

当前 Tree-sitter `.gs` 迁移计划把新作者语法收敛为：

```text
Block / Property / Command / Expr
```

核心目标是：

- AI 写作友好。
- 编辑器能把具体 graph/node/property/edge 修改定位到稳定 source range。
- 可视化编辑不能依赖整文件重写。
- Graph、Table、HTN 等领域语义由 semantic/projection 层解释。
- CLI/Web 编辑器、source diagnostics、patch/apply 流程必须保留。

JSON5 路线不是否定这些目标，而是把表层作者格式从自定义 DSL 改成 JSON-family 格式。无论选择 JSON5 还是 `.gs`，GraphScript 仍然需要自己的：

```text
SourceText / SyntaxTree / SourceBinding
SemanticModel
Declaration/import binder
GraphProjection
Diagnostics
RewriteBuilder / TextPatch
Runtime bake
```

不能把“能 parse JSON5”误认为“已经有 GraphScript compiler service”。

---

## 2. CUE 作为参考技术

CUE 值得进入长期参考文档，因为它在以下方面非常贴近 GraphScript 的目标：

- data + schema + constraint 使用同一种语言表达。
- definition 可以表达可复用声明。
- default 是语言语义的一部分。
- required / optional / forbidden fields 是原生能力。
- unification 能合并多个片段，并在冲突时产生诊断。
- 可以导出 concrete JSON/YAML/TOML，适合作为验证和生成管线参考。

示意：

```cue
#Node: {
    id!:   string
    name!: string
    type!: string
    editor: {
        pos:  [number, number] | *[0, 0]
        size: [number, number] | *[220, 80]
    }
}

graphs: Execute: {
    schema: "AbilityGraph"
    nodes: log: #Node & {
        id:   "node.log"
        name: "log"
        type: "PrintString"
        editor: pos: [100, 200]
    }
}
```

### 2.1 CUE 不作为当前默认目标格式的原因

CUE 的主要问题不是 parse，而是 source ownership。

CUE 的核心语义是 unification。一个最终值可能来自：

- schema default
- local override
- imported package
- shared fragment
- pattern constraint
- computed expression

当编辑器想把 `nodes.log.editor.pos` 改成 `[120, 240]` 时，CUE 不会天然告诉 GraphScript 应该修改哪一个源码贡献。GraphScript 仍然必须维护 source binding 和回写策略。

此外，CUE 官方工具链以 Go 为中心。GraphScript 当前是 C++17 library + CLI。把 CUE 作为 canonical source 会引入以下工程选择：

- 依赖 `cue` sidecar binary。
- 使用 Go `c-shared` 暴露 C ABI。
- 或重新实现一部分 CUE parser/evaluator。

前两种会改变部署和构建边界，第三种不现实。

### 2.2 CUE 应该如何被吸收

CUE 的价值应体现在 GraphScript 设计中，而不是直接照搬完整 CUE 运行时：

- 参考 CUE 的 definition/default/constraint 模型设计 `.d.json5` 声明。
- 借鉴 unification 的冲突诊断，但 GraphScript fragment 合并必须显式记录 source contribution。
- 将 CUE 作为后续 prototype：验证“声明 + 默认值 + 实例 patch”是否能比 JSON Schema 更自然。
- 可以提供 CUE export/import adapter，但不让 CUE AST 成为 GraphScript public API。

---

## 3. 为什么选择 JSON5 作为目标作者格式

JSON5 的优势是低语义负担。

它仍然是 JSON object/array/value 模型，但对人类和 AI 友好：

- 支持注释。
- 支持 trailing comma。
- 支持未加引号的 identifier key。
- 支持单引号字符串。
- 与 editor state JSON、runtime JSON、Web API payload 的心智模型接近。
- 可以借助 JSON Pointer / JSON Patch / JSON Schema 生态，但不被它们限制。

GraphScript 应采用一个受限 JSON5 profile，而不是完整放开 JSON5 所有语义。

### 3.1 GraphScript JSON5 Profile

允许：

- object / array / string / number / boolean / null。
- line comment 和 block comment。
- trailing comma。
- unquoted identifier keys。
- single quoted string，formatter 可以统一输出双引号或保留原样。

禁止或规范化：

- `NaN`、`Infinity`、`-Infinity`。
- hex number，除非明确用于 bitmask/flags。
- `+1` 这种 JSON 不可移植数字形式。
- 依赖数组 index 作为长期 stable identity。
- 隐式 merge、include、substitution、anchor/alias 之类额外数据语义。

每个可被编辑器/AI 长期引用的对象都必须有 stable id。

---

## 4. 文件类型

建议先使用新扩展名，不复用当前 `.gs/.d.gs`，避免和 DSL 迁移计划混淆。

```text
.graph.json5      graph/asset authoring source
.graph.d.json5    declaration/source schema
.graph.lock.json  optional normalized/cooked dependency snapshot
.graph.json       optional cooked/runtime/interchange output
```

如果后续决定完全替换 `.gs`，再做扩展名迁移；prototype 阶段不要混用。

---

## 5. 分层模型

JSON5 路线必须严格分层，避免把“能解析 JSON5”误认为“已经有图资产语言”。

```text
Layer 1: JSON5 syntax layer
  只负责 source text、token、object、field、array、value、comment、range、error recovery。

Layer 2: JSON document tools layer
  提供 JSON DOM facade、JSON Pointer、JSON Patch-like edit IR、JSON Schema validation、
  source binding、comment ownership、formatter/rewrite。

Layer 3: Graph asset schema layer
  定义 GraphScript asset document shape、graph/node/event/step 语义、
  declaration schema vocabulary、symbol binding、graph projection、runtime bake。
```

这三层的边界是 JSON5 方案成败的关键：

- JSON5 syntax layer 不知道 graph、node、pin、schema。
- JSON document tools layer 不知道 FlowGraph fan-in/fan-out；它只知道 JSON path、field range、schema validation、局部 rewrite。
- Graph asset schema layer 才解释 `graphs.Execute.nodes.log`、`steps.start_log.kind = "connect"`、`x-gs-pin` 等领域语义。

因此 JSON5 的核心工程不是“换 parser”，而是把现有 DSL compiler service 拆成：

```text
JSON5 parser
  -> JSON document model
  -> GraphScript asset model
  -> Graph projection/runtime
```

---

## 6. 核心文档模型

### 6.1 Graph asset

推荐 canonical shape：

```json5
{
  version: 1,

  imports: [
    { id: "ue.core", path: "ue_core.graph.d.json5" },
    { id: "ability.nodes", path: "ability_nodes.graph.d.json5" },
  ],

  graphs: {
    Execute: {
      id: "graph.execute",
      schema: "AbilityGraph",

      params: {
        target: {
          id: "param.target",
          direction: "input",
          type: "Actor",
        },
        result: {
          id: "param.result",
          direction: "output",
          type: "bool",
        },
      },

      nodes: {
        log: {
          id: "node.log",
          type: "PrintString",
          properties: {
            message: "done",
          },
          editor: {
            pos: [360, 100],
          },
        },
      },

      events: {
        BeginPlay: {
          id: "event.begin_play",
          steps: {
            start_log: {
              id: "edge.start_log",
              kind: "connect",
              from: "context.start",
              to: "log.enter",
              order: 10,
            },
            bind_message: {
              id: "edge.bind_message",
              kind: "bind",
              from: "target.name",
              to: "log.message",
              order: 20,
            },
          },
        },
      },
    },
  },
}
```

设计要点：

- `graphs`、`nodes`、`events`、`params` 使用 map，key 是 source alias。
- `id` 是 stable identity，rename alias 时不变。
- `steps` 不使用数组，避免 public patch target 依赖 index。
- `order` 负责保留作者顺序和图执行/显示顺序。
- `properties` 存节点默认值；`editor` 存编辑器元数据。
- `from` / `to` 第一版用字符串 pin path，projection 层解析并校验。

### 6.2 Declaration file as JSON Schema

如果采用 JSON5 路线，声明文件不应该僵化复刻 `.d.gs declare ...`。更自然的方案是：`.graph.d.json5` 本身就是 JSON Schema 文档，GraphScript 在 JSON Schema 之上定义一个受控 vocabulary/extension。

第一版建议采用：

```text
标准 JSON Schema:
  $schema / $id / $defs / type / properties / required / enum / const / default / description

GraphScript extension:
  x-graphscript
  x-gs-kind
  x-gs-id
  x-gs-pin
  x-gs-node
  x-gs-graph-schema
  x-gs-editor
```

这样声明文件可以直接被普通 JSON Schema 工具消费，用于基础结构校验、补全和 hover；GraphScript 自己再读取 `x-gs-*` 扩展完成 pin、connection policy、projection 和 runtime bake 相关语义。

```json5
{
  $schema: "https://json-schema.org/draft/2020-12/schema",
  $id: "graphscript://ability.nodes",

  title: "Ability Node Declarations",

  x-graphscript: {
    module: {
      id: "ability.nodes",
      version: "1.0.0",
    },
  },

  $defs: {
    Actor: {
      x-gs-kind: "type",
      x-gs-id: "type.actor",
    },

    AbilityGraph: {
      type: "object",
      description: "Flow graph schema for abilities.",
      x-gs-kind: "graphSchema",
      x-gs-id: "schema.ability_graph",
      x-gs-graph-schema: {
        connectionPolicy: {
          maxExecFanOut: 1,
          allowExecFanIn: false,
          strictTypeMatch: true,
        },
        requiredEvents: ["BeginPlay"],
      },
    },

    PrintString: {
      type: "object",
      description: "Prints a string and continues execution.",
      x-gs-kind: "node",
      x-gs-id: "node_decl.print_string",

      properties: {
        message: {
          type: "string",
          default: "",
          x-gs-pin: {
            id: "pin.print.message",
            kind: "data",
            direction: "input",
          },
        },
      },

      x-gs-node: {
        pins: {
          enter: {
            id: "pin.print.enter",
            kind: "exec",
            direction: "input",
          },
          exit: {
            id: "pin.print.exit",
            kind: "exec",
            direction: "output",
          },
        },
        editor: {
          defaultSize: [220, 80],
        },
      },
    },
  },
}
```

解释：

- `$defs.<Name>` 是声明符号表的自然载体。
- 普通数据字段、类型、默认值、required/optional 规则优先使用标准 JSON Schema。
- GraphScript 专属语义放在 `x-gs-*`，避免伪装成标准 JSON Schema。
- node 的 data input 可以直接是 `properties.<field>`，再通过 `x-gs-pin` 标注为 pin。
- exec pin 没有自然 JSON value 字段，放在 `x-gs-node.pins`。
- graph schema 的 fan-in/fan-out、required events 等不是 JSON Schema 的结构校验职责，放在 `x-gs-graph-schema`。

声明层第一版不引入表达式语言。复杂跨符号规则仍由 C++ semantic/validator 实现。

---

## 7. Patch 和 Source Binding

JSON5 路线必须保留当前迁移计划里的 patch 原则。

最低 source binding：

```text
GraphBinding:
  graph id
  source alias key range
  object range
  schema field range

NodeBinding:
  node id
  alias key range
  object range
  type field range
  property field ranges
  editor metadata ranges

StepBinding:
  edge/command id
  alias key range
  object range
  from/to/kind/order field ranges

DeclarationBinding:
  declaration id
  alias key range
  member field ranges
```

JSON Pointer 可以作为内部定位机制，但 public editor/AI patch API 不应暴露数组 index 或脆弱路径。推荐 patch request：

```json
{
  "op": "setNodeProperty",
  "graphId": "graph.execute",
  "nodeId": "node.log",
  "path": ["properties", "message"],
  "value": "hello"
}
```

RewriteBuilder 再解析到 source binding，并生成具体 text edit。

### 7.1 局部修改策略

常见操作必须能局部修改：

| 操作 | 首选 patch |
| --- | --- |
| move node | 替换 `editor.pos` value range；缺失时插入 `editor.pos` field |
| set property | 替换 `properties.<name>` value range；缺失时插入 field |
| add node | 插入 `nodes.<alias>` object |
| rename node alias | 替换 `nodes` map key，并更新同图 pin path 引用 |
| connect pins | 插入 `steps.<stable_alias>` object |
| delete connection | 删除对应 `steps.<alias>` field |
| rename node type | 替换 declaration 或 instance `type` string/token |

不要在普通编辑操作中运行整文件 pretty print。Formatter 只能用于：

- 新建文件。
- 用户显式格式化。
- 小范围插入 fragment 的内部格式。

---

## 8. 诊断模型

JSON5 parse diagnostics 和 GraphScript semantic diagnostics 必须分层：

```text
Syntax diagnostics:
  invalid JSON5 token
  missing colon/comma/brace
  duplicate object key if parser can detect it

Schema diagnostics:
  missing id
  forbidden JSON5 number form
  wrong field type
  unknown top-level section

Binding diagnostics:
  unresolved import
  unresolved node type
  duplicate stable id
  alias/id mismatch

Graph diagnostics:
  unknown pin
  wrong pin direction
  exec/data mismatch
  fan-in/fan-out violation
  required event missing
```

所有 diagnostics 都必须携带：

- source range。
- stable diagnostic code。
- semantic target。
- optional quick fix。

这与现有 CLI/Web 诊断和 DiagnosticTarget 方向保持一致。

---

## 9. Runtime Bake

JSON5 source 不直接运行。目标管线：

```text
JSON5 SourceText
  -> JSON5 SyntaxTree with trivia/ranges
  -> JSON Document facade / JSON Pointer / JSON Schema / source binding
  -> Graph Asset document shape / declaration vocabulary / import binder
  -> Graph Asset SemanticModel
  -> GraphProjection
  -> AuthoringModel + diagnostics + source bindings
  -> RuntimeGraph / baked IR + source map
```

Cooked 输出可以是严格 JSON：

```text
.graph.json5  authoring source
  -> .graph.json cooked/interchange/runtime input
```

runtime JSON 不保留注释和局部格式；source map 负责把 runtime item 映射回 authoring JSON5。

---

## 10. 与 Tree-sitter `.gs` 计划的关系

两条路线共享大部分 compiler-service 目标：

| 能力 | Tree-sitter `.gs` 路线 | JSON5 路线 |
| --- | --- | --- |
| 作者格式 | 自定义 Block/Property/Command DSL | JSON5 object/array/value |
| parser | 自定义 tree-sitter grammar | JSON5 parser / tree-sitter-json5 / jsonc-style parser |
| AST facade | Block/Property/Command/Expr | JSON object field/value facade + GraphScript semantic wrappers |
| source binding | 必须自研 | 必须自研 |
| graph projection | 必须自研 | 必须自研 |
| rewrite | DSL-aware TextPatch | JSON5-aware TextPatch |
| schema/default 参考 | `.d.gs` contract | JSON Schema `$defs` + `x-gs-*` vocabulary + CUE-inspired constraints |

JSON5 不会减少 semantic/projection/rewrite 的工作量，但会降低表层语法和 AI 结构化输出成本。

---

## 11. JSON5 路线的迁移原则

如果 JSON5 prototype 通过，并决定把 JSON5 提升为主迁移方向，它应该和 Tree-sitter `.gs` 迁移计划一样允许破坏性更新，但不能降低产品能力。

迁移原则：

- 不把 JSON5 当作纯导入/导出格式；如果采用，它必须能成为 authoring source。
- 不用严格 JSON 作为作者源格式；严格 JSON 只作为 cooked/interchange/runtime artifact。
- 不让 JSON Pointer 或 JSON Patch 替代 GraphScript semantic layer；它们只是 companion standards。
- 不依赖数组 index 作为长期引用 identity；所有 graph/node/edge/param/declaration 级对象必须有 stable id。
- 不通过整文件 stringify/pretty print 实现编辑器操作；普通编辑必须走 source binding 和局部 text patch。
- declaration/source schema 以 JSON Schema 为主体；GraphScript 通过 `x-gs-*` vocabulary 承载 pin、schema policy、projection hint 等领域语义。
- 不在 prototype 阶段删除现有 `.gs/.d.gs` 路径；只有 editor/session 完整消费 JSON5 projection 后，才考虑替换旧路径。
- CLI/Web 编辑器、source diagnostics、可视化图编辑、patch/apply、后端/前端 API 合约必须保留；若 API 改动，必须同一步替换调用方。

与 `.gs` plan 最大的不同是：JSON5 路线不需要设计自定义 block grammar，但必须更严格设计数据 shape、stable id、comment ownership 和 map/order 规则。否则 JSON5 会退化成“更容易 parse 的文本”，无法支撑图文双向编辑。

---

## 12. JSON5 方案复审

基于 `.gs` plan 中的 AI 写作、行级 patch、序列化扩展三个目标，JSON5 syntax layer 应只收敛到四个核心 JSON 形状：

```text
Object
Field
Array
Value
```

JSON document tools layer 在这些形状之上提供：

```text
Document / FieldIndex / JsonPointer / JsonPatchIR / JsonSchema / SourceBinding
```

Graph asset schema layer 再把 JSON document 解释为：

```text
Asset / Declaration / Symbol / Type / Command / Property / Fragment
```

### 12.1 复审结论

- JSON5 object field 比 `.gs` block header 更适合稳定机器定位：`nodes.log` 的 key range、object range 和 `id` field range 都能分别绑定。
- JSON5 天然区分 property value 和 nested object，但语义上不是所有 nested object 都是 scope；scope 必须由 GraphScript schema 明确解释。
- `nodes`、`params`、`events`、`steps` 应使用 map，不使用 array；array 只用于没有稳定身份的值，例如坐标、颜色、普通列表。
- `steps` 代表 `.gs` plan 里的 `Command`；它必须有 `kind`、`id`、`order`，否则重复 `connect` 无法稳定定位。
- `properties` 是节点或资产的普通序列化字段；`editor` 是 authoring metadata；两者都以普通 field 形式表达。
- `imports` 可以暂时使用 array，因为 import 顺序本身有意义且 import 项有 `id`；长期 public patch 仍应按 `id` 定位。
- JSON5 declaration 文件不应该模拟 `.d.gs` 的“语法扩展 kind”；它应该声明数据模型和 domain contract，parser 不需要扩展。

### 12.2 形状判定

JSON5 语法层不区分 graph/node/event/edge。判定规则应在 semantic model 中完成：

```text
root.graphs.<alias>                -> Graph declaration
root.graphs.<g>.nodes.<alias>      -> Node instance
root.graphs.<g>.params.<alias>     -> Graph parameter
root.graphs.<g>.events.<alias>     -> Event/entry block
root.graphs.<g>.events.<e>.steps.* -> Graph command / edge / bind
schema.$defs.<name>[x-gs-kind=node]        -> Node declaration
schema.$defs.<name>[x-gs-kind=graphSchema] -> Graph schema declaration
```

这样 parser 只关心 JSON5 object/field/value 和 source range，不知道 Graph/HTN/Table。

### 12.3 Command 表达

`.gs` plan 里 `connect(...)` 和 `bind(...)` 是 `Command` item。JSON5 里推荐统一成 keyed command object：

```json5
steps: {
  start_log: {
    id: "edge.start_log",
    kind: "connect",
    from: "context.start",
    to: "log.enter",
    order: 10,
  },
  bind_message: {
    id: "edge.bind_message",
    kind: "bind",
    from: "player.name",
    to: "log.message",
    order: 20,
  },
}
```

原因：

- key 提供可读 alias。
- `id` 提供 stable identity。
- `kind` 保留 command dispatch。
- `order` 保留作者顺序，不依赖 map 迭代顺序。
- `from` / `to` 字段拥有独立 source range，便于 endpoint 替换。

第一版不推荐：

```json5
steps: [
  { kind: "connect", from: "context.start", to: "log.enter" },
]
```

数组写法适合作为 import adapter 输入，但不适合作为 canonical authoring source。

### 12.4 参数表达

`.gs` plan 已决定使用 `param` 声明型 Command。JSON5 里参数应成为 `params` map：

```json5
params: {
  amount: {
    id: "param.amount",
    direction: "input",
    type: "float",
    default: 50.0,
  },
  result: {
    id: "param.result",
    direction: "output",
    type: "bool",
  },
}
```

这样参数声明名、类型、方向、默认值和 source range 都是普通 JSON5 field，编辑器 patch 比命令式语法更直接。

### 12.5 节点类型声明与使用

JSON5 中同样需要区分三件事：

1. `nodes` section 表示“这里有图节点实例”。
2. `PrintString` 是 declaration file 中的 node type symbol。
3. `log` 是实例 alias，`node.log` 是 stable id。

实例：

```json5
nodes: {
  log: {
    id: "node.log",
    type: "PrintString",
    properties: {
      message: "hello",
    },
    editor: {
      pos: [100, 200],
    },
  },
}
```

声明：

```json5
$defs: {
  PrintString: {
    type: "object",
    x-gs-kind: "node",
    x-gs-id: "node_decl.print_string",
    properties: {
      message: {
        type: "string",
        x-gs-pin: {
          id: "pin.print.message",
          kind: "data",
          direction: "input",
        },
      },
    },
    x-gs-node: {
      pins: {
        enter: { id: "pin.print.enter", kind: "exec", direction: "input" },
        exit: { id: "pin.print.exit", kind: "exec", direction: "output" },
      },
    },
  },
}
```

实例 body 不重复声明 pin；GraphProjection 通过 `type` 找到 declaration，再解析 `log.enter`、`log.message`。

---

## 13. JSON5 目标模块结构

如果 JSON5 成为主路线，目标结构应尽量复用 `.gs` plan 的 domain/layer 边界，只替换 syntax frontend。

```text
include/
  graphscript/
    dsl/
      interface/
        document.h
        compiler.h
        diagnostics.h
        rewrite.h
        runtime.h
      source/
        source_text.h
        text_range.h
        source_range.h
        line_map.h
        source_hash.h
      json5/
        json5_parser.h          # parser facade；具体实现可为 tree-sitter-json5 或受限 parser
        json5_tree.h            # object/field/array/value syntax facade
        json5_node.h
        json5_token.h
        json5_trivia.h
        json5_error.h
      jsondoc/
        json_document.h        # JSON DOM facade + path/index over JSON5 syntax
        json_pointer.h
        json_patch_ir.h
        json_schema.h          # JSON Schema loading/validation facade
        json_source_binding.h
        json_comment_policy.h
      declarations/
        declaration_model.h     # built from JSON Schema $defs + x-gs-* vocabulary
        import_model.h
        export_table.h
        module_declaration.h
        node_declaration.h
        schema_declaration.h
      graph_asset/
        graph_asset_document.h  # graphs/nodes/events/steps canonical source shape
        graph_asset_schema.h    # JSON Schema + x-gs-* vocabulary adapter
        graph_asset_binding.h
        graph_asset_semantics.h
        graph_asset_diagnostics.h
      symbols/
      types/
      semantics/
      ir/
      compiler/
      rewrite/
        text_patch.h
        rewrite_builder.h
        json5_rewrite.h         # set/insert/delete field，保留 trivia
        formatter.h
        source_binding.h

    graph/
      interface/
      schema/
      model/
      projection/
      validation/
      rewrite/
      runtime/
      editor/

    editor/
      interface/
      cli/
      web/
      diagnostics/
```

关键依赖方向：

- `dsl/json5` 只维护 JSON5 syntax，不 include `graph/*`。
- `dsl/jsondoc` 只维护 JSON document tooling：path、schema、source binding、comment policy、JSON Patch-like edit IR。
- `dsl/graph_asset` 才解释 GraphScript 的 JSON document shape：`graphs`、`nodes`、`events`、`steps`、`$defs` 和 `x-gs-*`。
- `dsl/semantics` 编排 `jsondoc + graph_asset + declarations/symbols/types`，产出 GraphScript semantic model。
- `graph/projection` 消费 `dsl/interface` 产出的 graph asset semantic/IR，不直接依赖 JSON5 syntax node。
- `graph/rewrite` 只描述图编辑意图，例如 `MoveNode`、`ConnectPins`。
- `dsl/rewrite/json5_rewrite` 才负责生成具体 text patch。
- `editor/*` 只依赖 `dsl/interface` 和 `graph/interface`。

### 13.1 与现有 `asset/language.*` 的关系

现有 `include/graphscript/asset/language.h` 和 `src/asset/language.cpp` 如果继续保留，应该成为临时 facade：

```text
asset/language Parser        -> dsl/json5 parser facade + dsl/jsondoc document facade
asset/language Linter        -> dsl/jsondoc schema validation + dsl/graph_asset semantics + graph/validation
asset/language Patcher       -> dsl/rewrite/json5_rewrite + graph/rewrite
asset/language FlowProjector -> graph/projection
```

不要让 `asset/language.*` 同时长期承载 JSON5 parser、semantic binder、graph projector、patcher；否则 JSON5 路线会重复当前旧架构的问题。

---

## 14. JSON5 新旧功能映射

| 旧概念 / `.gs` plan 概念 | JSON5 目标 |
| --- | --- |
| `.d.gs declare type` | JSON Schema `$defs.<Name> { x-gs-kind: "type" }` |
| `.d.gs declare node` | JSON Schema `$defs.<Name> { type: "object", x-gs-kind: "node" }` |
| 旧 exec/data/input/output pin | data pin 用 `$defs.<Node>.properties.<field>.x-gs-pin`；exec pin 用 `$defs.<Node>.x-gs-node.pins` |
| 旧 schema | JSON Schema `$defs.<Name> { x-gs-kind: "graphSchema", x-gs-graph-schema: ... }` |
| `Graph Name : Schema` | `graphs.<Name> { id, schema, ... }` |
| graph parameter / `param` command | `graphs.<g>.params.<name>` |
| node instance / `node log { type Print; }` | `graphs.<g>.nodes.<alias> { id, type, properties, editor }` |
| initializer field | `nodes.<alias>.properties.<field>` |
| flow connection / `connect` | `events.<e>.steps.<alias> { kind: "connect", from, to }` |
| data link / `bind` | `events.<e>.steps.<alias> { kind: "bind", from, to }` |
| event/function block | `events.<name>` / `functions.<name>` maps |
| generate comment | `comments` map or `editor.comments` contribution |
| layout metadata | `editor.pos`, `editor.size` fields |
| prefix annotation | `annotations` map/list with stable ids, or domain-specific metadata field |
| graph-as-node type | generated declaration in semantic model or explicit `$defs.<GraphName>` node schema |

### 14.1 Annotation 映射

JSON5 没有 attribute 语法。现有 annotation 语义应转为显式 metadata：

```json5
annotations: {
  persistent: {
    id: "anno.node.log.persistent",
    name: "PersistentId",
    args: ["node.log"],
  },
}
```

对于高频 editor metadata，不建议走通用 annotations：

```json5
editor: {
  pos: [100, 200],
  color: "blue",
}
```

规则：

- `editor.*` 用于编辑器一等字段。
- `annotations` 用于保留通用、插件、导入兼容 metadata。
- declaration metadata 同样使用 explicit field，而不是 comment magic。

### 14.2 Fragment / overlay 映射

`.gs` plan 里 `for Fireball;` 是 fragment target command。JSON5 中推荐：

```json5
fragments: {
  FireballBalance: {
    id: "fragment.fireball.balance",
    for: "asset.fireball",
    kind: "tuning",
    properties: {
      damage: 50,
      cooldown: 3.0,
    },
  },
}
```

合并策略不能采用隐式 last-writer-wins。SemanticModel 必须记录每个 contribution 的 source range，重复属性默认诊断 conflict，除非 schema 明确允许 override/merge。

---

## 15. 分阶段工作

### Phase 0：确认 JSON5 prototype 边界

- 不替换现有 `.gs/.d.gs`。
- 新增 `docs/plans/json5-graph-asset-format-discussion.md` 作为讨论源。
- 选定 prototype 扩展名：建议 `.graph.json5` / `.graph.d.json5`。
- 确认 parser 候选：tree-sitter-json5、JSONC parser 风格库、或手写受限 JSON5 parser。
- 确认 CUE 只作为参考/对照，不作为第一版 runtime dependency。

### Phase 1：JSON5 syntax/source prototype

- 增加 `dsl/json5` 最小 parser facade。
- 支持 parse object/field/array/value/comment/source range。
- 输出 syntax dump JSON，供测试和 UI 对比。
- 增加 fixtures：
  - `minimal.graph.json5`
  - `invalid.graph.json5`
  - `declarations.graph.d.json5`
- 测试 duplicate key、missing colon、missing brace、invalid number form。

### Phase 2：JSON document tools layer

- 实现 JSON DOM facade、field index、JSON Pointer 查询。
- 实现 JSON Schema loading/validation facade。
- 实现 JSON source binding：path -> field/value/key/object range。
- 实现 comment ownership 和插入格式策略。
- 实现 JSON Patch-like edit IR，但不直接用它写文件。

### Phase 2A：Graph asset schema layer

- 实现 asset source root shape validator：`version/imports/graphs`。
- 实现 declaration source root shape validator：`$schema/$id/$defs/x-graphscript`。
- 实现 stable id registry，检测 duplicate id。
- 实现 imports 和 declaration export table。
- 实现 node type、pin、schema 的 name resolution。
- 实现 Graph/Node/Event/Step source binding。

### Phase 3：Graph projection

- 从 JSON5 SemanticModel 投影 GraphModel。
- 支持 graph params、node instances、events、connect、bind。
- 保留 invalid node/edge，并挂 source diagnostics。
- 复用或迁移 connection policy、strict type match、required events。

### Phase 4：JSON5 rewrite prototype

- 实现 `MoveNode`：替换或插入 `editor.pos`。
- 实现 `SetNodeProperty`：替换或插入 `properties.<name>`。
- 实现 `ConnectPins`：插入 `steps.<alias>` object。
- 实现 `DeleteConnection`：删除 step field，并保留 surrounding trivia。
- 所有操作使用 source hash guard 和 range guard。

### Phase 5：CLI 临时命令

prototype 阶段不要直接覆盖现有 `parse/lint/project/patch`。建议临时命令：

```text
json5-parse
json5-lint
json5-project
json5-patch
```

通过 prototype gate 后，再决定是否按破坏性迁移覆盖最终命令。

### Phase 6：Editor adapter

- 新增 JSON5 editor adapter，把 JSON5 GraphProjection 转成现有 `/api/state`。
- Web 前端尽量不感知 source format，只消费 graph/editor state JSON。
- Source panel 能打开 `.graph.json5`，并定位 graph/node/edge/property source ranges。
- Quick fix 和 replayable commands 先覆盖 move/set/connect/delete 四类操作。

### Phase 7：决策点

满足以下条件后，才决定是否暂停 `.gs` 迁移路线：

- JSON5 parser/source binding 能支撑错误文本下部分图。
- JSON5 rewrite 能稳定保留注释和未触碰格式。
- CLI/Web editor 主要工作流可用。
- 旧功能清单里核心 graph/source diagnostics/rename/patch 能映射到 JSON5。
- 构建和依赖风险可接受。

如果任一条件失败，JSON5 保留为 import/export 或 AI patch 中间格式，不替换 `.gs` canonical source。

---

## 16. 验证计划

最低检查：

```bash
rg "graph\\.json5|graph\\.d\\.json5" docs tests include src cli
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/gs_tests.exe
```

prototype 检查：

```bash
./build/Release/gs.exe json5-parse -i tests/fixtures/minimal.graph.json5
./build/Release/gs.exe json5-lint -i tests/fixtures/minimal.graph.json5 -I tests/fixtures/core.graph.d.json5
./build/Release/gs.exe json5-project -i tests/fixtures/minimal.graph.json5 --graph Execute
./build/Release/gs.exe json5-patch -i tests/fixtures/minimal.graph.json5 --op move-node --node node.log --pos 120,240
```

必须增加测试：

- JSON5 syntax/source range tests。
- JSON5 semantic shape tests。
- declaration/import binding tests。
- graph projection tests。
- rewrite round-trip tests。
- diagnostics target tests。
- CLI editor adapter tests。
- Web source navigation smoke tests。

---

## 17. 待决问题决策队列

### Q1：JSON5 路线是否直接覆盖 `.gs`？

临时结论：不直接覆盖。

- prototype 使用 `.graph.json5` / `.graph.d.json5`。
- 通过 editor/source patch gate 后再决定是否替换 `.gs`。

### Q2：parser 采用什么？

候选：

- tree-sitter-json5：更贴近编辑器 CST、错误恢复和 source range。
- JSONC parser 风格库：更成熟的 edit/visitor API，但 JSON5 支持可能不完整。
- 手写受限 JSON5 parser：最可控，但工作量更大。

临时倾向：优先验证 tree-sitter-json5；如果 comment/trivia/rewrite 不够，再考虑受限手写 parser。

### Q3：map key 和 `id` 谁是身份？

临时结论：

- map key 是 source alias，服务阅读和局部路径。
- `id` 是 stable identity，服务跨 rename、runtime source map、外部引用。
- 两者冲突不自动修复，产生 diagnostic。

### Q4：steps 是否允许数组？

临时结论：

- canonical source 不允许。
- import adapter 可以接受数组并 normalize 成 map + stable id + order。

### Q5：声明/default 用 JSON Schema 还是自研？

结论：以 JSON Schema 为声明文件主体，GraphScript 自研 domain vocabulary。

- `.graph.d.json5` 是 JSON Schema 文档，而不是仿 `.d.gs` 的自定义 declare 表。
- 标准 JSON Schema 负责基础 shape validation、默认值 annotation、editor completion、hover/documentation。
- GraphScript 使用 `x-gs-*` extension 表达 node、pin、graph schema、projection、editor metadata 等领域语义。
- GraphScript semantic validator 继续负责跨符号和图语义：import resolution、node type lookup、pin direction/type、fan-in/fan-out、required event、graph-as-node 等。
- CUE 作为模型参考和后续 prototype，不进入第一版 runtime dependency。

### Q6：RuntimeGraph 保留还是替换？

同 `.gs` plan 的 Q5。JSON5 只是 source frontend，不应决定 runtime 结构。倾向先保留 `RuntimeGraph` 外壳，输入改为新 GraphProjection。

---

## 18. Prototype Gate

进入实现前，先做一个最小 prototype，不替换现有 `.gs`：

1. 解析一个 `minimal.graph.json5`。
2. 生成 GraphModel：graph、node、event、connect、bind。
3. 对 unknown node type 产生带 source range 的 diagnostic。
4. 实现 `move node`：只替换 `editor.pos` value，不动其他文本。
5. 实现 `set node property`：更新现有 field 或插入缺失 field。
6. 实现 `connect pins`：插入 `steps.<alias>` object，使用 stable id。
7. 从 GraphModel bake 一个简化 RuntimeGraph 或 JSON dump。
8. Web/CLI state JSON 能展示并定位 graph/node/edge。

通过 prototype 后，再决定是否把 JSON5 升级为主迁移方向。

---

## 19. 开放问题

- 扩展名最终用 `.graph.json5`，还是复用 `.gs`？
- JSON5 parser 选 tree-sitter-json5、jsoncons/json5、jsonc-parser 风格实现，还是手写受限 parser？
- object key 和内部 `id` 冲突时以哪个为准？
- `steps` 是否一定使用 map + `order`，还是允许数组作为导入格式？
- `x-gs-*` vocabulary 的正式命名、版本和 JSON Schema `$vocabulary` 声明如何设计？
- formatter 是否保留原有 quote style，还是统一双引号？
- 注释归属规则：field-leading、field-trailing、object-leading 如何绑定？
- 跨文件 rename 是否通过 source binding + symbol table 实现，还是先限制为显式文件列表？
- 现有 `.gs/.d.gs` 迁移计划是否继续推进，还是 JSON5 prototype 通过后暂停？
