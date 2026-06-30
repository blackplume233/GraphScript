# Loop 015 - 注释框编辑、缩放和包裹节点联动

## 子目标

- 补齐 UE 蓝图式注释框的核心编辑能力：标题文本编辑、右下角 resize、拖动注释框时带动框内节点。
- 所有图操作继续通过现有 CLI/API 命令持久化，并同步 Current Graph 文本。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮改动集中在 Web GUI：
  - `webapp/src/canvas/FlowCanvas.tsx`
  - `webapp/test_visual_comment_box_replay.py`
  - `.artifact/goal/2026-06-29-ue-blueprint-interaction-parity/references.md`
- 注释框继续用 graph annotation 表示：
  - `annotate graph CommentBox_<id> Text=<text> X=<x> Y=<y> W=<w> H=<h>`
  - `unannotate graph CommentBox_<id>`
- 框内普通节点跟随移动时继续走已有节点位置命令：
  - `annotate node <instance> Position X=<x> Y=<y>`
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 执行记录

- `FlowCanvas` 的 `CommentBoxNode` 新增：
  - 标题双击后显示内联 input。
  - Enter 或 blur 提交标题；Escape 取消。
  - 右下角 resize handle。
- `FlowCanvas` 新增注释框事件处理：
  - `graphscript:comment-box-text-commit` 将标题改动写入 `onCommentBoxMove`。
  - `graphscript:comment-box-resize-start` 在 pointermove 中更新本地尺寸，在 pointerup 后通过 `onCommentBoxMove` 写回。
- `FlowCanvas` 新增注释框拖动联动：
  - drag start 时记录完全落在注释框内的 `blueprint` 节点。
  - drag 中同步移动这些节点的本地位置，保证交互反馈跟手。
  - drag stop 时写回注释框 `X/Y/W/H/Text`，并逐个调用 `onNodeMove` 写回框内节点 Position。
- 修正一次失败路径：
  - 初版把标题区域标成 `nodrag`，导致从标题区域拖动注释框时 React Flow 不启动 node drag。
  - 已移除标题普通显示态的 `nodrag`；只有 input 和 resize handle 保留 `nodrag/nopan`。
- 扩展 `test_visual_comment_box_replay.py`：
  - 创建注释框后双击标题改为 `Edited`。
  - 拖动右下角 resize handle，验证 `W/H` 增大。
  - 拖动注释框，验证 `X/Y` 增大。
  - 验证框内 `branch` / `printer` 节点跟随移动，并有 `annotate node ... Position` 命令。
  - 继续验证删除注释框不会修改 edge。

## 验证

- `pnpm build` 于 `webapp/`：通过，仅保留既有 Vite chunk-size warning。
- `python webapp/test_visual_comment_box_replay.py`：通过。
  - `PASS comment title edit command replayed`
  - `PASS comment title annotation updated`
  - `PASS comment resize command replayed`
  - `PASS comment move command replayed`
  - `PASS wrapped branch moved with comment`
  - `PASS wrapped printer moved with comment`
  - `PASS wrapped node position commands replayed`
  - `PASS comment delete command replayed`
- 失败修正后复跑 `python webapp/test_visual_comment_box_replay.py`：通过。

## 结果

- 注释框现在具备创建、标题编辑、resize、拖动、删除、包裹节点联动、命令回放和 Current Graph 文本同步证据。
- 注释框仍保持在 Web GUI 层用 graph annotation 表示，没有污染 core C++ 的领域无关模型。

## 延续项

- 继续校准多选集合外框、节点标题/category 色、fields declaration source/default 显示。
- 可继续增加更多轴向/组合操作截图级验证，但当前注释框核心操作已闭环。
