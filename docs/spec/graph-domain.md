# 当前 Graph Domain

> 序列化层之上的图领域语义契约。

Graph domain 是序列化资产事实之上的投影。它为人类提供可视化图编辑模型，
同时保持 source text 对 AI、lint、LSP 和精确 patch 是 canonical。

---

## 边界

Graph domain 可以定义：

- graph authoring model。
- graph entry。
- node authoring model。
- pin definition 和 pin reference。
- edge 和 connection policy。
- graph diagnostic。
- graph edit operation。
- graph runtime 或 bake input。

Graph domain 不能要求基础 parser 或基础 serialization AST 包含 graph-specific
node。

---

## 源码映射

本文描述 graph-domain source role，不描述具体 grammar。当前语法或草稿语法示
例属于 `docs/syntax/`。

| 序列化 source role | Graph 解释 |
| --- | --- |
| 带名称和可选 schema/type 的 graph authoring root | 同名、同 schema/type 的 graph domain root。 |
| 嵌套的 entry/event-like source item | Graph entry 或执行 block。 |
| 带稳定本地名称的 typed node contribution | 使用该本地名称和类型的候选 graph node。 |
| editor metadata source item | Node/editor metadata。 |
| 被标记为 flow/data pin 的 declaration member | FlowGraph pin definition。 |
| 受限 connection command | 候选 graph edge。 |

每个被投影出的 graph item 都应保留 source binding：

```text
Graph -> source graph-root range
Entry -> source entry/event range
Node -> source node-contribution range
Node property -> source property or field range
Pin definition -> declaration field range
Edge -> source command call range
Diagnostic -> best source range plus related ranges
```

如果 graph item 没有精确 source binding，必须标记为 synthetic 或 invalid，
让编辑器和 AI 工具知道 patch 时需要保守处理。

---

## 绑定源码的创作模型

面向编辑器的 graph model 不应是一个脱离源码的 DTO，再被序列化回文本。它
应该是一个 source-bound projection，通过稳定 anchor 与 serialization
document library 交互：

```text
Document model
  -> typed AST facade
  -> semantic model
  -> graph projection with SourceBinding anchors
  -> editor graph state
```

Graph node、pin、edge、entry 和可编辑 metadata 应直接携带 `SourceBinding`，
或引用一个能解析到 `SourceBinding` 的 domain item。这并不意味着 Graph
domain 拥有或直接操作 CST 内部。CST 保留在 serialization document layer；
Graph domain 只消费该层公开的 binding/anchor handle。

有用的 binding 数据包括：

- source file identity。
- document layer 暴露的 primary syntax anchor kind。
- primary source range。
- 编辑 property value 时的 value range。
- 新增相关语法时的 insertion anchor。
- fragment-based asset 的 contribution range。
- 可用时的 stable semantic/domain id。

这不是“runtime graph 依赖 CST”，也不是“Graph domain 通过拥有 parser 细节
破坏分层”。runtime/baked graph data 可以保持独立。关联关系属于 editor、
diagnostics、quick fix 和 AI repair loop 使用的 authoring graph model。

没有这种关联，graph edit 只能猜测如何写回文本，通常会退化成整文件输出并
破坏注释或局部格式。有了 document anchor，视觉编辑就能变成针对已知 source
range 的 document operation。

---

## Edit Contract

Graph edit operation 应降到 serialization document operation：

| Graph edit | 首选 source operation |
| --- | --- |
| add node | 在 graph authoring root 中插入 typed node contribution。 |
| delete node | 在安全时删除 source node contribution 和相关 source-bound edge。 |
| rename node | patch node contribution name 和引用。 |
| move node | patch editor metadata property。 |
| set node property | patch 对应 property value。 |
| connect pins | 插入受限 command call。 |
| disconnect edge | 删除 source command call。 |
| add entry | 插入嵌套 entry/event-like source item。 |

UI、CLI 或 SDK 可以暴露简单的集合式操作，例如 `steps.push(...)`、
`nodes.delete(...)` 或 `edge.reconnect(...)`。这些只是外层 authoring API，
后端必须把它们记录为 source-bound semantic operation，再生成
serialization document operation 和 `TextPatch`。不能先修改一个脱离源码的
graph DTO，再在保存时整图序列化。

每个 graph edit 在接受前必须完成：

```text
graph operation
  -> semantic edit op
  -> source anchor lookup
  -> TextPatch
  -> reparse + relint + reproject
  -> expected semantic delta check
```

如果重新投影后的 graph facts 与操作意图不一致，本次编辑必须失败并返回
diagnostic。重复节点、重复 step 或重复 edge 的删除必须依赖 stable id、
source range 或同一列表内的 item index，不能只按语义相等删除。

当首选 patch anchor 缺失时，系统应插入新的 fragment 或 contribution，而不
是重写整个文件。

---

## Graph 诊断

Graph diagnostic 是投影出的 graph fact 上的领域诊断，不是 parser error。例
如：

- unknown node type。
- unknown pin。
- pin direction mismatch。
- type mismatch。
- fan-in/fan-out policy violation。
- missing required entry。
- disconnected required flow。

诊断必须包含 source range，让 AI 可以修复文本，也让人类能在文本和
图视图中看到问题。

---

## Web Editor 职责

Web editor 应渲染 graph domain model，并发出 graph operation。它不应发明
backend 无法 parse、bind、project、diagnose 和 patch 的语义。

期望流程：

```text
UI action
  -> backend graph operation
  -> document operation / TextPatch
  -> reparse + relint + reproject
  -> refreshed UI state
```

这能让可视化图编辑和 AI 文本编辑保持在同一个协作循环里。
