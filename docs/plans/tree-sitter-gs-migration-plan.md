# Tree-sitter `.gs` 迁移计划

> 目的：以文件为单位规划迁移工作，用 tree-sitter asset 新语法替换旧的手写图 DSL 实现。旧语法、旧 parser/compiler/emitter 不需要继续可用；旧功能必须用新语法方案重新满足，尤其要保留编辑器相关能力。

## 迁移原则

这是一次允许破坏性更新的迁移。

- 不保留旧语法兼容。
- 不为了兼容保留旧 parser/compiler/emitter。
- 必须保留编辑器产品能力：CLI 编辑器、Web 编辑器、source diagnostics、可视化图编辑、patch/apply 流程，以及后端/前端 API 合约；如果要改合约，必须同一步完成替换。
- 旧测试改写到新语法，不继续保留旧 fixture。
- 描述旧语法为当前行为的旧文档应改写或删除。
- 必须新增或更新根目录 `AGENTS.md`，让后续 agent 明确遵守本迁移方向。

## 语法设计目标

`.gs/.d.gs` 脚本语法的首要目标不是模拟传统编程语言，而是作为 AI、编辑器和程序化工具共同维护的可读资产文档。

优先级如下：

1. AI 写文档友好，信息密度良好。语法应该接近结构化文档：少语法糖、少隐式规则、关键词直观、局部片段可独立理解。
2. 程序对具体 Object/Node/Property 的修改，应该方便映射到文档中的某一行或一个稳定 range。编辑器 patch 应尽量是行级或小块替换，不依赖重排整个 block header。
3. 语义模型应该方便序列化和扩展。AST 保持 `Block / Property / Command / Expr` 四类核心节点，domain 通过 `.d.gs` 声明扩展具体 kind 和 command，而不是要求 parser/C++ 为每个 domain 写死新节点。

由此得到的语法取舍：

- Block header 只表达 `kind + name`，不承载 `type/schema/extends/for` 等身份信息。
- `type/schema/extends/for/meta` 这类结构性信息放在 block body 顶部，表达为 `Command` item。
- 普通数据放在 `Property` item 中，优先一行一个属性，便于 patch 和 diff。
- `Attribute` 只做注解，不承载对象身份；例如不使用 `@type(Print)` 代替 `type Print;`。
- Formatter 可以把结构性 Command 固定整理到 block body 顶部，保证 AI 输出和程序 patch 的稳定性。

面向 patch/序列化的具体约束：

- 一行尽量只表达一个语义单元：一个结构性 Command、一个 Property、一个连接 Command 或一个 member declaration。
- Block opening line 应尽量稳定，例如 `node log {`；修改节点类型只改 `type Print;` 这一行，不改 block header。
- 只有真正形成嵌套作用域或独立 domain object 的内容才使用 nested Block。
- 编辑器和 domain metadata 优先使用 dotted property path，例如 `editor.pos: [100, 200];`，IR 中保存为 path segments。
- Inline object 只作为属性值使用，方便序列化为 JSON object；它不形成 Block，不参与 scope/name resolution。
- Formatter 输出应稳定：结构性 Command 在前，普通 Property 居中，连接/行为 Command 按作者顺序保留，metadata 可以聚合到尾部或按 namespace 排序。

## 语法方案复审

基于 AI 写作、行级 patch、序列化扩展这三个目标，当前方案继续保留 `Block / Property / Command / Expr` 四类核心 AST，但需要明确以下约束。

### 复审结论

- `Block header = kind + name` 是正确方向。它牺牲了一点首行信息密度，但换来稳定 patch：改类型、改 schema、改 target 都只改 body 里的单行 Command。
- `schema AbilityGraph;`、`type Print;` 作为 body Command 比 header clause 更适合编辑器。程序可以定位到这一行替换，不需要重写 `{` 前的复杂头部。
- `editor.pos: [100, 200];` 作为 dotted Property 是正确方向。它是数据，不是作用域；用 block 会让 metadata 获得不该有的语义重量。
- `.d.gs` 扩展 kind 是必要方向。Graph 的 `node/event/function/schema/connect/bind` 不应该写死在 parser 中，应该由 prelude 声明并由 semantic/domain 绑定。
- `Command` 会承载多种语义角色，但不应该拆成多个 AST 类。拆 AST 会让 grammar、formatter、patcher 和序列化都变复杂；角色应该在 semantic 层由 `.d.gs` contract 判定。

### 形状判定

grammar 第一版应该尽量用源码形状区分节点，而不是靠 domain 关键词：

```text
kind name { ... }      -> Block
name: expr;            -> Property
command args...;       -> Command
expression             -> Expr
```

因此新增 domain block kind 不需要改 grammar：

```gs
dialog Greeting {
    type Conversation;
    title: "Intro";
}
```

只要 `.d.gs` 声明了 `block dialog` 和对应 contract，semantic 层就能验证它；parser 只需要知道这是一个 `Block(kind=dialog, name=Greeting)`。

### Command 角色

`Command` 是 AST 形状，不等于“运行时动作”。semantic 层至少区分这些角色：

```text
StructuralCommand  # schema AbilityGraph; type Print; extends Base; for Fireball;
ActionCommand      # connect(a, b); bind(a, b); call-like graph behavior
DeclarationCommand # param amount: float = 50.0; input enter: exec;
DirectiveCommand   # domain/tooling directive, 只有确实需要时再引入
```

这些角色由 `.d.gs` contract 判定，不体现在 AST 大类上。这样保持语法简单，同时避免 `type`、`connect`、`param` 在语义层混成一类。

### Patch Locator

为了让程序修改具体 Object/Node/Property 时能映射到源码稳定位置，semantic model 必须为每个可编辑语义对象保留 source binding：

```text
BlockLocator    = block kind + block name + source range
CommandLocator  = parent block + command kind + occurrence/id + source range
PropertyLocator = parent block + property path + source range
MemberLocator   = declaration kind + declaration name + member kind + member name + source range
```

设计含义：

- 修改 `node log` 的类型，只替换 `type Print;` 这一行。
- 修改位置，只替换 `editor.pos: [100, 200];` 这一行。
- 修改连接，只替换对应 `connect(...)` 或 `bind(...)` 这一行。
- 修改 `Print` 的输入输出，只替换 `.d.gs` 中对应 member declaration 的一行。

