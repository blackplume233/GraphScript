---
status: complete
updated_at: 2026-06-30
artifact_language: zh-CN
execution_mode: native-codex-goal
native_goal_id: 019f0434-47f7-7c60-9496-6fea360aa994
current_loop: loops/loop-017.md
next_sub_goal: 已完成；后续只剩非阻塞视觉 polish
last_verified_loop: loops/loop-017.md
references: references.md
---

# Auto Goal 任务

## 目标

- 基础目标: 让 GraphScript Web GUI 在节点图渲染、节点操作、连线操作、选择/框选、右键菜单、快捷键和文本同步方面尽量贴近 Unreal Engine Blueprint Editor 的交互体验，并把差异逐条列举、逐条实现、逐条验证。
- 完成证据:
  - `references.md` 明确列出 UE 蓝图的节点渲染设计和操作设计，并为每一项标注 GraphScript 当前状态、实现方案和验证方式。
  - React Flow 画布支持蓝图式高频操作：空白框选、多选、选中集合拖动、右键菜单、右键/中键/Space 平移、滚轮缩放、快捷键删除/聚焦、pin 拖出连接、pin 拖出空白处打开上下文搜索、edge 重连/删除。
  - 节点渲染支持蓝图式结构：标题栏、exec/data pin 视觉区分、输入/输出列、内禀 fields 独立显示、选中/hover/diagnostic 状态、连线层级和非遮挡布局。
  - 图操作继续通过现有 CLI/API 命令持久化，和 Source/Current Graph 文本保持协同；图上改动不能静默覆盖用户未应用文本。
  - 每个已实现项有前端构建、必要 C++ 测试和浏览器/脚本实测证据；未实现项保留在 `references.md` 的差距清单中，不伪装完成。

## 约束

- 回复和目标文档使用中文。
- 前端为 React 19 + TypeScript + Vite + Tailwind，画布为 `@xyflow/react`；优先复用 React Flow 原生选择、拖拽、连接能力。
- Core C++ 保持领域无关；不得把 UE/Blueprint 语义写入 core，只能作为 Web GUI 交互参考。
- 编辑操作继续走 CLI/API 命令，保证可记录、可回放。
- 不执行破坏性 git 操作；不回滚用户已有未提交改动。
- 验证命令: `pnpm build` 于 `webapp/`；必要时运行 `build/Release/gs_tests.exe`。完整 `gs.exe` 重链若因本地 server 占用失败，需要记录原因。

## 执行指导

- 每轮开始前重读本文件。
- 第一轮先建立 UE 蓝图契约清单，并实现高频基础操作，不追求一次吞掉全部细节。
- 每个蓝图行为都按 `UE 行为 -> 当前差距 -> 实现路径 -> 验证` 记录在 `references.md`。
- 优先级: 选择/框选/多选拖动 > 右键/中键/Space 平移和菜单冲突 > pin 拖出搜索 > 连线/edge 操作 > 节点视觉细节 > 快捷键和 polish。
- 如果浏览器自动化不可用，必须记录阻塞，并用构建、单元测试和可人工复现步骤补证据。
