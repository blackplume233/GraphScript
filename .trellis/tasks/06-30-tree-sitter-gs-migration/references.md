# 参考记录

## 仓库基准摘要

- 语言偏好: 仓库 `AGENTS.md` 要求文档及回答中文优先；Auto Goal artifact language 为 `zh-CN`。
- 技术栈: C++17 + CMake + GoogleTest；tree-sitter asset grammar；CLI/Web server；`webapp/` 使用 React、Vite、TypeScript、ESLint。
- 核心原则: 文本与图等价、domain agnostic、Graph-as-Node、scope isolation、CLI-first。
- 验证命令: `cmake -B build -DCMAKE_BUILD_TYPE=Release`；`cmake --build build --config Release`；`./build/Release/gs_tests.exe`；`rg "\.d\.sc|\.sc\b"`；`npm run build`/`npm run lint` 于 `webapp/`；必要 webapp smoke/视觉测试。
- 安全边界: 工作区已有大量未提交改动，执行时只修改本任务相关文件；不回滚不属于本轮的用户改动；删除旧代码前必须先建立新方案覆盖。

## 已接受决策

- 执行组织: 单一 Trellis task 内分阶段完成，每阶段保持可验证。
- 迁移范围: 完整完成计划 Phase 1-6，不只做后缀或 CLI 切片。
- Q5: 选择方案 B，替换 `RuntimeGraph` 为新的 graph runtime IR。

## 错误路径

-

## 失败计划

-

## 有用模式

- 每个阶段先做可机械验证的扫描或测试入口，再删除 legacy 文件，减少大范围迁移中的回归定位成本。
- 对 `.sc/.d.sc` 迁移使用 `rg "\.d\.sc|\.sc\b"` 做强制残留扫描；历史说明如必须保留，应明确标注为历史/迁移记录。
- canonical `presets/*.d.gs` 已迁到 asset declaration syntax 后，测试不能再把这些真实 preset 当旧 parser/compiler 输入。旧 `.gs` pipeline 测试如只需要预加载 Environment，应通过 `EditSession::load_import()` 加载真实 preset；旧 parser/compiler/emitter 单元测试如仍需覆盖 legacy 行为，应使用内联 legacy fixture，避免反向恢复 preset fallback 依赖。

## 外部参考经验

- 待循环开始后按需审查 Ponytail/SkillOpt 本地或上游资料；当前草案阶段未引入外部执行策略。

## 子代理发现

- Loop 008 / Franklin：公共 `Module` / `ImportDecl` / `LetDecl` 已适合放入 `include/graphscript/core/module.h`；`Emitter`、`EditSession`、`debug::dump_module` 不应为这些类型传播 `compile/compiler.h`。删除旧 parser/compiler/emitter 前的明显 blocker 仍包括：`src/edit/edit_session.cpp` 旧 parser/compiler fallback；`cli/main.cpp` 的 `edit/serve` import 加载；`cli/editor.cpp` source patch/range 逻辑依赖旧 `ModuleNode`/`Compiler`；核心 `SourceRange` 仍来自 `parse/token.h`；旧 parser/compiler/emitter 测试与 `RuntimeGraph` 仍未迁移。
- Loop 009 / Newton：`SourceLocation` / `SourceRange` 已迁到 `core/source_range.h` 且无重复定义/ODR 风险；非 parse public headers 不再依赖 `parse/token.h`。交付时注意新增 public headers 不能漏提交：`core/source_range.h`、`core/module.h`、`parse/token_type.h`。
- Loop 010 / Dewey：`cli/main.cpp` 的 `edit` / `serve` import 预加载已统一调用 `EditSession::load_import`，旧 parse/compile/emit 依赖扫描干净；真实 CLI smoke 覆盖 `edit -I missing` warning 后继续进入 REPL。后续 blocker：`EditSession::load_import` 的 asset 判定范围仍偏窄，只含 `module`、`enum`、`block_kinds`、`commands` 或 `lints` 的 `.d.gs` 会落入旧 parser/compiler fallback。
- Loop 014：非 CLI `load_import` 声明测试和 `mixed_declarations.d.gs` 已迁到 asset declaration；asset declaration 现在带出声明注解、name/type/source range 和 schema constructor call range。阻止删除 `EditSession::load_import` 旧 fallback 的剩余 blocker 已缩小到 CLI import node/schema/type rename：`cli/editor.cpp` 中这些命令仍直接使用旧 Lexer/Parser/Compiler 修补声明文件，`tests/test_cli_editor.cpp` 对应临时 `.d.gs` 仍使用旧 `declare Node/Schema/type` 文本。下一轮必须先迁移这些命令到 asset declaration patch/parse path，再删除 fallback。
- Loop 015：CLI import node/pin/schema/schema-field/type rename 已迁到 asset declaration patch/parse/reload path；`EditSession::load_import` 的旧 parser/compiler fallback 已删除，非 asset declaration import 现在返回显式错误。剩余旧 parser/compiler 迁移阻塞转移到 `EditSession::load_source` 的旧兼容 fallback，以及 CLI source/files rename 的 active source graph 路径仍依赖旧 `ModuleNode`/`Compiler`/旧 `.gs` 测试形态。
- Loop 024 / Laplace：`GraphRuntimeIR` Environment enrichment 必须按 pin name 合并 projected pins 和 `Environment::nodes()` pins，不能只处理全空 projected pins；runtime pin index 使用 `uint32_t`，invalid pin edge 不写入 IR；source diagnostics 前后端合约使用 asset/semantic 术语，不再暴露 `compile_error` 或 `"stage": "compiler"`。