如果同一 block 内出现重复 command，例如多个 `connect(...)`，patcher 需要使用 source range 或 domain 生成的 stable id 辅助定位；不要依赖“第 N 个 connect”作为长期稳定 API。

### 序列化映射

序列化不应该要求重建源码语法；它应该消费 AST/IR 的结构化事实：

```text
Block(kind=node, name=log)
  Command(type, [Print])
  Property(message, "hello")
  Property(editor.pos, [100, 200])
```

可以序列化为：

```json
{
  "kind": "node",
  "name": "log",
  "type": "Print",
  "properties": {
    "message": "hello",
    "editor": {
      "pos": [100, 200]
    }
  }
}
```

也就是说，dotted property 是源码层的 patch-friendly 表达；IR/JSON 层仍然可以恢复成嵌套对象。

## 命名决策

tree-sitter asset 新语法统一使用 `.gs` 和 `.d.gs`。

- `.gs`：资产源文件，包含 block、property、command、reference、graph/table/asset body。
- `.d.gs`：声明文件，包含 type、node declaration、block kind、command、schema、lint、export、module metadata。
- `.sc` 和 `.d.sc`：从用户可见文档、测试、CLI help、package metadata 中移除。

## 核心 AST 决策

新语法的 AST 核心收敛为四类节点：

```text
Block
Property
Command
Expr
```

所有带 `{}` 的结构都统一是 `Block`，`graph`、`event`、`function`、`object`、`asset`、`table`、`row` 都只是 block kind，不是独立 AST 节点。

```text
Block
  attributes
  kind        # graph / event / function / object / asset / table / row / ...
  name?
  body: Item[]

Item =
  Block
  Property
  Command
```

示例源码：

```gs
graph Execute {
    schema AbilityGraph;

    node log {
        type PrintString;
        message: "done";
        editor.pos: [100, 200];
    }

    event BeginPlay {
        connect(context.start, log.enter);
        bind(player.name, log.message);
    }
}
```

AST：

```text
Block(kind=graph, name=Execute)
  Command(name=schema, args=[AbilityGraph])

  Block(kind=node, name=log)
    Command(name=type, args=[PrintString])
    Property(path=message, value="done")
    Property(path=editor.pos, value=[100, 200])

  Block(kind=event, name=BeginPlay)
    Command(name=connect, args=[context.start, log.enter])
    Command(name=bind, args=[player.name, log.message])
```

语义层仍保留 scope 概念：Block 绑定后形成 ScopeSymbol，用于名字空间、嵌套、可见性和引用解析。

IR 层使用：

```text
ObjectIR
PropertyIR
CommandIR
FragmentIR
```

普通序列化可以直接把嵌套 Block lower 成嵌套 ObjectIR；GraphDomain 则把特定 block kind 和 command 投影成图模型。

`:` 不作为 Block 头部的固定“类型语义”。Block 头部只支持 `kind + name`；结构性信息放在 body 顶部，用 Command 表达，由 DSL semantic 和 domain 解释：

```gs
graph Execute {
    schema AbilityGraph;
}

node log {
    type PrintString;
}

node child {
    type TaskNode;
    extends BaseTask;
    meta editorOnly;
}
```

这避免 `kind` 和 `type` 在语义上重复：`graph/node/event` 是 block kind，`schema/type/extends/meta` 是可扩展 Command。GraphDomain 可以要求 `graph` 必须有 `schema` command，要求 `node` 必须有 `type` command。

第一版禁止不必要的语法糖，尤其不支持把 type 提到 block kind 位置：

```gs
// 不作为 canonical，也不在第一版支持
PrintString log {
}

// canonical
node log {
    type PrintString;
}
```

inline object 可以作为 `Expr` value，但它不是 `Block`，不会形成 `ScopeSymbol`，也不会直接 lower 成顶层 `ObjectIR`。它只表示某个 property 的结构化值：

```gs
node log {
    type PrintString;

    style: {
        color: "green";
        weight: 600;
    };
}
```

Fragment/overlay 场景第一版只预留语义，不急着让 GraphDomain 消费。目标对象通过 body command 表达，不新增 AST 字段：

```gs
tuning Balance {
    for Fireball;
    damage: 50;
}
```

这里 `for Fireball;` 是 Command item；是否表示 fragment target，由对应 domain 的 contract 决定。

编辑器和 domain metadata 的 canonical 写法使用 dotted property path：

```gs
node log {
    editor.pos: [100, 200];
    editor.size: [220, 80];
}
```

不建议把 metadata 写成 block：

```gs
// 不作为 canonical
node log {
    editor {
        pos: [100, 200];
    }
}
```

原因是 `{}` 在语法/语义中表示 `Block`，会自然引入 kind、body item、ScopeSymbol、domain validation 和 rewrite 边界；`editor.pos` 只是一个普通属性路径，适合 source patch、序列化和 editor metadata diff。属性路径在 IR 中应保留为结构化 path segments，而不是单纯字符串；序列化层可以按需要输出为嵌套 JSON。

如果 metadata 很多，可以使用 inline object 作为属性值，但它仍是 `Property + Expr`，不是 block：

```gs
node log {
    editor: {
        pos: [100, 200];
        size: [220, 80];
    };
}
```

第一版 formatter 优先输出 dotted property，因为它对局部 patch 最稳定。

### Kind 分层

需要严格区分两层概念：

1. `KindFamily`：AST/语义里的固定家族，例如 `block`、`command`、`declaration`、`member`、`attribute`。这一层决定节点形状和基础编辑能力，属于 C++/grammar 层，不允许 `.d.gs` 随意增加第五种 AST 大类。
2. `KindSymbol` 和它的 contract：某个家族下的具体 kind，例如 `block graph`、`block node`、`command connect`、`command schema`。这一层应该可以由 `.d.gs` 定义和扩展。

所以结论是：

- `.d.gs` 可以定义新的 block kind、command kind、declaration kind、member kind、attribute kind。
- `.d.gs` 不定义新的 AST 节点家族；新增 AST 家族才需要 C++/grammar 变更。
- C++ 只提供固定 `KindFamily`、内建 prelude 加载、绑定和验证框架。
- GraphDomain 可以把标准 graph kind 放在内建 `.d.gs` prelude 中，而不是把 `graph/event/function/object` 写死为 parser 关键字。

