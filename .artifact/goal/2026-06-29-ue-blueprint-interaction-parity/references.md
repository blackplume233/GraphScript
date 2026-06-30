# 参考记录

## UE 蓝图渲染设计清单

| 项 | UE 蓝图行为 | GraphScript 当前状态 | 实现/验证状态 |
|---|---|---|---|
| 节点标题栏 | 节点有清晰标题栏，颜色表达节点类别/执行语义，标题显示类型或实例语义 | 已有标题栏、实例名、类型名、category/tag 徽标；header 颜色按 Logic/IO/Flow/Graph/Native 等类别区分 | 已验证：`test_visual_node_rendering_metadata.py` 覆盖 `Logic` / `IO` category 渲染 |
| Pin 布局 | 输入 pin 在左，输出 pin 在右；exec pin 与 data pin 形状/颜色不同 | 已有左右列、exec 方形/data 圆形、类型颜色 | 已补连接拖拽期间 origin/compatible/disabled 视觉状态；已由 `test_visual_pin_compatibility_preview.py` 做截图级验证 |
| 内禀 Fields | 节点可有不参与连线的自有字段，显示/编辑不同于 in/out pin | 已新增 node `field` 模型和 UI 展示；节点卡显示 field/pin 来源、默认值、override 状态；属性面板显示 declaration source 和 default | 已验证：`test_visual_node_rendering_metadata.py` 覆盖 canvas field source/default 标记、PropertiesPanel source/default 文本 |
| 连接线 | exec/data 线风格不同，连线不遮挡节点，选中/hover 有反馈 | 已有 exec 实线、data 虚线、选中/诊断高亮 | 已补兼容目标高亮和 pin 拖出搜索；连线拖拽预览继续沿用 React Flow |
| 状态反馈 | 选中、hover、错误/警告有清晰视觉反馈 | 已有 selected/diagnostic 样式；多选两个或更多普通节点时显示集合外框和计数 | 已验证：`test_visual_node_rendering_metadata.py` 覆盖双选后 `data-multi-select-outline` / `data-multi-select-count=2` |

## UE 蓝图操作设计清单

