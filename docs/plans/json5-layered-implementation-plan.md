# JSON5 三层实现计划

> 状态：计划草案。
> 目的：基于 `JSON5 syntax layer -> JSON document tools layer -> Graph asset schema layer` 三层模型，规划 GraphScript JSON5 路线的可执行落地步骤。
> 范围：先做 prototype，不替换现有 `.gs/.d.gs` 主路径；通过 gate 后再决定是否升级为 canonical source。

---

## 1. 核心分层

```text
Layer 1: JSON5 syntax layer
  parse token/range/comment

Layer 2: JSON document tools layer
  generic JSON tree/graph inspector
  JSON Pointer navigation
  schema validation
  source range preview

Layer 3: Graph asset schema layer
  GraphScript-specific graph projection
  node/pin/edge semantic editing
  local source rewrite
```

关键原则：

- Layer 1 不知道 GraphScript，只处理 JSON5 源码。
- Layer 2 不知道 FlowGraph 规则，只提供通用 JSON 文档工具。
- Layer 3 才解释 GraphScript 资产语义。
- Graph projection / validation / runtime 不直接依赖 JSON5 parser 节点，只依赖 Graph asset semantic model。

目标管线：

```text
SourceText
  -> JSON5 SyntaxTree
  -> JsonDocument
  -> GraphAssetDocument
  -> GraphAssetSemanticModel
  -> GraphProjection
  -> RuntimeGraph / editor state / diagnostics / TextPatch
```

---

## 2. 非目标

第一轮 prototype 不做：

- 替换现有 `.gs/.d.gs` parser/compiler/editor。
- 完整迁移旧 fixture。
- 完整跨文件 rename。
- LSP。
- 全量 JSON Schema `$ref` / remote schema 支持。
- CUE runtime integration。
- 复杂 expression/default evaluation。

第一轮只证明：

- JSON5 可以作为 authoring source。
- source range 和 semantic binding 足以支持图编辑。
- 局部 rewrite 能保留未触碰文本。
- GraphProjection 可以从 JSON5 asset model 生成现有 editor/runtime 所需模型。

---

## 3. 目录目标

建议目标模块：

```text
include/graphscript/
  dsl/
    source/
      source_text.h
      line_map.h
      source_range.h
      source_hash.h

    json5/
      json5_parser.h
      json5_syntax_tree.h
      json5_node.h
      json5_token.h
      json5_trivia.h
      json5_diagnostics.h

    jsondoc/
      json_document.h
      json_pointer.h
      json_field_index.h
      json_schema.h
      json_patch_ir.h
      json_source_binding.h
      json_rewrite.h
      json_comment_policy.h

    graph_asset/
      graph_asset_document.h
      graph_asset_schema.h
      graph_asset_binding.h
      graph_asset_semantic_model.h
      graph_asset_diagnostics.h
      graph_asset_rewrite.h

  graph/
    interface/
      graph_model.h
      graph_projector.h
      graph_editor.h
    projection/
    validation/
    runtime/
    rewrite/

  editor/
    interface/
    diagnostics/
```

依赖方向：

```text
dsl/source <- dsl/json5 <- dsl/jsondoc <- dsl/graph_asset <- graph <- editor
```

禁止：

- `dsl/json5` include `graph/*`。
- `dsl/jsondoc` include `graph/*`。
- `graph/projection` 直接读取 JSON5 syntax node。
- `editor` 直接构造 JSON5 text patch。

---

## 4. 核心 SourceMap 契约

JSON5 路线的核心成败不是 parse，而是能否稳定建立三段映射：

```text
JsonSourceMap:
  JsonPointer + RangeRole -> SourceRange

GraphAssetSourceMap:
  GraphId / NodeId / EventId / StepId / DeclarationId -> JsonPointer(s) + role ranges

RuntimeSourceMap:
  RuntimeNode / RuntimeEdge / RuntimeValue -> GraphAsset target -> SourceRange
```

第一版必须支持这些 role：

