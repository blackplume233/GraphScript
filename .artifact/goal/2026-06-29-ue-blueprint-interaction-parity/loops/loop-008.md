# Loop 008 - Space 拖拽平移验证

## 子目标

- 闭环 UE 蓝图式画布导航中的 Space+左键拖拽平移。
- 保持右键拖动、中键拖动和右键菜单分离的既有验证。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮只改 Web E2E：`webapp/test_visual_canvas_pan_context.py`。
- 产品代码无需新增实现；当前 React Flow `panActivationKeyCode="Space"` 已满足行为。
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 执行记录

- 扩展 `test_visual_canvas_pan_context.py`：
  - 在右键拖动、中键拖动验证之后，按下 `Space`。
  - 在空白 pane 上执行左键拖动。
  - 释放左键并松开 `Space` 后，断言 `.react-flow__viewport` transform 发生变化。
  - 保留普通右键点击打开 add-node 菜单的断言。

## 验证

- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- `python test_visual_canvas_pan_context.py`：通过。
  - `PASS right drag panned viewport`
  - `PASS right drag did not open context menu`
  - `PASS middle drag panned viewport`
  - `PASS space left-drag panned viewport`
  - `PASS right click opens add-node menu`
- 回归验证：
  - `python test_visual_edge_source_reconnect_replay.py`：通过。
  - `python test_visual_connection_feedback.py`：通过。

## 结果

- `平移画布` 项已覆盖右键拖动、中键拖动、Space+左键拖动三种蓝图式平移入口。
- 右键拖动和右键菜单分离仍通过。

## 延续项

- 兼容 pin 拖动中高亮仍需截图级验证。
- 节点复制、对齐、注释框等高级蓝图操作仍未覆盖。