声明侧对应使用 `declare kind` 定义 kind symbol，再使用 `declare block` 等声明这个 kind 的 contract，不再使用 `declare scope`：

```gs
export declare kind block graph;
export declare kind block event;
export declare kind block function;
export declare kind block object;
export declare kind command connect;
export declare kind command bind;
export declare kind command schema;
export declare kind command type;
export declare kind command extends;
export declare kind command meta;

export declare block graph {
    allows block event;
    allows block function;
    allows block object;
    allows command connect;
    allows command bind;
    requires command schema;
}

export declare block table {
    allows block row;
}
```

`declare kind block graph;` 表示“存在一个名为 `graph` 的 block kind”。`declare block graph { ... }` 表示“`graph` 这种 block kind 的实例允许什么 body item 和 command”。定义 kind 和定义 kind 的声明规则是两个不同层次，语义模型中也要分开存储。

这里声明的是语法/语义中的 block kind。绑定后仍可以形成 ScopeSymbol；`scope` 是语义概念，不再作为作者侧关键字或声明关键字。

## 实现边界

### 保留并提升的新实现

| 路径 | 当前角色 | 调整计划 |
| --- | --- | --- |
| `tools/tree-sitter-graphscript-asset/grammar.js` | 新语法的 canonical grammar | 把文件类型假设从 `.sc/.d.sc` 改成 `.gs/.d.gs`；把旧 `scope_declaration` 改成通用 `block_declaration`，block kind 解析为标识符。 |
| `tools/tree-sitter-graphscript-asset/src/parser.c` | 生成出来的 parser | grammar/package metadata 调整后重新生成。 |
| `tools/tree-sitter-graphscript-asset/src/grammar.json` | 生成的 grammar metadata | 重新生成。 |
| `tools/tree-sitter-graphscript-asset/src/node-types.json` | 生成的 node type metadata | 重新生成。 |
| `tools/tree-sitter-graphscript-asset/queries/highlights.scm` | 编辑器 query 支持 | 保留；只有 grammar node 名称变化时才调整。 |
| `tools/tree-sitter-graphscript-asset/package.json` | tree-sitter package metadata | description/file types 从 `.sc/.d.sc` 改成 `.gs/.d.gs`。 |
| `tools/tree-sitter-graphscript-asset/package-lock.json` | npm lockfile | 如果 `package.json` 变化则刷新。 |
| `tools/tree-sitter-graphscript-asset/* bindings` | 语言绑定 | 保留；只有生成 metadata 需要时才刷新。 |
| `include/graphscript/asset/language.h` | 新语法 C++ facade | 提升为主 parser/projector/patch API；只有编辑器迁移需要时才加兼容别名。 |
| `src/asset/language.cpp` | 基于 tree-sitter CST 的 C++ 实现 | source name 示例和后缀处理改为 `.gs/.d.gs`；保留 parser、linter、patcher、projector 行为。 |
| `tests/test_asset_language.cpp` | 当前新语法测试 | 所有 fixture 名称和 import 字符串从 `.sc/.d.sc` 改为 `.gs/.d.gs`；补充后缀行为测试。 |

## 目标模块结构

迁移完成后的目标结构如下。重点是：tree-sitter 新语法成为唯一语法入口；旧 `parse/compile/emit` 管线删除；DSL 自身不仅包含 parser，也包含源文本、语法、声明、符号、类型、语义绑定、IR、rewrite、通用 authoring runtime 基础；Graph 是 DSL 上的一个 domain；Editor 是产品交互层。每个模块都应该有自己的 `interface/`，模块间优先依赖对方的 interface，而不是跨层引用内部实现。

