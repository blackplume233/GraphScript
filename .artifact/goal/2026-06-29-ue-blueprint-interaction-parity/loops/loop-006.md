# Loop 006 - Source Endpoint 重连闭环

## 子目标

- 修复并验证 source endpoint 重连，覆盖 exec flow 和 data link 两类边。
- 保持 target endpoint 重连、无效重连保护和连接失败反馈不回退。
- 恢复 visual edge DOM 测试契约，保证 E2E 能稳定定位连线和端点。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮只改 Web GUI 与 Web E2E：`webapp/src/canvas/FlowCanvas.tsx`、`webapp/test_visual_edge_replay.py`、`webapp/test_visual_edge_reset_guard.py`、`webapp/test_visual_edge_reconnect_replay.py`、`webapp/test_visual_edge_source_reconnect_replay.py`。
- 图编辑继续通过既有 CLI/API 命令 replay；source/target 重连测试均断言命令顺序。
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 执行记录

- 将 React Flow edge 改为自定义 `BlueprintEdge`：
  - 继续用 `BaseEdge` 与 `getSmoothStepPath` 渲染 smoothstep 曲线。
  - 输出稳定 `data-testid="sdk.workflow.canvas.line"` 与语义 `data-line-id`，例如 `branch_exec-out-onTrue-printer_exec-in-enter`。
  - 保留内部 `data-edge-id` 以对应包含 block/persistent id 的 React Flow edge id。
- 关闭 React Flow native edge updater，避免其 source updater 覆盖自定义 hit target 但不触发 source 重连。
- 在自定义 edge 上添加 source/target endpoint hit target，并沿连线方向偏移，避免和节点 pin handle 重叠。
- 新增手动重连状态：
  - endpoint pointer down 记录边 payload 与 source/target 端。
  - window pointer up 落到 pin handle 时，按 source 或 target 端组装新的 `Connection`。
  - 复用同一个 `applyReconnect` 校验和 `onEdgeReconnect` 事务路径。
  - 非同 kind、source 落到 input、target 落到 output、空白释放等路径给出连接反馈。
- 更新既有 E2E helper：
  - pin 定位改为当前 handle 自身的 `[data-port-id][data-testid]`。
  - target 重连 helper 改用自定义 `[data-edge-reconnect-handle="target"]`。
  - reconnect 测试先选择 active block 再等待 edge，因为当前画布只在 active event/function 下显示连接。
- 新增 `webapp/test_visual_edge_source_reconnect_replay.py`：
  - exec source：`branch.onTrue -> printer.enter` 重连为 `printer2.exit -> printer.enter`。
  - data source：`text.value -> printer.message` 重连为 `text2.value -> printer.message`。
  - 断言命令顺序、状态、annotation 迁移、线可见、旧线消失。

## 验证

- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- `python test_visual_edge_source_reconnect_replay.py`：通过。
  - source flow/link 重连命令 replay 通过。
  - command order preserved 通过。
  - state reconnected flow/link source 通过。
  - annotation migrated 通过。
  - flow/link line visible 通过。
- `python test_visual_edge_reconnect_replay.py`：通过，target endpoint 重连、persistent id 选择迁移、source range 刷新、command log 可见均通过。
- `python test_visual_edge_reset_guard.py`：通过，无效重连不 replay，原 flow/link 保持。
- `python test_visual_connection_feedback.py`：通过，无效 exec->data 拖拽显示反馈且不 replay edge 命令。

## 结果

- `重连/断线` 从“source endpoint 需要人工或更强自动化验证”推进为 source/target endpoint 均有可重复 E2E 覆盖。
- 自定义 edge 恢复了稳定 DOM 契约，后续截图级验证和精细操作测试可以复用 `data-line-id`、`data-edge-reconnect-handle`。

## 延续项

- 右键拖动画布与菜单冲突保护仍需可模拟右键按住拖动的验证。
- 兼容 pin 拖动中高亮仍需截图级验证。
- 节点复制、对齐、注释框等高级蓝图操作仍未覆盖。
