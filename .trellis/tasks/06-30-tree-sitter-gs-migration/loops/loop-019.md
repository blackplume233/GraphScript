# Loop 019：迁移 lexer/parser/compiler 单元测试到 asset 路径

## 子目标

继续 Phase 5，迁移 `tests/test_lexer.cpp`、`tests/test_parser.cpp`、`tests/test_compiler.cpp` 中旧手写 lexer/parser/compiler 覆盖到 tree-sitter asset parser/linter/projector 与 `EditSession` 路径，优先减少可独立迁移的旧单元测试。

## 本轮仓库基准检查

- 重新读取 `goal.md`。
- 沿用 loop018 已读取的 backend/spec/guides 约束：C++17、GoogleTest、asset parser/projector、CLI-first、round-trip/diagnostic 覆盖，且不回滚用户已有改动。
- 使用 Codex 子代理做只读 review，重点审查本轮三测试文件是否仍有旧 API、覆盖是否过宽、是否需要补 focused 回归。

## 计划

1. 将 `test_lexer.cpp` 从旧 token 流测试改为 asset parser 词法等价 smoke：空白、import/string、数值/bool/null/ref/call、attribute/array/inline object、错误 range。
2. 将 `test_parser.cpp` 从旧 `ModuleNode` AST 测试改为 asset declaration/module/graph/projection/generate/diagnostic 测试。
3. 将 `test_compiler.cpp` 从旧 `Compiler` 测试改为 `EditSession::load_import/load_source` 与 `asset::FlowGraphProjector` 的环境/投影/graph-as-node/错误路径测试。
4. 跑 focused，修正与真实 asset AST/adapter 不一致的测试断言。
5. Review 后补强过宽断言。
6. 跑 full C++、CLI build、旧 include 扫描、diff check。

## 执行记录

- `tests/test_lexer.cpp`
  - 移除 `graphscript/parse/lexer.h` include。
  - 改为通过 `asset::Parser` 覆盖 asset source 的词法/基础表达式表面。
  - 覆盖：
    - 空输入/空白注释输入。
    - import 路径与字符串转义保留。
    - int/float/bool/null/asset ref/call expression。
    - attribute、array、inline object。
    - 语法错误 diagnostic range。

- `tests/test_parser.cpp`
  - 移除旧 `Lexer/Parser/ModuleNode` helper。
  - 保留 preset 文件读取。
  - 改为覆盖：
    - `export declare type/object/schema/command`。
    - `ue_core.d.gs`、`htn_nodes.d.gs`、`task_nodes.d.gs`、`levelscript_nodes.d.gs` parse count。
    - import、top-level const、graph block。
    - `FlowGraphProjector` 的 param/node/flow/data edge/block。
    - generate comment/metadata projection 与精确 source span。
    - syntax/lint diagnostics。

- `tests/test_compiler.cpp`
  - 移除旧 parser/compiler include 和旧 fixture compiler helper。
  - 改为通过 `EditSession` 验证 asset declaration import 填充 `Environment`：
    - constructible/non-constructible types。
    - native node pins/fields。
    - schema policy 和 schema field value。
  - 通过 `asset::FlowGraphProjector` 验证 graph projection。
  - 通过 `EditSession::load_source()` 验证 graph-as-node 派生：
    - input/output/event pin kind/direction。
    - `@graph.var` param 不派生 pin。
  - 错误引用当前在 asset projection/load 阶段被拒绝，测试断言 `load_source` 返回 error。

## Review

子代理 review 结论：

- 三个目标文件内没有旧 `graphscript/parse`、`graphscript/compile`、`graphscript/emit` include，也没有旧 `Lexer` / legacy `Parser` / `Compiler` 直接 API。
- 初版迁移方向合理，但覆盖偏宽，建议补：
  - preset count 与 attribute guard。
  - graph-as-node pin kind/direction 与 var 排除。
  - generate exact span。
  - schema/import 更具体断言。

本轮已采纳并补强：

- parser preset count。
- declaration attribute guard。
- generate block/comment/metadata exact span。
- import type/node/pin/field 断言。
- graph-as-node pin 数、kind、direction 与 var 排除。
- projector node properties guard。

未采纳：

- `HTNGraph.allowed_node_tags` 断言。当前 asset declaration import adapter 只把 schema properties 保留到 `schema.fields` 并解析核心 policy，不填旧 `GraphSchema::allowed_node_tags`，因此本轮不把它写成迁移测试契约。

## 验证

- Focused build/test：
  - `cmake --build build-codex --config Release --target gs_tests -- /m:1`
  - `build-codex\Release\gs_tests.exe --gtest_filter="Lexer.*:Parser.*:Compiler.*"`
  - 18/18 通过；证据：`evidence/phase5_loop019_focused.log`
- Full C++：
  - `build-codex\Release\gs_tests.exe`
  - 308/308 通过；证据：`evidence/phase5_loop019_full_cpp.log`
- CLI build：
  - `cmake --build build-codex --config Release --target gs -- /m:1`
  - 通过；证据：`evidence/phase5_loop019_build_gs.log`
- 旧 include 扫描：
  - `rg -n '#include "graphscript/(parse|compile|emit)' tests cli\editor.cpp cli\source_diagnostics.cpp -g '*.cpp'`
  - `test_lexer.cpp`、`test_parser.cpp`、`test_compiler.cpp` 不再出现旧 include；剩余旧 include 在 `test_blueprint_scenarios.cpp`、`test_integration_deep.cpp`、`test_qa_loop.cpp`、`test_qa_stress.cpp`；证据：`evidence/phase5_loop019_old_include_scan.log`
- 宽旧依赖扫描：
  - `rg -n "graphscript/(parse|compile|emit)|parse_text|ModuleNode|\bCompiler\b|\bParser\b|\bLexer\b|Emitter" tests cli\editor.cpp cli\source_diagnostics.cpp -g '*.cpp'`
  - 证据：`evidence/phase5_loop019_old_dep_scan.log`
- Diff check：
  - `git diff --check -- tests\test_lexer.cpp tests\test_parser.cpp tests\test_compiler.cpp`
  - 无 whitespace error，仅 Windows LF/CRLF 提示；证据：`evidence/phase5_loop019_diff_check.log`

## 结果

- 已完成本轮子目标：`test_lexer.cpp`、`test_parser.cpp`、`test_compiler.cpp` 不再依赖旧手写 lexer/parser/compiler API，并改为 asset parser/projector/EditSession 覆盖。
- Full C++ 测试通过。测试总数从 348 降到 308，是因为三个文件删除了大量 legacy-only token/AST/compiler 用例，保留的是当前 asset 等价契约。
- 剩余旧测试依赖集中在：
  - `tests/test_blueprint_scenarios.cpp`
  - `tests/test_integration_deep.cpp`
  - `tests/test_qa_loop.cpp`
  - `tests/test_qa_stress.cpp`

## 下一轮建议

Loop 020 继续 Phase 5：迁移深度集成和 QA/Blueprint 测试中旧 parse/compiler/emitter round-trip 依赖。优先拆出与 RuntimeGraph 强耦合较低的 fixture round-trip 测试，再处理 `RuntimeGraph` replacement 前暂时无法纯 asset 化的 bake/performance 场景。
