# 旧 Graph DSL 功能清单

> 状态：Phase 3 迁移清单。本文记录旧手写图 DSL 当前能力、代码证据、tree-sitter asset 新方案覆盖方式，以及删除旧 parser/compiler/emitter/runtime 相关代码前必须保留的测试意图。

## 范围和结论

旧实现由以下层组成：

- 语法层：`include/graphscript/parse/*`、`src/parse/*`，手写 lexer/parser，输出旧 `ModuleNode` AST。
- 语义层：`include/graphscript/compile/*`、`src/compile/*`，把旧 AST 降到 `Module`、`Graph`、`NodeDefinition`、`GraphSchema` 和 registry。
- 输出层：`include/graphscript/emit/*`、`src/emit/*`，把旧 `Module/Graph` 输出回旧 DSL 文本和 Mermaid diagram。
- 编辑器层：`include/graphscript/edit/*`、`src/edit/*`、`cli/editor.*`、`cli/server.*`、`cli/source_diagnostics.*`，提供 CLI/Web 编辑、状态 JSON、source patch、诊断和 replay。
- Runtime/debug 层：`include/graphscript/runtime/*`、`src/runtime/*`、`include/graphscript/debug/*`、`src/debug/*`，提供 `RuntimeGraph` bake、debug dump 和结构 diff。

迁移目标不是保留这些旧语法和旧管线，而是保证等价产品能力通过 tree-sitter asset `.gs/.d.gs`、Block/Property/Command/Expr、graph projection、patch/rewrite、editor adapter 和新的 graph runtime IR 重新满足。

## 覆盖状态图例

| 状态 | 含义 |
| --- | --- |
| 已覆盖 | 当前 tree-sitter asset 路径已有可用实现和测试。 |
| 部分覆盖 | 新路径覆盖核心形状，但缺语义、编辑器、诊断或测试完整性。 |
| 缺失 | 新路径尚无等价实现。 |
| 后续 Phase | 本清单确认必须迁移，但按计划在 Phase 4-6 完成。 |

## 计划要求覆盖对照

| 计划旧功能区域 | 本文覆盖位置 | 关键证据路径 |
| --- | --- | --- |
| 文件结构 | 旧语法与 AST 功能 | `include/graphscript/parse/ast.h`、`src/parse/parser.cpp` |
| 声明系统 | 旧语法与 AST 功能、旧 compiler / semantic 功能 | `src/parse/parser.cpp`、`src/compile/compiler.cpp`、`presets/*.d.gs` |
| 校验 | 旧 compiler / semantic 功能、旧 Runtime / validation / debug 能力 | `src/schema/*`、`include/graphscript/schema/*`、`src/compile/compiler.cpp` |
| 编辑 | 旧编辑器与 Web/API 能力 | `include/graphscript/edit/edit_session.h`、`src/edit/edit_session.cpp` |
| Runtime | 旧 Runtime / validation / debug 能力 | `include/graphscript/runtime/runtime_graph.h`、`src/runtime/runtime_graph.cpp` |
| 输出 | 旧 emitter / 输出功能 | `include/graphscript/emit/emitter.h`、`src/emit/emitter.cpp` |
| Debug dump | 旧 Runtime / validation / debug 能力 | `include/graphscript/debug/dump.h`、`src/debug/dump.cpp` |
| CLI 编辑器 | 旧编辑器与 Web/API 能力 | `cli/editor.cpp` |
| Web 后端 | 旧编辑器与 Web/API 能力 | `cli/server.cpp`、`cli/source_diagnostics.cpp`、`webapp/**` |

## 旧语法与 AST 功能

