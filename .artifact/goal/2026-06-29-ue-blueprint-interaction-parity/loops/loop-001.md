# 循环 001

## 基础目标切片

建立 UE 蓝图节点图渲染/操作契约清单，并完成第一批高频图操作校准。

## 子目标

先让选择、框选、多选和平移行为接近蓝图，并修复右键拖动画布与右键菜单的冲突；同时保留当前 React Flow 渲染和 GraphScript 文本同步路径。

## 仓库基准检查

- 文档和回复语言: 中文。
- 前端: React 19 + TypeScript + Vite + `@xyflow/react`。
- Core: C++17，领域无关，不能把 UE 语义写入 core。
- 编辑: CLI/API first，GUI 操作必须可记录/可回放。
- 验证: `pnpm build`；必要时 `build/Release/gs_tests.exe`；浏览器实测若工具可用。
- 安全: 不回滚用户未提交改动；不执行破坏性 git 操作。

## 计划

- [x] 建立 UE 蓝图渲染/操作契约清单。
- [x] 记录当前 GraphScript 与蓝图操作差距。
- [x] 修复右键拖动画布误弹右键菜单。
- [x] 确认多选拖动释放后是否批量持久化；缺失则实现。
- [x] 构建和必要测试。
- [x] 更新 references/goal 状态。

## 参考审查

- React Flow 原生支持 `selectionOnDrag`、`selectionMode`、`panOnDrag`、`multiSelectionKeyCode` 和 selection change；优先使用这些原生能力。
- Epic 官方 Blueprint 节点文档用于建立行为清单，不把 UE 名称写入 core。

## 执行记录

- 已创建本 Auto Goal 文件组。
- `references.md` 已列出 UE 蓝图节点渲染设计和操作设计清单，并记录当前 GraphScript 差距。
- `webapp/src/canvas/FlowCanvas.tsx`: 为画布增加 pointer drag 状态，右键拖动超过阈值后阻止 `contextmenu` 打开，保留右键单击空白打开节点菜单。
- `webapp/src/canvas/FlowCanvas.tsx`: `onNodeDragStop` 改为检测当前选中节点集合；当拖动节点属于多选集合时，释放后逐个调用 `onNodeMove` 持久化位置，避免只保存单个节点。
- 已保留 React Flow 原生 `selectionOnDrag`、`SelectionMode.Partial`、中键/右键/Space 平移、多选键配置。

## 验证

- `pnpm build` 于 `webapp/` 通过，只有 Vite chunk size warning。
- `build/Release/gs_tests.exe` 通过：308 tests from 23 test suites, 308 passed。
- 浏览器自动化未在本轮执行；当前环境先前 Playwright MCP 报本机 Chrome 分发缺失，后续如要视觉验收需使用可用浏览器入口或人工试用。

## 结果

result: complete

## 更新

- goal.md: 下一子目标应转向 pin 拖出空白处打开上下文搜索，并补连接候选高亮。
- references.md: 第一轮已完成项保持在清单中，未完成项继续留作后续循环。