```text
JsonRangeRole:
  FieldWhole
  Key
  Value
  ObjectWhole
  ArrayWhole
  InsertionPoint

GraphAssetRangeRole:
  AliasKey
  StableIdValue
  TypeValue
  SchemaValue
  PropertyValue
  EditorValue
  EndpointFrom
  EndpointTo
  CommandKind
  DeclarationKey
  PinDeclaration
```

示例：

```text
NodeId("node.log")
  AliasKey       -> /graphs/Execute/nodes/log key range
  StableIdValue  -> /graphs/Execute/nodes/log/id value range
  TypeValue      -> /graphs/Execute/nodes/log/type value range
  PropertyValue  -> /graphs/Execute/nodes/log/properties/message value range
  EditorValue    -> /graphs/Execute/nodes/log/editor/pos value range
```

不满足此契约时，不允许进入 Editor Adapter 阶段。否则 UI 可以显示图，但无法安全回写。

---

## 5. 最小纵向切片

先实现一个完整 end-to-end thin slice，再横向扩展功能。

最小输入：

```json5
{
  version: 1,
  imports: [{ id: "core", path: "core.graph.d.json5" }],
  graphs: {
    Execute: {
      id: "graph.execute",
      schema: "AbilityGraph",
      nodes: {
        log: {
          id: "node.log",
          type: "PrintString",
          properties: { message: "done" },
          editor: { pos: [100, 200] },
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
          },
        },
      },
    },
  },
}
```

最小声明：

```json5
{
  $schema: "https://json-schema.org/draft/2020-12/schema",
  $id: "graphscript://core",
  x-graphscript: { module: { id: "core", version: "0.1.0" } },
  $defs: {
    AbilityGraph: {
      x-gs-kind: "graphSchema",
      x-gs-id: "schema.ability",
      x-gs-graph-schema: { connectionPolicy: { maxExecFanOut: 1 } },
    },
    PrintString: {
      type: "object",
      x-gs-kind: "node",
      x-gs-id: "node.print",
      properties: {
        message: {
          type: "string",
          x-gs-pin: { id: "pin.print.message", kind: "data", direction: "input" },
        },
      },
      x-gs-node: {
        pins: {
          enter: { id: "pin.print.enter", kind: "exec", direction: "input" },
          exit: { id: "pin.print.exit", kind: "exec", direction: "output" },
        },
      },
    },
  },
}
```

最小编辑：

```text
MoveNode("node.log", [120, 240])
```

必须只修改：

```diff
- editor: { pos: [100, 200] },
+ editor: { pos: [120, 240] },
```

这个切片通过后，才扩展 `SetNodeProperty`、`ConnectPins`、`DisconnectEdge`、schema diagnostics 和 Web adapter。

---

## 6. Layer 1: JSON5 Syntax Layer

### 6.1 职责

只负责源码保真解析：

- token。
- object / field / array / value。
- string / number / bool / null。
- comments。
- source range。
- error node / skipped token。
- duplicate key 的 syntax-level visibility。
- line/column map。

不负责：

- JSON Pointer。
- JSON Schema。
- GraphScript graph/node/pin。
- source rewrite policy。

### 6.2 交付物

接口：

```cpp
class Json5Parser {
public:
    Json5ParseResult parse(SourceText source);
};

struct Json5ParseResult {
    Json5SyntaxTree tree;
    std::vector<Diagnostic> diagnostics;
};
```

最小 syntax facade：

```cpp
Json5SyntaxNode
Json5ObjectNode
Json5FieldNode
Json5ArrayNode
Json5ValueNode
Json5Token
Json5Trivia
```

每个 `Json5FieldNode` 至少暴露：

```text
whole range
key range
colon range
value range
leading comments
trailing comments
```

### 6.3 Parser 选型 Gate

候选：

- tree-sitter-json5：优先验证，适合 CST/range/error recovery。
- JSONC parser 风格实现：如果 source range 和 edit support 更成熟，可作为替代。
- 受限手写 JSON5 parser：当现有 parser 无法满足 comment/rewrite 时再考虑。

