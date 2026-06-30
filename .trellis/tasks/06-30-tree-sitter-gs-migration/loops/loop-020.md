# Loop 020：迁移深度集成/QA/Blueprint 测试到 asset 路径

## 子目标

继续 Phase 5，迁移 `test_blueprint_scenarios.cpp`、`test_integration_deep.cpp`、`test_qa_loop.cpp`、`test_qa_stress.cpp` 中旧 parse/compiler/emitter round-trip 依赖，优先拆出与 RuntimeGraph 强耦合较低的 asset fixture 回归。

## 本轮仓库基准检查

- 重新读取 `goal.md`。
- 沿用 backend/spec 约束：测试迁移必须覆盖当前 asset parser/projector/EditSession 能力，不再保留 legacy parser/compiler/emitter API 作为测试依赖。
- 使用 Codex 子代理做只读 review，重点审查四个大测试文件是否仍有旧 API，以及迁移是否过度削弱覆盖。

## 计划

1. 将四个旧集成/QA/Blueprint 测试文件改写为 asset/EditSession/RuntimeGraph 当前路径。
2. 保留测试 suite 名，减少测试索引和历史趋势冲击。
3. 跑 focused，按当前 RuntimeGraph 桥接契约修正断言。
4. Review 后补强 round-trip 结构比较、多图、多事件/函数、较大图压力覆盖。
5. 跑 full C++、CLI build、旧 include 扫描、diff check。

## 执行记录

- `tests/test_integration_deep.cpp`
  - 移除旧 lexer/parser/compiler/emitter helper。
  - 新增 asset 深度集成覆盖：
    - 三层 graph-as-node 链。
    - `asset::FlowGraphProjector` flow/data edge。
    - asset session → EditGraph → RuntimeGraph bake。
    - annotation/generate round-trip。
    - schema/default/multiple events/functions round-trip。
  - `expect_emit_reloads()` 从只比 graph 数扩展为比较 graph 名、params、nodes、events/functions、annotations、generate、flow/data link 数。

- `tests/test_blueprint_scenarios.cpp`
  - 移除旧 parser/compiler/emitter 和旧 scenario DSL。
  - 新增 asset Blueprint 覆盖：
    - gameplay event + data link round-trip。
    - math branch projection。
    - blueprint preset + RuntimeGraph bake。
    - multi-graph blueprint round-trip 与 graph-as-node 派生。

- `tests/test_qa_loop.cpp`
  - 移除旧 parser/compiler/emitter round-trip helper。
  - 新增 asset QA loop：
    - rename/annotation/mutation 后 emit/reload。
    - undo/redo 后 emit/reload。
    - 重复 edit/reload。
    - multi-graph + event/function + undo/redo。
  - round-trip helper 比较 graph 名、params、nodes、events/functions、flow/data link 数。

- `tests/test_qa_stress.cpp`
  - 移除旧 parser/compiler/emitter 压测路径。
  - 新增 asset 压力覆盖：
    - 350 节点 asset parser/projector 性能 sanity。
    - 120 节点 EditSession load/emit/reload。
    - 80 节点 RuntimeGraph bake。
  - 添加 `<functional>` 显式 include。
  - 记录当前 RuntimeGraph bake 只 materialize node-to-node data links；bare parameter data links 由 asset projection/module round-trip 覆盖。

## Review

只读 review 初版发现：

- 四个文件已无旧 parse/compiler/emitter include 和旧 helper/API。
- 初版覆盖从原大场景降为 smoke 级，不足以替代高层意图。
- round-trip helper 过宽，只比较 graph 数。
- RuntimeGraph data edge `0` 的断言需要说明语义。
- `test_qa_stress.cpp` 应显式 include `<functional>`。

本轮已采纳并补强：

- round-trip helper 改为比较结构数量。
- 增加 deep schema/default/multiple blocks。
- 增加 blueprint multi-graph 场景。
- 增加 QA multi-graph/multi-block/undo-redo 场景。
- stress 从 200 节点 projection 提升到 350 节点，并保留 120/80 节点 load/bake。
- RuntimeGraph data edge 现状加注释说明。
- 显式 include `<functional>`。

## 验证

- Focused：
  - `build-codex\Release\gs_tests.exe --gtest_filter="DeepCycle.*:Blueprint.*:QALoop.*:QAStress.*"`
  - 16/16 通过；证据：`evidence/phase5_loop020_focused.log`
- Full C++：
  - `build-codex\Release\gs_tests.exe`
  - 272/272 通过；证据：`evidence/phase5_loop020_full_cpp.log`
- CLI build：
  - `cmake --build build-codex --config Release --target gs -- /m:1`
  - 通过；证据：`evidence/phase5_loop020_build_gs.log`
- 旧 include 扫描：
  - `rg -n '#include "graphscript/(parse|compile|emit)' tests cli\editor.cpp cli\source_diagnostics.cpp -g '*.cpp'`
  - 无输出；证据：`evidence/phase5_loop020_old_include_scan.log`
- 宽旧依赖扫描：
  - `rg -n "graphscript/(parse|compile|emit)|parse_text|ModuleNode|\bCompiler\b|\bParser\b|\bLexer\b|Emitter" tests cli\editor.cpp cli\source_diagnostics.cpp -g '*.cpp'`
  - 无旧 API 输出；证据：`evidence/phase5_loop020_old_dep_scan.log`
- Diff check：
  - `git diff --check -- tests\test_blueprint_scenarios.cpp tests\test_integration_deep.cpp tests\test_qa_loop.cpp tests\test_qa_stress.cpp`
  - 无 whitespace error，仅 Windows LF/CRLF 提示；证据：`evidence/phase5_loop020_diff_check.log`

## 结果

- 已完成本轮子目标：四个深度集成/QA/Blueprint 测试文件不再依赖旧 parse/compiler/emitter，全部改为 asset parser/projector/EditSession/RuntimeGraph 当前路径。
- 测试与 CLI 目标范围内旧 `graphscript/parse|compile|emit` include 扫描清零。
- Full C++ 测试通过。测试总数从 308 变为 272，是因为旧 legacy-only fixture/round-trip 场景被收敛为当前 asset 等价覆盖。

## 下一轮建议

Loop 021 继续 Phase 5：在测试层旧 parser/compiler/emitter 依赖清零后，处理生产和库层旧实现删除准备。先扫描 `src/parse`、`src/compile`、`src/emit`、`include/graphscript/parse`、`include/graphscript/compile`、`include/graphscript/emit`、`src/debug/dump.cpp`、CMake glob 影响和 docs 旧引用；若生产代码已无必要依赖，删除 legacy 源码并跑全量验证。