| 旧能力 | 代码证据 | 新方案覆盖 | 删除前测试意图 |
| --- | --- | --- | --- |
| 词法关键字和错误 token：`import/let/declare/Graph/event/function/generate/in/out/var/link/exec/data`、字符串/数字/bool、注释、符号。 | `src/parse/lexer.cpp`；`include/graphscript/parse/token.h`。 | 部分覆盖：tree-sitter grammar 覆盖新语法的 import/declaration/block/property/call/assignment/directive/attribute/expr；旧 token 级 API/trivia/error token 不迁移为兼容接口。 | 保留“语法产出结构化模块和可诊断错误，而非纯文本扫描”的测试意图。 |
| 顶层 `import`，包含路径 range 和 annotation。 | `include/graphscript/parse/ast.h` `ImportNode`；`src/parse/parser.cpp` `parse_import()`。 | 部分覆盖：`gs::asset::ImportDecl` 已解析 import；annotation 和 import-aware editor 行为需 Phase 4 迁移。 | import parse、import source range、import annotation、import rename/guard 测试需迁移。 |
| 顶层 `let name = Type("arg")` constructible binding。 | `LetDeclNode`；`Compiler::compile()` top-level lets；`Emitter::emit_lets()`。 | 缺失/后续 Phase：新 asset 当前可用 property/command 表达，但尚无等价 semantic contract。 | constructible type、let source range、let annotation、let rename 引用测试需改写或明确废弃。 |
| `declare type Name [: constructible]`。 | `DeclareTypeNode`；`Compiler::process_declare_types()`。 | 部分覆盖：asset 有 `declare type`，但 constructible 语义和 editor state 字段需迁移。 | 类型注册、constructible 默认值、类型 rename/source range 测试需保留意图。 |
| `declare Node Name { exec/data in/out pin; field: Type = default; }`。 | `DeclareNodeNode`、`PinDeclNode`、`NodeFieldDeclNode`；`Compiler::process_declare_nodes()`。 | 部分覆盖：asset 当前有 `object/node` declaration 和 field/pin 属性解析；最终应改为 `.d.gs` declaration/member kind contract。 | node declaration、pin kind/direction/type、field default、annotation、source range、pin rename 测试需迁移。 |
| `declare Schema Name { field = value; }`。 | `DeclareSchemaNode`、`SchemaFieldNode`；`Compiler::process_declare_schemas()`。 | 部分覆盖：asset 有 `declare schema` 与 properties；schema policy 绑定和 editor state 需迁移。 | schema fields、policy values、schema field annotation/source range、schema rename 测试需迁移。 |
| preset `.d.gs` 声明包。 | `presets/ue_core.d.gs`、`presets/ue_blueprint.d.gs`、`presets/task_nodes.d.gs`、`presets/levelscript_nodes.d.gs`、`presets/htn_nodes.d.gs`。 | 后续 Phase：这些文件仍是旧声明语法，需要迁移为 canonical `.d.gs` declaration/kind/member contract。 | preset import、schema policy、node tags、constructible type smoke 需迁移。 |
| `Graph Name [: Base] { ... }`。 | `GraphNode`；`Parser::parse_graph()`；`Compiler::compile_graph()`。 | 部分覆盖：canonical 新语法是 `graph Name { schema Base; ... }`，asset projector 已投影 graph name/schema。 | graph name/base/schema、graph-as-node、graph source range、graph rename 测试需迁移。 |
| graph 参数 `in/out/var name : Type = default`。 | `ParamDeclNode`；`Compiler::compile_param()`；`Emitter::emit_params()`。 | 部分覆盖：新语法决定为 `@graph.input param name: Type = default;` 等 declaration command；asset 已能 parse directive param，GraphDomain 投影仍需补。 | 参数方向、默认值、constructor default、source range、param rename、graph-as-node pin 测试需迁移。 |
| 节点实例 `Type alias{initializer}`。 | `NodeInstanceNode`；`Compiler::compile_node_instance()`；`Emitter::emit_node_instances()`。 | 部分覆盖：canonical 新语法为 `node alias { type Type; property: value; }`；asset parser/projector/patcher 已覆盖基础节点和属性。 | 节点类型、alias、initializer field、constructor field、node rename、node type rename 测试需迁移。 |
| event/function logic block。 | `EventNode`、`FunctionNode`；`Parser::parse_event()`、`parse_function()`；`Compiler::compile_event()`、`compile_function()`。 | 部分覆盖：新语法用 `event Name { ... }` / `function Name { ... }` block；asset 已可 parse 任意 block，GraphDomain 对 function/entry 语义待补。 | event/function rename、scope rule、source range、command replay 测试需迁移。 |
| flow connection `a.pin -> b.pin`。 | `FlowStmtNode`；`Parser::parse_flow_stmt()`；`Compiler::compile_logic_stmts()`。 | 部分覆盖：新 canonical 是 `connect(a.pin, b.pin);`，asset projector 已支持 flow edge。 | flow endpoint parsing、unknown node/pin diagnostics、fan-out/fan-in policy、disconnect/reconnect 测试需迁移。 |
| data link `target.pin = source.pin` 或 bare parameter source。 | `LinkStmtNode`；`Parser::parse_link_stmt()`；`Compiler::compile_logic_stmts()`。 | 缺失/后续 Phase：新 canonical 是 `bind(source, target)`，asset parser 可 parse call，但 projector 尚未投影 data link。 | data direction、bare parameter link、type compatibility、unlink/relink 测试需迁移。 |
| generate block comments 和 metadata。 | `GenerateNode`、`CommentNode`、`MetadataNode`；`Parser::parse_generate()`；`Emitter::emit_generate()`。 | 缺失/后续 Phase：新方案应改为 editor/domain metadata property 或 annotation comment item，目前 asset 没有等价 graph editor projection。 | comment box、metadata、layout/source range、annotation、move/rename/remove 测试需迁移。 |
| C# 风格 annotation/attribute，含命名参数、constructor value ranges。 | `core/annotation.h`；`Parser::parse_annotations()`；大量 AST `annotations` 字段。 | 部分覆盖：asset 支持 `@qualified.name(args)` attribute；语义绑定、state JSON 和 patch/replay 需迁移。 | annotation parse、annotation id、annotation arg ranges、annotate/unannotate replay 测试需迁移。 |
| 源码 range/trivia 记录。 | `ASTNode::range/leading_trivia`；各 AST source range 字段。 | 部分覆盖：asset `TextSpan`/CST range 可用，但编辑器 adapter 还未对齐旧 JSON 合约。 | source-range navigation、diagnostic highlight、range patch guard 测试需迁移。 |
| 解析错误、context、hint 和 quickfix action。 | `Parser::record_error()`；`tests/test_parser.cpp` parser diagnostic/quickfix 用例。 | 部分覆盖：asset parser 记录 tree-sitter error/missing diagnostics，坏文本可保留 partial AST；quickfix/action 尚未覆盖。 | Source diagnostics 必须保留 range/context/hint/action 或等价 patch action；坏文本仍应可编辑。 |

