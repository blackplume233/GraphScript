# Loop 011：Phase 5 load_import 识别 declaration-only asset imports

## 子目标

修正 `EditSession::load_import` 的 asset 判定范围：只含 `declare module`、`declare enum`、`declare kind`、`declare block`、`declare command` 或 `declare lint` 的 `.d.gs` 也应被识别为 asset declaration import，避免落入旧 parser/compiler fallback。

## 范围

- 在 `src/edit/edit_session.cpp` 中提取/使用 asset declaration import 判定 helper。
- 保持当前可注册声明的行为：type/object/schema 继续注册到 `Environment`。
- 对暂不注册到 runtime environment 的 declaration-only metadata/kind/command/lint，执行 asset parse/lint 后标记 import loaded。
- 增加 `EditSession` 回归测试覆盖这些 declaration-only `.d.gs`。

## 非范围

- 不实现 kind/command/lint 的完整 semantic registry。
- 不删除旧 parser/compiler fallback。
- 不改变 graph source load 或 Web API 合约。
- 不修改 `AGENTS.md`。

## 本轮仓库基准检查

- 已重读 `goal.md`；本轮延续 Loop 010 Review 发现，仍属于 Phase 5 删除旧 parser/compiler 前的解耦切片。
- 遵循 C++17、显式 `Result` 错误、focused/full 测试和自动 Review。

## 计划

- [x] 重读 `goal.md` 与当前 `load_import` 实现。
- [x] 扩展 asset declaration import 判定。
- [x] 增加 focused 回归测试。
- [x] 构建、focused/full 测试和扫描。
- [x] 自动 Review。

## 执行记录

- `src/edit/edit_session.cpp` 增加 `parse_asset_result()`，让 `EditSession::load_import()` 可以保留 asset parse diagnostics，而不是只拿到 `std::optional<asset::Module>`。
- `has_asset_declaration_import_items()` 现在只接受 declaration-only asset module：顶层 source items（properties、consts、calls、assignments、directives、blocks）存在时返回 false。
- `has_asset_declaration_symbol()` 排除 export-list 产生的 `symbol.kind == "export"`，避免 `export { Execute }; graph Execute {}` 被误判为 declaration import。
- `EditSession::load_import()` 对看起来像新 asset declaration 或 lowercase graph source 的 parse diagnostics 直接返回 `Asset parse error in:`，不落入旧 parser/compiler fallback。
- 旧 preset `.d.gs` fallback 暂时保留，因为 `presets/ue_core.d.gs` 当前仍是旧 declaration 语法；后续 preset 迁移时再删除该 fallback。
- 增加/补齐 `EditSession.LoadAssetImportAcceptsDeclarationMetadataOnlyFile`，样例包含 `declare module`、`declare enum`、`declare kind block`、`declare kind command`、`declare block`、`declare command`、`declare lint`，验证 import 被标记 loaded，且不会向 `TypeRegistry`、`NodeRegistry`、`SchemaRegistry` 注册条目。
- 增加 `EditSession.LoadAssetImportRejectsGraphSourceWithImportOrExportList`，覆盖带 import 和 export-list 的 graph source 不被标记为 declaration import。
- 保留 `EditSession.LoadAssetImportAcceptsImportOnlyDeclarationFile`，验证 import-only 文件在当前过渡期仍可按旧 import fallback 标记 loaded。

## Review

- 子代理 Descartes 只读 Review：无阻断 findings。
- 非阻断观察：asset parse error 后 `load_import` 仍可能尝试旧 fallback；这属于后续移除旧 fallback 前需要收束的行为。
- 非阻断观察：asset schema policy 当前严格校验 `max_exec_fan_out`，布尔策略值更严格的注册校验仍可作为后续迁移缺口处理。
- 子代理 Aquinas 只读 Review：发现 graph source with import/export-list 可能误判为 declaration import；已通过 source item 排除和 export-list 排除修正，并补回归。
- Aquinas 复审后提示 malformed asset declaration 单测曾被旧 fallback 吞掉；最终版改为对看起来像新 asset declaration/graph source 的 parse diagnostics 直接返回 asset parse error，同时保留旧 preset fallback。报告见 `subagents/loop-011-aquinas-review.md`。

## 验证

