# Loop 018：迁移 emitter 测试与 CLI diagram 到 asset 路径

## 子目标

继续 Phase 5，迁移旧 parser/compiler/emitter 测试和 fixtures 的一块可验证切片：先移除 `tests/test_emitter.cpp` 对旧 parser/compiler/emitter 的直接依赖，并收敛 `cli/editor.cpp` 的 Mermaid diagram 旧 emitter 用途。

## 本轮仓库基准检查

- 重新读取 `goal.md` 和 `prd.md`。
- 读取 `trellis-before-dev`，并补读 backend index、`docs/spec/index.md`、`docs/spec/architecture.md`、`docs/spec/development-guide.md`、`docs/spec/scope-rules.md`、backend quality guidelines、shared guides index。
- 使用 Codex 子代理做只读审查，确认旧依赖集中在 parser/compiler/emitter 测试、深度集成/QA 测试和 fixtures；建议本轮先迁 `test_emitter.cpp` 与 CLI diagram。

## 计划

1. 将 Mermaid diagram 输出从旧 `Emitter` 拆到 `debug` helper。
2. 让 CLI `diagram` 优先使用 `asset::Parser + asset::FlowGraphProjector` 生成 Mermaid。
3. 将 `tests/test_emitter.cpp` 改写为 `EditSession::load_source -> emit -> load_source` 的 asset round-trip 测试。
4. 增加 focused debug/CLI diagram 回归。
5. 跑 focused、全量 C++、CLI 构建、旧 include 扫描和 diff check。
6. 将 diagram 迁移规则回写到 backend quality spec。

## 执行记录

- 新增 `include/graphscript/debug/diagram.h`、`src/debug/diagram.cpp`。
  - 提供 `debug::emit_mermaid_graph_diagram(const Graph&)`，作为桥接期旧 session graph fallback。
  - 提供 `debug::emit_mermaid_flow_graph_diagram(const asset::FlowGraph&)`，作为当前 CLI diagram 的主路径。
- 更新 `cli/editor.cpp`。
  - 移除 `graphscript/emit/emitter.h` include。
  - `cmd_diagram()` 先用当前 `session_.emit()` 解析 asset source，再按 active graph 名调用 `asset::FlowGraphProjector::project()`。
  - asset projection 成功时输出 asset Mermaid；失败时打印 warning 并回退到 session graph Mermaid。
- 重写 `tests/test_emitter.cpp`。
  - 不再 include `graphscript/parse/*`、`graphscript/compile/*`、`graphscript/emit/*`。
  - 保留历史 `Emitter` test suite 名，但内容改成 asset canonical emit/round-trip：
    - minimal graph/import/param/node/connect/bind
    - schema directive
    - top-level const body
    - generate comment/metadata
    - annotation constructor expression
    - constructor string arguments
    - function 与 `@graph.var` param
- 新增 `tests/test_debug_diagram.cpp`。
  - 覆盖 session graph fallback helper。
  - 覆盖 asset `FlowGraph` Mermaid helper。
- 更新 `tests/test_cli_editor.cpp`。
  - 新增 `CLIEditor.DiagramCommandUsesAssetProjection`，捕获 stdout，确认无 fallback warning，输出 asset 投影 Mermaid。
- 更新 `.trellis/spec/backend/quality-guidelines.md`。
  - 新增 Common Mistake：CLI diagram 不应重新使用 legacy emitter，应优先 asset parser/projector。

## Review

子代理只读审查结论：

- `test_emitter.cpp`、`cli/editor.cpp diagram` 是本轮最小安全切片。
- `test_blueprint_scenarios.cpp`、`test_integration_deep.cpp`、`test_qa_loop.cpp`、`test_qa_stress.cpp` 与 RuntimeGraph/EditGraph/旧 fixtures 绑定更深，应作为后续循环处理。
- CLI diagram 的最终目标应是 asset `FlowGraph` helper，而不只是从旧 `Emitter` 搬到旧 `Graph` helper。

本轮据此补做 asset `FlowGraph` Mermaid overload 和 CLI focused 回归。

## 验证

- `cmake --build build-codex --config Release --target gs_tests`
  - 通过；证据：`evidence/phase5_loop018_build_gs_tests.log`
- Focused 回归：
  - `build-codex\Release\gs_tests.exe --gtest_filter="Emitter.*:DebugDiagram.*:CLIEditor.DiagramCommandUsesAssetProjection"`
  - 10/10 通过；证据：`evidence/phase5_loop018_focused.log`
- Full C++：
  - `build-codex\Release\gs_tests.exe`
  - 348/348 通过；证据：`evidence/phase5_loop018_full_cpp.log`
- CLI build：
  - `cmake --build build-codex --config Release --target gs`
  - 通过；证据：`evidence/phase5_loop018_build_gs.log`
- 旧 include 扫描：
  - `rg -n '#include "graphscript/(parse|compile|emit)' tests cli\editor.cpp cli\source_diagnostics.cpp -g '*.cpp'`
  - `cli/editor.cpp`、`tests/test_emitter.cpp` 不再出现旧 include；剩余旧 include 在 `test_lexer.cpp`、`test_parser.cpp`、`test_compiler.cpp`、`test_blueprint_scenarios.cpp`、`test_integration_deep.cpp`、`test_qa_loop.cpp`、`test_qa_stress.cpp`；证据：`evidence/phase5_loop018_old_include_scan.log`
- 宽旧依赖扫描：
  - `rg -n "graphscript/(parse|compile|emit)|parse_text|ModuleNode|\bCompiler\b|\bParser\b|\bLexer\b|Emitter" tests cli\editor.cpp cli\source_diagnostics.cpp -g '*.cpp'`
  - 仍包含 asset `Parser` 与测试 suite 名噪声；用于后续定位；证据：`evidence/phase5_loop018_old_dep_scan.log`
- Diff check：
  - `git diff --check -- include\graphscript\debug\diagram.h src\debug\diagram.cpp cli\editor.cpp tests\test_debug_diagram.cpp tests\test_emitter.cpp tests\test_cli_editor.cpp`
  - 无 whitespace error，仅 Windows LF/CRLF 提示；证据：`evidence/phase5_loop018_diff_check.log`

## 结果

- 已完成本轮子目标：
  - `tests/test_emitter.cpp` 从旧 parser/compiler/emitter 测试迁为 asset emit round-trip 测试。
  - CLI diagram 不再 include 或调用 legacy `Emitter`，主路径改为 asset projection Mermaid。
  - 新增 debug/CLI focused 回归，full C++ 通过。
- 旧 `include/graphscript/emit` / `src/emit` 仍未删除，因为深度集成、QA、Blueprint 测试仍引用旧 emitter，且 parser/compiler/lexer 测试仍未迁移。

## 下一轮建议

Loop 019 继续 Phase 5：迁移 `tests/test_parser.cpp`、`tests/test_compiler.cpp`、`tests/test_lexer.cpp` 中旧手写 parser/compiler/lexer 覆盖到 asset parser/linter/projector 或明确删除 legacy-only 用例；优先减少可独立迁移的单元测试，再处理与 RuntimeGraph/fixtures 深度耦合的集成和 QA 压测。
