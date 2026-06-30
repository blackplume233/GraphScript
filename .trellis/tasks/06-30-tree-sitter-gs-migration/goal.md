---
status: active
updated_at: 2026-06-30
artifact_language: zh-CN
execution_mode: native-codex-goal
native_goal_id: codex_019f188c-0800-7041-9d48-9a868ddcb496
current_loop: loops/loop-024.md
next_sub_goal: Loop 024：补齐 GraphRuntimeIR bake 的 imported declaration / Environment pin enrichment，并清理剩余当前文档与前后端合约中的旧 compile/emit 命名噪声
last_verified_loop: loops/loop-023.md
references: references.md
---

# Auto Goal 任务

## 目标

- 基础目标: 按 `docs/plans/tree-sitter-gs-migration-plan.md` 完整完成 GraphScript 从旧手写图 DSL 到 tree-sitter asset `.gs/.d.gs` 新语法的迁移。范围包括 Phase 1-6：后缀迁移、最终 CLI 命令替换、旧功能清单、编辑器替换层、旧 parser/compiler/emitter 删除、legacy-only 代码删除。迁移允许破坏旧语法兼容，但必须用新方案重新满足 CLI 编辑器、Web 编辑器、source diagnostics、可视化图编辑、patch/apply、后端/前端 API 合约、runtime/graph projection 能力。
- 完成证据: `rg "\.d\.sc|\.sc\b"` 仅剩明确历史说明或无残留；旧 `include/graphscript/parse`, `src/parse`, `include/graphscript/compile`, `src/compile`, `include/graphscript/emit`, `src/emit` 及旧 parser/compiler/emitter 测试被删除或改写；`RuntimeGraph` 被新的 graph runtime IR 替换；CLI 最终命令为 `parse/lint/project/patch/edit/serve` 且不保留受支持的 `sc-*`；编辑器和 Web 回归能力仍可用；`docs/spec/legacy-graph-dsl-feature-inventory.md` 完成；构建、C++ 测试、webapp build/lint 和必要 smoke/视觉测试通过或记录可接受外部阻塞。

## 约束

- 文档语言: 中文优先；本目标文件、循环记录、最终报告默认 `zh-CN`。
- 回复语言: 中文优先。
- 执行过程中要充分利用SubAgent来为自己加速,调用子代理的时候试用Codex自带的子代理不要使用orca的子代理
- 技术要求: C++17、CMake、GoogleTest、tree-sitter asset grammar、React/Vite/TypeScript webapp；优先复用仓库既有模式，不引入不必要新依赖。
- 验证命令: `cmake -B build -DCMAKE_BUILD_TYPE=Release`；`cmake --build build --config Release`；`./build/Release/gs_tests.exe`；`rg "\.d\.sc|\.sc\b"`；`npm run build` 和 `npm run lint` 于 `webapp/`；必要时运行相关 `webapp/test_*.py` smoke/视觉回归。
- 产物目录约定: Auto Goal 状态文件放在 `.trellis/tasks/06-30-tree-sitter-gs-migration/`；迁移文档更新在 `docs/plans/`、`docs/spec/` 和根 `AGENTS.md`；代码遵循 `include/graphscript/**` 与 `src/**` 镜像结构。
- 依赖策略: 默认不新增外部运行时依赖；如 tree-sitter 生成工具或 npm lockfile 需要刷新，记录命令和结果。
- 安全边界: 不回滚用户已有未提交改动；不执行 `git reset --hard` 或破坏性清理；删除 legacy 文件前必须有新方案覆盖相同用户能力并通过相应测试；不推送远端。
- 不要再修改AGENTS.md了，当前都按照trellis来进行了，相关Spec应该遵循update-spec流程

## 执行指导

- 每轮开始前重新读取本文件和当前 Trellis PRD。
- 采用单一 Trellis task 内分阶段完成的方式执行，按 Phase 1-6 建立可验证检查点，不创建子任务，除非某个阶段过宽到无法在一个循环内验证。
- 每轮只选择一个有边界的子目标，先读取相关 spec/代码，再编辑。
- 优先保持仓库可构建；如果阶段中间必须短暂破坏构建，记录到 `loops/` 并尽快恢复。
- 循环细节记录到 `loops/loop-XXX.md`，错误路径和策略调整记录到 `references.md`，原始验证证据放到 `evidence/`。
- Q5 已决：选择方案 B，替换 `RuntimeGraph` 为新的 graph runtime IR，不保留长期兼容外壳。
- 如目标、完成证据或安全边界需要改变，先更新本文件并请求用户确认。
- 着重要求采用开发，测试，Review的循环来完成任务，测试和Review都要自动执行
- 每一轮都要进行commit
