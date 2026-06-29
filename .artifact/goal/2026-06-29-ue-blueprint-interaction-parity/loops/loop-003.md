# loop-003

## 子目标

收敛节点/连线删除、断线和重连体验，并继续浏览器验证多选拖动与右键拖动画布。

## 本轮仓库基准检查

- 继续使用 React Flow 原生事件，不新增前端依赖。
- 图操作继续通过现有 CLI/API 命令落库。
- Core C++ 不引入 UE/Blueprint 语义；本轮只修改 Web GUI。

## UE 蓝图行为拆解

- Delete/Backspace 删除当前选中节点或连线。
- 删除节点时，与节点相连的 flow/link 由后端图编辑语义清理。
- 选中连线可删除，等价于断开 pin。
- 连线端点应可重连到新的兼容 pin；蓝图里 source/target 两端都可操作。
- 多选节点拖动后，所有选中节点位置都要持久化。
- 右键拖动画布不应误弹出 action menu，普通右键仍应打开菜单。

## 实现记录

- `webapp/src/canvas/FlowCanvas.tsx`
  - 增加 `onNodeDelete` prop。
  - 接入 React Flow `onNodesDelete`，Delete/Backspace 删除节点时调用后端删除路径，并清空选中态。
  - 将生成的 flow/link edge 和 `defaultEdgeOptions` 的 `reconnectable` 从 `target` 改为 `true`，使 React Flow 同时显示 source/target updater。
- `webapp/src/App.tsx`
  - 增加 `handleNodeDelete`，执行 `remove_node <instance>` 并清空选中 edge/node。

## 浏览器验证

- 使用 in-app browser 刷新 `http://127.0.0.1:8080/`。
- 准备临时节点:
  - `qa_delete_node`
  - `qa_reconnect_a`
  - `qa_reconnect_b`
  - `qa_reconnect_t`
  - 临时 flow: `qa_reconnect_a.completed -> qa_reconnect_t.enter`
- 节点删除:
  - 点击 `qa_delete_node` 后按 Delete。
  - DOM 中节点消失。
  - `/api/state` 确认 `qa_delete_node` 不存在，`command_log` 有 `remove_node qa_delete_node`。
- edge 删除/断线:
  - 点击临时 flow 后按 Delete。
  - DOM 中对应 edge 消失。
  - `/api/state` 确认无 `qa_reconnect_*` flow，`command_log` 有 `unflow qa_reconnect_a.completed qa_reconnect_t.enter`。
- 普通右键菜单:
  - 在画布空白处右键。
  - DOM 出现 `[data-canvas-context-menu]`，显示 `Add node at ...`，候选按钮数量 50。
- source endpoint 重连:
  - React Flow DOM 已显示 `.react-flow__edgeupdater-source`，实现路径存在。
  - 当前浏览器控制通道的拖拽未触发 source updater 的 reconnect 回调，未作为通过证据。
- 多选拖动:
  - CUA 的 Shift/Ctrl click 未触发 React Flow 多选；框选能选中节点，但本轮未得到两个节点同时选中的强证据。
  - 保留为下一轮需要用更合适的浏览器输入方式或补 UI 内部测试验证。
- 清理:
  - 临时 `qa_reconnect_*` 节点已通过 `remove_node` 清理。

## 命令验证

- `pnpm build` 于 `webapp/` 通过。
- `build/Release/gs_tests.exe` 通过，308/308。

## 结果

- 节点 Delete/Backspace 持久化删除完成并浏览器验证。
- edge Delete/Backspace 持久化断线完成并浏览器验证。
- source/target 双端重连已按 React Flow 能力打开，但 source 端本轮自动化未能实证触发。
- 多选拖动和右键拖动画布保护仍需更强浏览器验证，不伪装完成。

## 下轮建议

- 增加画布级快捷键/按钮辅助验证：例如临时 debug/test-only 不可取，应优先找 React Flow store 或更可靠的 Playwright/CUA 输入方式。
- 收敛焦点快捷键：F 聚焦选中/全图。
- 补更蓝图式的多选集合外框、edge hover/selected 视觉状态。