验收：

- 能解析 comments、trailing comma、unquoted keys、single quotes。
- 能定位 key/value/object range。
- 错误 JSON5 也能产生部分 syntax tree。
- 不丢 duplicate key 的所有 source occurrence。

### 6.4 测试

新增：

```text
tests/test_json5_syntax.cpp
tests/fixtures/json5/minimal.graph.json5
tests/fixtures/json5/invalid.graph.json5
tests/fixtures/json5/comments.graph.json5
```

覆盖：

- key range。
- value range。
- object range。
- comment attachment raw ranges。
- missing colon。
- missing brace。
- duplicate key。
- forbidden number form diagnostic hook。

### 6.5 禁止事项

- 不在这一层生成 GraphScript diagnostics。
- 不在这一层 normalize field order。
- 不在这一层丢弃 comments/trivia。
- 不把 duplicate key 合并成最后一个值；必须保留所有 occurrence 给上层诊断。

---

## 7. Layer 2: JSON Document Tools Layer

### 7.1 职责

把 JSON5 syntax tree 提升为通用 JSON 文档工具层：

- JsonDocument facade。
- JsonPointer navigation。
- Json field index。
- schema validation facade。
- source range preview。
- generic JSON tree/graph inspector。
- JSON Patch-like edit IR。
- source binding for JSON paths。
- comment ownership policy。
- local rewrite primitives。

不负责：

- node type lookup。
- pin resolution。
- fan-in/fan-out。
- GraphProjection。

### 7.2 JsonDocument

目标是能从 syntax tree 查询结构化 JSON，同时保留 source node：

```cpp
class JsonDocument {
public:
    JsonValue root() const;
    std::optional<JsonValue> find(JsonPointer pointer) const;
    std::optional<JsonField> findField(JsonPointer pointer) const;
    SourceRange rangeOf(JsonPointer pointer, JsonRangeRole role) const;
};
```

`JsonRangeRole`：

```text
FieldWhole
Key
Value
ObjectWhole
ArrayWhole
InsertionPoint
```

### 7.3 JSON Pointer

内部可以使用 JSON Pointer：

```text
/graphs/Execute/nodes/log/type
```

但 public editor API 不应长期暴露 array index。

规则：

- object map path 可用于内部定位。
- array index 只用于无身份值，如坐标 `[0]` / `[1]`。
- graph/node/edge/param public target 使用 stable id。

### 7.4 JSON Schema Facade

目标不是实现完整 JSON Schema 生态，而是提供足够 GraphScript 使用的 facade：

- load local schema document。
- read `$id`。
- read `$defs`。
- validate basic shape。
- expose validation diagnostics with source range。
- expose completion/hover metadata。

第一版支持：

```text
type
properties
required
additionalProperties
enum
const
default
description
$defs
local $ref
```

暂缓：

```text
remote $ref
dynamicRef
unevaluatedProperties
complex applicators
custom format validators
```

### 7.5 Generic JSON Tree/Graph Inspector

借鉴 JSON Crack：

- 将 JSON document 投影为 generic tree graph。
- 支持 collapse / search / source preview。
- 只用于 debug/inspect，不作为 GraphScript semantic graph。

输出节点：

```text
ObjectFieldNode
ArrayItemNode
PrimitiveValueNode
CommentNode optional
```

该 inspector 可以用于 VS Code/Web 的“查看 JSON 文档结构”，但不能替代 GraphProjection。

### 7.6 JSON Patch-like Edit IR

定义通用 edit IR：

```json
[
  { "op": "replace", "path": "/graphs/Execute/nodes/log/editor/pos", "value": [120, 240] },
  { "op": "add", "path": "/graphs/Execute/nodes/log/properties/message", "value": "hello" }
]
```

注意：这只是中间 edit IR，不直接写文件。

写文件必须经过：

```text
JsonPatchIR
  -> JsonDocument source binding
  -> JsonRewriteBuilder
  -> TextPatch
```

### 7.7 Rewrite Primitives

