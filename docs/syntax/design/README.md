# 语法设计草稿

本目录保存 AI Native 语法和资产格式的设计草稿。这些文档是设计上下文，不会
自动成为当前实现规范。当某个设计决策进入实现指导阶段时，应把提炼后的结论
移动到 `../current/` 或 `docs/spec/`。

---

## 推荐阅读顺序

1. [JSON-like Authoring Syntax 草稿](./json-like-authoring/syntax-draft.md)
   - 当前主线目标语法。用于继续收敛 `.gs/.d.gs` 的 authoring surface。
2. [JSON-like Authoring Syntax 记录](./json-like-authoring/record.md)
   - 记录这条主线为什么这样选，以及哪些问题后续必须验证。
3. [语法草稿对比](./syntax-draft-comparison.md)
   - 比较主线和历史替代路线，辅助判断是否需要回退或吸收局部设计。
4. [语法草稿文档要求](./syntax-draft-requirements.md)
   - 写新草稿或审草稿时使用的格式要求。

---

## 当前主线

| 文档 | 状态 | 用途 |
| --- | --- | --- |
| [JSON-like Authoring Syntax 草稿](./json-like-authoring/syntax-draft.md) | 当前主线目标草稿 | 以 JSON/TS-like object structure 为基础，加入 dotted key、typed slot、command、assignment、annotation。 |
| [JSON-like Authoring Syntax 记录](./json-like-authoring/record.md) | 决策记录 | 保存本轮讨论背景、取舍、多入多出节点映射、`steps` 定位和 linter 约束。 |

如果只想知道“后续最应该实现哪条语法”，优先看这一组。

---

## 替代路线

这些文档不是当前主线，但保留为设计对照和局部方案来源：

| 文档 | 状态 | 保留原因 |
| --- | --- | --- |
| [面向节点的语法草稿](./node-oriented/syntax-draft.md) | 替代草稿 | 借鉴 KDL/节点式 DSL，适合评估更强 DSL 感的写法。 |
| [JSON 超集 DocumentNode 语法草稿](./json-superset-documentnode/syntax-draft.md) | 替代草稿 | 评估严格接近 JSON/JSON5 的结构化 authoring 成本。 |
| [语法草稿对比](./syntax-draft-comparison.md) | 对比文档 | 横向比较主线和替代路线。 |

---

## 背景材料

这些是早期大范围设计记录，信息量大但不应作为当前 grammar 的直接入口：

| 文档 | 状态 | 用途 |
| --- | --- | --- |
| [AI Native Syntax Draft](./ai-native/syntax-draft.md) | 早期讨论草案 | 记录 TypeScript-flavored source language 的早期想法。 |
| [AI Native Asset Format](./ai-native/asset-format.md) | 早期设计记录 | 记录通用游戏资产格式、图/文本互转和 Roslyn-like 架构讨论。 |

---

## 维护规则

- 新的语法主线先更新当前主线草稿，不要再平行新增同级候选，除非确实是替代
  路线。
- 替代草稿如果不再参与决策，应移动到 `../archive/`，或在本文中标为归档。
- 稳定实现契约放到 `docs/spec/`，不要写进语法草稿。
- 当前已实现语法放到 `../current/`，不要从 design 草稿反推实现状态。
