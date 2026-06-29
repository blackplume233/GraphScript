# Loop 013 - 节点分布

## 子目标

- 实现 UE 图编辑器 Align 菜单里的 Distribute Horizontally / Vertically。
- 操作必须通过现有 CLI/API 位置注解命令回放，并同步 Current Graph 文本。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮改动集中在 Web GUI：
  - `webapp/src/canvas/FlowCanvas.tsx`
  - `webapp/test_visual_node_distribute_replay.py`
  - `.artifact/goal/2026-06-29-ue-blueprint-interaction-parity/references.md`
- 分布通过现有 CLI 命令实现：`annotate node <name> Position X=<x> Y=<y>`。
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 外部参考

- Epic 官方文档 [`Organizing a Material Graph in Unreal Engine`](https://dev.epicgames.com/documentation/unreal-engine/organizing-a-material-graph-in-unreal-engine?lang=en-US) 的 Align 菜单包含 `Distribute Horizontally` 和 `Distribute Vertically`。
- 该文档未给出默认快捷键；GraphScript 本轮采用本项目映射：
  - `Alt+Shift+H`: 水平分布。
  - `Alt+Shift+V`: 垂直分布。

## 执行记录

- `FlowCanvas` 新增 `distributeSelectedNodes`：
  - 需要至少 3 个选中节点。
  - 按 X 或 Y 排序。
  - 固定首尾节点的外边界。
  - 按所有选中节点的 measured width/height 计算等间距 gap。
  - 只持久化位置实际变化的节点。
  - 对每个变化节点调用 `onNodeMove`，最终走 `annotate node ... Position`。
- 新增 `test_visual_node_distribute_replay.py`：
  - 构造 3 个 `PrintString` 节点。
  - 多选后按 `Alt+Shift+H`。
  - 验证中间节点命令回放、mock state、画布左右 gap 相等和 Current Graph 文本。

## 验证

- `python webapp/test_visual_node_distribute_replay.py`：通过。
  - `PASS horizontal distribute command replayed`
  - `PASS state middle x distributed`
  - `PASS state middle y preserved`
  - `PASS canvas gaps match`
  - `PASS current graph text updated`
- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- 回归验证：
  - `python webapp/test_visual_node_align_replay.py`：通过。
  - `python webapp/test_visual_node_duplicate_replay.py`：通过。
  - `python webapp/test_visual_straighten_connection_replay.py`：通过。

## 结果

- Distribute Horizontally / Vertically 已实现。
- 水平分布已有浏览器脚本证据，证明分布会回放 CLI 命令、更新状态、移动画布节点并同步 Current Graph 文本。
- 垂直分布复用同一计算与持久化路径，已由 TypeScript 构建覆盖类型正确性。

## 延续项

- 注释框仍未实现；需决定是复用 generate comment 元数据，还是新增图上 comment box 表示。
- 多选集合外框、节点标题/category 色、fields declaration source/default 显示仍需继续校准。
