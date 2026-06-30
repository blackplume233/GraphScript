# Loop 016：load_source 与 CLI source/files rename 迁移到 asset graph path

## 子目标

迁移 `EditSession::load_source` 与 CLI source/files/import-current-source rename 的范围构建路径到 tree-sitter asset `.gs`，拆除这条路径对旧 `Lexer/Parser/Compiler` 的 fallback；同步迁移相关 CLI/EditSession 测试夹具。

## 本轮基准检查

- 已按 Auto Goal 要求重读 `goal.md`。
- 已读取 `trellis-before-dev` 与 backend/spec 指南。
- 采用现有 asset parser/projector、`EditSession`、CLI patch helper 和 GoogleTest，不新增依赖。
- 不修改 `AGENTS.md`。

## 主要变更

- `EditSession::load_source` 改为 asset-only：
  - 使用 `asset::Parser`、asset lint、asset graph projection。
  - 删除旧 `parse_text`、旧 parser/compiler fallback 以及 `Lexer/Parser/Compiler` include。
  - asset source 必须至少包含一个 graph。
- CLI source/files/import-current-source rename 的 range module 改为通过临时 `EditSession::load_source` 获取，移除旧 Lexer/Parser/Compiler range 编译。
- CLI identifier rename 改为本地 identifier scanner，跳过字符串与注释，并拒绝 asset/旧 DSL 保留字。
- asset projection 增补 legacy editor/CLI 需要的 source ranges：
  - graph/name/schema、param name/type/default ctor、node alias/type/property ctor、flow/data endpoint、top-level const、generate block。
  - graph/param/node/block/edge/link attributes 投影到 legacy annotations（受当前 grammar 限制，call 级 attributes 尚不是可解析语法）。
- 测试迁移：
  - `CLIEditor.ApplySource*`、`ApplyFiles*`、`ApplyImport*` 中涉及 source graph rename/type/pin/schema 的旧 DSL fixture 改为 asset graph syntax。
  - `EditSession` 中 `load_source` 旧 DSL fixture 改为 asset graph syntax。
  - 对当前 asset grammar 尚不支持的 import 前置 attributes、call 级 attributes，保留编辑 API 覆盖，load_source 夹具收窄到当前语法可表达能力。

## 验证

- `cmake --build build-codex --config Release --target gs_tests`
  - 证据：`evidence/phase5_loop016_build_after_final_edit_fixes.log`
- `build-codex/Release/gs_tests.exe --gtest_filter="CLIEditor.ApplySource*:CLIEditor.ApplyFiles*:CLIEditor.ApplyImport*:CLIEditor.RenameGraphDerivedPinCommandsAreReplayable"`
  - 证据：`evidence/phase5_loop016_cli_source_files_imports_after_directive_fix.log`
  - 结果：28/28 通过。
- `build-codex/Release/gs_tests.exe --gtest_filter="EditSession.RenameGraphMigratesGraphNodeReferencesAndKeepsPersistentId:EditSession.RenameGraphDerivedPinsMigratesCrossGraphReferencesAndKeepsPersistentIds:EditSession.StateJsonExportsGenerateSourceRanges:EditSession.GenerateAnnotationsRoundTripAndExportPersistentIds:EditSession.StateJsonExportsStableElementIds:EditSession.LogicBlockAnnotationsRoundTripAndExportPersistentIds:EditSession.ConnectionAnnotationsRoundTripAndExportPersistentIds:EditSession.TopLevelAnnotationsRoundTripAndExportPersistentIds:EditSession.StateJsonExportsElementSourceRanges:EditSession.LoadSourceReplacesModuleAtomically"`
  - 证据：`evidence/phase5_loop016_edit_session_migrated_focused_final.log`
  - 结果：10/10 通过。
- `build-codex/Release/gs_tests.exe`
  - 证据：`evidence/phase5_loop016_gs_tests_full_final.log`
  - 结果：348/348 通过。
- `cmake --build build-codex --config Release --target gs`
  - 证据：`evidence/phase5_loop016_build_gs_final.log`
- 旧依赖扫描：
  - `rg -n "parse_text|ModuleNode|Compiler|Parser|Lexer\(" src/edit/edit_session.cpp cli/editor.cpp`
  - 结果：只剩 `asset::Parser` 使用。

## 结果

Loop016 子目标完成。`load_source` 和 CLI source/files/import-current-source rename 已不再依赖旧 parser/compiler fallback，相关测试已迁移并通过全量 C++ 回归。

## 后续

Loop017 建议继续 Phase 5：拆除或迁移剩余 editor/source diagnostics/web 路径中的旧 emitter/parser/compiler 依赖，优先处理仍由 legacy emitter 维持的编辑 API emit/round-trip 面。