## 旧 compiler / semantic 功能

| 旧能力 | 代码证据 | 新方案覆盖 | 删除前测试意图 |
| --- | --- | --- | --- |
| import 文件进入 `Environment`，注册 type/node/schema。 | `Compiler::compile()`；`cli/main.cpp`/`EditSession::load_import()` 使用 compiler 加载 `.d.gs`。 | 后续 Phase：asset `merge_asset_declarations()` 只覆盖 prototype 投影；最终需要 asset declaration environment。 | import resolution、duplicate import、loaded flag、environment hash 测试需迁移。 |
| type registry 和 constructible 类型。 | `process_declare_types()`；`Environment::types()`。 | 部分覆盖：asset declaration 能收集 type symbol；constructible 规则未迁移。 | type registry JSON、constructible constructor validation 测试需迁移。 |
| node registry、pin registry、node field/default。 | `process_declare_nodes()`；`Environment::nodes()`。 | 部分覆盖：asset object declaration 能承载 field/pin 属性，GraphDomain binding 未完整迁移。 | available node types、pins panel、field default、pin direction 测试需迁移。 |
| schema registry 和 connection policy。 | `process_declare_schemas()`；`EditGraph::validate()` 消费 schema policy。 | 部分覆盖：asset schema declaration 可解析 properties，policy binding/validation 未迁移。 | max exec fan-out、allow fan-in、strict type match、allowed tags、required events 测试需迁移。 |
| schema validator：通用图校验和 schema-specific policy。 | `include/graphscript/schema/validator.h`；`src/schema/validator.cpp`；`include/graphscript/schema/connection_policy.h`。 | 后续 Phase：应迁移为 GraphDomain validation，输入为 asset graph projection/model。 | orphan/cycle/pin existence、required events、allowed tags、fan-in/fan-out、strict type match 测试需迁移。 |
| graph-as-node：每个 graph 派生 `NodeDefinition`。 | `Compiler::derive_node_from_graph()`。 | 缺失/后续 Phase：新 graph projection/runtime 必须重新提供子图作为节点能力。 | graph-as-node type refs、graph rename 更新引用、nested graph tests 需迁移。 |
| block-level scope rule：event/function 可见性不同。 | `Compiler::validate_graph_scope()`；`docs/syntax/archive/legacy-scope-rules.md`。 | 缺失/后续 Phase：asset projector 当前只检查连接 alias 存在，未实现 event/function scope policy。 | context/param/node visibility 正反例测试需迁移。 |
| 结构化 diagnostics。 | `Compiler::diagnostics()`；`DiagnosticTarget/DiagnosticAction`。 | 部分覆盖：asset parser/linter/projector 有 diagnostics，但还未覆盖旧 compiler diagnostic targets/actions。 | diagnostic code、target/action、source highlight、quick fix 测试需迁移。 |

