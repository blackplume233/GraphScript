# Loop 022：Phase 6 remaining legacy-only surface 清理

## 基础目标切片

进入 Phase 6 后续清理，处理仍暴露旧 parser/compiler/emitter 或旧 `.sc/.d.sc` 表面的低风险残留，并完成 Web 前端 build/lint 验证。`RuntimeGraph` replacement 只做依赖面审查，不在本轮混入实现。

## 子目标

- 清理 `docs/spec` 中仍把 `.sc/.d.sc` 当当前资产后缀的内容。
- 将已迁移到 asset 语义的旧 lexer/parser/compiler/emitter 测试套件和文件名改成 asset 语义。
- 清理 CLI 内部旧 `compile_source_for_ranges` 命名噪声和 server diagnostics 注释。
- 跑 C++ 与 webapp 验证，记录 `RuntimeGraph` 后续替换范围。

## 仓库基准检查

- 已重新读取 `goal.md` 和 `docs/plans/tree-sitter-gs-migration-plan.md`。
- 继续遵守中文优先、每轮提交、不可回滚用户既有改动、不修改 `AGENTS.md`。
- 测试文件重命名触发 CMake glob，必须重新 `cmake -B build-codex -DCMAKE_BUILD_TYPE=Release`。

## 执行记录

- 把 `docs/spec/ai-native-syntax.md`、`docs/spec/ai-native-asset-format.md` 中现行后缀描述从 `.sc/.d.sc` 改为 `.gs/.d.gs`。
- 将测试 suite 改名：
  - `Lexer` -> `AssetLexicalSurface`
  - `Parser` -> `AssetParser`
  - `Compiler` -> `AssetProjection`
  - `Emitter` -> `AssetSourceEmission`
- 将测试文件重命名：
  - `tests/test_lexer.cpp` -> `tests/test_asset_lexical_surface.cpp`
  - `tests/test_parser.cpp` -> `tests/test_asset_parser.cpp`
  - `tests/test_compiler.cpp` -> `tests/test_asset_projection.cpp`
  - `tests/test_emitter.cpp` -> `tests/test_asset_source_emission.cpp`
- 将 `cli/editor.cpp` 内部 helper `compile_source_for_ranges` 改名为 `load_asset_source_for_ranges`。
- 将 `cli/server.cpp` diagnostics 注释从 `parse/compile` 改为 `parse/semantic`。
- 修复 webapp lint 阻塞：
  - 删除 `FlowCanvas.tsx` 未使用 helper。
  - 避免 render 期间读取 `wrapperRef.current`，改为 wrapper 宽度状态。
  - 对现有受控草稿同步模式添加局部 lint 规则说明。
  - 处理 UI variant helper 的 fast-refresh lint 规则。
  - 移除 `App.tsx` 中不必要 hook dependency。
- 子代理 Cicero 只读审查确认：旧 parser/compiler/emitter 代码目录已删除，剩余主要阻塞是 `RuntimeGraph` public API 与测试依赖。

## 验证

- `cmake --build build-codex --config Release --target gs_tests -- /m:1`：通过，见 `evidence/phase6_loop022_build_gs_tests.log`。
- 聚焦测试 `AssetLexicalSurface.*:AssetParser.*:AssetProjection.*:AssetSourceEmission.*`：25/25 通过，见 `evidence/phase6_loop022_focused_cpp.log`。
- 全量 C++ 测试：272/272 通过，见 `evidence/phase6_loop022_full_cpp.log`。
- `npm run build`：通过，见 `evidence/phase6_loop022_webapp_build_after_fix.log`。
- `npm run lint`：初次失败已记录到 `evidence/phase6_loop022_webapp_lint.log`；修复后通过，见 `evidence/phase6_loop022_webapp_lint_after_fix.log`。
- 重新 configure：通过，见 `evidence/phase6_loop022_reconfigure_after_renames.log`。
- 重命名后 `gs_tests` 和 `gs` 重建：通过，见 `evidence/phase6_loop022_build_gs_tests_after_renames.log`、`evidence/phase6_loop022_build_gs_after_renames.log`。
- 重命名后聚焦测试：25/25 通过，见 `evidence/phase6_loop022_focused_cpp_after_renames.log`。
- 重命名后全量 C++ 测试：272/272 通过，见 `evidence/phase6_loop022_full_cpp_after_renames.log`。
- 当前 surface 后缀扫描排除迁移计划历史说明后为 `NO_MATCHES`，见 `evidence/phase6_loop022_current_suffix_surface_scan_after_docs.log`。
- 旧 suite/旧 helper 名扫描为 `NO_MATCHES`，见 `evidence/phase6_loop022_legacy_name_scan_after_renames.log`。
- `RuntimeGraph` 依赖面扫描已记录，见 `evidence/phase6_loop022_runtimegraph_surface_scan.log`。
- `git diff --check`：无 whitespace error，仅 CRLF/LF 提示。

## 结果

本轮完成 Phase 6 低风险遗留表面清理，并恢复 webapp lint/build 证据。代码、测试、webapp、tree-sitter package metadata 中已无 `.sc/.d.sc` 当前 surface；迁移计划中的旧后缀仅作为历史目标说明存在。旧 `Lexer/Parser/Compiler/Emitter` suite 和测试文件名已迁移为 asset 语义。

## 回写

- `goal.md` 下一轮推进到 `RuntimeGraph` replacement：设计并实现新的 graph runtime IR，替换 public `RuntimeGraph` API、debug dump、CLI bake 和相关测试依赖。