最小 rewrite primitives：

```text
replaceValue(pointer, value)
insertField(objectPointer, key, value, positionPolicy)
deleteField(fieldPointer)
renameKey(fieldPointer, newKey)
replaceStringToken(pointer, newString)
```

必须支持：

- range guard。
- source hash guard。
- preserving untouched text。
- trailing comma policy。
- indentation from siblings。
- leading comment preservation on delete/rename。

### 7.8 测试

新增：

```text
tests/test_json_document.cpp
tests/test_json_pointer.cpp
tests/test_json_schema_facade.cpp
tests/test_json_rewrite.cpp
tests/test_json_tree_inspector.cpp
```

覆盖：

- pointer -> field/value range。
- schema diagnostic -> source range。
- insert field into empty object。
- insert field into non-empty object。
- delete field with leading comment。
- rename key without changing value。
- replace value preserving quote style policy。

### 7.9 禁止事项

- 不在这一层解析 `log.enter` 的 pin 语义。
- 不在这一层判断 edge 合法性。
- 不把 JSON Patch 直接应用为文件写入。
- 不使用 `std::map` 或排序 JSON object 的方式改变作者字段顺序。
- 不把 generic JSON tree inspector 暴露成 semantic graph editor。

---

## 8. Layer 3: Graph Asset Schema Layer

### 8.1 职责

把通用 JSON document 解释成 GraphScript asset source：

- graph asset root shape。
- declaration schema vocabulary。
- imports。
- stable id registry。
- symbol table。
- type/schema/node declaration binding。
- graph/node/event/step source binding。
- GraphScript diagnostics。
- graph edit intents。

不负责：

- JSON5 token parsing。
- generic JSON tree visualization。
- low-level text patch generation。
- final runtime execution.

### 8.2 Asset Source Shape

第一版 `.graph.json5`：

```json5
{
  version: 1,
  imports: [
    { id: "ability.nodes", path: "ability_nodes.graph.d.json5" },
  ],
  graphs: {
    Execute: {
      id: "graph.execute",
      schema: "AbilityGraph",
      params: {},
      nodes: {},
      events: {},
    },
  },
}
```

Canonical rules：

- `graphs` 是 map。
- graph key 是 alias。
- graph `id` 是 stable identity。
- `nodes` / `params` / `events` / `steps` 都是 map。
- command/edge 用 `steps` map + `kind` + `id` + `order`。
- array 只用于坐标、普通 list 等无身份值。

### 8.3 Declaration Schema Shape

第一版 `.graph.d.json5` 是 JSON Schema + GraphScript vocabulary：

```json5
{
  $schema: "https://json-schema.org/draft/2020-12/schema",
  $id: "graphscript://ability.nodes",
  x-graphscript: {
    module: { id: "ability.nodes", version: "1.0.0" },
  },
  $defs: {
    PrintString: {
      type: "object",
      x-gs-kind: "node",
      x-gs-id: "node_decl.print_string",
      properties: {
        message: {
          type: "string",
          default: "",
          x-gs-pin: { id: "pin.print.message", kind: "data", direction: "input" },
        },
      },
      x-gs-node: {
        pins: {
          enter: { id: "pin.print.enter", kind: "exec", direction: "input" },
          exit: { id: "pin.print.exit", kind: "exec", direction: "output" },
        },
      },
    },
  },
}
```

### 8.4 GraphScript Vocabulary

第一版 vocabulary：

```text
x-graphscript
  module.id
  module.version

x-gs-kind
  "type"
  "node"
  "graphSchema"

x-gs-id
  stable declaration id

x-gs-pin
  id
  kind: "exec" | "data"
  direction: "input" | "output"

x-gs-node
  pins
  editor.defaultSize

x-gs-graph-schema
  connectionPolicy
  requiredEvents
```

后续再考虑正式 JSON Schema `$vocabulary` 声明。

### 8.5 Semantic Model

核心数据：