## 旧 emitter / 输出功能

| 旧能力 | 代码证据 | 新方案覆盖 | 删除前测试意图 |
| --- | --- | --- | --- |
| `Module` round-trip 输出旧 DSL。 | `Emitter::emit()`、`emit_graph()`、`emit_params()`、`emit_node_instances()`。 | 后续 Phase：新方案应由 formatter/rewrite 输出 canonical asset syntax；当前 asset patcher 只覆盖局部 patch。 | parse-compile-emit-reparse structural diff 测试需改写到 asset formatter/rewrite。 |
| annotation 输出。 | `Emitter::emit_annotations()`。 | 部分覆盖：asset attribute syntax 可表达，但 formatter 未完成。 | annotation round-trip 测试需迁移。 |
| generate/comment/meta 输出。 | `Emitter::emit_generate()`。 | 缺失/后续 Phase。 | comment/metadata round-trip 测试需迁移。 |
| Mermaid diagram 输出。 | `Emitter::emit_diagram()`、`emit_graph_diagram()`。 | 待产品决策/后续 Phase：不再作为顶层 `gs diagram` 命令；如可视化/debug 文本输出仍是产品能力，应作为 graph projection/debug/viewer 能力重建。 | diagram shape tests 需决定迁移为 graph visualization/debug 输出还是删除。 |

## 旧编辑器与 Web/API 能力

