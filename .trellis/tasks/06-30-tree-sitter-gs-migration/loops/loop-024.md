# Loop 024：补齐 GraphRuntimeIR Environment pin enrichment 并清理旧 compile/emit 合约噪声

## 基础目标切片

补齐 Loop 023 留下的 runtime bake import/preset pin 缺口，并清理当前前后端诊断合约和当前 spec 中仍会误导实现的旧 compile/emit 术语。

## 子目标

- `GraphRuntimeIR::bake()` 增加 `Environment` overload，用 loaded import / preset 声明补齐 asset projection 缺失的 pins。
- CLI editor `bake` 使用 session environment。
- 诊断响应不再暴露旧 `compile_error` / `"stage": "compiler"` 合约。
- 当前 `docs/spec` 不再描述旧 handwritten lexer/parser/compiler/emitter pipeline。

## 仓库基准检查

- 每轮开始已重读 `goal.md`、Trellis backend/frontend spec、cross-layer guide。
- 本轮触及 C++ runtime/CLI/tests、webapp TypeScript contract、当前 spec 文档，必须跑 C++ build/tests、webapp build/lint 和残留扫描。
- 工作区已有大量既有脏文件；本轮只暂存 Loop 024 相关文件和证据，不回滚用户改动。

## 执行记录

- `GraphRuntimeIR`：
  - 新增 `GraphRuntimeIR::bake(const asset::FlowGraph&, const Environment&)`。
  - `GraphRuntimeIR::bake_impl()` 先保留 projection pins，再按 pin name 从 `Environment::nodes()` 补齐缺失 pins。
  - `RuntimeIRPin::pin_index`、edge pin index 从 `uint8_t` 提升到 `uint32_t`，并引入 `invalid_pin_index`。
  - flow/data edge 仅在两端 node 和 pin 都解析成功时写入 IR，避免 `0xFF`/invalid pin 混入 runtime edge。
- CLI：
  - `cli/editor.cpp` 的 `bake` 改为 `GraphRuntimeIR::bake(projected.value(), session_.env())`。
- 测试：
  - `tests/test_graph_runtime_ir.cpp` 新增 env 全缺补齐、partial projected pins 合并、unresolved pin edge 跳过覆盖。
  - blueprint/deep/stress runtime bake 测试改用 env overload，并断言 pins 数量。
  - debug diagram 测试名从 legacy emitter 术语改为当前 EditSession graph 语义。
- 前后端诊断合约：
  - import semantic failure 状态从 `compile_error` 改为 `semantic_error`，错误码从 `GS_IMPORT_COMPILE_ERROR` 改为 `GS_IMPORT_SEMANTIC_ERROR`。
  - `DiagnosticsResponse.stage` 从 `compiler` 改为 `asset`。
  - webapp import status 样式和 E2E mock 同步为 `semantic_error` / `asset`。
  - source environment notice 去掉 “Apply compile” 文案。
- 文档：
  - `docs/spec/architecture.md`、`docs/spec/development-guide.md`、`docs/spec/dsl-reference.md`、`docs/spec/scope-rules.md` 改为 asset parser / lint / projection / source emission 术语。

## 子代理审查

- Codex 子代理 Laplace 做只读审查，指出：
  - 不能只在 projected pins 全空时 enrichment，必须按 pin name 合并。
  - `uint8_t` pin index 有截断风险。
  - invalid pin edge 不应进入 runtime IR。
  - webapp mock 和文案仍有旧 compiler/compile 噪声。
- 这些发现已全部接受并修复；摘要见 `subagents/loop-024-review.md`。

## 验证

- `cmake --build build-codex --config Release --target gs_tests -- /m:1`：通过，见 `evidence/phase6_loop024_build_gs_tests.log`；文档/测试名调整后重建通过，见 `evidence/phase6_loop024_build_gs_tests_after_docs.log`。
- `cmake --build build-codex --config Release --target gs -- /m:1`：通过，见 `evidence/phase6_loop024_build_gs.log`。
- 聚焦测试 `GraphRuntimeIR.*:SourceDiagnostics.*:Blueprint.AssetRuntimeBakeUsesBlueprintPresets:DeepCycle.GraphRuntimeIRBakeFromAssetProjection:QAStress.GraphRuntimeIRBakeLargeAssetProjection`：27/27 通过，见 `evidence/phase6_loop024_focused_runtime_enrichment.log`。
- 全量 C++ 测试：276/276 通过，见 `evidence/phase6_loop024_full_cpp_after_docs.log`。
- `webapp` `npm run build`：通过，见 `evidence/phase6_loop024_webapp_build.log`。
- `webapp` `npm run lint`：通过，见 `evidence/phase6_loop024_webapp_lint.log`。
- `RuntimeGraph` 活跃残留扫描：`NO_MATCHES`，见 `evidence/phase6_loop024_active_runtimegraph_scan_after_docs.log`。
- 旧 compile diagnostics contract 扫描：`NO_MATCHES`，见 `evidence/phase6_loop024_compile_contract_scan.log`。
- 当前 spec/cli/tests/webapp 旧 compile/emit pipeline 扫描：`NO_MATCHES`，见 `evidence/phase6_loop024_old_compile_emit_scan_after_docs.log`。

## 结果

Loop 024 子目标完成。Runtime bake 现在能消费 import/preset 已加载到 `Environment` 的 node pins，前后端 source diagnostics 合约不再暴露旧 compiler 命名，当前 spec 不再描述已删除的 handwritten compile/emit pipeline。

## 后续风险

- 仍需做最终完成审计：确认 `.sc/.d.sc`、旧 `parse/compile/emit` 目录、旧 top-level CLI command、legacy parser/compiler/emitter 测试、webapp smoke/视觉必要证据都满足 `goal.md` 完成证据。

## 回写

- `goal.md` 下一轮推进到 Loop 025：最终完成审计与必要收尾修复。