```text
GraphScript/
  AGENTS.md
  CMakeLists.txt
  README.md
  CLAUDE.md

  include/
    graphscript/
      dsl/
        interface/
          document.h          # DSL 对外文档模型 API：source + syntax + semantic view
          compiler.h          # DSL source -> semantic/IR 的公共入口
          diagnostics.h       # DSL diagnostic API
          rewrite.h           # TextPatch/RewriteBuilder/formatter 公共入口
          runtime.h           # 通用 authoring/runtime 基础接口
        source/
          source_text.h
          text_range.h
          source_range.h
          line_map.h
          source_hash.h
        syntax/
          tree_sitter_parser.h
          syntax_tree.h
          block_node.h
          property_node.h
          command_node.h
          expr_node.h
          syntax_node.h
          syntax_token.h
          trivia.h
        declarations/
          declaration_model.h
          kind_declaration.h
          block_declaration.h
          attribute_declaration.h
          import_model.h
          export_table.h
          module_declaration.h
          object_declaration.h
          schema_declaration.h
          command_declaration.h
        symbols/
          symbol.h
          kind_symbol.h
          symbol_table.h
          module_graph.h
          name_resolution.h
        types/
          type_ref.h
          type_symbol.h
          type_registry.h
          type_check.h
        semantics/
          semantic_model.h
          scope_symbol.h
          binding.h
          semantic_diagnostics.h
        ir/
          object_ir.h
          command_ir.h
          property_ir.h
          fragment_ir.h
          asset_ir.h
        compiler/
          asset_compiler.h
          compile_pipeline.h
        rewrite/
          text_patch.h
          text_edit.h
          rewrite_builder.h
          formatter.h
          edit_transaction.h
          source_binding.h
        runtime/
          authoring_runtime.h
          debug_target.h
          trace_event.h
        diagnostic/
          diagnostic.h

      graph/
        interface/
          graph_model.h       # GraphDomain 对外模型
          graph_projector.h   # DSL semantic/IR -> graph model
          graph_editor.h      # 图编辑操作 API，不含 UI
          graph_runtime.h     # graph runtime/bake API
          graph_diagnostics.h
        schema/
          graph_schema.h
          connection_policy.h
          schema_registry.h
        model/
          node.h
          pin.h
          edge.h
          graph.h
          entry.h
          source_binding.h
        projection/
          graph_projection.h
          flow_graph_projection.h
          projection_diagnostics.h
        validation/
          validator.h
          connection_validator.h
          schema_validator.h
        rewrite/
          graph_rewrite.h     # 图编辑意图 -> DSL rewrite request
          graph_patch_ops.h
        runtime/
          runtime_graph.h
          bake.h
          source_map.h
        editor/
          graph_edit_session.h
          graph_patch_ops.h

      editor/
        interface/
          editor_session.h    # CLI/Web 共用编辑器 session API
          state_json.h        # 前后端状态 JSON 合约
          source_diagnostics.h
          commands.h
        cli/
          cli_editor.h
        web/
          web_server.h
        diagnostics/
          source_diagnostics.h

      core/
        result.h              # 足够通用的基础工具可保留
      debug/
        dump.h                # 改为 dump DSL/Graph/Editor 新模型

  src/
    dsl/
      interface/
      source/
        source_text.cpp
        line_map.cpp
      syntax/
        tree_sitter_parser.cpp
        syntax_tree.cpp
        block_node.cpp
        property_node.cpp
        command_node.cpp
        expr_node.cpp
      declarations/
        declaration_model.cpp
        kind_declaration.cpp
        block_declaration.cpp
        attribute_declaration.cpp
        export_table.cpp
      symbols/
        symbol_table.cpp
        module_graph.cpp
        name_resolution.cpp
      types/
        type_registry.cpp
        type_check.cpp
      semantics/
        semantic_model.cpp
        scope_symbol.cpp
        binding.cpp
        semantic_diagnostics.cpp
      ir/
        object_ir.cpp
        command_ir.cpp
        property_ir.cpp
        fragment_ir.cpp
      compiler/
        asset_compiler.cpp
        compile_pipeline.cpp
      rewrite/
        text_patch.cpp
        rewrite_builder.cpp
        formatter.cpp
        edit_transaction.cpp
      runtime/
        authoring_runtime.cpp
      diagnostic/

    graph/
      interface/
      schema/
        graph_schema.cpp
        connection_policy.cpp
        schema_registry.cpp
      model/
        graph.cpp
        node.cpp
        pin.cpp
        edge.cpp
      projection/
        graph_projection.cpp
        flow_graph_projection.cpp
      validation/
        validator.cpp
        connection_validator.cpp
      rewrite/
        graph_rewrite.cpp
        graph_patch_ops.cpp
      runtime/
        runtime_graph.cpp
        bake.cpp
      editor/
        graph_edit_session.cpp

    editor/
      interface/
        editor_session.cpp
        state_json.cpp
      cli/
        cli_editor.cpp
      web/
        web_server.cpp
      diagnostics/
        source_diagnostics.cpp

    debug/
      dump.cpp

  cli/
    main.cpp                  # parse/lint/project/patch/edit/serve 统一走新模块 interface
    editor.h                  # 迁移期 wrapper，可最终转发到 graphscript/editor/cli
    editor.cpp
    server.h                  # 迁移期 wrapper，可最终转发到 graphscript/editor/web
    server.cpp
    source_diagnostics.h      # 迁移期 wrapper，可最终转发到 graphscript/editor/diagnostics
    source_diagnostics.cpp

  tools/
    tree-sitter-graphscript-asset/
      grammar.js
      package.json
      package-lock.json
      src/
        parser.c
        grammar.json
        node-types.json
        tree_sitter/
      queries/
        highlights.scm
      bindings/
        c/
        node/
        python/
        rust/
        go/
        swift/

  presets/
    *.d.gs                    # 改写为新 declare object/type/schema/command 语法；由 dsl/interface 编译

  tests/
    fixtures/
      *.gs                    # 新 asset source fixtures
      *.d.gs                  # 新 declaration fixtures
    test_dsl_syntax.cpp       # tree-sitter/CST/source range
    test_dsl_declarations.cpp # import/export/declare module/type/object/schema/command
    test_dsl_symbols.cpp      # symbol table/module graph/name resolution
    test_dsl_types.cpp        # type ref/type check
    test_dsl_semantics.cpp    # binding/semantic model
    test_dsl_compiler.cpp     # source -> command/object IR
    test_dsl_rewrite.cpp      # TextPatch/RewriteBuilder/formatter/edit transaction
    test_graph_projection.cpp # graph/flow graph projection
    test_graph_validator.cpp
    test_graph_runtime.cpp
    test_graph_editor.cpp
    test_cli_editor.cpp       # 保留编辑器工作流覆盖，期望源码改新语法
    test_edit_session.cpp     # 保留编辑器工作流覆盖，内部改新模型

  webapp/
    ...                       # 保留；按后端 API 变化同步调整

  docs/
    spec/
      tree-sitter-gs-migration-plan.md
      migration/legacy-graph-dsl-feature-inventory.md
      ../syntax/design/ai-native-syntax-draft.md
      ../syntax/design/ai-native-asset-format.md
      index.md
```

过渡期允许保留 `include/graphscript/asset/language.h` 和 `src/asset/language.cpp` 作为兼容 facade，但它们应逐步拆分到 `dsl/` 和 `graph/`：

```text
asset/language Parser        -> dsl/interface + dsl/syntax
asset/language Linter        -> dsl/semantics + graph/validation diagnostics
asset/language Patcher       -> dsl/rewrite + graph/rewrite
asset/language FlowProjector -> graph/projection
```

迁移完成后应删除的目录/文件：

```text
include/graphscript/parse/
src/parse/
include/graphscript/compile/
src/compile/
include/graphscript/emit/
src/emit/
tests/test_lexer.cpp
tests/test_parser.cpp
tests/test_compiler.cpp
tests/test_emitter.cpp
```

旧 `schema/`、`runtime/`、`registry/`、`debug/` 中仍有价值的类型可以迁移到 `dsl/` 或 `graph/` 对应模块；不要保持原目录只是为了少改 include。

## 架构审视

这个架构的核心判断是：目录名应该表达“这里维护哪一种语义事实”，而不是表达“这里用了什么技术”。`dsl` 不是“parser 目录”，而是通用语言平台；`graph` 是 DSL 上的一个 domain；`editor` 是产品交互层。`interface/` 不是单独大模块，而是每个模块的公共门面。

### 合理之处