```cpp
struct GraphAssetSemanticModel {
    ModuleGraph modules;
    StableIdIndex stable_ids;
    SymbolTable symbols;
    std::vector<GraphSymbol> graphs;
    std::vector<NodeInstanceSymbol> nodes;
    std::vector<EventSymbol> events;
    std::vector<StepSymbol> steps;
    std::vector<Diagnostic> diagnostics;
    GraphAssetSourceMap source_map;
};
```

Source binding：

```text
GraphId -> graph object range / alias key range / schema value range
NodeId -> node object range / alias key range / type value range / property ranges
EventId -> event object range / alias key range
StepId -> step object range / alias key range / from/to/kind/order ranges
DeclarationId -> $defs key range / schema object range / pin ranges
```

### 8.6 Diagnostics

Graph asset layer diagnostics：

```text
GSJSON-SHAPE-001 missing graphs
GSJSON-ID-001 missing stable id
GSJSON-ID-002 duplicate stable id
GSJSON-IMPORT-001 unresolved import
GSJSON-SYM-001 unresolved node type
GSJSON-SYM-002 unresolved graph schema
GSJSON-PIN-001 unresolved pin
GSJSON-PIN-002 pin direction mismatch
GSJSON-EDGE-001 exec/data mismatch
GSJSON-SCHEMA-001 connection policy violation
```

每个 diagnostic 必须有：

- code。
- message。
- source range。
- semantic target。
- optional quick fix。

### 8.7 Graph Projection

GraphProjection 只消费 GraphAssetSemanticModel：

```text
GraphAssetSemanticModel
  -> GraphModel
  -> FlowGraphValidation
  -> RuntimeGraph
```

它不读取 JSON5 syntax node。

Projection 输出：

```text
Graph
Node
Pin
Edge
InvalidNode
InvalidEdge
SourceBinding
Diagnostics
```

### 8.8 Semantic Editing

Graph edit intent：

```text
MoveNode(nodeId, pos)
SetNodeProperty(nodeId, propertyPath, value)
ConnectPins(graphId, eventId, from, to)
DisconnectEdge(edgeId)
RenameNodeAlias(nodeId, newAlias)
RenameNodeType(oldType, newType)
```

Lowering：

```text
GraphEditIntent
  -> GraphAssetRewriteRequest
  -> JsonPatchIR
  -> JsonRewriteBuilder
  -> TextPatch
```

### 8.9 禁止事项

- 不直接拼接 JSON5 字符串。
- 不直接使用 array index 作为 graph/node/edge public identity。
- 不在 GraphAssetSemanticModel 中保存 JSON5 syntax node 指针作为 public API。
- 不把 JSON Schema validation error 当成最终 graph diagnostic；需要转换成 GraphScript diagnostic target。
- 不在 GraphProjection 中重新读取 imports 或 schemas；这些必须在 semantic model 阶段完成。

---

## 9. Phased Implementation Plan

### Phase 0: Decision And Fixture Setup

Deliverables:

- Keep `.gs/.d.gs` path untouched.
- Add JSON5 plan docs.
- Add fixtures under `tests/fixtures/json5/`.
- Choose provisional extension `.graph.json5` / `.graph.d.json5`.

Exit gate:

- Fixtures reviewed.
- Parser candidate chosen for spike.

### Phase 1: JSON5 Syntax Spike

Deliverables:

- `Json5Parser`.
- `Json5SyntaxTree`.
- range queries for object/field/key/value.
- syntax diagnostics.

Exit gate:

- `minimal.graph.json5` parses.
- `invalid.graph.json5` produces partial tree.
- comments and trailing comma ranges are visible.

### Phase 2: JSON Document Tools

Deliverables:

- `JsonDocument`.
- `JsonPointer`.
- `JsonFieldIndex`.
- JSON Schema facade with local `$defs`.
- source range preview.
- generic JSON tree inspector model.

Exit gate:

- `/graphs/Execute/nodes/log/type` resolves to a value range.
- schema validation emits diagnostics with ranges.
- generic tree inspector can visualize the document shape.

