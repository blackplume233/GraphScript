# Loop 024 子代理审查：Laplace

## 范围

只读审查 `GraphRuntimeIR` Environment pin enrichment、CLI bake、source diagnostics 前后端合约、当前 spec 残留旧 compile/emit 命名。

## 必须修复项与处理

- `GraphRuntimeIR` env enrichment 不能只处理 `en.pins.empty()`：已改为先保留 projected pins，再按 pin name 从 `Environment::nodes()` 合并缺失 pins。
- `uint8_t` pin index 存在截断风险：已将 runtime pin index 和 edge pin fields 提升到 `uint32_t`，并用 `GraphRuntimeIR::invalid_pin_index` 表示失败。
- invalid pin edge 不应进入 IR：已在 bake 阶段跳过任一端 pin unresolved 的 flow/data edge。
- webapp mock 仍使用 `"stage": "compiler"`：已同步为 `"asset"`。
- UI 文案仍有 “Apply compile”：已改为 source diagnostics/apply 使用当前 session Environment。

## 建议项与处理

- CLI `bake` 使用 `session_.env()`：已确认并保留。
- 补 projected pins empty 测试：已新增 `BakeFillsMissingPinsFromEnvironment`。
- 补 partial pins 测试：已新增 `BakeMergesPartialProjectedPinsFromEnvironment`。
- 补 invalid pin edge 行为：已新增 `BakeSkipsEdgesWithUnresolvedPins`。
- 防止 `compile_error` 回流：已通过 C++/webapp 合约改名和残留扫描验证。

## 可接受残留

- 历史迁移清单和旧循环日志中的 parser/compiler/emitter 说明保留为历史上下文。
- REPL 内部 `emit` / `bake` 命令不是 top-level `gs emit` / `gs bake` 支持面，未在本轮删除。

## 建议验证

- `cmake --build build-codex --config Release --target gs_tests -- /m:1`
- `.\build-codex\Release\gs_tests.exe --gtest_filter="GraphRuntimeIR.*:SourceDiagnostics.*"`
- `npm run build` / `npm run lint` in `webapp/`
- runtime / compile contract / old compile-emitter residual scans