- `dsl/source` 维护原始文本事实：文本内容、offset、line/column、hash、range；它不依赖 AST 或 graph。
- `dsl/syntax` 维护语法结构事实：CST facade、BlockNode、PropertyNode、CommandNode、ExprNode、syntax token、trivia、error node；tree-sitter 是实现细节。作者侧 `graph/event/function/object/table/row` 等都解析为不同 kind 的 Block。
- `dsl/declarations` 维护 `.d.gs` 声明事实：import、export、declare kind/block/type/node/schema/command/attribute/module；声明不是绑定结果。
- `dsl/symbols` 和 `dsl/types` 分别维护名字空间事实和类型事实，避免 `semantic_model` 变成大杂烩。
- `dsl/semantics` 只做 binding/checking 编排，把 syntax + declarations + symbols + types 绑定成 semantic model。
- `dsl/semantics` 中保留 scope 语义：Block 经过绑定后形成 ScopeSymbol，用于名字边界、嵌套关系、可见性和引用解析。
- `dsl/ir` 维护通用资产语义：ObjectIR、PropertyIR、CommandIR、FragmentIR；Graph 节点和边不放这里。
- `dsl/rewrite` 维护源码改写语义：TextEdit、TextPatch、RewriteBuilder、Formatter、EditTransaction；它不理解图编辑按钮。
- `dsl/runtime` 仅放通用 authoring/debug 协议，如 DebugTarget、TraceEvent、ExecutionLevel；GraphRuntime 放在 `graph/runtime`。
- `graph/schema`、`graph/model`、`graph/projection`、`graph/validation` 的边界清楚：规则、作者模型、投影、合法性分别独立。
- `graph/rewrite` 只负责把图编辑意图转成 DSL rewrite request，真正文本 patch 仍由 `dsl/rewrite` 生成。
- `graph/editor` 放无 UI 的图编辑命令语义；CLI/Web 都通过 `editor/interface` 调它。
- 每个模块都有 `interface/`，依赖方向可以写清楚：`graph` 依赖 `dsl/interface`，`editor` 依赖 `dsl/interface` 和 `graph/interface`。

### 风险

- 目录过细会导致迁移初期文件数量暴涨，接口先行但实现空洞。
- `dsl/semantics` 仍然可能变成垃圾桶；约束是它只做 binding/checking 编排，声明、符号、类型模型分别放到独立目录。
- `dsl/compiler` 与 `graph/projection` 容易职责重叠：前者只能产通用 IR，后者才能产 graph model。
- `dsl/runtime` 与 `graph/runtime` 容易混淆：前者是通用 authoring/debug 基础，后者是 graph bake/runtime。
- `dsl/rewrite` 与 `graph/rewrite` 容易混淆：前者生成文本补丁，后者只做图编辑意图到 DSL rewrite request 的映射。
- `editor/interface` 如果过早固定，会绑死 Web API；迁移中应先保持现有 API，再逐步收敛。
- `asset/language.*` 现有代码同时包含 parser、linter、patcher、projector，拆分时必须用测试保护，避免一次性大爆炸。

### 落地约束

- 第一阶段不要立即创建所有目录；先创建 `dsl/interface`、`dsl/source`、`dsl/syntax`、`dsl/rewrite`、`graph/interface`、`editor/interface` 的最小外壳。
- 保持 `asset/language.*` 作为临时 facade，逐步把内部实现搬到目标模块，外部调用稳定后再删除 facade。
- 所有跨模块 include 优先指向 `*/interface/*.h`。
- 不允许 `dsl/*` include `graph/*` 或 `editor/*`。
- 不允许 `graph/model`、`graph/schema`、`graph/validation`、`graph/runtime` include `editor/*`。
- `editor/*` 可以 include `dsl/interface` 和 `graph/interface`，不要 include `dsl/syntax` 内部或 `graph/model` 内部。
- `graph/projection` 可以消费 `dsl/interface` 产出的 semantic/IR，不直接依赖 tree-sitter node。
- `graph/rewrite` 可以描述图编辑意图和 DSL rewrite request，不直接创建未校验的字符串 patch。
- 删除旧代码前，必须有新模块测试覆盖同等用户功能，而不是只覆盖同名类。

### 面向实现者的代码心智

Graph 投影实现者只需要遍历 Block/Property/Command：

```cpp
for (const Block& graph : document.blocks("graph")) {
    GraphModel model(graph.name(), graph.required_command_arg("schema", 0));

    for (const Item& item : graph.body()) {
        if (item.is_block("node")) {
            model.nodes.push_back(project_node(item.block()));
        }

        if (item.is_block("event") || item.is_block("function")) {
            model.exec_blocks.push_back(project_exec_block(item.block()));
        }
    }
}
```

通用序列化实现者也只需要同一套遍历：

```cpp
ObjectIR lower_block(const Block& block) {
    ObjectIR object;
    object.kind = block.kind();
    object.name = block.name();

    for (const Item& item : block.body()) {
        if (item.is_property()) object.properties.push_back(lower_property(item.property()));
        if (item.is_command()) object.commands.push_back(lower_command(item.command()));
        if (item.is_block()) object.children.push_back(lower_block(item.block()));
    }

    return object;
}
```

因此 Graph 不是 AST 特例，而是 domain projection：

```text
Block(kind=graph)    -> Graph
Block(kind=object)   -> Node
Block(kind=event)    -> Event/Entry block
Command(connect)     -> Exec edge
Command(bind)        -> Data edge
Property(editor.pos) -> Layout metadata
```

### 删除的旧实现

| 路径 | 当前角色 | 迁移规则 |
| --- | --- | --- |
| `include/graphscript/parse/*` | 旧手写 lexer/parser AST | 新 asset parser 接入 CLI/editor diagnostics 后删除；不保留旧 grammar。 |
| `src/parse/*` | 旧 parser 实现 | 删除；需要的 source range 行为在 `gs::asset` 中重新实现。 |
| `include/graphscript/compile/*` | 旧 AST 到 Module compiler | 提取功能清单后删除；等价 semantic binding 基于 tree-sitter asset AST/CST 重建。 |
| `src/compile/*` | 旧 compiler 实现 | 删除；只移植行为，不保留架构。 |
| `include/graphscript/emit/*` | 旧 `.gs` emitter | 删除；用新语法 formatter/source patch 输出替代。 |
| `src/emit/*` | 旧 emitter 实现 | 新 formatter/patch 路径覆盖编辑器保存/导出后删除。 |
| `tests/test_lexer.cpp` | 旧 lexer 测试 | 删除或改写为 tree-sitter parser facade 测试。 |
| `tests/test_parser.cpp` | 旧 parser 测试 | 改写到新语法。 |
| `tests/test_compiler.cpp` | 旧 compiler 测试 | 改写为新 semantic/projector 测试。 |
| `tests/test_emitter.cpp` | 旧 emitter 测试 | 改写为新 formatter/patch/reparse 测试。 |

