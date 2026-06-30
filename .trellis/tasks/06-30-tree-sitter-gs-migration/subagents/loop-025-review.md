# Loop 025 子代理最终审计摘要

## 审计者

- Codex 子代理 Ptolemy（只读最终审计）。

## 发现

- `goal.md` 仍为 active，缺少 Loop 025 收尾记录。
- `docs/spec/legacy-graph-dsl-feature-inventory.md` 已存在但未被当前 spec index 明确引用。
- Loop 025 evidence 日志尚未纳入本轮提交。
- `docs/spec/development-guide.md` 仍残留旧 `Result<Module> compile(const ModuleNode& ast)` 示例。
- `docs/spec/architecture.md` 仍有 “legacy parser/compiler paths until editor replacement phase” 旧阶段说明。
- Web smoke 证据不足：真实后端 replay 与 source preview 需要有当前 asset 语法的通过证据。

## 处理结果

- 已更新 `docs/spec/index.md`，把旧 Graph DSL 功能清单纳入 spec 索引。
- 已更新 `docs/spec/development-guide.md`，用 asset graph projection 示例替换旧 compile/ModuleNode 示例。
- 已更新 `docs/spec/architecture.md`，明确 editor/server 入口使用 tree-sitter asset path，额外 replay/debug 命令只属于 interactive edit surface。
- 已修正 Web 当前图 source preview 的旧 Graph DSL 输出，改为 canonical asset 输出：`graph`、`schema`、`@graph.* param`、`node { type ... }`、`connect`、`bind`。
- 已更新真实后端 replay smoke，使其优先使用本轮 `build-codex` 可执行文件，并断言 canonical emit。
- 已新增 `test_asset_source_preview_smoke.py`，覆盖当前 asset source preview、source diagnostics range focus 和 canvas 节点渲染。

## 结论

Loop 025 已按子代理审计反馈完成必要修正，并通过最终构建、测试、扫描与 Web smoke 证据。
