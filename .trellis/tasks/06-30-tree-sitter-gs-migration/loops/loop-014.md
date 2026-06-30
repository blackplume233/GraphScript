# Loop 014：asset declaration 覆盖剩余非 CLI load_import 声明路径

## 子目标

迁移剩余非 CLI `load_import` 旧声明测试/fixture 到 asset declaration，补齐 asset declaration 注解与 source range 能力，并验证现有 C++ 回归保持通过。`EditSession::load_import` 旧 parser/compiler fallback 的最终拆除顺延到下一轮，因为 CLI import rename 命令仍直接依赖旧 Lexer/Parser/Compiler 修补声明文件。

## 仓库基准检查

- 语言：中文优先。
- 开发约束：不回滚既有未提交改动；本轮只触及 asset language、EditSession 声明注册、相关测试 fixture 与 Auto Goal 记录。
- 验证命令：`npm run generate`、`cmake --build build-codex --target gs_tests --config Release`、聚焦 `gs_tests`、完整 `gs_tests`。

## 计划

1. 迁移 `tests/fixtures/mixed_declarations.d.gs` 到 asset declaration 并让深度集成测试通过 `EditSession::load_import` 加载真实 fixture。
2. 为 `type/object/schema` declaration 暴露 name/source range 和 attribute 信息。
3. 支持 schema property 的 constructor call 表达式 range，覆盖 `DeclValue("schema")`。
4. 迁移 `test_edit_session.cpp` 中声明 source range 和持久 ID 注解测试。
5. 扫描 remaining legacy import rename 依赖，决定 fallback 删除的下一步。

## 执行记录

- `tools/tree-sitter-graphscript-asset/grammar.js`：
  - `type_declaration` / `schema_declaration` 接受声明属性。
  - `_expression` 接入已有 `call_expression`，允许 schema property 使用 `DeclValue("schema")`。
- `include/graphscript/asset/language.h`、`src/asset/language.cpp`：
  - `Expression` 增加 `Call`、`callee`、`callee_span`。
  - `FieldDecl`、`ObjectDecl`、`SchemaDecl`、`SymbolDecl` 带出 type/name span 和属性。
  - export wrapper 的 span 传入声明 AST，使 editor state 的 declaration `source_range` 从 `export` 起始。
  - 字段 source range 起点定位到字段名，避免结构属性 `@flow.*` 抢占 pin/field 的实体范围。
- `src/edit/edit_session.cpp`：
  - asset declaration 属性转换为 core `Annotation`，并过滤 `flow.pin` / `flow.input` / `flow.output` 结构属性。
  - 声明注册填充 type/node/pin/schema/field 的 annotation、name range、type range。
  - schema field 的 constructor range 从 asset call expression 填入 JSON state。
- `tests/fixtures/mixed_declarations.d.gs`：
  - 改为 `export declare type/object/schema` asset syntax。
  - 移除重复声明内建 `Exec`，避免与预置环境冲突。
- `tests/test_integration_deep.cpp`：
  - `load_mixed` 改为通过 `EditSession::load_import` 加载 canonical fixture。
- `tests/test_edit_session.cpp`：
  - `StateJsonExportsDeclarationSourceRanges` 和 `DeclarationAnnotationsExportPersistentIds` 使用 asset declaration source。
  - 更新 JSON source range 断言到新语法位置。

## 验证

- `npm run generate`
  - 证据：`evidence/phase5_loop014_tree_sitter_generate.log`
- `npm run generate`（call expression 接入后重跑）
  - 证据：`evidence/phase5_loop014_tree_sitter_generate_retry.log`
- `cmake --build build-codex --target gs_tests --config Release`
  - 证据：`evidence/phase5_loop014_build_gs_tests_retry3.log`
- 聚焦测试：
  - 命令：`./build-codex/Release/gs_tests.exe --gtest_filter="EditSession.StateJsonExportsDeclarationSourceRanges:EditSession.DeclarationAnnotationsExportPersistentIds:DeepCycle.S8_MixedDeclarations_FullLoad:DeepCycle.S9_Cinematic_FullPipeline:DeepCycle.S10_AbilityBranching_FullPipeline:AssetLanguage.*"`
  - 结果：22/22 通过。
  - 证据：`evidence/phase5_loop014_focused_tests_retry4.log`
- 完整 C++ 测试：
  - 命令：`./build-codex/Release/gs_tests.exe`
  - 结果：348/348 通过。
  - 证据：`evidence/phase5_loop014_full_cpp.log`
- 残留扫描：
  - `evidence/phase5_loop014_legacy_import_rename_scan.log` 显示旧声明 source 与旧 Lexer/Parser/Compiler 依赖集中在 `tests/test_cli_editor.cpp` import rename 用例和 `cli/editor.cpp` import rename 命令实现。

## 结果

本轮完成了剩余非 CLI `load_import` 声明路径迁移，并补齐 asset declaration 对 editor state 必需的注解与 source range 能力。`EditSession::load_import` 的旧 parser/compiler fallback 尚未拆除；原因是 CLI import node/schema/type rename 命令仍直接用旧 Lexer/Parser/Compiler 对声明文件做 dry-run 和 real import patch。下一轮应先迁移这些 CLI import rename 命令与测试到 asset declaration patch path，再删除 `load_import` fallback。

## 回写

- `goal.md`：
  - `last_verified_loop` 更新为 `loops/loop-014.md`。
  - `current_loop` 更新为 `loops/loop-015.md`。
  - `next_sub_goal` 更新为 CLI import rename asset 化并拆除 `EditSession::load_import` 旧 fallback。
- `references.md`：
  - 记录 CLI import rename 是阻止本轮删除 `load_import` fallback 的明确 blocker。