### 必须保留能力的编辑器相关代码

| 路径 | 当前角色 | 调整计划 |
| --- | --- | --- |
| `include/graphscript/edit/*` | 编辑器 mutation/session API | API 形状按需要保留；内部替换成 tree-sitter asset model 和 patcher。 |
| `src/edit/*` | 当前 edit 实现 | 允许破坏性重写，但每个验证点都要保持编辑器工作流可用。 |
| `cli/editor.*` | CLI 编辑器和 source patch 命令面 | 保留用户工作流；命令可以输出新语法并调用新 parser/projector API。 |
| `cli/server.*` | Web 编辑器后端 | 保留 Web 编辑器路由；若改路由，必须同步更新 `webapp`。 |
| `cli/source_diagnostics.*` | 编辑器 source diagnostics | 用 tree-sitter asset parser/semantic diagnostics 重新实现。 |
| `webapp/**` | 可视化编辑器前端 | 保留产品能力；如果后端合约改变，前端可同步调整。 |
| `tests/test_edit_session.cpp` | 编辑器行为测试 | 期望源码改为新 asset 语法；保留工作流覆盖。 |
| `tests/test_cli_editor.cpp` | CLI 编辑器行为测试 | 期望源码和后端命令改到新语法；保留工作流覆盖。 |
| `webapp/test_*.py` | Web 编辑器测试 | 保留；后端替换后作为回归测试。 |

## CLI 计划

当前 CLI 同时有旧命令和 tree-sitter 原型命令。

| 当前命令 | 当前含义 | 计划状态 |
| --- | --- | --- |
| `sc-parse` | parse tree-sitter asset 语法 | 改为最终 `parse`，或迁移期间临时 `asset-parse`。 |
| `sc-lint` | lint tree-sitter asset 语法 | 改为最终 `lint`，或迁移期间临时 `asset-lint`。 |
| `sc-project` | 把 asset graph block 投影成 flow graph | 改为最终 `project`，或迁移期间临时 `asset-project`。 |
| `sc-patch` | 应用 tree-sitter-aware text patch | 改为最终 `patch`，或迁移期间临时 `asset-patch`。 |
| `parse` | 旧手写 `.gs` parser | 替换为新 tree-sitter parse 行为。 |
| `compile` | 旧 compiler | 删除或改为新 semantic/projector 行为。 |
| `emit` | 旧 emitter | 删除或改为新 formatter/source 输出。 |
| `edit` / `serve` | 编辑器入口 | 保持稳定。 |

推荐最终命令面：

```text
parse
lint
project
patch
edit
serve
```

迁移期间允许临时 `asset-*` 命令用于测试；不要把 `legacy-*` 命令作为受支持接口保留下来。

## 后缀替换清单

第一步把 `.sc/.d.sc` 改成 `.gs/.d.gs`：

- `docs/syntax/design/ai-native-syntax-draft.md`
- `docs/syntax/design/ai-native-asset-format.md`
- `tools/tree-sitter-graphscript-asset/package.json`
- `tests/test_asset_language.cpp`
- `cli/main.cpp` help 文案和命令说明
- `AGENTS.md`
- 所有传给 `gs::asset::Parser` 的 source name 字符串

然后扫描仓库：

```bash
rg "\.d\.sc|\.sc\b"
```

预期结果：无残留；除非明确保存在归档说明中。

## 旧功能清单提取

删除旧实现前，需要记录旧手写图 DSL 当前提供的所有行为，并标注如何用新语法满足。

输出文档：

```text
docs/spec/migration/legacy-graph-dsl-feature-inventory.md
```

| 旧功能区域 | 需要检查的文件 | 清单目标 |
| --- | --- | --- |
| 文件结构 | `include/graphscript/parse/ast.h`、`src/parse/parser.cpp` | import、let、declaration、graph、event、function、generate block。 |
| 声明系统 | `src/parse/parser.cpp`、`src/compile/compiler.cpp`、`presets/*.d.gs` | type、node、pin、schema、tag、default、annotation。 |
| 校验 | `src/schema/*`、`src/compile/compiler.cpp` | connection policy、scope rule、duplicate check、diagnostic。 |
| 编辑 | `include/graphscript/edit/edit_session.h`、`src/edit/edit_session.cpp` | graph/node/param/connection/comment/metadata/source-patch 操作。 |
| Runtime | `include/graphscript/runtime/runtime_graph.h`、`src/runtime/runtime_graph.cpp` | bake model、runtime source mapping、node/pin/edge layout。 |
| 输出 | `include/graphscript/emit/emitter.h`、`src/emit/emitter.cpp` | round-trip 期望和 diagram 输出。 |
| Debug dump | `include/graphscript/debug/dump.h`、`src/debug/dump.cpp` | 测试或工具依赖的 JSON/text debug 输出。 |
| CLI 编辑器 | `cli/editor.cpp` | 用户可见命令和 source patch guard。 |
| Web 后端 | `cli/server.cpp`、`cli/source_diagnostics.cpp` | `webapp` 消费的 HTTP/API 合约。 |

## `AGENTS.md` 补充要求

根目录必须新增或更新 `AGENTS.md`，作为后续 agent 进入仓库时优先读取的项目级指令。

建议内容：

```markdown
# AGENTS.md

## 回复语言

总是使用中文回复。

## 当前迁移方向

GraphScript 正在从旧手写图 DSL 迁移到 tree-sitter asset 新语法。

- 新方案是 `include/graphscript/asset/language.h`、`src/asset/language.cpp`、`tools/tree-sitter-graphscript-asset/`。
- 新方案统一使用 `.gs` 和 `.d.gs` 后缀。
- 旧手写 `parse/compile/emit` 管线不需要保留语法兼容。
- 旧功能必须用新方案重新满足。
- 编辑器相关能力必须保留：CLI editor、Web editor、source diagnostics、可视化编辑、patch/apply 流程。
- 允许破坏性更新，但不要删除编辑器能力，除非同一步提供新实现。

## 工作边界

- 优先修改 tree-sitter asset 新语法相关文件。
- 删除旧 parser/compiler/emitter 前，先确认对应功能已经用新方案覆盖。
- 文档、测试、CLI help 中不得继续把 `.sc/.d.sc` 作为当前后缀。
```