- `cmake --build build-codex --config Release --target gs_tests -- /m:1`：通过，证据见 `evidence/phase5_asset_import_declaration_only_build_gs_tests.log`。
- `./build-codex/Release/gs_tests.exe --gtest_filter="EditSession.LoadAssetImportAcceptsDeclarationMetadataOnlyFile:EditSession.LoadAssetImportRejectsInvalidSchemaPolicyValue:EditSession.LoadAssetImportRejectsInvalidMaxExecFanOutValue:EditSession.LoadAssetImportRejectsLintErrorsWithoutRegistering:EditSession.LoadAssetImportRejectsEnvironmentConflictsWithoutMarkingLoaded:EditSession.SourceImportDeclarationIsNotLoadedUntilImportCommand"`：6/6 通过，证据见 `evidence/phase5_asset_import_declaration_only_focused_tests.log`。
- `rg -n "has_asset_declaration_import_items|LoadAssetImportAcceptsDeclarationMetadataOnlyFile|declare kind command" src/edit/edit_session.cpp tests/test_edit_session.cpp`：确认 helper 与覆盖测试位置，证据见 `evidence/phase5_asset_import_declaration_only_scan.log`。
- `git diff --check -- src/edit/edit_session.cpp tests/test_edit_session.cpp .trellis/tasks/06-30-tree-sitter-gs-migration/loops/loop-011.md`：退出码 0，仅 CRLF 工作区提示，证据见 `evidence/phase5_asset_import_declaration_only_diff_check.log`。
- `./build-codex/Release/gs_tests.exe`：340/340 通过，证据见 `evidence/phase5_asset_import_declaration_only_full_cpp.log`。
- `cmake --build build-codex --config Release --target gs -- /m:1`：通过，证据见 `evidence/phase5_asset_import_declaration_only_build_gs.log`。
- `cmake --build build-codex --config Release --target gs_tests -- /m:1`：通过，证据见 `evidence/phase5_loop011_build_gs_tests.log`。
- `./build-codex/Release/gs_tests.exe --gtest_filter="EditSession.LoadAssetImport*:EditSession.LoadImportDeduplicatesAndMarksLoaded:EditSession.ImportAndModuleState"`：通过，证据见 `evidence/phase5_loop011_focused_tests.log`。
- `./build-codex/Release/gs_tests.exe`：通过，证据见 `evidence/phase5_loop011_full_cpp.log`。
- `rg -n "has_asset_declaration_import_items|has_asset_declarations|parse_asset_text\(src|Compiler compiler\(env_\)|load_import" src/edit/edit_session.cpp tests/test_edit_session.cpp`：确认 `load_import` asset 判定与 fallback 边界，证据见 `evidence/phase5_loop011_import_scan.log`。
- `cmake --build build-codex --config Release --target gs -- /m:1`：通过，证据见 `evidence/phase5_loop011_build_gs.log`。
- `git diff --check -- src/edit/edit_session.cpp tests/test_edit_session.cpp .trellis/tasks/06-30-tree-sitter-gs-migration/loops/loop-011.md`：退出码 0，证据见 `evidence/phase5_loop011_diff_check.log`。
- 最终验证：`cmake --build build-codex --config Release --target gs_tests -- /m:1` 通过，证据见 `evidence/phase5_load_import_asset_declaration_build_gs_tests.log`。
- 最终验证：`cmake --build build-codex --config Release --target gs -- /m:1` 通过，证据见 `evidence/phase5_load_import_asset_declaration_build_gs.log`。
- 最终验证：import-focused tests 16/16 通过，覆盖旧 preset fallback、import-only、malformed asset declaration、malformed graph source、graph source with import/export-list、declaration-only imports，证据见 `evidence/phase5_load_import_asset_declaration_focused_tests.log`。
- 最终验证：`./build-codex/Release/gs_tests.exe` 345/345 通过，证据见 `evidence/phase5_load_import_asset_declaration_full_tests.log`。
- 最终验证：无用 helper 扫描清理 `is_asset_declaration_path` / `ends_with_ignore_case`，证据见 `evidence/phase5_load_import_asset_declaration_unused_helper_scan.log`。

## 需要回写

- `goal.md` 的 `last_verified_loop` 更新为 `loops/loop-011.md`。
- 下一轮建议收束 `load_import` asset parse error 后落入旧 fallback 的行为，让 `.d.gs` asset import 的语法错误稳定暴露为 asset diagnostics/error，而不是被旧 parser/compiler 信息覆盖。

## 结果

result: passed