| 项 | UE 蓝图行为 | GraphScript 当前状态 | 实现/验证状态 |
|---|---|---|---|
| 空白右键菜单 | 在图空白处右键打开 action menu，可搜索节点 | 已有右键创建节点菜单 | 已验证：`test_visual_canvas_pan_context.py` 覆盖右键点击打开 add-node 菜单，右键拖动画布不会误弹菜单 |
| 从 pin 拖出菜单 | 从 pin 拖到空白处释放，打开上下文相关节点搜索 | 已实现 React Flow `onConnectStart/onConnectEnd` 版本 | 已浏览器验证：`print_text.exit` 拖空白弹出兼容菜单，选择 `Delay` 后自动创建并连到 `delay_61848.enter` |
| Pin 连接 | 从 pin 拖到兼容 pin 创建连接，不兼容目标不可用或有反馈 | 已有 React Flow 连接和校验 | 已补拖拽期间兼容 pin 高亮、不兼容 pin 弱化和无效连接反馈；`test_visual_pin_compatibility_preview.py` 验证拖拽中的 origin/compatible/disabled 状态并保存截图，`test_visual_connection_feedback.py` 验证 exec->data 无效拖拽会显示错误提示且不 replay edge 命令 |
| 框选 | 空白左键拖拽出现 selection box，可多选节点 | 已开启 `selectionOnDrag` 和 partial mode | 已浏览器验证：框选两个临时 Delay 节点后都进入 selected 状态 |
| 多选点击 | Shift/Ctrl 添加/切换选择 | 已补点击前选择快照，避免 React Flow 先改 selection 后把新节点误切掉 | 已浏览器验证：点选 `qa_multi_a` 后 Shift 点 `qa_multi_b` 两者均 selected；Ctrl 点 `qa_multi_a` 后只保留 `qa_multi_b` |
| 多选拖动 | 多个选中节点可一起拖动，释放后全部持久化位置 | React Flow 多选拖动已接入，释放后逐个持久化选中节点位置 | 已浏览器验证：双选 `qa_multi_a/qa_multi_b` 后拖动集合，两个节点 DOM 坐标同时移动，后端 `Position` 注解分别写回 |
| 平移画布 | 中键/右键拖拽或 Space 拖拽平移 | 已配置中键/右键/Space 平移，并加右键拖动菜单保护 | 已验证：`test_visual_canvas_pan_context.py` 覆盖右键拖动平移、中键拖动平移、Space+左键拖动平移、右键拖动不打开菜单 |
| 缩放/聚焦 | 滚轮缩放，快捷键聚焦选择或全图 | 基础 zoom 已有；已补 `F` 聚焦选中节点/选中边/全图 | 已浏览器验证：滚动偏移视口后按 `F`，viewport transform 从 `translate(294.61px, 9.42589px) scale(0.491557)` 变为 `translate(682.843px, 90.698px) scale(1.43177)` |
| 删除 | Delete/Backspace 删除选中节点/edge | 节点和 edge 删除均已接到后端命令 | 已浏览器验证：节点 Delete 触发 `remove_node`；edge Delete 触发 `unflow` |
| 重连/断线 | 选中线可重连，pin/edge 可断开 | edge 删除断线已验证；edge 改为自定义 source/target endpoint hit target，统一走 `onEdgeReconnect` 事务路径 | 已验证：`test_visual_edge_reconnect_replay.py` 覆盖 target endpoint 重连，`test_visual_edge_source_reconnect_replay.py` 覆盖 source endpoint 重连，`test_visual_edge_reset_guard.py` 覆盖无效重连不 replay |
| 节点复制 | 选中一个或多个节点后可用快捷键 Duplicate，副本偏移出现并保持选中上下文 | 已实现 `Ctrl+D`/`Cmd+D` 与 `Ctrl+W`/`Cmd+W`，通过 `add_node` + `annotate node Position` 回放，副本命名为 `原名_copy` | 已验证：`test_visual_node_duplicate_replay.py` 覆盖多选复制、命令顺序、mock state、画布节点和 Current Graph 文本同步 |
| 节点对齐 | 选中多个节点后可按图编辑器 Align 菜单六轴对齐：Top/Middle/Bottom/Left/Center/Right | 已实现 `Shift+W`、`Alt+Shift+W`、`Shift+S`、`Shift+A`、`Alt+Shift+S`、`Shift+D`；位置通过 `annotate node Position` 回放 | 已验证：`test_visual_node_align_replay.py` 覆盖左对齐的命令、state、画布坐标和 Current Graph 文本同步；`test_visual_node_layout_axes.py` 覆盖六轴对齐的命令回放、state 变化和画布几何对齐 |
| Straighten Connection | 选中连接后按 `Q` 拉直连线 | 已实现：对选中 edge 的 target 节点做垂直中心对齐，保留 X，位置通过 `annotate node Position` 回放 | 已验证：`test_visual_straighten_connection_replay.py` 覆盖选线、按 `Q`、命令回放、画布中心线对齐和 Current Graph 文本同步 |
| 节点分布 | 选中 3 个或更多节点后执行 Distribute Horizontally / Vertically，让节点间距均匀 | 已实现 `Alt+Shift+H` 水平分布、`Alt+Shift+V` 垂直分布；按节点 measured 尺寸计算等间距 gap，位置通过 `annotate node Position` 回放 | 已验证：`test_visual_node_distribute_replay.py` 覆盖水平分布的命令、state、画布 gap 和 Current Graph 文本同步；`test_visual_node_layout_axes.py` 覆盖垂直分布命令回放和画布 gap |
| 注释框 | 选中节点后可创建 comment box，框可移动、缩放、改标题，用于给节点区域加说明；移动框时可带动框内节点 | 已实现：按 `C` 从选中节点外接框创建，拖动/resize/标题编辑写回，Delete 删除；完全包裹在框内的普通节点会随框移动；通过 `CommentBox_<id>` graph annotation 持久化 | 已验证：`test_visual_comment_box_replay.py` 覆盖创建、标题编辑、resize、拖动写回、包裹节点 Position 写回、删除、命令回放、画布渲染和 Current Graph 文本同步 |

