# Loop 025：最终完成审计与 Goal 收口

## 基础目标切片

按 `goal.md` 完成 Phase 1-6 迁移最终审计：确认旧后缀、旧 parser/compiler/emitter 目录、旧 RuntimeGraph 合约、旧顶层 CLI 命令和 Web/editor 回归证据均满足完成条件，并把 Auto Goal 标记为 complete。

## 子目标

- 运行最终残留扫描和门禁证据。
- 修正最终审计发现的当前 spec/Web source preview 迁移缺口。
- 补齐 Web smoke 证据。
- 记录子代理最终审计，并关闭 goal。

## 执行记录

- 文档/spec 收口：
  - `docs/spec/index.md` 增加 `legacy-graph-dsl-feature-inventory.md` 索引。
  - `docs/spec/development-guide.md` 用 asset graph projection 示例替换旧 `compile(const ModuleNode&)` 示例。
  - `docs/spec/architecture.md` 移除旧 “legacy parser/compiler paths until editor replacement phase” 说明，明确 editor/server 入口使用 tree-sitter asset path。
- Web source preview：
  - `webapp/src/App.tsx` 的当前图文本预览从旧 Graph DSL 输出改为 canonical asset 输出：`graph`、`schema`、`@graph.input/output/var`、`param`、`node { type ... }`、`connect`、`bind`、`generate Layout`。
  - source preview 注解输出改为当前 `@Annotation(...)` 风格，并对缺失 annotations 做防御。
- Web smoke：
  - `test_real_backend_replay_smoke.py` 优先使用 `build-codex/Release/gs.exe`，避免误跑陈旧 `build/Release`。
  - 真实后端 smoke 增加 command replay 后端状态断言，并把 emit 断言迁到 `connect(...)` / `bind(...)`。
  - 新增 `test_asset_source_preview_smoke.py`，覆盖当前 asset source preview、source diagnostics range focus、canvas 节点渲染和 canonical current graph source。
- 子代理审计：
  - Ptolemy 只读审计指出 goal 未收口、inventory 未索引、spec 旧示例、architecture 旧阶段说明、Web smoke 证据不足。
  - 发现均已处理，摘要见 `subagents/loop-025-review.md`。

## 验证

- 后缀残留扫描：`.sc/.d.sc` 仅在迁移计划历史说明中出现，见 `evidence/phase6_loop025_suffix_scan.log`。
- `RuntimeGraph` / 旧 compile diagnostics 合约 / 旧 compiler stage 扫描：`NO_MATCHES`，见 `evidence/phase6_loop025_final_legacy_contract_scan.log`。
- 旧 legacy 目录检查：旧 `include/graphscript/parse`、`src/parse`、`include/graphscript/compile`、`src/compile`、`include/graphscript/emit`、`src/emit` 均缺失，见 `evidence/phase6_loop025_legacy_dirs.log`。
- 旧顶层 CLI 命令检查：`compile/emit/bake/sc-parse/sc-project/validate/diagram/info/schema` 均为 unknown，见 `evidence/phase6_loop025_cli_legacy_unknown.log`。
- CLI usage：顶层用户命令为 `edit/serve/parse/lint/project/patch`，见 `evidence/phase6_loop025_cli_usage.log`。
- `git ls-files` 旧 legacy 文件/测试扫描为空，见 `evidence/phase6_loop025_git_ls_legacy_files.log`。
- inventory 引用扫描确认已在 `docs/spec/index.md` 索引，见 `evidence/phase6_loop025_inventory_reference_scan.log`。
- `cmake --build build-codex --config Release -- /m:1`：通过，见 `evidence/phase6_loop025_full_build.log`。
- 全量 C++ 测试 `build-codex/Release/gs_tests.exe`：276/276 通过，见 `evidence/phase6_loop025_full_cpp.log`。
- `webapp` `npm run build`：通过，见 `evidence/phase6_loop025_webapp_build.log`。
- `webapp` `npm run lint`：通过，见 `evidence/phase6_loop025_webapp_lint.log`。
- `webapp/test_real_backend_replay_smoke.py`：通过，见 `evidence/phase6_loop025_webapp_real_backend_replay_smoke.log`。
- `webapp/test_asset_source_preview_smoke.py`：通过，见 `evidence/phase6_loop025_webapp_asset_source_preview_smoke.log`。

## 结果

Loop 025 完成。`goal.md` 中列出的完成证据均已有当前证据支撑，tree-sitter asset `.gs/.d.gs` 迁移目标可标记 complete。

## 后续风险

- 仓库中仍有大量本任务早期历史 evidence/loop 文件处于未跟踪状态；本轮只提交 Loop 025 必需证据和文件，不清理或回滚历史工作区状态。
- 若后续继续整理旧 `docs/spec/dsl-reference.md` 中的历史语法说明，应作为独立 spec 文档清理任务处理。