| 旧能力 | 代码证据 | 新方案覆盖 | 删除前测试意图 |
| --- | --- | --- | --- |
| `EditSession` 模块/图 CRUD：new/open/rename/switch/delete/import/let。 | `include/graphscript/edit/edit_session.h` module-level API；`cli/editor.cpp` dispatch。 | 后续 Phase：需要 asset-backed editor adapter，不保留旧 AST。 | CLI editor graph/import/let CRUD、undo/redo、save/load 测试需迁移。 |
| 参数、节点、event/function、flow/link 编辑命令。 | `EditSession` graph-level API；`CLIEditor::execute()` command table。 | 部分覆盖：asset patcher 已有 add-node/connect/set-property/rename-node 基础；param/function/link/generate 等仍缺。 | CLI/Web replay 操作测试必须保持。 |
| 节点 initializer 细粒度编辑。 | `EditSession::set_node_initializer*`, `remove_node_initializer_field`, `rename_node_initializer_field`；`cli/editor.cpp` `set_init*`, `unset_init`, `rename_init`。 | 部分覆盖：asset property/assignment 可表示实例配置，Patcher 目前只有 `set_property`。 | 保留 field append/remove/rename、constructor type/argument 修改、raw initializer 替换的编辑意图。 |
| source patch 和 rename replay。 | `cli/editor.cpp` `apply_source_*` 命令；`cli/server.cpp` `/api/source_patch`。 | 部分覆盖：asset `Patcher` 有局部文本 patch；完整 source hash/env hash/import-aware rename 待迁移。 | source patch guard、multi-range patch、identifier/schema/node/pin rename tests 需迁移。 |
| cross-file/import-aware rename 矩阵。 | `cli/editor.cpp` `apply_files_graph_*`, `apply_files_node_*`, `apply_files_schema_rename`, `apply_files_type_rename`, `apply_import_*`；`tests/test_cli_editor.cpp` cross-file/import rename tests。 | 缺失/后续 Phase：asset declaration/source model 尚未提供跨文件 atomic rename。 | 保留 graph param/event/function 跨文件迁移、schema field rename、type rename、nested import replay、environment guard、dry-run-before-write。 |
| source diagnostics with import resolution。 | `cli/source_diagnostics.cpp`；`cli/server.cpp` POST `/api/diagnostics`。 | 部分覆盖：asset parse/lint/project diagnostics 已有基础；import-aware declaration resolution、environment hash、diagnostic actions 待迁移。 | diagnostic highlights、environment hash、import cycle/missing import 测试需迁移。 |
| Web server state/API。 | `cli/server.cpp` `/api/state`、`/api/diagnostics`、`/api/source`、`/api/source_patch`、`/api/exec`、`/api/emit`。 | 后续 Phase：保持 API 或同一步更新 `webapp` 合约。 | real backend replay、source range preview、visual replay、diagnostics tests 需迁移。 |
| declaration source preview。 | `cli/server.cpp` `/api/declaration_source`；`webapp/src/api/types.ts` declaration source types；`webapp/test_declaration_source_preview.py`。 | 后续 Phase：`.d.gs` 仍保留，但 declaration model、content hash guard 和 import rename patch 要迁移到 asset declaration。 | declaration source preview、content hash guard、import node/pin/schema/type rename 测试需迁移。 |
| `state_to_json()` 完整编辑器状态。 | `src/edit/edit_session.cpp` `state_to_json()` 和各 `*_to_json()` helper。 | 后续 Phase：asset-backed state JSON 必须保留字段语义或同步前端。 | frontend state types、canvas/render/source navigation tests 需迁移。 |
| stable/persistent element id。 | `state_to_json()` element id helpers；`tests/test_edit_session.cpp` stable id 和 duplicate generate id 用例。 | 后续 Phase：asset source binding/model 必须生成稳定元素 id。 | 保留 Web 选中态、source navigation、重复 generate/comment/meta id 消歧、replay target 稳定性。 |
| command log replay。 | `EditSession::log_command()`；`CLIEditor.execute()`；server `/api/exec`。 | 后续 Phase：新 editor adapter 必须继续产生命令日志或替代 replay contract。 | command log、undo/redo、GUI action replay tests 需保留。 |
| Web 可视化编辑操作。 | `webapp/test_visual_*_replay.py`, `test_empty_graph_replay.py`, `test_node_initializer_replay.py`, `test_module_annotation_replay.py`。 | 后续 Phase：后端 asset adapter 和前端 state/API 必须同一步迁移。 | 保留 edge reconnect/source reconnect/delete/redirect/reset、node duplicate/align/distribute、comment box、layout metadata、pin compatibility preview、real backend replay。 |

## 旧 Runtime / validation / debug 能力