## 新语法映射

| 旧概念 | tree-sitter asset 新语法目标 |
| --- | --- |
| `.d.gs declare type` | `export declare type Name;` |
| `.d.gs declare node` | `export declare node NodeType { ... }` |
| 旧 `exec/data/input/output` pin | `declare node` 内的 `input` / `output` / `param` member |
| 旧 schema | `export declare schema Name: FlowGraphSchema { ... }` |
| `Graph Name : Schema` | `graph Name { schema Schema; ... }`，AST 为 `Block(kind=graph)` + `Command(schema)` |
| graph parameter | 变量/参数属性化，见 Q4 决策 |
| node instance | `node alias { type Type; ... }`，AST 为 `Block(kind=node)` + `Command(type)` |
| initializer field | node/object property |
| flow connection | `connect(from, to);`，AST 为 `Command(connect)` |
| data link | `bind(source, target);`，AST 为 `Command(bind)` |
| event/function block | `event Name { ... }` / `function Name { ... }`，AST 为对应 kind 的 Block |
| generate comment | editor/domain metadata property 或带 annotation 的 comment item |
| layout metadata | `editor.pos`、`editor.size` property；不使用 `@editor.*` 承载普通 metadata |

## 分阶段工作

### Phase 0：rebase 并确认基线

- fetch 最新远端。
- rebase 到指定目标分支。
- 确认 tree-sitter 实现存在，测试仍引用当前 `.sc/.d.sc` 名称，然后再开始修改。

### Phase 1：后缀迁移

- 用户可见后缀从 `.sc/.d.sc` 改为 `.gs/.d.gs`。
- 更新 `tests/test_asset_language.cpp`。
- 更新 CLI help 和命令名称。
- 新增或更新根目录 `AGENTS.md`，写明新旧边界、破坏性迁移原则、编辑器保留要求、中文回复要求。
- 执行 `rg "\.d\.sc|\.sc\b"` 验证无残留。

### Phase 2：破坏性提升新 CLI 路径

- 用 tree-sitter parse/lint/project/patch 替换旧 `parse/compile/emit` 命令行为。
- 移除 `sc-*` 名称。
- 为最终 CLI 名称补测试。

### Phase 3：提取旧功能清单

- 写 `docs/spec/migration/legacy-graph-dsl-feature-inventory.md`。
- 标注哪些行为已由 `gs::asset::Parser`、`Linter`、`FlowGraphProjector`、`Patcher` 覆盖。
- 缺失行为转成实现任务。

### Phase 4：编辑器替换层

- 新增或重写 editor/session 层，让它消费 tree-sitter asset projection，不保留旧语法。
- 候选路径：
  - `include/graphscript/asset/editor_adapter.h`
  - `src/asset/editor_adapter.cpp`
- adapter 负责在当前编辑器状态 JSON 和新 projected graph model 之间转换。

### Phase 5：删除旧 parser/compiler/emitter

- editor/session 能完成加载、编辑、诊断、投影、保存、patch 新语法后，删除旧 parser/compiler/emitter。
- runtime/validation 模块只有在消费新 projected model 时才保留。

### Phase 6：删除旧代码

编辑器相关测试通过后，删除 legacy-only 代码：

- `include/graphscript/parse/*`
- `src/parse/*`
- `include/graphscript/compile/*`
- `src/compile/*`
- `include/graphscript/emit/*`
- `src/emit/*`
- 旧 parser/compiler/emitter 测试

不要在这个阶段删除 editor/server/webapp。

## 验证计划

最低本地检查：

```bash
rg "\.d\.sc|\.sc\b"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/gs_tests.exe
```

如果当前环境没有 CMake，需要明确记录，并至少执行静态扫描。

adapter 工作后的编辑器回归检查：

```bash
./build/Release/gs.exe parse -i tests/fixtures/minimal_asset.gs
./build/Release/gs.exe project -i tests/fixtures/minimal_asset.gs --graph Execute
./build/Release/gs.exe serve
```

## 待决问题决策队列

后续逐项确认，每次只解决一个问题。确认后把结论写回本文档，再进入下一项。

### Q1：CLI 命令是否允许迁移期临时名称？（已决）

结论：选择方案 A。

- 直接覆盖最终命令 `parse/lint/project/patch`。
- 不保留 `sc-*`。
- 不引入 `asset-*` 作为迁移期受支持命令。
- 原因：本迁移允许破坏性更新，旧语法不需要保留；直接覆盖可以减少双轨命令和测试负担。

备选方案记录：

- 方案 A：直接覆盖最终命令 `parse/lint/project/patch`，不保留 `sc-*` 或 `asset-*`。
- 方案 B：迁移期先提供 `asset-parse/asset-lint/asset-project/asset-patch`，验证完成后再覆盖最终命令。

### Q2：旧 event/function block 如何映射到通用 block/scope？（已决）

结论：

- 作者语法不使用 `scope` 关键字。
- 语法/AST 层命名为 `Block`，例如 `BlockDecl(kind=graph, name=Execute)`；`schema AbilityGraph;` 是 block body 里的 `Command`。
- 语义层保留 scope 概念，Block 绑定后形成 `ScopeSymbol`，用于名字空间、嵌套、可见性和引用解析。
- IR 层使用 `ObjectIR` / `CommandIR` / `PropertyIR` / `FragmentIR` 表达资产事实。
- `graph`、`event`、`function`、`entry`、`table`、`row` 都是 block kind；是否允许、如何投影，由对应 domain 验证。

示例：

```gs
graph Execute {
    schema AbilityGraph;

    event BeginPlay {
        connect(context.start, log.enter);
    }

    function ComputeDamage {
        @graph.input
        param amount: float;

        connect(context.start, calc.enter);
    }
}
```

分层结果：

