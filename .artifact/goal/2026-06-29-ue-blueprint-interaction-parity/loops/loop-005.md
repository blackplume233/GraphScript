# Loop 005 - 无效连接反馈收敛

## 子目标

- 对齐 UE 蓝图 pin 操作：无效连接不能静默失败，必须在画布上给出即时、局部、可理解的反馈。
- 保证无效连接不会产生 CLI replay 命令，不破坏文本/图协同。
- 补一个可重复的前端 E2E 回归验证。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮只改 Web GUI 与 Web E2E：`webapp/src/canvas/FlowCanvas.tsx`、`webapp/src/canvas/BlueprintNode.tsx`、`webapp/test_visual_connection_feedback.py`。
- 图编辑仍以 CLI/API 命令为源，测试断言无效连接不会 replay `flow/link/unflow/unlink`。
- Core C++ 未改动，不需要新增 C++ 单元测试。

## 执行记录

- 在 `FlowCanvas` 中新增 `ConnectionFeedback` 状态和 `data-connection-feedback` 画布提示。
- 新增 `connectionFailureMessage`，根据目标 handle 判断失败原因：
  - 同方向连接：提示从 output 拖到 input。
  - exec/data 混连：提示 exec 只连 exec、data 只连匹配 data。
  - data 类型不匹配：显示源/目标类型。
  - 目标 input 已有输入：提示目标 pin 已有 incoming connection。
  - reconnect payload 缺失或目标重复：给出 reconnect failed 提示并刷新。
- 在 `onConnectEnd` 的无效 handle 释放路径显示反馈；保留空白释放打开兼容节点搜索菜单。
- 在 `BlueprintNode` 的 pin handle 上补回 `data-testid="sdk.workflow.canvas.node.port"`，便于自动化定位 pin。
- 新增 `webapp/test_visual_connection_feedback.py`：
  - mock `/api/state` 使用 `Branch`、`PrintString`、`StringSource`。
  - 选择 `event:BeginPlay` 为 active block。
  - 将 `Branch.onTrue` exec 输出拖到 `PrintString.message` data 输入。
  - 断言出现错误反馈，且没有发送 edge replay 命令，graph 中 flows/links 仍为空。

## 验证

- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- `python webapp/test_visual_connection_feedback.py`：通过。
  - `PASS active block selected`
  - `PASS feedback shown`
  - `PASS no edge commands replayed`
  - `PASS no flow created`
  - `PASS no data link created`
  - 反馈文本：`Exec pins connect to exec pins; data pins connect to matching data pins`
- 本轮尝试使用 in-app browser 验证当前 127.0.0.1 页面，但浏览器控制连续超时并重置；改用仓库 Playwright E2E 作为可重复验证证据。

## 结果

- `Pin 连接` 从“错误 toast/失败文案待细化”推进为“无效连接反馈已实现并有 E2E 覆盖”。
- 无效连接反馈不改变后端图，不产生 replay 命令，符合文本/图协同约束。

## 延续项

- 兼容 pin 拖动中高亮仍需截图级验证。
- source endpoint 重连仍需人工或更强自动化验证。
- 右键拖动画布与菜单冲突保护仍需可模拟右键按住拖动的验证。
- 节点复制、对齐、注释框等高级蓝图操作仍未覆盖。
