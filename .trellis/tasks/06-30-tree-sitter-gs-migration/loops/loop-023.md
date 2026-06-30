# Loop 023：替换 RuntimeGraph 为 GraphRuntimeIR

## 基础目标切片

完成 Q5 决策的第一步：旧 public `RuntimeGraph` 不再作为 runtime 类型存在，新的 graph runtime IR 消费 tree-sitter asset projection，而不是消费 `EditGraph`。

## 子目标

- 将旧 `include/graphscript/runtime/runtime_graph.h`、`src/runtime/runtime_graph.cpp`、`tests/test_runtimegraph.cpp` 迁移为 graph runtime IR 命名和路径。
- `GraphRuntimeIR::bake()` 输入改为 `asset::FlowGraph`。
- CLI bake、debug dump、runtime tests 和集成测试改到 `GraphRuntimeIR`。
- 活跃代码/测试/当前规范中不再出现旧 `RuntimeGraph`/`runtime_graph` 标识；历史清单可保留。

## 仓库基准检查

- 每轮开始已重读 `goal.md`。
- 新增/移动 `include/src/tests` 文件后必须重新 `cmake -B build-codex -DCMAKE_BUILD_TYPE=Release`。
- 不删除 `EditSession::build_edit_graph()`；它仍支撑 editor/validation 路径，本轮只保证 runtime bake 不再消费它。

## 执行记录

- 移动并重命名：
  - `include/graphscript/runtime/runtime_graph.h` -> `include/graphscript/graph/runtime_ir.h`
  - `src/runtime/runtime_graph.cpp` -> `src/graph/runtime_ir.cpp`
  - `tests/test_runtimegraph.cpp` -> `tests/test_graph_runtime_ir.cpp`
- 新 runtime 类型：
  - `GraphRuntimeIR`
  - `RuntimeIRNode`
  - `RuntimeIRPin`
  - `RuntimeIRFlowEdge`
  - `RuntimeIRDataEdge`
- `GraphRuntimeIR::bake(const asset::FlowGraph&)` 通过 asset projection 的 node alias、pin definition、flow/data edge endpoint 构建 flat runtime arrays。
- `cli/editor.cpp` 的 `bake` 命令改为 `session_.emit()` -> `asset::Parser` -> `FlowGraphProjector::project()` -> `GraphRuntimeIR::bake()`，help 文案改为 graph runtime IR。
- `debug::dump_runtime_graph` 改为 `debug::dump_graph_runtime_ir`，输出 `GraphRuntimeIR`。
- `tests/test_graph_runtime_ir.cpp` 改为直接解析 asset source、投影 `FlowGraph`、bake `GraphRuntimeIR`，并新增 debug dump 覆盖。
- `tests/test_blueprint_scenarios.cpp`、`tests/test_integration_deep.cpp`、`tests/test_qa_stress.cpp` 的 runtime bake 路径改为 asset projection。
- `tests/test_edit_session.cpp` 中旧 `BuildEditGraphAndBake` 测试名改为 `BuildEditGraphAndValidate`。
- `docs/spec/architecture.md`、`docs/spec/development-guide.md`、`docs/spec/ai-native-asset-format.md` 中当前 runtime 术语更新为 `GraphRuntimeIR`。
- 子代理 Tesla 只读审查确认该边界合理，并指出 import/preset pin enrich 是后续独立任务。

## 验证

- `cmake -B build-codex -DCMAKE_BUILD_TYPE=Release`：通过，见 `evidence/phase6_loop023_reconfigure.log`。
- 初次并行构建 `gs_tests` 和 `gs` 时，`gs_tests` 因两个 MSBuild 进程同时写 `runtime_ir.obj` 出现 Windows `Permission denied`，见 `evidence/phase6_loop023_build_gs_tests_initial.log`；顺序重跑通过，见 `evidence/phase6_loop023_build_gs_tests_after_parallel_collision.log`。
- `cmake --build build-codex --config Release --target gs -- /m:1`：通过，见 `evidence/phase6_loop023_build_gs_initial.log` 和 `evidence/phase6_loop023_build_gs_final.log`。
- `cmake --build build-codex --config Release --target gs_tests -- /m:1`：最终通过，见 `evidence/phase6_loop023_build_gs_tests_final.log` 和 `evidence/phase6_loop023_build_gs_tests_after_test_names.log`。
- 聚焦 runtime 测试最终通过：14/14，见 `evidence/phase6_loop023_focused_runtime_tests_after_rebuild.log`。
- 全量 C++ 测试最终通过：273/273，见 `evidence/phase6_loop023_full_cpp_after_test_names.log`。
- 活跃代码/测试/当前规范旧 `RuntimeGraph` 扫描为 `NO_MATCHES`，见 `evidence/phase6_loop023_active_runtimegraph_scan_after_names.log`。
- `git diff --check`：无 whitespace error，仅 CRLF/LF 提示。

## 结果

旧 public `RuntimeGraph` 已从活跃代码、测试、CLI 和当前规范中移除；runtime bake 主路径现在消费 `asset::FlowGraph`。`EditGraph` 仍保留在 editor/validation 层，不再是 runtime bake 的 public input。

## 后续风险

- `FlowGraphProjector` 当前只从同一个 asset module 的 `export declare object` 收集 pins；CLI/editor 使用 import/preset 时，runtime IR 可能得到 nodes/edges，但 pin array 不完整或 edge pin index 为 `0xFF`。下一轮应建立 projection enrich 层，用 `FlowGraph + Environment/registry` 补齐 node pin definitions，再交给 `GraphRuntimeIR::bake()`。

## 回写

- `goal.md` 下一轮推进到 Loop 024：补齐 GraphRuntimeIR bake 的 imported declaration / environment pin enrichment，并清理剩余当前文档/合约中的旧 compile/emit 命名噪声。