```text
Syntax:   BlockDecl(kind=graph/event/function, ...)
Semantic: ScopeSymbol(...)
IR:       ObjectIR + CommandIR + PropertyIR
Domain:   Graph / EventBlock / FunctionBlock / Edge
```

### Q2A：kind 能否在 `.d.gs` 中定义？（已决）

结论：

- `.d.gs` 可以定义新的 block kind、command kind、declaration kind、member kind、attribute kind。
- `.d.gs` 不定义新的 AST 节点家族；`Block`、`Property`、`Command`、`Expr` 这类节点家族由 C++/grammar 固定。
- `declare kind block graph;` 负责定义 kind symbol。
- `declare block graph { ... }` 负责定义这个 kind 的 contract。
- 定义 kind 和定义 kind 的声明规则是两个不同层次，语义模型中需要分开存储。
- Graph 标准 kind 应作为内建 `.d.gs` prelude 加载，而不是写死在 parser 关键字里。

示例：

```gs
export declare kind block graph;
export declare kind block dialog;
export declare kind command connect;
export declare kind command schema;

export declare block dialog {
    allows property *;
    allows command connect;
    optional command schema;
}
```

### Node 类型声明与使用

GraphDomain 中需要区分三件事：

1. `node` 是 block kind，表示源码里可以出现一种图节点实例 block。
2. `Print` 是 node declaration/type symbol，表示一种可实例化的节点类型。
3. `log` 是 `Print` 的一个实例。

因此 `.d.gs` 里不应该写成 `declare object Print`。更准确的写法是先定义 `node` 这种 block kind，再声明一个 `node` 类型：

```gs
export declare kind block node;
export declare kind declaration node;
export declare kind member param;
export declare kind member input;
export declare kind member output;
export declare kind member property;

export declare block node {
    requires command type;
    resolves type from declaration node;
    allows property *;
}

export declare node Print {
    param message: string = "done";
    input enter: exec;
    input text: string;
    output done: exec;
    output printed: string;
    property color: string = "white";
}
```

这里 `param message: string = "done";` 是声明文件里的接口签名语法，不是 Block 头部的 `: Type` 语义。Block 头部只写 `kind + name`，类型放在 body command 中：

```gs
graph Execute {
    schema AbilityGraph;

    node log {
        type Print;
        message: "hello";
        color: "green";
        editor.pos: [100, 200];
    }

    event BeginPlay {
        connect(context.start, log.enter);
        bind(player.name, log.text);
    }
}
```

实例 block 中不重复声明 `enter/text/done/printed` 这些 pin；它们来自 `Print` 的 node declaration。实例 body 只写参数值、属性值和 editor/domain metadata。GraphDomain 绑定时把 `log.enter`、`log.text`、`log.done` 解析到 `Print` 声明里的 input/output member。

### Q3：data link 最终用哪种语法？（已决）

结论：选择方案 C。

- flow/topology connection 使用 `connect(...)`。
- data link 使用 `bind(source, target)`。
- 两者都 lower 成 `CommandIR`。
- GraphDomain 负责验证 pin 类型、方向、fan-in/fan-out 和 schema policy。
- 不使用 assignment 表达 data link，避免和“设置默认属性值”混淆。
- 第一版 canonical formatter 只输出 `connect(...)` / `bind(...)`。如果 parser 支持 `a.connect(b)`，只能作为可选输入形式并 normalize 为 canonical command，不作为推荐写法。

示例：

```gs
graph Execute {
    schema AbilityGraph;

    node log {
        type PrintString;
        message: "default";
    }

    event BeginPlay {
        connect(context.start, log.enter);
        bind(player.name, log.message);
    }
}
```

备选方案记录：

- 方案 A：和 flow 一样使用 `.connect(...)`。
- 方案 B：使用 assignment，例如 `target.pin = source.pin;`。
- 方案 C：使用显式 command，例如 `bind(source, target);`。

### Q4：graph parameter / value declaration 使用哪种表面语法？（已决）

备选方案记录：

- 方案 A：使用 `param` 声明型 Command，并用长名 attribute 标注方向，类型/默认值使用 command 签名语法。

  ```gs
  @graph.input
  param target: Actor;

  @graph.output
  param result: bool;
  ```

- 方案 B：统一用 `Command` 表达，不引入 `param` 专用表面语法。

  ```gs
  declare_param(target, Actor, direction: input);
  ```

- 方案 C：把参数也表达成 `Block(kind=object)` 或普通 property，不引入变量/参数声明。

结论：选择方案 A。

- `param` 是声明型 Command，不是新的 AST 大类。
- 参数方向使用长名 attribute：`@graph.input`、`@graph.output`。
- 参数类型和默认值使用 command 签名语法；这只适用于 `param` 这种声明型 Command，不是 Block header 语义：

  ```gs
  @graph.input
  param target: Actor;

  @graph.input
  param amount: float = 50.0;

  @graph.output
  param result: bool;
  ```

- AST 可以表示为 `Command(kind=param, name=target, signature_type=Actor, attributes=[graph.input])`。
- GraphDomain 根据 attribute 和 command signature 投影为 graph parameter/pin。
- 原因：参数需要声明名、类型、默认值、attribute 和 source binding；结构化 `param` 比普通 call command 更利于补全、rename、patch 和阅读，同时不增加新的 AST 大类。

### Q5：RuntimeGraph 保留还是替换？（已决）

结论：选择方案 B。

- 替换为新的 graph runtime IR。
- 不把旧 `RuntimeGraph` 作为长期 public runtime type 保留。
- 不采用兼容外壳作为目标架构。
- 新 runtime IR 应消费 `graph/model` 或 `graph/projection` 产物，而不是依赖 `EditGraph`。
- 现有依赖 `RuntimeGraph` 的测试、debug dump 和 runtime bake 路径需要迁移或改写到新 IR。
- 原因：本迁移已经允许破坏性更新；runtime 层应趁迁移机会摆脱旧 `EditGraph`/旧 compiler 管线形状，避免把旧 API 假设继续固化到 tree-sitter 方案里。

备选方案记录：

- 方案 A：保留 `RuntimeGraph` 类型，但输入改为新 `graph/model` 或 `graph/projection`。
- 方案 B：替换为新的 graph runtime IR。
- 方案 C：保留兼容外壳，内部换成新 runtime IR。
