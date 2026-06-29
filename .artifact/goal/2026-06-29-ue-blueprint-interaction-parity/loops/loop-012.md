# Loop 012 - Straighten Connection

## 子目标

- 实现 UE 图编辑器 Align 菜单里的 `Straighten Connection (Q)`。
- 操作必须通过现有 CLI/API 位置注解命令回放，并同步 Current Graph 文本。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮改动集中在 Web GUI：
  - `webapp/src/canvas/FlowCanvas.tsx`
  - `webapp/test_visual_straighten_connection_replay.py`
  - `.artifact/goal/2026-06-29-ue-blueprint-interaction-parity/references.md`
- Straighten 通过现有 CLI 命令实现：`annotate node <name> Position X=<x> Y=<y>`。
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 外部参考

- Epic 官方文档 [`Organizing a Material Graph in Unreal Engine`](https://dev.epicgames.com/documentation/unreal-engine/organizing-a-material-graph-in-unreal-engine?lang=en-US) 的 Align 菜单包含 `Straighten Connection (Q)`。
- GraphScript 当前 React Flow 节点 pin 为左右布局，所以本轮把 Straighten 定义为：对选中 edge 的 target 节点进行垂直中心对齐，使 source 和 target 节点中心线一致，从而把左右 pin 连接拉直为水平线。

## 执行记录

- `FlowCanvas` 新增 `straightenSelectedEdges`：
  - 只在当前有选中 edge 时响应 `Q`。
  - 对每条选中 edge 读取 source/target 节点。
  - 保留 target 节点 X 坐标。
  - 将 target 节点 Y 坐标设置为 `sourceCenterY - targetHeight / 2`。
  - 只持久化位置实际变化的 target 节点。
  - 对每个变化节点调用 `onNodeMove`，最终走 `annotate node ... Position`。
- 新增 `test_visual_straighten_connection_replay.py`：
  - 构造一条弯曲的 `branch.onTrue -> printer.enter` exec 线。
  - 点击选中该线。
  - 按 `Q`。
  - 验证命令、mock state、画布中心线对齐和 Current Graph 文本。

## 验证

- `python webapp/test_visual_straighten_connection_replay.py`：通过。
  - `PASS edge selection did not mutate graph`
  - `PASS straighten command replayed`
  - `PASS state target y aligned`
  - `PASS canvas centers aligned`
  - `PASS current graph text updated`
- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- 回归验证：
  - `python webapp/test_visual_node_align_replay.py`：通过。
  - `python webapp/test_visual_edge_selection_focus.py`：通过。

## 结果

- `Straighten Connection (Q)` 已实现并具备截图和脚本验证。
- 选中 edge 本身不 replay 图结构命令；只有 `Q` 才持久化 target 节点位置。

## 延续项

- Distribute Horizontally / Vertically 尚未实现。
- 注释框仍未实现；需决定是复用 generate comment 元数据，还是新增图上 comment box 表示。
- 多选集合外框、节点标题/category 色、fields declaration source/default 显示仍需继续校准。
