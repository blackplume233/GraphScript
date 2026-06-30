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

- `src/edit/edit_session.cpp` 已有 `has_asset_declaration_import_items()`，覆盖 `imports`、`modules`、`enums`、`objects`、`block_kinds`、`commands`、`schemas`、`lints`、`symbols`，并在 `EditSession::load_import()` 中作为 asset import 入口判定。
- `EditSession::load_import()` 对 asset declaration import 先 parse/lint，再调用 `register_asset_declarations()`；其中 type/object/schema 会注册到 `Environment`，module/enum/kind/block/command/lint 这类暂未有 runtime registry 的 metadata declaration 只参与 parse/lint，然后标记 import loaded。
- 增加/补齐 `EditSession.LoadAssetImportAcceptsDeclarationMetadataOnlyFile`，样例包含 `declare module`、`declare enum`、`declare kind block`、`declare kind command`、`declare block`、`declare command`、`declare lint`，验证 import 被标记 loaded，且不会向 `TypeRegistry`、`NodeRegistry`、`SchemaRegistry` 注册条目。
- 增加 `EditSession.LoadAssetImportAcceptsImportOnlyDeclarationFile`，覆盖只包含 `import "core.d.gs";` 的 asset `.d.gs` 也被视为 asset import，不再落入旧 compiler fallback。

## Review

- 子代理 Descartes 只读 Review：无阻断 findings。
- 非阻断观察：asset parse error 后 `load_import` 仍可能尝试旧 fallback；这属于后续移除旧 fallback 前需要收束的行为。
- 非阻断观察：asset schema policy 当前严格校验 `max_exec_fan_out`，布尔策略值更严格的注册校验仍可作为后续迁移缺口处理。

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

## 需要回写

- `goal.md` 的 `last_verified_loop` 更新为 `loops/loop-011.md`。
- 下一轮建议收束 `load_import` asset parse error 后落入旧 fallback 的行为，让 `.d.gs` asset import 的语法错误稳定暴露为 asset diagnostics/error，而不是被旧 parser/compiler 信息覆盖。

## 结果

result: passed
