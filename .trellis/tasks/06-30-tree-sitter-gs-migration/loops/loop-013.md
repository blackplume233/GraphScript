# Loop 013：preset `.d.gs` 迁到 asset declaration syntax

## 基础目标切片

Phase 5：把 `presets/*.d.gs` 从旧 `declare type/Node/Schema` 语法迁到 tree-sitter asset declaration syntax，解除真实 preset 对 `EditSession::load_import` 旧 parser/compiler fallback 的依赖。

## 子目标

- 迁移 `presets/ue_core.d.gs`、`ue_blueprint.d.gs`、`task_nodes.d.gs`、`levelscript_nodes.d.gs`、`htn_nodes.d.gs`。
- 保留旧 preset 的语义：constructible type、node field 默认值、exec/data pins、schema policy。
- 更新测试，使真实 preset 通过 asset parser / `EditSession::load_import` 覆盖；旧 parser/compiler/emitter 测试不再把 canonical preset 当旧 DSL 输入。

## 仓库基准检查

- 已重新读取 `goal.md` 与 `prd.md`。
- 已读取 `auto-goal`、`trellis-before-dev`、`trellis-check`。
- 已按 backend spec 检查 C++17、显式错误、domain-agnostic、GoogleTest 覆盖要求。
- 本轮未新增外部依赖，未修改 `AGENTS.md`。

## 计划

1. 补 asset declaration 注册语义：`type X: constructible` 映射到 `TypeInfo.constructible`。
2. 允许 asset object 中普通 field 与同名 flow pin 共存，用于保留旧 preset 的 field 默认值 + data pin 模型。
3. 机械迁移真实 preset 文件。
4. 更新测试加载路径：真实 preset 走 asset import，旧 compiler/emitter 单元测试使用内联 legacy fixture。
5. 运行 focused、CLI parse/lint smoke、全量 C++ 测试和 review scan。

## 执行记录

- `include/graphscript/asset/language.h` / `src/asset/language.cpp`
  - `SymbolDecl` 增加 `base_type` 与 `name_span`。
  - asset parser 记录 `declare type Name: Base` 的 base type 与 name span。
  - linter 的 duplicate field 检查按角色区分 `field:` 与 `pin:`，允许同名 field + flow pin，但仍拒绝同角色重复。
- `src/edit/edit_session.cpp`
  - asset declaration 注册类型时保留 `constructible` 与精确 `name_range`。
- `presets/*.d.gs`
  - `declare type` -> `export declare type`。
  - `declare Node` -> `export declare object`。
  - `exec in/out` -> `@flow.pin(kind = "exec", direction = "...")`。
  - `data in/out` -> `@flow.input` / `@flow.output`。
  - `declare Schema` -> `export declare schema ...: FlowGraphSchema`。
  - `ue_core.d.gs` 增加 `Exec` marker type。
- 测试更新
  - 新增 asset parser/linter 测试：constructible base type、field/pin 同名、同角色重复。
  - 新增 `EditSession.LoadPresetImportsUseAssetDeclarations`，逐个加载真实 preset 并检查类型、节点、schema。
  - `Blueprint`、`DeepCycle`、`QALoop`、`QAStress` 的环境预加载改用 `EditSession::load_import()`。
  - 旧 compiler/emitter 单元测试改用内联 legacy declarations，避免 canonical preset 反向耦合旧 parser。
  - 旧 parser 的 preset fixture 测试改成 asset parser 检查。

## 验证

- `cmake --build build-codex --config Release --target gs_tests -- /m:1`
  - 通过，见 `evidence/phase5_loop013_build_gs_tests.log`。
- Focused tests：
  - `AssetLanguage.ParsesTypeBaseAndAllowsFieldPinNamePair`
  - `AssetLanguage.RejectsDuplicateFieldsWithSameRole`
  - `EditSession.LoadPresetImportsUseAssetDeclarations`
  - `Compiler.CompileDeclareTypes`
  - `Compiler.CompileDeclareNodes`
  - `Compiler.CompileDeclareSchemas`
  - `Emitter.EmitMinimal`
  - `Emitter.EmitWithBaseType`
  - 通过，见 `evidence/phase5_loop013_focused_tests_after_fix.log`。
- `cmake --build build-codex --config Release --target gs -- /m:1`
  - 通过，见 `evidence/phase5_loop013_build_gs.log`。
- `rg "declare Node|declare Schema|exec in|exec out|data in|data out|^\s*field\s+" presets`
  - 无旧 preset 声明残留，见 `evidence/phase5_loop013_preset_legacy_scan.log`。
- 对每个 `presets/*.d.gs` 运行 `gs parse -i` 与 `gs lint -i`
  - 全部通过，见 `evidence/phase5_loop013_parse_*.log` 与 `evidence/phase5_loop013_lint_*.log`。
- 回归集合：
  - `Blueprint.*:DeepCycle.*:Parser.Asset*:QALoop.*:QAStress.*:EditSession.LoadPresetImportsUseAssetDeclarations`
  - 57/57 通过，见 `evidence/phase5_loop013_regression_after_loader_fix.log`。
- 全量 C++：
  - `./build-codex/Release/gs_tests.exe`
  - 348/348 通过，见 `evidence/phase5_loop013_full_cpp_after_loader_fix.log`。
- Review / diff check：
  - `git diff --check -- <本轮相关文件>`
  - 通过，见 `evidence/phase5_loop013_diff_check.log`。
  - `do_compile(read_preset(...))` 已无残留；剩余 `read_preset` 只用于 asset parser 测试读取文件内容，见 `evidence/phase5_loop013_review_scan.log`。

## 结果

Loop 013 已完成。真实 preset 文件已迁到 canonical asset declaration syntax，并由 asset parser、CLI parse/lint、EditSession import、旧图 DSL 集成路径共同验证。旧 `load_import` fallback 仍存在用于剩余旧声明测试/临时 fixture，但真实 preset 不再依赖它。

## 回写

- `goal.md`
  - `last_verified_loop` 更新为 `loops/loop-013.md`。
  - `current_loop` 推进到 `loops/loop-014.md`。
  - `next_sub_goal` 建议：迁移剩余 `load_import` 旧声明测试/临时 fixture 到 asset declaration，并拆除 `EditSession::load_import` 的旧 parser/compiler fallback。
- `references.md`
  - 记录测试加载规则：canonical preset 不再作为旧 parser/compiler 输入；需要 Environment 预加载时使用 `EditSession::load_import()`。
