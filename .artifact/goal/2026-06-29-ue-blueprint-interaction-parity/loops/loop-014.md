# Loop 014 - 注释框基础操作

## 子目标

- 实现 UE 蓝图式注释框的基础创建、拖动持久化和删除。
- 操作必须通过现有 CLI/API graph annotation 命令回放，并同步 Current Graph 文本。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮改动集中在 Web GUI：
  - `webapp/src/canvas/FlowCanvas.tsx`
  - `webapp/src/App.tsx`
  - `webapp/test_visual_comment_box_replay.py`
  - `webapp/test_visual_edge_replay.py`
  - `.artifact/goal/2026-06-29-ue-blueprint-interaction-parity/references.md`
- 注释框不新增 core C++ 类型；用 graph annotation 表示：
  - 创建/移动：`annotate graph CommentBox_<id> Text=<text> X=<x> Y=<y> W=<w> H=<h>`
  - 删除：`unannotate graph CommentBox_<id>`
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 外部参考

- Epic 官方文档和 UE 图编辑器惯例均支持图上的 comment box；本轮实现基础等价能力：选中节点后按 `C` 创建围住选区的注释框，注释框可拖动、可删除。

## 执行记录

- `FlowCanvas` 新增 `commentBox` React Flow node type：
  - 从 `graph.annotations` 中读取 `CommentBox_<id>` annotation。
  - `Text`、`X`、`Y`、`W`、`H` 参数映射为注释框标题、位置和尺寸。
  - 作为独立 React Flow node 渲染在节点背后。
- `FlowCanvas` 新增注释框操作：
  - 选中一个或多个普通节点后按 `C`，按选中节点外接框创建注释框。
  - 拖动注释框后调用 `onCommentBoxMove`。
  - 删除选中注释框后调用 `onCommentBoxDelete`。
  - 普通节点复制、对齐、分布只处理 `blueprint` 节点，不把注释框误当作 graph node。
- `App` 新增注释框命令回放：
  - 自动分配 `CommentBox_1`、`CommentBox_2` 等 annotation 名。
  - 创建/移动均走 `annotate graph ...`。
  - 删除走 `unannotate graph ...`。
- `test_visual_edge_replay.py` mock 状态更新补充：
  - 支持 `annotate graph`。
  - 支持 `unannotate graph`。
- 新增 `test_visual_comment_box_replay.py`：
  - 多选 `branch` / `printer` 后按 `C`。
  - 验证 `CommentBox_1` annotation 创建、画布渲染和 Current Graph 文本。
  - 拖动注释框，验证 annotation X/Y 写回。
  - Delete 删除注释框，验证 annotation 和 Current Graph 文本移除。

## 验证

- `python webapp/test_visual_comment_box_replay.py`：通过。
  - `PASS comment create command replayed`
  - `PASS comment annotation created`
  - `PASS comment box rendered after create`
  - `PASS current graph text had comment annotation`
  - `PASS comment move command replayed`
  - `PASS comment x moved`
  - `PASS comment y moved`
  - `PASS comment delete command replayed`
  - `PASS comment box removed from canvas`
  - `PASS comment annotation deleted`
  - `PASS current graph text removed comment annotation`
  - `PASS comment box did not mutate edges`
- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- 回归验证：
  - `python webapp/test_visual_node_duplicate_replay.py`：通过。
  - `python webapp/test_visual_node_align_replay.py`：通过。
  - `python webapp/test_visual_edge_selection_focus.py`：通过。

## 结果

- 注释框基础操作已具备创建、拖动、删除、CLI/API 回放和 Current Graph 文本同步证据。
- 注释框当前用 graph annotation 表示，保持 core 领域无关。

## 延续项

- 注释框 resize、文本编辑、包裹节点拖动联动尚未实现。
- 多选集合外框、节点标题/category 色、fields declaration source/default 显示仍需继续校准。
