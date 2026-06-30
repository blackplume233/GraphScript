# Loop 015：CLI import rename 迁到 asset declaration

## 基础目标切片

推进 Phase 5 编辑器替换层：让 CLI import rename 命令不再依赖旧 `Lexer` / `Parser` / `Compiler` 修补 `.d.gs` 声明文件，并删除 `EditSession::load_import` 的旧 parser/compiler fallback。

## 子目标

迁移 CLI import node/schema/type rename 命令与测试到 asset declaration patch/parse path，并删除 `EditSession::load_import` 旧 parser/compiler fallback。

## 仓库基准检查

- 读取 `goal.md`，当前循环为 `loops/loop-015.md`，上一已验证循环为 `loops/loop-014.md`。
- 读取 `trellis-before-dev` / backend spec / `docs/spec` / `trellis-check`；本轮触及 `include/graphscript/edit/`、`src/edit/`、`cli/`、`tests/`，需保持 C++17、CLI-first、显式错误、GoogleTest 回归。
- 工作区存在大量既有脏改动；本轮只处理 Loop 015 相关文件与证据，不回滚用户改动。

## 计划

1. 为 `EditSession` 增加可绕过 import cache 的 reload 路径，并让 `load_import` 只接受 asset declaration。
2. 将 CLI import rename 的声明解析、声明 patch、dry-run/replay/real reload 都改为 asset parser/linter/import registration 路径。
3. 将相关 CLI rename 测试中的临时 `.d.gs` 声明迁到 asset declaration syntax。
4. 运行聚焦测试、全量 C++ 测试和 residual scan，确认旧 import fallback 被删除。

## 执行记录

- `EditSession` 新增 `reload_import(path)` 和 `load_import_impl(path, force_reload)`；`reload_import` 用于 CLI 在写入声明文件后绕过 loaded-import cache 重新注册声明。
- `load_import_impl` 现在对 `.d.gs` 统一走 asset parser/linter/register path；如果 asset parse 有诊断直接返回 asset parse error；如果没有 asset declaration item，返回 `Import file has no asset declarations`。已删除旧 `parse_text` + `Compiler::compile` fallback。
- `cli/editor.cpp` 新增 asset declaration 辅助：
  - asset declaration parser/linter helper。
  - asset range 到现有 patch range 的转换。
  - asset type/object/schema 查找。
  - asset attribute/call expression 中 type constructor rename patch。
  - 按 source file 注销旧 asset declarations 后再 reload 的环境同步 helper。
- `apply_import_node_rename`、`apply_import_node_pin_rename`、`apply_import_schema_rename`、`apply_import_schema_field_rename`、`apply_import_type_rename` 已改为：
  1. 解析原始 asset declaration。
  2. 从 asset AST 生成声明 patch。
  3. 用临时 `.d.gs` 走 `EditSession::load_import` 验证 patched declaration。
  4. 在 replay env 中注销原 source 的旧声明并加载 patched declaration，再校验当前 source patch。
  5. 写回声明文件后，对真实 session env 注销原声明并 `reload_import(path)`。
- `tests/test_cli_editor.cpp` 中 import rename 相关测试和同组 source/files rename 预加载声明测试迁为 asset declaration syntax。

## 验证

- `cmake --build build-codex --config Release --target gs_tests`
  - 证据：`evidence/phase5_loop015_build_gs_tests_final.log`
  - 结果：通过。
- `build-codex/Release/gs_tests.exe --gtest_filter=CLIEditor.*`
  - 证据：`evidence/phase5_loop015_cli_editor_tests_after_fallback_removal.log`
  - 结果：44/44 通过。
- `build-codex/Release/gs_tests.exe`
  - 证据：`evidence/phase5_loop015_full_cpp.log`
  - 结果：348/348 通过。
- 旧 import compiler path 扫描：
  - 证据：`evidence/phase5_loop015_cli_import_old_compiler_scan.log`
  - 结果：`no matches`。
- CLI 旧声明 fixture 扫描：
  - 证据：`evidence/phase5_loop015_cli_legacy_declaration_scan.log`
  - 结果：无 `declare Node` / `declare Schema` / `declare type` / `exec in` / `data in` 测试声明残留。
- `load_import` fallback 扫描：
  - 证据：`evidence/phase5_loop015_load_import_fallback_scan.log`
  - 结果：`load_import` fallback 已移除；固定字符串剩余命中为 `load_source` 旧兼容路径。
- `git diff --check -- include/graphscript/edit/edit_session.h src/edit/edit_session.cpp cli/editor.cpp tests/test_cli_editor.cpp`
  - 结果：通过。

## 结果

Loop 015 完成。CLI import rename 的声明文件修补与环境重载已不再依赖旧 parser/compiler，`EditSession::load_import` 也不再接受旧 DSL fallback。旧 parser/compiler 的下一处主要阻塞转移到 `EditSession::load_source` 和 CLI source/files rename 仍在使用的旧 active source graph path。

## 回写

- `goal.md`：
  - `last_verified_loop` 更新为 `loops/loop-015.md`。
  - `current_loop` 推进到 `loops/loop-016.md`。
  - `next_sub_goal` 推进为迁移 `EditSession::load_source` 与 CLI source/files rename 的 asset graph path，并拆除 `load_source` 旧 parser/compiler fallback。
- `references.md`：
  - 记录 Loop 015 的已接受发现和下一阻塞点。