### Phase 3: JSON Rewrite

Deliverables:

- `JsonPatchIR`.
- `JsonRewriteBuilder`.
- replace/insert/delete/rename primitives.
- comment and trailing comma policy.

Exit gate:

- replace `editor.pos` without reformatting file.
- insert missing `properties.message`.
- delete a step preserving surrounding syntax.
- rename a map key preserving the object body.

### Phase 4: Graph Asset Schema

Deliverables:

- asset root shape validator.
- declaration schema loader from `$defs + x-gs-*`.
- stable id registry.
- import/declaration binding.
- GraphAssetSemanticModel.

Exit gate:

- node type resolves from `.graph.d.json5`.
- duplicate id diagnostic points to both source ranges.
- missing node type diagnostic points to `type` value range.

### Phase 5: Graph Projection And Validation

Deliverables:

- GraphProjection from GraphAssetSemanticModel.
- invalid node/edge preservation.
- pin direction/type validation.
- connection policy validation.

Exit gate:

- GraphModel contains graph/node/event/edge.
- invalid edge is shown with diagnostic target.
- RuntimeGraph or debug graph dump can be produced.

### Phase 6: Semantic Graph Editing

Deliverables:

- `MoveNode`.
- `SetNodeProperty`.
- `ConnectPins`.
- `DisconnectEdge`.
- all edits lower to local JSON5 TextPatch.

Exit gate:

- no whole-file stringify.
- source hash guard and range guard work.
- after patch, reparse/projection succeeds.

### Phase 7: CLI Integration

Prototype commands:

```text
json5-parse
json5-lint
json5-project
json5-patch
json5-inspect-json
json5-inspect-graph
```

Exit gate:

- commands work on fixtures.
- diagnostics are JSON output first.
- command output can be consumed by tests.

### Phase 8: Editor Adapter

Deliverables:

- JSON5 editor adapter.
- `/api/state` projection from JSON5 GraphModel.
- source preview opens `.graph.json5`.
- locate/highlight graph/node/edge/property source ranges.

Exit gate:

- Web can display JSON5-backed graph.
- move node round-trips to source.
- source panel jumps to node and edge ranges.

### Phase 9: Canonical Source Decision

Decision criteria:

- local rewrite quality acceptable.
- GraphProjection parity with existing core workflows.
- source diagnostics parity for primary errors.
- dependency/build risk acceptable.
- implementation complexity lower than custom `.gs` route.

Outcomes:

```text
A. Promote JSON5 to canonical source.
B. Keep JSON5 as AI patch/interchange/import-export format.
C. Drop JSON5 route and continue tree-sitter .gs route.
```

---

## 10. Work Estimates

Rough estimate for one experienced engineer:

| Scope | Duration | Purpose |
| --- | ---: | --- |
| Thin slice through MoveNode | 1-2 weeks | prove source map + local rewrite |
| CLI prototype through graph projection | 3-5 weeks | prove semantic model and diagnostics |
| Editor alpha | 6-10 weeks | prove Web/CLI authoring workflow |
| Replace `.gs` main path | 2-4 months | parity migration, cross-file edits, regression coverage |

Cost drivers:

- parser range/trivia quality.
- comment ownership and trailing comma policy.
- JSON Schema facade depth.
- cross-file import and source hash guards.
- existing editor API compatibility.

Stop condition:

- If thin slice cannot preserve comments and perform local rewrite without whole-file formatting, keep JSON5 as interchange/AI patch format only.

---

## 11. Interface Stability Levels

Use stability levels to avoid freezing bad APIs too early:

```text
Experimental:
  Json5SyntaxNode concrete shape
  tree-sitter wrapper details
  generic JSON tree inspector graph shape

Prototype-stable:
  JsonPointer
  JsonRangeRole
  JsonPatchIR
  GraphEditIntent

Must stabilize before Editor Adapter:
  SourceRange
  Diagnostic
  SemanticTarget
  GraphAssetSourceMap
  TextPatch
```

Rule:

- UI and CLI JSON output may only depend on prototype-stable or stable contracts.
- Concrete parser node shapes remain internal.

---

## 12. Test Matrix

| Layer | Unit tests | Integration tests | Golden tests |
| --- | --- | --- | --- |
| JSON5 syntax | token/range/error | parse fixture set | syntax dump |
| JSON document tools | pointer/schema/rewrite | patch then reparse | text patch output |
| Graph asset schema | ids/symbols/imports | asset + declaration binding | semantic dump |
| Graph projection | pin/edge/schema rules | graph model from fixture | graph JSON dump |
| Editor adapter | state mapping | move/connect through API | `/api/state` snapshot |

Required golden fixtures:

```text
minimal.graph.json5
comments.graph.json5
invalid.graph.json5
duplicate-id.graph.json5
unknown-pin.graph.json5
core.graph.d.json5
```

Each rewrite golden must assert:

- exact changed text.
- unchanged comments.
- unchanged unrelated whitespace.
- successful reparse.
- diagnostics after patch.

---

## 13. Prototype Acceptance Criteria

Prototype is successful only if all are true:

- JSON5 parser preserves source ranges for key/value/object/comment.
- JsonDocument can resolve JSON Pointer to source ranges.
- JSON Schema facade can validate local declaration schema.
- GraphAssetSemanticModel resolves node type and pin.
- GraphProjection produces graph/node/edge model.
- Move node modifies only `editor.pos`.
- Connect pins inserts one `steps.<alias>` object.
- Unknown pin diagnostic points to the bad endpoint string.
- Web/CLI can inspect source range for graph/node/edge.

---

## 14. Risk Register

| Risk | Layer | Mitigation |
| --- | --- | --- |
| JSON5 parser lacks good error recovery | Layer 1 | spike tree-sitter-json5 first; fallback to restricted parser |
| comments are hard to preserve | Layer 2 | define comment ownership early; test delete/insert cases |
| JSON Schema gets overused for graph semantics | Layer 3 | keep graph validation in GraphScript validator |
| stable ids duplicate or drift from aliases | Layer 3 | central StableIdIndex + diagnostics |
| rewrite becomes whole-file stringify | Layer 2/3 | forbid stringify in editor ops; require TextPatch tests |
| arrays make patch targets unstable | Layer 3 | canonical maps for semantic objects |
| UI confuses JSON tree graph with semantic graph | Layer 2/3 | keep separate inspector and GraphProjection views |
| JSON Schema `$ref` scope grows too large | Layer 2/3 | support local `$defs` first; reject remote refs in prototype |
| source bindings drift after patch | Layer 2/3 | always reparse after patch; compare stable targets before/after |
| semantic model duplicates existing graph code | Layer 3 | keep graph validation/projection in `graph/*`; JSON5 only feeds it |

---

## 15. Decision Matrix

Use this matrix at Phase 9:

| Criterion | Promote JSON5 | Keep as interchange | Drop route |
| --- | --- | --- | --- |
| Local rewrite | robust, low-noise | works only for simple edits | unreliable |
| Source diagnostics | precise semantic targets | syntax/shape only | weak ranges |
| Graph projection | parity for core graph | partial graph only | too much special casing |
| Editor integration | Web/CLI workflow works | useful for import/export | blocks UX |
| Build/deps | acceptable | acceptable as tool-only | too risky |
| Complexity vs `.gs` | clearly lower | mixed | higher |

Promotion requires all “Promote JSON5” cells to be true for core workflow.

---

## 16. Open Questions

- Which JSON5 parser is the best fit for range + comment + error recovery?
- Should declaration schemas use official JSON Schema `$vocabulary` immediately, or plain `x-gs-*` first?
- Should `imports` be array or map in canonical source?
- Should `steps.order` be integer gaps of 10, fractional, or generated stable order keys?
- How much JSON Schema validation should run before GraphAssetSemanticModel construction?
- Should graph-as-node be generated, explicit in `$defs`, or both?
- What is the minimal Web adapter surface needed for prototype?