| 旧能力 | 代码证据 | 新方案覆盖 | 删除前测试意图 |
| --- | --- | --- | --- |
| `EditGraph` mutable graph validation。 | `include/graphscript/edit/edit_graph.h`；`src/edit/edit_graph.cpp`；`EditSession::validate_graph()`。 | 后续 Phase：GraphDomain validation 应消费 asset graph model/projection。 | connection validation、schema policy、pin compatibility、required events tests 需迁移。 |
| `RuntimeGraph` flat node/pin/edge arrays。 | `include/graphscript/runtime/runtime_graph.h`；`src/runtime/runtime_graph.cpp`。 | 缺失/后续 Phase：Q5 决定替换为新 graph runtime IR，不保留兼容外壳。 | runtime node/pin lookup、edge array counts、source mapping intent 需迁移到新 IR。 |
| `RuntimeGraph::bake(EditGraph)`。 | `RuntimeGraph::bake()`。 | 缺失/后续 Phase：新 bake 应消费 graph projection/model，不依赖 `EditGraph`。 | bake from projected graph、invalid graph rejection、large graph performance tests 需迁移。 |
| runtime node/pin 查询失败语义。 | `RuntimeGraph::find_node()`、`find_pin_index()`；`tests/test_runtimegraph.cpp` lookup tests。 | 缺失/后续 Phase：新 runtime IR 尚未定义 stable id/index 查询。 | 新 runtime IR 需提供稳定 id/index 查询和明确失败语义。 |
| runtime/source mapping。 | 旧 runtime 无独立 source map；source range 主要在 AST/Module/EditSession JSON/diagnostics；asset `FlowNode/FlowEdge` 已有 `TextSpan`。 | 缺失/后续 Phase：asset projection span 不等于 runtime IR source-map 合约。 | runtime/debug/source navigation 测试要覆盖 source binding 或 source map。 |
| debug dump AST/Module/EditGraph/RuntimeGraph。 | `include/graphscript/debug/dump.h`；`src/debug/dump.cpp`。 | 后续 Phase：dump 目标应改为 asset syntax tree、semantic model、graph projection、新 runtime IR。 | debug dump readability 和 structural diff tests 需迁移。 |
| structural diff compiled Module。 | `debug::diff_modules()`。 | 后续 Phase：新 formatter/semantic model 需要等价 round-trip diff 或 projection diff。 | round-trip structural equivalence tests 需迁移。 |

## 删除旧实现前的最低迁移门槛

1. 新 asset parser 能解析 canonical `.gs/.d.gs`：import、declare kind/type/node/schema、graph、param、node、event/function、connect、bind、property、attribute、metadata。
2. 新 semantic/declaration environment 能绑定 type、node declaration、pin/member、schema policy、graph-as-node，并提供 source binding。
3. 新 graph projection/model 能表达 graph 参数、节点、event/function、flow/data edges、layout/editor metadata、diagnostics。
4. 新 patch/rewrite/formatter 能覆盖当前 CLI/Web 编辑操作：增删改图、参数、节点、属性、连接、metadata、annotation、rename 和 source range patch。
   - 节点 initializer 必须覆盖 field append/remove/rename、constructor type/argument 修改、raw initializer 替换。
   - Rename 必须覆盖 graph param/event/function 跨文件迁移、schema field rename、type rename、import-aware rename、nested import replay 和 environment guard。
5. 新 source diagnostics 能返回 parser/semantic/import diagnostics、range、target、action、environment hash。
6. 新 editor/server/webapp 合约能保持 replayable command flow 或提供同步替代合约，并更新前端类型与测试。
   - State JSON 必须保留或替换 stable/persistent element id、source navigation target、diagnostic target/action 和 Web 视觉编辑所需的 layout/comment/connection metadata。
7. Web 可视化编辑操作矩阵必须保留或同一步替换：edge delete/reconnect/redirect/source reconnect/reset guard/straighten、node duplicate/align/distribute/layout axes、comment box replay、stable ids、state source ranges、pin compatibility preview。
8. 新 graph runtime IR 替换 `RuntimeGraph`，并以 graph projection/model 作为输入。
9. 旧 parser/compiler/emitter/debug/runtime 测试要么改写到新模型，要么明确标记为旧语法专属并删除。

## 迁移测试映射

