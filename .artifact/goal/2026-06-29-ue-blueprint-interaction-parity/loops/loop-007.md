# Loop 007 - 右键拖动画布与右键菜单分离验证

## 子目标

- 闭环 UE 蓝图高频画布操作：右键拖动用于平移，右键点击用于打开节点搜索菜单，两者不能互相误触。
- 同时验证中键拖动画布平移，避免 pan 配置回退。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮新增 Web E2E：`webapp/test_visual_canvas_pan_context.py`。
- 产品代码没有新增实现；使用上一轮已经存在的 `panOnDrag={[1, 2]}` 与右键拖动阈值保护。
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 执行记录

- 新增 `test_visual_canvas_pan_context.py`：
  - mock `/api/state`，复用 `test_visual_edge_replay.initial_state()`。
  - 自动查找画布空白 pane 坐标，避免点到节点、minimap 或侧栏。
  - 记录 `.react-flow__viewport` transform。
  - 右键按下拖动后释放，断言 viewport transform 改变，且没有出现 `data-canvas-context-menu`。
  - 中键按下拖动后释放，断言 viewport transform 再次改变。
  - 普通右键点击空白画布，断言 add-node 菜单出现，且 input placeholder 是 `Search node type, pin, field, or tag`。

## 验证

- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- `python test_visual_canvas_pan_context.py`：通过。
  - `PASS right drag panned viewport`
  - `PASS right drag did not open context menu`
  - `PASS middle drag panned viewport`
  - `PASS right click opens add-node menu`
- 回归验证：
  - `python test_visual_edge_source_reconnect_replay.py`：通过。
  - `python test_visual_connection_feedback.py`：通过。

## 结果

- `空白右键菜单` 从“待浏览器验证”推进为已 E2E 验证。
- `平移画布` 的右键拖动和中键拖动已 E2E 验证；右键拖动和右键菜单冲突保护已闭环。

## 延续项

- Space 拖拽平移仍可补专门验证。
- 兼容 pin 拖动中高亮仍需截图级验证。
- 节点复制、对齐、注释框等高级蓝图操作仍未覆盖。
