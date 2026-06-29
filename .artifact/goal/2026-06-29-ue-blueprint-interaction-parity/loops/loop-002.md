# loop-002

## 子目标

实现 pin 拖出空白处打开上下文搜索，并补连接候选反馈。

## 本轮仓库基准检查

- 继续使用 React 19 + TypeScript + Vite + `@xyflow/react`。
- 图操作仍通过现有 CLI/API 命令落库：创建节点走 `add_node` + `annotate node Position`，自动连线走既有 `flow`/`link` 命令。
- Core C++ 不引入 UE/Blueprint 语义；本轮只修改 Web GUI。

## UE 蓝图行为拆解

- 从 pin 拖到空白处释放时，Blueprint 打开上下文相关 action menu。
- 菜单候选与拖出的 pin 类型和方向相关，只显示能自然接上的节点。
- 选择候选节点后，新节点放在释放位置，并自动接上原始 pin。
- 拖线过程中，兼容/不兼容 pin 应有直接视觉反馈。

## 实现记录

- `webapp/src/canvas/FlowCanvas.tsx`
  - 接入 `onConnectStart` / `onConnectEnd`。
  - 记录拖出的 `PendingConnection`，释放到非 handle 的画布空白处时打开上下文菜单。
  - 上下文菜单在 pending connection 模式下只展示兼容节点类型，并在条目副标题标出会连接到哪个 pin。
  - `onNodeCreate` 返回新实例名后，自动生成 `EdgeEditPayload` 并调用既有 `onEdgeCreate`。
  - 连接前复用 `hasIncomingConnection`，避免静默覆盖已有输入 pin。
- `webapp/src/canvas/BlueprintNode.tsx`
  - 增加 `connectionPreview` 数据。
  - 拖线期间源 pin、兼容 pin、不兼容 pin 分别呈现 origin/compatible/disabled 状态。
- `webapp/src/App.tsx`
  - `handleAddNodeAt` 返回创建出的实例名，供画布自动连线。

## 验证

- `pnpm build` 于 `webapp/` 通过。
- in-app browser 实测:
  - 页面加载到 `http://127.0.0.1:8080/`。
  - React Flow 画布存在，验证时有 5 个节点、18 个 handle。
  - 从 `print_text.exec-out-exit` 拖到空白处，菜单显示 `Add and connect from print_text.exit`。
  - 菜单候选被过滤为兼容 exec 输入节点，条目显示 `to in enter` 等目标 pin。
  - 点击 `Delay` 后创建 `delay_61848`，后端 `/api/state` 出现 `event OnStart: print_text.exit -> delay_61848.enter`。
- `build/Release/gs_tests.exe` 通过，308/308。

## 结果

- 本轮子目标完成。
- 当前 session 为验证自动连线新增了 `delay_61848` 节点和 `print_text.exit -> delay_61848.enter` flow；这是实测证据，不是源码 fixture。

## 下轮建议

- 收敛删除/断线/重连体验：节点 Delete/Backspace、edge 删除、source endpoint 重连、pin 断开。
- 继续补浏览器实测：多选拖动、右键拖动画布、兼容 pin 拖动中的高亮截图或 DOM 证据。
