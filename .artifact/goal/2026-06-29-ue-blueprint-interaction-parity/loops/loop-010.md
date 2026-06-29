# Loop 010 - 蓝图式节点复制

## 子目标

- 在 React Flow 画布上实现蓝图式选中节点复制。
- 复制操作必须通过 CLI/API 命令回放，并同步 Current Graph 文本数据。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮改动集中在 Web GUI：
  - `webapp/src/canvas/FlowCanvas.tsx`
  - `webapp/src/App.tsx`
  - `webapp/src/panels/PropertiesPanel.tsx`
  - `webapp/test_visual_node_duplicate_replay.py`
  - `webapp/test_visual_edge_replay.py`
- 复制通过现有 CLI 命令组合实现，不新增 core C++ 命令：
  - `add_node <Type> <newName> [initializer]`
  - `annotate node <newName> Position X=<x> Y=<y>`
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 执行记录

- `FlowCanvas` 新增 `onNodesDuplicate` 回调，并在非输入控件焦点下响应：
  - `Ctrl+D` / `Cmd+D`
  - `Ctrl+W` / `Cmd+W`
- `App` 新增复制实现：
  - 按当前图中节点顺序复制选中节点。
  - 副本命名为 `原名_copy`，冲突时递增为 `原名_copy2` 等。
  - 保留节点 initializer 表达式。
  - 副本位置相对原节点偏移 `(48, 48)`。
  - 每个副本依次执行 `add_node` 和 `annotate node Position`，失败时停止后续复制。
- 修复 `PropertiesPanel` / `NodeInfo` 对旧 mock 或旧后端 JSON 的容错：
  - `NodeTypeDef.fields`、`NodeTypeDef.pins`、`NodeTypeDef.annotations` 和 pin `annotations` 均允许缺省。
  - 该修复来自复制测试时发现的真实崩溃路径：选中节点后 `NodeInfo` 读取缺省数组导致 UI 被清空。
- 扩展 `test_visual_edge_replay.py` mock 状态更新：
  - 支持 `add_node`。
  - 支持 `annotate node ... Position`。
- 新增 `test_visual_node_duplicate_replay.py`：
  - 选中 `branch` 和 Shift 选中 `printer`。
  - 按 `Ctrl+D`。
  - 验证命令顺序、mock state、画布节点和 Current Graph 文本。
  - 保存截图：
    - `webapp/test_screenshots/visual_node_duplicate_replay/01_before_duplicate.png`
    - `webapp/test_screenshots/visual_node_duplicate_replay/02_after_duplicate.png`

## 验证

- `python webapp/test_visual_node_duplicate_replay.py`：通过。
  - `PASS duplicate commands replayed`
  - `PASS mock state has branch copy`
  - `PASS mock state has printer copy`
  - `PASS canvas shows branch copy`
  - `PASS canvas shows printer copy`
  - `PASS current graph text has branch copy`
  - `PASS current graph text has printer copy`
- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- 回归验证：
  - `python webapp/test_visual_pin_compatibility_preview.py`：通过。
  - `python webapp/test_visual_connection_feedback.py`：通过。

## 结果

- `节点复制` 已具备蓝图式快捷键入口、CLI/API 回放、位置持久化和 Current Graph 文本同步证据。
- 选中节点详情面板对旧 JSON 的缺省数组容错增强，避免选中节点时整页崩溃。

## 延续项

- 节点对齐仍未实现。
- 注释框仍未实现；需决定是复用 generate comment 元数据，还是新增图上 comment box 表示。
- 多选集合外框、节点标题/category 色、fields declaration source/default 显示仍需继续校准。
