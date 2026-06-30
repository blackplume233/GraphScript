# Loop 017：收敛 EditSession asset emit/round-trip 生产路径

## 子目标

继续 Phase 5，优先收敛 legacy emitter emit/round-trip 面：让 `EditSession::emit()` / `emit_active()` 不再依赖旧 `graphscript/emit/Emitter` 输出旧 Graph DSL，而是输出可被 tree-sitter asset parser 重新读取的 `.gs` asset 语法。

## 本轮仓库基准检查

- 重新读取 `goal.md` 与 `prd.md`。
- 读取 Trellis Phase 2/3、`trellis-before-dev`、backend/frontend spec index、backend quality/error/directory/logging、cross-layer/code-reuse guides。
- 读取并执行 `trellis-check`、`trellis-update-spec` 的适用部分。
- 使用 Codex 子代理做只读 review；子代理指出 3 个问题，本轮已修复并补回归。

## 计划

1. 扫描 editor/source diagnostics/web 旧 parser/compiler/emitter 残留。
2. 在 `EditSession` 增加局部 asset source emitter，保留旧 `Emitter` 的 Mermaid diagram 用途但移除 `EditSession` 对旧 emitter 的生产依赖。
3. 更新 EditSession/CLI 测试断言到 asset canonical 输出。
4. Review 并修复 emitter 生成不可解析 asset source 的边界。
5. 运行 focused、全量 C++、`gs` 构建、diff check、残留扫描。
6. 记录 spec 学习点和 loop 证据。

## 执行记录

- `src/edit/edit_session.cpp`
  - 移除 `graphscript/emit/emitter.h` include。
  - 新增 module/graph → asset source helpers：
    - `graph Name { ... }`
    - `schema Schema;`
    - `@graph.input/output/var` + `param name: Type`
    - `node alias { type Type; field: value; }`
    - `event/function` 内 `connect(from, to)`、`bind(source, target)`
    - `generate Layout { comment(...); metadata(...); }`
  - `EditSession::emit()` / `emit_active()` 改为 asset emitter。
  - 过滤 `graph.input/output/var` 方向 attributes，避免 asset load 后 mutation 再 emit 时重复方向标注。
  - 修复 review 发现的 expression 输出问题：
    - top-level `import` 暂不 emit attributes，因为当前 grammar 不支持 import attributes。
    - `LetDecl` 增加 `initializer_fields`，asset `const name = new Type { ... }` body properties 在 legacy adapter 中保留。
    - constructor 参数和 metadata/default/node initializer 输出走 asset expression literal 规范化，裸 `/Game/...` 输出为字符串字面量。
    - annotation positional identifier 按字符串输出，避免 `@Comment(title, ...)` 这种语义漂移。

- `include/graphscript/core/module.h`
  - `LetDecl` 增加 `std::vector<InitializerField> initializer_fields` 保存 asset const body properties。

- `tests/test_edit_session.cpp` / `tests/test_cli_editor.cpp`
  - 将旧 Graph DSL emit 断言迁移为 asset 语法断言。
  - `EmitRoundTrip` 改为 `emit -> load_source` 的 asset round-trip。
  - 增加回归：
    - asset-loaded param direction 不重复输出。
    - top-level const 非空 body 在 emit/reparse 后保留。
    - import annotation 不 emit 成 grammar 不支持的 source。
    - CLI `set_init_ctor_arg /Game/...` emit 后仍可 `load_source`。

- `.trellis/spec/backend/quality-guidelines.md`
  - 新增 Common Mistake：asset emitter 不得输出 grammar 不能 parse 的 source；import attributes 暂不 emit；asset const body 要保留并做 `emit -> load_source` 回归。

## Review

Codex 子代理 review 发现并确认：

1. 带注解 top-level import 被 emit 成不可解析语法。
2. asset-loaded top-level const body 首次编辑后会丢失。
3. raw constructor arg 可能 emit 成不可解析表达式。

本轮全部修复，并新增 focused 回归。

## 验证

- `cmake --build build-codex --config Release --target gs_tests`
  - 通过；证据：`evidence/phase5_loop017_build_after_annotation_literal_fix.log`
- Focused review 回归：
  - `build-codex\Release\gs_tests.exe --gtest_filter="CLIEditor.CanEditTopLevelImportAndLetAnnotations:CLIEditor.SetInitializerFieldCommandIsReplayable:EditSession.TopLevelAnnotationsRoundTripAndExportPersistentIds:EditSession.CanEditTopLevelImportAndLetAnnotations:EditSession.LoadAssetSourceProjectsGraphAndPreservesSourceEmit:EditSession.EmitRoundTrip"`
  - 5/5 通过；证据：`evidence/phase5_loop017_review_fixes_focused.log`
- Annotation focused 回归：
  - `build-codex\Release\gs_tests.exe --gtest_filter="CLIEditor.AnnotationCommandsAreReplayable:EditSession.AnnotationEditApiUpsertsRemovesAndEmits"`
  - 2/2 通过；证据：`evidence/phase5_loop017_annotation_literal_focused.log`
- Full C++：
  - `build-codex\Release\gs_tests.exe`
  - 348/348 通过；证据：`evidence/phase5_loop017_full_cpp_final.log`
- CLI build：
  - `cmake --build build-codex --config Release --target gs`
  - 通过；证据：`evidence/phase5_loop017_build_gs_final.log`
- Diff check：
  - `git diff --check -- include\graphscript\core\module.h src\edit\edit_session.cpp tests\test_edit_session.cpp tests\test_cli_editor.cpp .trellis\spec\backend\quality-guidelines.md`
  - 无 whitespace error，仅 Windows LF/CRLF 提示。
- 旧依赖扫描：
  - `rg -n "graphscript/(parse|compile|emit)|parse_text|ModuleNode|\bCompiler\b|\bParser\b|\bLexer\b|Emitter" src\edit\edit_session.cpp cli\editor.cpp cli\source_diagnostics.cpp`
  - `src/edit/edit_session.cpp` 仅剩 `asset::Parser`；`cli/source_diagnostics.cpp` 仅剩 `asset::Parser`；`cli/editor.cpp` 仍有旧 `Emitter`，用途为 Mermaid diagram 输出，后续循环处理。

## 结果

- 已完成本轮子目标：EditSession 的 source emit/round-trip 生产路径不再依赖 legacy emitter，输出 asset canonical source，并通过 full C++ 回归。
- 仍未删除旧 `include/graphscript/emit` / `src/emit`，因为测试套件和 `cli/editor.cpp` diagram 仍引用旧 emitter。

## 下一轮建议

Loop 018 继续 Phase 5：迁移旧 parser/compiler/emitter 测试和 fixtures，优先处理 `tests/test_emitter.cpp`、`tests/test_integration_deep.cpp`、`tests/test_blueprint_scenarios.cpp`、`tests/test_qa_loop.cpp`、`tests/test_qa_stress.cpp` 中旧 round-trip 依赖，并明确 `cli/editor.cpp` 的 Mermaid diagram 是否保留在旧 emitter 或迁到独立 diagram helper。