| 旧测试区域 | 迁移目标 |
| --- | --- |
| `tests/test_parser.cpp` | asset parser/semantic source binding tests。 |
| `tests/test_compiler.cpp` | asset declaration environment、GraphDomain binding、scope rules tests。 |
| `tests/test_emitter.cpp` | asset formatter/rewrite/reparse tests。 |
| `tests/test_lexer.cpp` | tree-sitter parser facade/highlight/query/token-equivalent smoke；若不再暴露 lexer，则删除旧 lexer 专属断言。 |
| `tests/test_type_registry.cpp` | asset declaration environment 的 type symbol、constructible contract、rename/source binding tests。 |
| `tests/test_node_registry.cpp` | asset declaration environment 的 node declaration、pin/member、tag/default tests。 |
| `tests/test_schema_registry.cpp` | asset schema declaration registry、policy fields、preset schema loading tests。 |
| `tests/test_connection_policy.cpp` | GraphDomain validation 的 fan-in/fan-out、strict type、allowed tags policy tests。 |
| `tests/test_validator.cpp` | GraphDomain validator 的 common/schema diagnostics tests。 |
| `tests/test_edit_session.cpp` | asset-backed editor adapter tests。 |
| `tests/test_cli_editor.cpp` | final CLI/Web replay command tests。 |
| `tests/test_source_diagnostics.cpp` | asset source diagnostics、semantic diagnostics、import resolver、environment hash tests。 |
| `tests/test_editgraph.cpp` | new graph projection/validation/editor model tests。 |
| `tests/test_runtimegraph.cpp` | new graph runtime IR tests。 |
| `tests/test_blueprint_scenarios.cpp`、`tests/test_integration_deep.cpp`、`tests/test_qa_loop.cpp`、`tests/test_qa_stress.cpp` | end-to-end asset syntax + graph projection + editor + runtime smoke/performance tests。 |
| `webapp/test_visual_edge_delete_replay.py`、`test_visual_edge_reconnect_replay.py`、`test_visual_edge_redirect_replay.py`、`test_visual_edge_source_reconnect_replay.py`、`test_visual_edge_reset_guard.py`、`test_visual_straighten_connection_replay.py` | Web edge visual editing replay matrix。 |
| `webapp/test_visual_node_duplicate_replay.py`、`test_visual_node_align_replay.py`、`test_visual_node_distribute_replay.py`、`test_visual_node_layout_axes.py`、`test_visual_node_rendering_metadata.py` | Web node visual editing/layout metadata matrix。 |
| `webapp/test_visual_comment_box_replay.py`、`test_state_stable_ids.py`、`test_state_source_ranges.py`、`test_source_range_preview.py`、`test_element_source_range_navigation.py` | Web source binding、stable id、comment/metadata matrix。 |
| `webapp/test_visual_pin_compatibility_preview.py`、`test_diagnostic_highlights.py`、`test_real_backend_diagnostic_target_smoke.py` | Web pin compatibility and diagnostics matrix。 |
| `webapp/test_*.py` | Remaining backend API/state/source diagnostics/declaration preview/diagnostic target/visual replay tests against new editor adapter。 |

## 已知缺口转 Phase

| 缺口 | 目标 Phase |
| --- | --- |
| `bind(source, target)` data link projection 与 validation。 | Phase 4 |
| graph parameter projection 和 graph-as-node。 | Phase 4 |
| asset-backed editor adapter 和 state JSON。 | Phase 4 |
| source diagnostics import-aware semantic binding。 | Phase 4 |
| environment hash、declaration preview、atomic multi-patch、cross-file rename。 | Phase 4 |
| stable/persistent element id、diagnostic target/action、Web 可视化编辑 replay。 | Phase 4 |
| asset formatter/rewrite 覆盖旧 emitter round-trip。 | Phase 4/5 |
| 删除旧 parser/compiler/emitter 文件和测试。 | Phase 5 |
| 新 graph runtime IR 替换 `RuntimeGraph`。 | Phase 5/6 |
| 新 runtime IR 的 node/pin lookup、flow/data edge bake、schema/domain metadata、source map。 | Phase 5/6 |
| debug dump 改为 asset/model/runtime 新目标。 | Phase 6 |
