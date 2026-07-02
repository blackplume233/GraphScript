# GraphScript 架构

> 当前用于人类 + AI 协作式游戏资产编辑的产品架构。

GraphScript 是一种 AI Native 游戏资产格式和创作栈。系统围绕一个 canonical
源码文本设计：AI 可以直接编辑它，工具可以像脚本一样 lint 它，领域层可以
把它投影成图或其他视图，视觉编辑器也可以把操作 patch 回文本，同时不破坏
用户已有文本。

---

## 最高目标

人类和 AI 应该能在同一个资产上自然协作：

- AI 直接处理序列化文本、诊断、source range 和结构化 patch。
- 人类通过图编辑器和资产编辑器工作。
- 两条路径都保留同一个 canonical source file，包括注释、空行、局部格式和
  稳定身份。

CST/AST/document model、Graph projection、LSP-facing diagnostics 和最小
source patching 规则都服务于这个目标。

---

## 实现层

```text
┌─────────────────────────────────────────────────────────────────────┐
│  Web Editor                                                         │
│  人类 graph/asset 编辑界面，发出可 replay 的操作。                  │
├─────────────────────────────────────────────────────────────────────┤
│  Graph Domain                                                       │
│  把序列化事实解释为 graph、node、pin、edge、entry。                 │
├─────────────────────────────────────────────────────────────────────┤
│  Serialization Document Library                                     │
│  CST、typed AST facade、semantic model、diagnostics、rewrite ops。  │
├─────────────────────────────────────────────────────────────────────┤
│  Serialization Syntax                                               │
│  .gs/.d.gs source grammar；具体语法形态放在 docs/syntax/。          │
└─────────────────────────────────────────────────────────────────────┘
```

### 1. 序列化语法

基础语言描述游戏资产事实：

- 文件和 import。
- 资产边界和贡献片段。
- 类型化记录和命名字段。
- 值和引用。
- attribute/metadata。
- declaration 和 schema。

它不把 `Graph`、`Node`、`Pin`、`Edge`、HTN task、table row 或 dialogue
branch 变成 parser 的基础概念。这些概念属于领域投影层。

当前语法形状见：[docs/syntax/current/serialization-syntax.md](../syntax/current/serialization-syntax.md)。

### 2. 序列化文档库

文档库是编辑基础。它必须提供：

- 无损 CST，包含 token、trivia、注释、空行、source range、missing node 和
  error node。
- CST 之上的 typed AST wrapper。
- semantic model 和 partial binding。
- 机器可用的 diagnostics 和 quick fix。
- document operation 和 rewrite planning。
- 最小 `TextPatch` 输出。

文档库让 AI 可以直接 patch 文本，也让图编辑可以保留周围源码，而不是整文
件重写。

### 3. Graph Domain

Graph domain 消费序列化事实，并投影出 authoring model：

- graph authoring root。
- node candidate。
- entry。
- pin。
- edge。
- graph diagnostic。
- 可以降到 document operation 的 graph edit operation。

Graph domain 规则由 declaration、schema、attribute、binder 和 projection
provider 描述。它们不能反向泄漏到基础 parser 概念里。

graph authoring model 应通过稳定 source binding 或 document anchor 与文档
模型关联。它不应依赖 parser 内部实现，也不应直接拥有 CST。graph item 可
以为了编辑器交互缓存领域数据，但可编辑项必须能解析回序列化文档层公开的
source anchor。这种关联让精确 patch 成为可能，而不需要把整个图重新序列
化回文本。

当前 Graph 契约见：[graph-domain.md](./graph-domain.md)。

### 4. Web Editor

Web editor 是人类视觉编辑界面。它应该：

- 从 backend/domain state 渲染，而不是发明自己的 graph semantics。
- 向 backend 发送可 replay 的编辑操作。
- 展示来自 document/domain model 的 diagnostics 和 source range。
- 即使工作流以图为先，也把 source text 当作 canonical。

---

## Source-First 流水线

```text
.gs/.d.gs source text
  -> Serialization parser
  -> Lossless CST + Syntax diagnostics
  -> Typed AST facade
  -> Semantic model + asset lint
  -> Domain projections
  -> Graph/Web/CLI authoring views
```

无效或不完整文本也应尽可能产生部分结构。一个损坏的 property 不应让整个
graph 消失；它应产生部分模型、diagnostics 和 invalid domain item。

---

## 视觉编辑到文本 Patch

```text
Human graph edit
  -> Graph domain operation
  -> Source binding / document anchor lookup
  -> Serialization document operation
  -> Minimal TextPatch
  -> Reparse + relint + reproject
```

要求：

- patch 最小稳定 source range。
- byte-for-byte 保留无关文本。
- 保留注释、空行和局部格式。
- patch 后重新 parse，并暴露新的 diagnostics。
- 如果找不到精确 patch anchor，应插入新 fragment，而不是重写整个文档。

编辑器可以在内存中维护交互式 graph model，但该模型必须通过 document
library anchor 绑定到源码。它应保存或引用足够的稳定绑定信息，以回答“这次
视觉编辑应该 patch 哪段 source range”。如果无法回答，应降级为显式 fragment
或 diagnostic，而不是假装可以安全地整体序列化 graph。

---

## AI 编辑到 Graph 刷新

```text
AI text patch
  -> Reparse
  -> Syntax/semantic/domain diagnostics
  -> Projection delta
  -> Web editor refresh
```

面向 AI 的 diagnostics 应识别 source range、期望形状、候选符号和可执行
quick fix。自然语言建议可以叠加在上层，但核心诊断契约应保持结构化。

---

## 依赖方向

允许：

```text
Serialization Syntax
  -> Serialization Document Library
  -> Graph Domain
  -> Web Editor
```

禁止：

```text
Serialization Syntax -> Graph Domain
Serialization Syntax -> Web Editor
Serialization Document Library -> Web Editor
```

基础 parser 必须能复用于 graph、table、dialogue、quest、level 等游戏资产
领域。

---

## 当前 CLI 面

| 子命令 | 目的 |
| --- | --- |
| `parse` | 解析 `.gs/.d.gs` 并报告语法结构和 diagnostics。 |
| `lint` | 在可用范围内运行语法、语义和领域 diagnostics。 |
| `project` | 投影 Graph/FlowGraph 等领域视图。 |
| `patch` | 应用文档感知的 source patch。 |
| `edit` | 运行交互式 source/domain editor。 |
| `serve` | 运行 Web editor server。 |

旧 graph-only 命令和旧手写 DSL 行为属于归档或迁移材料，不是当前架构目标。
