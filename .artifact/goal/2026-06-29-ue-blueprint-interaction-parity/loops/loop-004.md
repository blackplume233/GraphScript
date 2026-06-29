# Loop 004 - 多选点击、集合拖动与 F 聚焦收敛

## 子目标

- 修正 React Flow 多选点击在 Shift/Ctrl 下的选择集合时序问题。
- 浏览器验证多选点击、Ctrl 切换、多选集合拖动持久化、F 聚焦和空白右键菜单。
- 将已验证行为从剩余差距中移出，保留 source endpoint 重连和右键拖动画布保护为下一轮。

## 仓库基准检查

- 回复和持久任务文件继续使用中文。
- 本轮只改 Web GUI：`webapp/src/canvas/BlueprintNode.tsx` 与 `webapp/src/canvas/FlowCanvas.tsx`。
- 图编辑仍走现有 `/api/exec` CLI 命令，临时 QA 节点验证后已清理。
- Core C++ 未改动，因此不需要新增 C++ 单元测试；前端构建作为本轮必要验证。

## 执行记录

- 在 `BlueprintNode` 的 `onPointerDownCapture` 阶段派发 `graphscript:node-pointer-down` 自定义事件，记录节点 id 与 Shift/Ctrl/Meta additive 状态。
- 在 `FlowCanvas` 中监听该事件，并从 `nodesRef` 捕获点击前 selected id 集合。
- `onNodeClick` 改为使用点击前快照计算蓝图式 additive selection：
  - 普通点击：只选中当前节点。
  - Shift/Ctrl/Meta 点击未选中节点：保留原集合并添加当前节点。
  - Shift/Ctrl/Meta 点击已选中节点：从集合中移除当前节点。
- 保留上轮已有的 `preserveNodeSelection`、多选拖动持久化和 `F` 聚焦逻辑。

## 验证

- `webapp/` 下运行 `pnpm build`：通过，仅保留既有 Vite chunk-size warning。
- 浏览器验证临时节点：
  - 通过 `/api/exec` 创建 `qa_multi_a` 与 `qa_multi_b` 并设置 Position。
  - 单击 `qa_multi_a` 后 Shift 单击 `qa_multi_b`，DOM 结果为 `{ a.selected: true, b.selected: true }`。
  - Ctrl 单击已选中的 `qa_multi_a`，DOM 结果为 `{ a.selected: false, b.selected: true }`。
  - Shift 再选回 `qa_multi_a` 后拖动集合，两个节点 DOM 坐标同时移动；后端 `/api/state` 显示 `qa_multi_a Position X=-398,Y=31`、`qa_multi_b Position X=-178,Y=31`。
  - 滚动偏移视口后按 `F`，viewport transform 从 `translate(294.61px, 9.42589px) scale(0.491557)` 变为 `translate(682.843px, 90.698px) scale(1.43177)`。
  - 在画布空白坐标右键，出现 `Search node type, pin, field, or tag` 的 add-node 菜单。
  - 验证结束后执行 `remove_node qa_multi_a` 与 `remove_node qa_multi_b`，均返回 ok。

## 结果

- 多选点击从“配置了多选键但自动化无法验证”提升为已实现并已浏览器验证。
- 多选集合拖动与释放后位置持久化已完成浏览器验证。
- `F` 聚焦选中集合已完成浏览器验证。
- 空白右键菜单基本行为已完成浏览器验证。

## 延续项

- 当前浏览器 API 没有可靠的右键按住拖动输入，右键拖动画布与菜单冲突保护仍需人工或更强自动化验证。
- source endpoint 重连仍未被自动化稳定触发，需要下一轮继续收敛。
- 连接失败反馈、兼容 pin 高亮截图级验证、复制/对齐/注释框仍保留在剩余差距。
