# Loop 016 - 多选外框、节点类别色和 Fields 元数据显示

## 子目标

- 校准剩余节点渲染 polish：
  - 多选普通节点时显示蓝图式集合外框。
  - 节点标题栏按 category/tag/source 体现类别颜色和徽标。
  - 内禀 fields 在图上和属性面板里显示 declaration source、default、override 状态。
- 改动保持 Web GUI 层实现，不改变 core C++ 模型。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮改动集中在 Web GUI：
  - `webapp/src/canvas/BlueprintNode.tsx`
  - `webapp/src/canvas/FlowCanvas.tsx`
  - `webapp/src/canvas/port-config.ts`
  - `webapp/src/panels/PropertiesPanel.tsx`
  - `webapp/test_visual_node_rendering_metadata.py`
  - `.artifact/goal/2026-06-29-ue-blueprint-interaction-parity/references.md`
- 渲染 metadata 不产生编辑命令；测试必须确认没有 replay 命令。

## 执行记录

- `BlueprintNode`：
  - 新增 `category`、`sourceGraph`、`isNative` 数据。
  - 标题栏根据 category/tag/native/graph source 选择更接近蓝图语义的 header 颜色。
  - 标题栏显示 category 徽标，并用 `data-node-category` 暴露给测试。
  - field 预览行显示 field/pin 来源、default 值、override 状态，并暴露 `data-node-intrinsic-property-source/default`。
- `FlowCanvas`：
  - 从 `NodeTypeDef.tags/source_graph/is_native/fields` 生成节点 metadata。
  - 多选两个或更多 `blueprint` 节点时，根据节点 measured bounds 和 React Flow viewport 渲染 `data-multi-select-outline` 集合外框。
  - 外框是 `pointer-events: none`，不拦截节点拖拽、框选或右键菜单。
- `PropertiesPanel`：
  - `Intrinsic Fields` 每行直接显示 `default: ...` 和 declaration source 文件。
  - 增加 `data-node-intrinsic-property-source/default`，用于验证 source/default 确实映射到 UI。
- 新增 `test_visual_node_rendering_metadata.py`：
  - mock declaration 给 `Branch` 加 `Logic` tag，给 `PrintString` 加 `IO` tag 和 `debug.d.gs` fields。
  - 验证节点 category 徽标、canvas field source/default、PropertiesPanel field source/default。
  - Shift 多选两个节点后验证 `data-multi-select-outline=true` 和 `data-multi-select-count=2`。
  - 验证这些渲染操作没有 replay 编辑命令。

## 验证

- `pnpm build` 于 `webapp/`：通过，仅保留既有 Vite chunk-size warning。
- `python webapp/test_visual_node_rendering_metadata.py`：通过。
  - `PASS logic category rendered`
  - `PASS io category rendered`
  - `PASS canvas field source marked`
  - `PASS canvas field default marked`
  - `PASS properties field source marked`
  - `PASS properties field default marked`
  - `PASS properties field text shows default`
  - `PASS multi-select outline rendered`
  - `PASS no metadata rendering commands replayed`
- 回归验证：
  - `python webapp/test_visual_comment_box_replay.py`：通过。

## 结果

- 节点标题/category 色、fields declaration source/default 显示、多选集合外框三个剩余渲染差距已闭环，并有浏览器回放证据。
- 本轮未触碰 core C++，仍保持 GraphScript core 领域无关。

## 延续项

- 下一轮应做一次 completion audit：逐项检查 `goal.md` 完成证据和 `references.md` 清单，确认是否还有缺失的 UE 蓝图交互/渲染项、测试覆盖弱项或需要补充的手工验证。
- 若 audit 发现缺口，继续按最小切片实现；不要在证据不足时把 Goal 标记完成。
