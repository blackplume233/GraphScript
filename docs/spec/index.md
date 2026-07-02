# GraphScript 规范

> GraphScript 产品规范入口。

GraphScript 的最高目标不是“一个图 DSL”。它的目标是一种共享创作格式，让
人类和 AI 可以在同一个游戏资产上协作，而不是互相干扰：

- AI 可以直接读取、lint、诊断和 patch 序列化文本。
- 人类可以通过图编辑器和资产编辑器工作。
- 两条编辑路径都收敛到同一个源码文本，并保留注释、空行、局部格式和源码
  身份。

本规范中的其他内容都服务于这个目标。

---

## 当前原则

1. **人类 + AI 协作优先**：文本编辑和图编辑是同一资产的两个视图，不是两
   个真源。
2. **文本是 canonical source**：`.gs` 和 `.d.gs` 文件必须可读、可 lint、
   可诊断、可 patch，并适合 LSP 风格工具。
3. **Graph 是领域投影**：graph、node、entry、pin、edge、flow/link 和
   runtime 概念都在序列化层之上解释。
4. **无损文档模型**：CST/source range/trivia 是编辑契约的一部分。无关编辑
   必须保留空行、注释和局部格式。
5. **最小源码 patch**：视觉编辑应尽可能映射到最小稳定文本范围，而不是整
   文件重写。
6. **分层实现**：序列化语法、序列化文档库、Graph domain 和 Web editor 保
   持职责分离。
7. **语法中立原则**：产品设计理念不能内嵌临时语法形态。当前语法或实验性
   语法归 `docs/syntax/` 管；spec 可以链接语法文档，但不能把临时语法写成
   稳定设计理念。

---

## 规范地图

`docs/spec/` 保存项目产出的产品架构、领域规范和开发文档。`docs/syntax/`
保存语法设计材料。`.trellis/spec/` 保存 Agent 工作指引、设计审查标准和工
作检查清单。

### 当前规范

| 文档 | 目的 |
| --- | --- |
| [架构](./architecture.md) | 当前产品愿景、层边界和编辑流水线。 |
| [当前 Graph Domain](./graph-domain.md) | 当前 Graph/FlowGraph 投影契约和图领域规则。 |
| [开发指南](./development-guide.md) | 构建、测试、扩展和实现工作流指引。 |

### 语法工作区

所有语法设计材料都在 [docs/syntax/](../syntax/) 下：

| 目录 | 含义 |
| --- | --- |
| [docs/syntax/current/](../syntax/current/) | 当前已实现语法形状和决策。 |
| [docs/syntax/design/](../syntax/design/) | 活跃的长文语法草稿。 |
| [docs/syntax/archive/](../syntax/archive/) | 为迁移证据保留的历史语法资料。 |

稳定 spec 文档应使用语法中立的方式描述目标、层边界、数据流和契约。具体
语法示例应放在 syntax workspace 中，除非该章节明确是在描述当前实现行为。

### 迁移记录

| 文档 | 目的 |
| --- | --- |
| [旧 Graph DSL 功能清单](./migration/legacy-graph-dsl-feature-inventory.md) | 旧 DSL 能力和迁移测试意图归档。 |

---

## 命令面

当前 CLI 工作应保持 source-first 路径可见：

| 任务 | 命令 / 文件 |
| --- | --- |
| 解析 `.gs/.d.gs` 资产语法 | `gs parse -i file.gs` |
| Lint `.gs/.d.gs` 资产语法 | `gs lint -i file.gs` |
| 投影 graph domain view | `gs project -i file.gs -I import.d.gs --graph Execute` |
| 应用源码 patch | `gs patch -i file.gs --op <op> ...` |
| 交互式编辑器 | `gs edit [-I import.d.gs]` |
| Web GUI 编辑器 | `gs serve [-I import.d.gs] [-p 8080]` |
| 运行测试 | `./build/Release/gs_tests.exe` |

所有 `.gs` 和 `.d.gs` fixture 都在 `tests/fixtures/`。
