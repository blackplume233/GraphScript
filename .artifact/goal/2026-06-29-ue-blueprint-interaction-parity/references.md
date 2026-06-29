# 参考记录

## UE 蓝图渲染设计清单

| 项 | UE 蓝图行为 | GraphScript 当前状态 | 实现/验证状态 |
|---|---|---|---|
| 节点标题栏 | 节点有清晰标题栏，颜色表达节点类别/执行语义，标题显示类型或实例语义 | 已有标题栏、exec/pure 配色、实例名和类型名 | 需继续校准类别色、紧凑度、图标和状态 |
| Pin 布局 | 输入 pin 在左，输出 pin 在右；exec pin 与 data pin 形状/颜色不同 | 已有左右列、exec 方形/data 圆形、类型颜色 | 已补连接拖拽期间 origin/compatible/disabled 视觉状态；需继续截图级校准 |
| 内禀 Fields | 节点可有不参与连线的自有字段，显示/编辑不同于 in/out pin | 已新增 node `field` 模型和 UI 展示；旧 data in 回退仍保留 | 需继续验证 declaration source 跳转和默认值显示 |
| 连接线 | exec/data 线风格不同，连线不遮挡节点，选中/hover 有反馈 | 已有 exec 实线、data 虚线、选中/诊断高亮 | 已补兼容目标高亮和 pin 拖出搜索；连线拖拽预览继续沿用 React Flow |
| 状态反馈 | 选中、hover、错误/警告有清晰视觉反馈 | 已有 selected/diagnostic 样式 | 需补多选集合外框和更蓝图式 selection 样式 |

## UE 蓝图操作设计清单

| 项 | UE 蓝图行为 | GraphScript 当前状态 | 实现/验证状态 |
|---|---|---|---|
| 空白右键菜单 | 在图空白处右键打开 action menu，可搜索节点 | 已有右键创建节点菜单 | 已验证：`test_visual_canvas_pan_context.py` 覆盖右键点击打开 add-node 菜单，右键拖动画布不会误弹菜单 |
| 从 pin 拖出菜单 | 从 pin 拖到空白处释放，打开上下文相关节点搜索 | 已实现 React Flow `onConnectStart/onConnectEnd` 版本 | 已浏览器验证：`print_text.exit` 拖空白弹出兼容菜单，选择 `Delay` 后自动创建并连到 `delay_61848.enter` |
| Pin 连接 | 从 pin 拖到兼容 pin 创建连接，不兼容目标不可用或有反馈 | 已有 React Flow 连接和校验 | 已补拖拽期间兼容 pin 高亮、不兼容 pin 弱化和无效连接反馈；`test_visual_connection_feedback.py` 验证 exec->data 无效拖拽会显示错误提示且不 replay edge 命令 |
| 框选 | 空白左键拖拽出现 selection box，可多选节点 | 已开启 `selectionOnDrag` 和 partial mode | 已浏览器验证：框选两个临时 Delay 节点后都进入 selected 状态 |
| 多选点击 | Shift/Ctrl 添加/切换选择 | 已补点击前选择快照，避免 React Flow 先改 selection 后把新节点误切掉 | 已浏览器验证：点选 `qa_multi_a` 后 Shift 点 `qa_multi_b` 两者均 selected；Ctrl 点 `qa_multi_a` 后只保留 `qa_multi_b` |
| 多选拖动 | 多个选中节点可一起拖动，释放后全部持久化位置 | React Flow 多选拖动已接入，释放后逐个持久化选中节点位置 | 已浏览器验证：双选 `qa_multi_a/qa_multi_b` 后拖动集合，两个节点 DOM 坐标同时移动，后端 `Position` 注解分别写回 |
| 平移画布 | 中键/右键拖拽或 Space 拖拽平移 | 已配置中键/右键/Space 平移，并加右键拖动菜单保护 | 已验证：`test_visual_canvas_pan_context.py` 覆盖右键拖动平移、中键拖动平移、Space+左键拖动平移、右键拖动不打开菜单 |
| 缩放/聚焦 | 滚轮缩放，快捷键聚焦选择或全图 | 基础 zoom 已有；已补 `F` 聚焦选中节点/选中边/全图 | 已浏览器验证：滚动偏移视口后按 `F`，viewport transform 从 `translate(294.61px, 9.42589px) scale(0.491557)` 变为 `translate(682.843px, 90.698px) scale(1.43177)` |
| 删除 | Delete/Backspace 删除选中节点/edge | 节点和 edge 删除均已接到后端命令 | 已浏览器验证：节点 Delete 触发 `remove_node`；edge Delete 触发 `unflow` |
| 重连/断线 | 选中线可重连，pin/edge 可断开 | edge 删除断线已验证；edge 改为自定义 source/target endpoint hit target，统一走 `onEdgeReconnect` 事务路径 | 已验证：`test_visual_edge_reconnect_replay.py` 覆盖 target endpoint 重连，`test_visual_edge_source_reconnect_replay.py` 覆盖 source endpoint 重连，`test_visual_edge_reset_guard.py` 覆盖无效重连不 replay |

## 外部参考

- Epic 官方文档 `Nodes in Unreal Engine`: 用于确认 Blueprint 节点、pin、连接、选择、移动、右键菜单和 pin 拖出创建节点等基本交互概念。

## 错误路径

- 只追求视觉像蓝图，但不修正选择、右键拖动、pin 拖出搜索等操作契约，会继续让编辑器不可用。
- 把 data input pin 当作内禀属性的最终方案是不正确的；field 必须独立于 in/out pin。

## 剩余差距

- 右键菜单和画布平移已补 `test_visual_canvas_pan_context.py`：右键点击打开 add-node 菜单，右键拖动、中键拖动、Space+左键拖动均平移视口，右键拖动不误弹菜单。
- 兼容 pin 拖动中高亮仍需要截图级验证；连接失败反馈已补 `data-connection-feedback` 并由 `test_visual_connection_feedback.py` 覆盖。
- source endpoint 重连已由 `test_visual_edge_source_reconnect_replay.py` 覆盖；继续可做截图级交互 polish。
- 节点复制、对齐、注释框等高级蓝图操作。
