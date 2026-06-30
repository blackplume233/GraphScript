# Loop 021：删除 legacy parse/compile/emit 实现

## 子目标

继续 Phase 5，在测试层旧 parse/compiler/emitter 依赖清零后，扫描并删除 legacy parse/compile/emit 源码与头文件，处理 debug dump/docs/CMake 影响并跑全量验证。

## 本轮仓库基准检查

- 重新读取 `goal.md`。
- 重新扫描 `include/src/cli/tests` 中旧 `graphscript/parse|compile|emit` include、`ModuleNode`、`dump_ast`、旧 parser/compiler/emitter API。
- CMake 使用 `file(GLOB_RECURSE)`，删除源文件后必须重新 configure 再构建验证。
- 使用 Codex 子代理做只读 review，确认删除风险和未跟踪依赖。

## 计划

1. 移除 debug dump 对旧 AST 的唯一依赖：删除 `dump_ast(const ModuleNode&)` API 和实现。
2. 删除旧 legacy 文件：
   - `include/graphscript/parse/*`
   - `include/graphscript/compile/compiler.h`
   - `include/graphscript/emit/emitter.h`
   - `src/parse/*`
   - `src/compile/compiler.cpp`
   - `src/emit/emitter.cpp`
3. 重新 configure `build-codex`，构建 `gs_tests` 和 `gs`。
4. 跑 full C++。
5. 跑旧 include/API 扫描、legacy 文件删除状态扫描、diff check。
6. 记录 review 发现并修正提交范围。

## 执行记录

- `include/graphscript/debug/dump.h`
  - 移除 `struct ModuleNode` forward declaration。
  - 移除 `debug::dump_ast(const ModuleNode&)` 声明。

- `src/debug/dump.cpp`
  - 移除 `graphscript/parse/ast.h` include。
  - 删除 `dump_ast()` 实现。
  - 保留 `dump_module()`、`dump_edit_graph()`、`dump_runtime_graph()`、`diff_modules()`。
  - 恢复 `uint8_t` pin dump helper overload，供 `RuntimeGraph` dump 使用。

- 删除旧实现文件：
  - `include/graphscript/parse/ast.h`
  - `include/graphscript/parse/lexer.h`
  - `include/graphscript/parse/parser.h`
  - `include/graphscript/parse/token.h`
  - `include/graphscript/compile/compiler.h`
  - `include/graphscript/emit/emitter.h`
  - `src/parse/lexer.cpp`
  - `src/parse/parser.cpp`
  - `src/compile/compiler.cpp`
  - `src/emit/emitter.cpp`

- 本轮还纳入 `include/graphscript/core/source_range.h`。
  - 子代理 review 发现该文件未跟踪，但多个已迁移头文件依赖它；若不提交，干净 checkout 会编译失败。

## Review

子代理只读 review 结论：

- `include/src/cli/tests` 未发现旧 `graphscript/parse|compile|emit` include、旧 `ModuleNode`、`dump_ast` 或 legacy API 引用。
- `dump_ast` 无调用方。
- CMake 没有显式列出 legacy 文件，但旧 build 目录可能缓存删除前的文件列表；必须使用重新 configure 后的 `build-codex` 验证。
- 空目录已清理。
- 关键风险：`include/graphscript/core/source_range.h` 未跟踪但被多个头依赖；本轮已纳入提交。

## 验证

- Reconfigure：
  - `cmake -B build-codex -DCMAKE_BUILD_TYPE=Release`
  - 通过；证据：`evidence/phase5_loop021_configure.log`
- `gs_tests` build：
  - `cmake --build build-codex --config Release --target gs_tests -- /m:1`
  - 通过；证据：`evidence/phase5_loop021_build_gs_tests.log`
- Full C++：
  - `build-codex\Release\gs_tests.exe`
  - 272/272 通过；证据：`evidence/phase5_loop021_full_cpp.log`
- CLI build：
  - `cmake --build build-codex --config Release --target gs -- /m:1`
  - 通过；证据：`evidence/phase5_loop021_build_gs.log`
- 旧 include 扫描：
  - `rg -n '#include "graphscript/(parse|compile|emit)' include src cli tests -g '*.*'`
  - 无匹配；证据：`evidence/phase5_loop021_old_include_scan.log`
- 严格旧 API 扫描：
  - `rg -n "graphscript/(parse|compile|emit)|ModuleNode|\bdump_ast\b" include src cli tests -g '*.*'`
  - 无匹配；证据：`evidence/phase5_loop021_strict_old_api_scan.log`
- 删除状态扫描：
  - `git diff --name-status -- include/graphscript/parse include/graphscript/compile include/graphscript/emit src/parse src/compile src/emit`
  - 显示 legacy 文件删除；证据：`evidence/phase5_loop021_legacy_files_git_scan.log`
- Diff check：
  - `git diff --check -- include\graphscript\debug\dump.h src\debug\dump.cpp include\graphscript\core\source_range.h include\graphscript\parse include\graphscript\compile include\graphscript\emit src\parse src\compile src\emit`
  - 无 whitespace error，仅 Windows LF/CRLF 提示；证据：`evidence/phase5_loop021_diff_check.log`

## 结果

- 已完成本轮子目标：legacy parse/compile/emit 源码与头文件已删除，debug dump 不再依赖旧 AST。
- `include/src/cli/tests` 的旧 parse/compile/emit include 和 `ModuleNode`/`dump_ast` 扫描清零。
- 全量 C++ 与 CLI build 通过。

## 下一轮建议

Loop 022 进入 Phase 6/后续清理：扫描并处理 remaining legacy-only surface，包括文档中旧 `.sc/.d.sc`、旧架构图/开发指南的 parser/compiler/emitter 描述、测试 suite 命名噪声、`RuntimeGraph` replacement 前置评估，以及 webapp build/lint 验证。
