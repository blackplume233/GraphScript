# Loop 011 - 蓝图式节点对齐

## 子目标

- 实现 UE 图编辑器风格的多选节点对齐快捷键。
- 对齐操作必须通过现有 CLI/API 位置注解命令回放，并同步 Current Graph 文本。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮改动集中在 Web GUI：
  - `webapp/src/canvas/FlowCanvas.tsx`
  - `webapp/test_visual_node_align_replay.py`
  - `.artifact/goal/2026-06-29-ue-blueprint-interaction-parity/references.md`
- 对齐通过现有 CLI 命令实现：`annotate node <name> Position X=<x> Y=<y>`。
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 外部参考

- Epic 官方文档 [`Organizing a Material Graph in Unreal Engine`](https://dev.epicgames.com/documentation/unreal-engine/organizing-a-material-graph-in-unreal-engine?lang=en-US) 的 Align 菜单列出六个轴：
  - Align Top: `Shift+W`
  - Align Middle: `Alt+Shift+W`
  - Align Bottom: `Shift+S`
  - Align Left: `Shift+A`
  - Align Center: `Alt+Shift+S`
  - Align Right: `Shift+D`
  - Straighten Connection: `Q`
  - Distribute Horizontally / Vertically: 对选中节点创建等距间隔。

## 执行记录

- `FlowCanvas` 新增多选节点对齐：
  - `Shift+A`: 左对齐。
  - `Shift+D`: 右对齐。
  - `Shift+W`: 顶对齐。
  - `Shift+S`: 底对齐。
  - `Alt+Shift+W`: 垂直中线对齐。
  - `Alt+Shift+S`: 水平中心线对齐。
- 对齐计算使用选中节点的 React Flow measured width/height；缺省时使用稳定 fallback 尺寸。
- 只持久化位置实际变化的节点，避免无意义命令噪声。
- 每个变化节点调用 `onNodeMove`，最终走 `annotate node ... Position`。
- 新增 `test_visual_node_align_replay.py`：
  - 选中 `branch` 和 Shift 选中 `printer`。
  - 按 `Shift+A` 左对齐。
  - 验证命令、mock state、画布 X 坐标和 Current Graph 文本。

## 验证

- `python webapp/test_visual_node_align_replay.py`：通过。
  - `PASS left align command replayed`
  - `PASS state printer x aligned`
  - `PASS state printer y preserved`
  - `PASS canvas x aligned`
  - `PASS current graph text updated`
- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- 回归验证：
  - `python webapp/test_visual_node_duplicate_replay.py`：通过。
  - `python webapp/test_visual_pin_compatibility_preview.py`：通过。

## 结果

- `节点对齐` 已具备 UE 图编辑器同款六轴快捷键。
- 至少左对齐路径已有浏览器脚本证据，证明对齐会回放 CLI 命令、更新状态、移动画布节点并同步 Current Graph 文本。

## 延续项

- 注释框仍未实现；需决定是复用 generate comment 元数据，还是新增图上 comment box 表示。
- Straighten Connection (`Q`) 和 Distribute Horizontally/Vertically 尚未实现。
- 多选集合外框、节点标题/category 色、fields declaration source/default 显示仍需继续校准。