## 外部参考

- Epic 官方文档 `Nodes in Unreal Engine`: 用于确认 Blueprint 节点、pin、连接、选择、移动、右键菜单和 pin 拖出创建节点等基本交互概念。
- Epic 官方文档 [`Organizing a Material Graph in Unreal Engine`](https://dev.epicgames.com/documentation/unreal-engine/organizing-a-material-graph-in-unreal-engine?lang=en-US): 用于确认 UE 图编辑器 Align 菜单的六个对齐轴、Straighten Connection 和 Distribution 操作；该文档说明 Material Graph 的图操作通常与 Blueprint 等 Unreal 节点编辑器一致。

## 错误路径

- 只追求视觉像蓝图，但不修正选择、右键拖动、pin 拖出搜索等操作契约，会继续让编辑器不可用。
- 把 data input pin 当作内禀属性的最终方案是不正确的；field 必须独立于 in/out pin。

## 审计覆盖项

- 右键菜单和画布平移已补 `test_visual_canvas_pan_context.py`：右键点击打开 add-node 菜单，右键拖动、中键拖动、Space+左键拖动均平移视口，右键拖动不误弹菜单。
- 兼容 pin 拖动中高亮已由 `test_visual_pin_compatibility_preview.py` 截图级验证：data output 拖拽和 exec output 拖拽均覆盖 origin、compatible、disabled 状态；连接失败反馈已补 `data-connection-feedback` 并由 `test_visual_connection_feedback.py` 覆盖。
- source endpoint 重连已由 `test_visual_edge_source_reconnect_replay.py` 覆盖；继续可做截图级交互 polish。
- 节点复制已由 `test_visual_node_duplicate_replay.py` 覆盖：多选后按 `Ctrl+D` 会生成偏移副本，并通过 `add_node` / `annotate node Position` 同步到 Current Graph 文本。
- 节点对齐已由 `test_visual_node_align_replay.py` 覆盖左对齐回放，并由 `test_visual_node_layout_axes.py` 覆盖 Left/Right/Top/Bottom/Center/Middle 六轴。
- Straighten Connection (`Q`) 已由 `test_visual_straighten_connection_replay.py` 覆盖：选中弯曲 edge 后按 `Q`，target 节点写回 Position 并让画布中心线对齐。
- Distribute Horizontally/Vertically 已实现；`test_visual_node_distribute_replay.py` 覆盖水平分布回放，`test_visual_node_layout_axes.py` 覆盖垂直分布回放。
- 注释框操作已由 `test_visual_comment_box_replay.py` 覆盖：选中节点按 `C` 创建、标题双击编辑、右下角 resize、拖动框写回、框内 `branch` / `printer` 跟随移动并写回 `Position`、Delete 删除，均同步到 Current Graph 文本。
- 节点标题/category 色、fields declaration source/default 显示、多选集合外框已由 `test_visual_node_rendering_metadata.py` 覆盖；该测试验证渲染元数据不 replay 编辑命令。
- `NodeInfo` 已补缺省数组容错，避免旧 mock/旧后端 JSON 在选中节点时因 `fields`、`pins`、`annotations` 缺省而清空 UI。

## 完成审计结论

- 2026-06-30 审计：`goal.md` 完成证据已由 `references.md` 清单、17 个 `webapp/test_visual_*.py` 回放、`pnpm build`、`build/Release/gs_tests.exe` 和 `validate_goal_file.py` 覆盖。
- 审计时发现的弱验证项是对齐/分布非首轴只靠同一计算路径与构建覆盖；已新增 `test_visual_node_layout_axes.py` 覆盖六轴对齐和垂直分布。
- 当前清单没有保留未实现的 UE 蓝图交互一致性必需项；后续可继续做视觉细节 polish，但不再阻塞本 Auto Goal 的完成判定。
