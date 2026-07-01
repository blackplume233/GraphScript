# AI Native 资产格式设计记录

> 状态：讨论草案。
> 目的：把关于 GraphScript 语法、图/文本互转、通用游戏资产序列化、Roslyn-like 架构的持续讨论沉淀成可迭代文档。

---

## 1. 方向

GraphScript 不应只是一门图 DSL，而应逐步演进为一种通用的、类型化的、文本优先的游戏资产序列化格式。

图编辑器是这个格式的第一个投影，也是最能检验设计复杂度的投影，但它不应该是全部产品边界。同一套语法和 compiler-service 层应当能够支撑图、HTN、关卡脚本、表、对话、任务、技能资产，以及其他游戏开发模块。

核心模型：

```text
文本资产源文件
  -> 无损语法树
  -> 可部分绑定的语义模型
  -> Command/Object IR
  -> 领域投影
  -> Runtime/Baked IR
```

源语言采用 TypeScript-flavored 表层语法，但不能变成通用 TypeScript 运行时语言。它应该是一种静态、类型化、可错误恢复的序列化语言。

---

## 2. 核心目标

### 2.1 AI Native 编辑

格式必须适合 AI agent 操作：

- 文本是唯一真源。
- 每个资产事实附近都有足够上下文，便于安全修改。
- 诊断信息必须结构化，包含期望类型、候选符号、相关范围、修复建议等信息。
- 即使文本非法或不完整，也应尽可能产出部分 AST、部分语义模型和部分领域投影。
- AI 应当可以根据编译器诊断修复文件，而不是依赖隐藏的编辑器状态。

### 2.2 文本编辑和可视化编辑都是一等能力

系统必须支持精准双向编辑：

```text
文本编辑 -> Syntax/Semantic/Projection 更新 -> 可视化编辑器更新
可视化编辑 -> AST 感知的 source patch -> 文本更新
```

可视化编辑器不应因为普通操作重写整个文件，而应 patch 最小稳定语法范围：

- 创建对象/节点
- 删除对象/节点
- 更新属性
- 创建/删除连接
- 更新节点位置
- 更新表格单元格
- 重命名 scope

### 2.3 通用序列化，而不是图专用语法

语法树应该只包含通用概念，而不是图专用概念。

推荐的通用语法概念：

- module
- import
- scope
- const/object declaration
- object expression
- property
- assignment
- call
- reference
- literal
- array
- annotation/attribute
- missing/error node

Graph、entry、node、pin、link、edge、HTN task、table row、dialogue branch 等概念都应该在 binding 和 domain projection 阶段解释出来。

其中 `pin` 不应成为基础语法概念。它可以由普通字段上的 attribute/meta 表达，并由 FlowGraph projection 解释。

### 2.4 序列化层和 Graph 层必须分开设计

后续开发必须先区分两套概念，再决定语法和实现归属：

```text
序列化层 Serialization Layer
  负责描述资产事实：文件、导入、资产、片段、对象、属性、值、引用、属性标注、声明和 schema。

Graph 层 Graph / FlowGraph Layer
  负责把序列化事实投影成图：图、节点、入口、pin、edge、flow/link、连接策略、图运行时和图调试。
```

序列化层可以不知道某个对象未来会不会被投影成节点。它只需要保证这个对象、属性和值能被精确解析、绑定、诊断和 patch。

Graph 层不能要求 parser 或基础 AST 内建 `Graph`、`Node`、`Pin`、`Edge` 等领域节点。它应通过 schema、attribute、binder 和 projection 解释序列化层产物。

#### 2.4.1 序列化层最小概念

第一版讨论和实现序列化结构时，只允许使用这些基础概念：

| 概念 | 责任 | 示例 |
| --- | --- | --- |
| `File` | 一个可解析和可 patch 的源文本单元 | `.gs` / `.d.gs` |
| `Import` | 声明依赖，不执行代码 | `import "core.d.gs"` |
| `Asset` / `Scope` | 一个逻辑资产或资产片段边界 | `scope asset Fireball: Ability` |
| `Fragment` | 对已有逻辑资产的一段贡献 | `@for("ability.fireball") scope tuning ...` |
| `Object` | 类型化可序列化对象 | `const damage = new DamageEffect { ... }` |
| `Property` | 对象或 scope 上的命名字段贡献 | `damage: 50` |
| `Value` | 静态值、数组、inline object 或引用 | `"Fireball"`, `[1, 2]`, `ref "..."` |
| `Reference` | 可绑定的本地、符号或外部资产引用 | `damage`, `DamageType.Fire`, `ref "/Game/..."` |
| `Attribute` / `Metadata` | 附加工具、编辑器或领域元信息 | `@id(...)`, `@editor.field` |
| `Declaration` / `Schema` | 类型、对象形状、scope kind、命令、lint 和依赖声明 | `declare object Ability { ... }` |

这些概念可以支持表、对话、任务、技能、关卡数据和图投影，但它们本身不是任何一个领域模型。

#### 2.4.2 Graph 层概念不得倒灌

以下概念不得成为序列化层 AST/CST 的基础节点或 parser 关键语义：

```text
Graph
GraphNode
Entry
Pin
ExecPin
DataPin
Edge
Flow
Link
HTNTask
TableRow
DialogueBranch
```

如果需要在源文本里表达这些内容，应先表达成序列化层事实，再由 Graph/FlowGraph/Table/Dialogue projection 解释。例如：

- 图节点候选：类型化 `Object`。
- pin 候选：声明文件里带 attribute 的 `Field`。
- 连接候选：普通 `Property`、受限 `Command`，或 JSON-like `edges` 数据结构。
- entry 候选：某个 `Scope` 或属性集合。
- editor 位置：普通 metadata/property，例如 `editor.pos` 或 `editor: { pos: [...] }`。

这样可以先讨论和稳定序列化结构，再单独讨论 GraphProjection 如何映射它。

---

## 3. 非目标

- 基础资产格式不做完整 TypeScript、Python、C# 或 AngelScript 运行时。
- 表层语法选择 TypeScript-flavored，不采用 Python 缩进语法。
- 可逆作者子集不允许任意控制流。
- parser 不应该认识所有领域概念。
- 图编辑不能依赖整文件重新 emit。
- JSON/YAML 不作为主要作者格式，但可以有导入/导出适配器。

---

## 4. 硬性要求

### 4.0 Syntax Tree / CST 和 AST 的区别

本文档里需要区分两个容易混用的概念。

```text
Source Text
  -> Syntax Tree / CST
  -> AST
  -> Semantic Model / Command IR
```

Syntax Tree / CST（Concrete Syntax Tree，具体语法树）更贴近源码。它应该保留：

- token
- 空白、换行、注释等 trivia
- 标点符号
- source range
- missing token/node
- skipped/error token

它主要服务：

- round-trip
- 精准 source patch
- formatter
- IDE 高亮
- 错误恢复
- 保留用户注释和局部格式

AST（Abstract Syntax Tree，抽象语法树）更贴近语义结构。它可以从 CST 派生，通常不关心空格、注释、分号、花括号 token 本身等细节。

它主要服务：

- binding
- 类型检查
- lowering
- lint
- domain projection

对 GraphScript 来说，编辑器的根基应该是 lossless Syntax Tree/CST，而不只是 AST。因为图编辑、属性面板和 AI patch 都需要尽可能精准地映射回原始文本。

### 4.1 无损语法树

parser 必须保留足够信息，以支持精准 round-trip 和 source patch：

- token
- trivia：空白、注释、分隔符
- source range
- missing syntax node
- skipped/error token
- object/property/call range
- reference 和 argument 的子范围

这是设计里 Roslyn-like 的部分：语法树不是临时 parser 产物，而是编辑器面对的 document model。

### 4.2 错误容忍

错误文本不是失败输入，而是带洞的资产草稿。

示例：

```ts
scope graph Execute: AbilityGraph {
    const apply = new ApplyDamage {
        amount: 50
        target:
    }

    context.start.connect(apply.enter
}
```

系统仍应恢复出：

```text
Scope graph Execute
Object apply : ApplyDamage
  amount = 50
  target = MissingExpr
Call context.start.connect(apply.enter, MissingParen)
```

图投影可以显示部分有效/部分非法的节点和边，并把诊断挂在对应位置。

### 4.3 领域专属 Lint

不同上层模块必须能注册自己的 lint provider。

示例：

```text
Core asset lint:
  未知类型
  未知属性
  对象 alias 重复
  引用非法
  缺少稳定 id

Graph lint:
  未知 pin
  pin 方向非法
  类型不匹配
  fan-in/fan-out 违反 schema
  必需入口未连接

HTN lint:
  缺少 root task
  task/method/decorator 层级关系非法
  分解图存在环

LevelScript lint:
  缺少 OnBeginPlay
  construction/runtime 节点使用场景非法
  actor 或 asset 引用无法解析

Table lint:
  row key 重复
  必填列缺失
  enum 单元格非法
  跨行引用失效
```

lint 不属于 parser 逻辑。它应由 scope kind、schema、asset type、project config 或 engine adapter 注册。

### 4.4 部分编译

每一层都应该保留非法对象，而不是丢弃整个 scope：

```text
Syntax Layer:
  MissingExprAst
  ErrorAst
  SkippedToken

Binding Layer:
  UnknownType
  ErrorType
  UnresolvedSymbol
  MissingValue

Command IR:
  InvalidCommand
  PartialCreateObject
  PartialConnect

Domain Projection:
  InvalidNode
  InvalidEdge
  InvalidRow
```

### 4.5 稳定身份

只靠人类可读 alias 不足以支撑长期游戏资产。

格式应该支持稳定对象身份：

```ts
const damage @id("01J2...") = new DamageEffect {
    amount: 50;
}
```

具体语法仍开放，但模型必须区分：

- source alias：便于阅读和编辑
- stable id：用于重命名、合并和外部引用时保持稳定

稳定身份还用于支持多个 scope/object fragment 共同描述同一个逻辑对象。不同 fragment 必须通过 `@id(...)` / `@for(...)` 或等价机制显式关联，不能依赖同名 scope 隐式合并。

### 4.6 未知字段保留

工具应尽可能保留未知字段和未知领域数据。这对插件、引擎版本差异、前向兼容很重要。

使用旧 schema 的工具打开新文件时，不应因为无法绑定某些字段就破坏这些数据。

### 4.7 多 Fragment 对象

系统应允许同一个 logical object 由多个 source fragment 共同描述。

示例：

```ts
scope asset Fireball: Ability @id("ability.fireball") {
    name: "Fireball";
}

scope tuning FireballBalance: AbilityTuning @for("ability.fireball") {
    damage: 50;
    cooldown: 3.0;
}

scope graph FireballExecute: AbilityGraph @for("ability.fireball") {
    // graph behavior fragment
}
```

绑定后形成：

```text
LogicalObject ability.fireball : Ability
  fragments:
    asset Fireball
    tuning FireballBalance
    graph FireballExecute
```

要求：

- 每个 fragment 有独立 source range。
- 每个属性贡献有独立 source binding。
- 默认不允许同一属性被多个 fragment 无规则覆盖。
- 冲突应产生 diagnostic。
- schema 可以声明特定属性或 fragment kind 的合并策略。
- 编辑器修改属性时，应优先 patch 原贡献所在 fragment。

---

## 5. 临时源语法形态

这不是最终语法，只是讨论锚点。

```ts
scope asset Fireball: Ability {
    const damage = new DamageEffect {
        amount: 50;
        type: DamageType.Fire;
    }

    scope graph Execute: AbilityGraph {
        input target: Actor;
        input amount: float;

        const apply = new ApplyDamage {
            effect: damage;
            target: target;
            editor.pos: [100, 100];
        }

        const log = new PrintString {
            message: "done";
            editor.pos: [360, 100];
        }

        scope entry Start {
            context.start.connect(apply.enter);
            apply.exit.connect(log.enter);
            apply.result.connect(log.message);
        }
    }

    scope table Tuning: DataTable<AbilityLevel> {
        scope row Level1 {
            damage: 50;
            cooldown: 3.0;
        }

        scope row Level2 {
            damage: 75;
            cooldown: 2.5;
        }
    }
}
```

关键点：

- `scope` 是通用语法。
- `asset`、`graph`、`entry`、`table`、`row` 是 scope kind，由语义层/领域层解释。
- `const name = new Type { ... }` 在当前 scope 中创建一个类型化序列化对象。
- `.connect(...)` 是受限 command call，不是运行时函数调用。
- object property 是静态序列化数据，不是任意表达式。
- `editor.*` 暂时用于区分编辑器元数据；最终语法仍开放。

---

## 6. 通用 AST 目标

长期 AST 应该是通用的：

```cpp
struct ModuleAst {
    std::vector<ImportAst> imports;
    std::vector<ItemAst*> items;
};

struct ScopeAst : ItemAst {
    std::string kind;
    std::string name;
    std::optional<TypeRefAst> type;
    std::vector<ParamAst> params;
    std::vector<ItemAst*> items;
};

struct ConstAst : ItemAst {
    std::string name;
    ExprAst* value;
};

struct ObjectExprAst : ExprAst {
    TypeRefAst type;
    std::vector<PropertyAst> properties;
};

struct PropertyAst {
    std::string name;
    ExprAst* value;
};

struct CallStmtAst : ItemAst {
    ExprAst* callee;
    std::vector<ExprAst*> args;
};

struct AssignStmtAst : ItemAst {
    ExprAst* target;
    ExprAst* value;
};

struct RefExprAst : ExprAst {
    std::vector<std::string> path;
};

struct LiteralAst : ExprAst {};
struct ArrayAst : ExprAst {};
struct MissingExprAst : ExprAst {};
struct ErrorAst : ItemAst {};
```

长期架构里，AST 不应包含专门的 `GraphNode`、`EventNode`、`NodeInstanceNode`、`FlowStmtNode` 或 `LinkStmtNode`。这些是领域解释，不是通用语法。

---

## 7. Command/Object IR

语义层应把源语法 lower 成通用 Command/Object IR。

示例源代码：

```ts
const apply = new ApplyDamage {
    amount: 50;
    editor.pos: [100, 100];
}

context.start.connect(apply.enter);
```

可能的 Command IR：

```text
CreateObject alias=apply type=ApplyDamage loc(...)
SetProperty object=apply path=amount value=50 loc(...)
SetProperty object=apply path=editor.pos value=[100,100] loc(...)
CallCommand kind=connect callee=context.start args=[apply.enter] loc(...)
```

Graph projection 可以把同一份 IR 解释为：

```text
CreateNode apply : ApplyDamage
SetNodeDefault apply.amount = 50
SetNodeEditorPosition apply = [100,100]
CreateEdge context.start -> apply.enter
```

Table projection 可以把 scope 解释为：

```text
CreateTable Tuning : DataTable<AbilityLevel>
CreateRow Level1
SetCell Level1.damage = 50
```

---

## 8. Domain Projection 契约

Domain Projection 是把通用 `Command/Object IR` 解释成某个领域模型的阶段。它不是 parser，也不是通用 AST 的一部分。

最低定位：

```text
CST / AST
  -> SemanticModel
  -> Command/Object IR
  -> DomainProjection
  -> AuthoringModel / RuntimeBake
```

Projection 的核心价值是让 Graph、FlowGraph、HTN、关卡图、表、对话树等模块共享同一套文本、语法树、patch 和 lint 基础设施，而不用修改 parser。

### 8.1 Projection Provider

建议每个领域模块注册一个 `ProjectionProvider`。

```cpp
class ProjectionProvider {
public:
    virtual ProjectionResult project(
        const Document& document,
        const SemanticModel& semantic_model,
        const CommandObjectIR& ir,
        const ScopeSymbol& scope
    ) = 0;
};
```

`ProjectionProvider` 的输入只应依赖：

- 当前 `Document` 和无损 CST/AST。
- `SemanticModel` 里的符号、类型、声明、fragment 聚合结果。
- 通用 `Command/Object IR`。
- `.d.gs` 中声明的 object、scope、schema、command、attribute、lint 元数据。
- 当前投影入口 scope，例如 `scope graph Execute: AbilityGraph` 或 `scope table Tuning: DataTable<RowType>`。

`ProjectionProvider` 不应直接修改文档。任何图操作、表操作或 AI 修复都应返回可解释的 edit operation，再交给 AST/Text Framework 生成 `TextPatch`。

### 8.2 ProjectionResult

最低结果模型：

```cpp
struct ProjectionResult {
    DomainId domain;
    SymbolId scope_symbol;
    std::shared_ptr<DomainModel> model;
    std::vector<Diagnostic> diagnostics;
    std::vector<SourceBinding> source_bindings;
    std::vector<InvalidDomainItem> invalid_items;
    ProjectionDelta delta;
};
```

要求：

- `model` 是领域 authoring model，不一定是最终 runtime IR。
- `diagnostics` 可以来自 projection 阶段，例如无效 pin、非法 row key、HTN root 缺失。
- `source_bindings` 必须能把领域对象映射回文本。
- `invalid_items` 必须保留无法完整投影的对象、连接、行、单元格或任务节点。
- `delta` 用于增量编辑、图刷新、调试高亮和 AI 解释变更。

错误文本下也应尽量生成 `ProjectionResult`。失败不应退化成“没有图”，而应退化成“部分图 + diagnostics + invalid items”。

### 8.3 通用投影规则

所有领域投影共享这些规则：

- parser 不知道领域；projection 只消费 CST/AST、SemanticModel 和 Command/Object IR。
- `scope kind` 选择投影入口，例如 `graph`、`table`、`asset`、`level`。
- `scope type` 或 schema 选择具体领域规则，例如 `AbilityGraph`、`HTNGraph`、`DataTable<AbilityLevel>`。
- `const alias = new Type { ... }` 是对象贡献，不默认等价于图节点；是否成为 node/row/task 由 projection 决定。
- `property: value` 是对象或 scope 的字段贡献；是否成为 default value、pin binding、cell、editor metadata 由 projection 决定。
- `CallCommand` 是命令贡献；是否成为 edge、binding、transition、spawn relation 由 projection 决定。
- attribute/meta 是领域解释的主要扩展点，例如 `@flow.pin(...)`、`@table.key`、`@editor.field`。
- 多 fragment 聚合先发生在 semantic/binding 层，projection 消费 logical object 以及每个 contribution 的 source binding。

projection 不应发明不可回源的领域对象。每个 node、edge、row、cell、task、transition 至少要有一个 source range 或 synthetic source binding。

### 8.4 GraphProjection

GraphProjection 是最基础的图 authoring model。它只定义通用图结构，不定义 FlowGraph 的执行语义。

建议映射：

| 源/IR | GraphProjection |
| --- | --- |
| `scope graph Name: Schema` | `Graph id=Name schema=Schema` |
| 嵌套 `scope entry Start` | `GraphEntry id=Start` |
| `CreateObject alias type` | `GraphNode alias type` |
| `SetProperty object.editor.pos` | `NodeEditorMetadata.position` |
| 普通 `SetProperty object.path` | `NodePropertyContribution` |
| `CallCommand connect(from, to)` | `GraphEdge from -> to` |
| 无法解析端点的 connect | `InvalidEdge` |

GraphProjection 不应要求 pin 已经完全合法。它可以先生成 `GraphEdge` 或 `InvalidEdge`，再由 FlowGraph/HTN 等更具体的 projection 或 lint 判断合法性。

### 8.5 FlowGraphProjection

FlowGraphProjection 在 GraphProjection 之上解释 FlowGraph 语义。

输入来源：

- `.d.gs` 的 object declaration。
- 字段上的 `@flow.input`、`@flow.output`、`@flow.pin(...)`。
- schema 中的 fan-in/fan-out、entry/context、allowed object、allowed command 规则。
- GraphProjection 生成的 node/property/edge。

建议映射：

| 源/声明 | FlowGraphProjection |
| --- | --- |
| `@flow.pin(kind = "exec", direction = "in") enter: Exec` | `ExecInputPin enter` |
| `@flow.pin(kind = "exec", direction = "out") exit: Exec` | `ExecOutputPin exit` |
| `@flow.input amount: float` | `DataInputPin amount` 或 node parameter |
| `@flow.pin(kind = "data", direction = "out") result: DamageResult` | `DataOutputPin result` |
| `apply.exit.connect(log.enter)` | `FlowEdge apply.exit -> log.enter` |
| 类型不兼容的连接 | `InvalidFlowEdge` + diagnostic |

FlowGraphProjection 的关键约束：

- exec pin 和 data pin 的方向、fan-in/fan-out、类型兼容由 projection/lint 判断，不进入 parser。
- 未知节点类型仍保留为 `UnknownNode`，并带 source range。
- 未知 pin 仍保留为 `InvalidPinRef`，用于图上显示断线或错误端点。
- editor-only metadata 不进入 engine runtime，但 authoring model 必须保留。

### 8.6 HTNProjection

HTN 不需要新的基础语法，也不需要 parser 识别 `task`、`method`、`decorator` 等专用节点。

HTNProjection 可以作为 FlowGraphProjection 的一个 schema/domain：

```ts
scope graph PatrolPlan: HTNGraph {
    const root = new HTN_Sequence {
        editor.pos: [0, 0];
    }

    const move = new HTN_MoveTo {
        editor.pos: [240, 0];
    }

    scope entry Decompose {
        context.start.connect(root.enter);
        root.child0.connect(move.enter);
    }
}
```

投影规则：

- `HTNGraph` schema 选择 HTNProjection。
- `HTN_Sequence`、`HTN_MoveTo` 是普通 object type，由 `.d.gs` 声明。
- `root`、`method`、`task`、`decorator`、`condition` 等语义由 object type、attribute 和 schema 决定。
- HTN root 缺失、循环分解、非法 child 端口等都是 domain diagnostic。
- bake 阶段可以生成 HTN runtime tree 或 planner data，但 authoring model 仍保持 graph/source binding。

这保证后续添加 BehaviorTree、QuestGraph、DialogueGraph 时不需要改变基础语法。

### 8.7 TableProjection

TableProjection 把 scope/object/property 解释为表结构。

建议映射：

| 源/IR | TableProjection |
| --- | --- |
| `scope table AbilityLevels: DataTable<AbilityLevel>` | `Table name=AbilityLevels row_type=AbilityLevel` |
| `scope row Level1` | `Row key=Level1` |
| row 内 `damage: 50` | `Cell row=Level1 column=damage value=50` |
| `@table.key` 字段 | row key source |
| 缺失必填列 | `InvalidCell` 或 row diagnostic |

TableProjection 的重点不是执行，而是：

- schema 校验。
- 表格视图和文本视图双向 patch。
- 单元格级 source binding。
- 行/列级 quick fix，例如添加缺失列、重命名字段、提取公共默认值。

### 8.8 多 Fragment 投影

当多个 scope/object fragment 描述同一个 logical object 时，projection 消费的是 semantic layer 聚合后的 logical object，同时保留每个 contribution 的 source binding。

示例：

```ts
@id("ability.fireball")
scope asset Fireball: Ability {
    name: "Fireball";
}

@for("ability.fireball")
scope tuning FireballBalance: AbilityTuning {
    damage: 50;
}

@for("ability.fireball")
scope graph FireballExecute: AbilityGraph {
    const apply = new ApplyDamage {
        amount: damage;
    }
}
```

projection 应看到：

```text
LogicalObject ability.fireball
  fragment asset -> source range A
  fragment tuning -> source range B
  fragment graph -> source range C
```

领域模块可以决定哪些 fragment 参与自己投影：

- Ability asset projection 消费 asset + tuning + graph 摘要。
- GraphProjection 只消费 graph fragment。
- TableProjection 只消费 table/row fragment。
- 打包/依赖分析可以消费所有 fragment。

### 8.9 Projection Source Binding

领域对象至少需要这些回源信息：

```text
Graph -> source scope range
GraphEntry -> source entry scope range
Node -> source const/object expression range
NodeProperty -> source property range
PinDefinition -> .d.gs field range
Edge -> source command call range
EdgeEndpoint -> source callee/member/argument range
Table -> source table scope range
Row -> source row scope range
Cell -> source property range
HTN task -> source object range
```

如果对象来自多个 contribution，binding 必须能表达 primary range 和 contribution ranges：

```cpp
struct DomainSourceBinding {
    DomainItemId item_id;
    SourceRange primary_range;
    std::vector<SourceRange> contribution_ranges;
    std::optional<SourceRange> name_range;
    std::optional<SourceRange> value_range;
};
```

这使属性面板、图拖拽、边重连、表格单元格编辑和 debug trace 都能定位到最小可 patch 区域。

### 8.10 Projection Delta

增量编辑时，projection 应输出领域层 delta。

```text
ProjectionDelta
  added_items
  removed_items
  changed_items
  invalidated_items
  source_range_changes
  diagnostics_delta
```

用途：

- 图编辑器只刷新受影响节点/边。
- 表格视图只刷新受影响行/单元格。
- 调试器把旧断点、watch、trace selection 迁移到新 source range。
- AI 修复可以解释“这次 patch 创建了 node X，修复了 edge Y，但留下 diagnostic Z”。

Delta 是 authoring runtime 的能力，不要求 engine runtime 保留。

---

## 9. Source Binding 要求

每个语义对象都应该携带 source binding 数据。

对于连接：

```ts
apply.exit.connect(log.enter);
```

绑定后的 command 应保留：

```text
commandRange: 整个 call
fromRange: apply.exit
toRange: log.enter
calleeRange: apply.exit.connect
argRange[0]: log.enter
```

这样可视化编辑才能只 patch 被影响的源代码范围。

---

## 10. AST/Text Framework 操作契约

AST/Text Framework 是文本资产的编辑核心。图编辑器、属性面板、AI 修复、lint quick fix 都不应直接拼接字符串，而应通过结构化操作生成 `TextPatch`。

### 10.1 核心对象

最低对象模型：

```cpp
struct SourceText {
    std::string text;
    std::string file_path;
    // line/column index, encoding policy, newline style
};

struct Document {
    SourceText source;
    SyntaxTree syntax;
    SemanticModel semantic;
    DiagnosticList diagnostics;
};

struct TextPatch {
    SourceRange range;
    std::string replacement;
};

struct EditTransaction {
    std::string operation;
    std::string semantic_target;
    std::vector<TextPatch> patches;
    std::vector<Diagnostic> diagnostics_before;
    std::vector<Diagnostic> diagnostics_after;
};
```

`SyntaxTree` 来自 Tree-sitter CST facade；`SemanticModel`、`DiagnosticList` 和 `EditTransaction` 由 GraphScript 自研。

### 10.2 RewriteBuilder

推荐使用 rewrite builder 收集结构化意图，再统一生成 patch：

```cpp
class RewriteBuilder {
public:
    void insert_scope(ScopeId parent, NewScope spec);
    void rename_scope(ScopeId scope, std::string new_name);
    void set_scope_type(ScopeId scope, TypeRef new_type);

    void insert_object(ScopeId parent, NewObject spec);
    void rename_object(ObjectId object, std::string new_alias);
    void set_object_type(ObjectId object, TypeRef new_type);

    void set_property(PropertyTarget target, Expr new_value);
    void remove_property(PropertyTarget target);

    void add_attribute(SyntaxTarget target, AttributeSpec attr);
    void set_attribute_arg(AttributeId attr, std::string arg, Expr value);
    void remove_attribute(AttributeId attr);

    void insert_call(ScopeId parent, CallSpec call);
    void replace_call_callee(CallId call, RefExpr new_callee);
    void replace_call_arg(CallId call, size_t index, Expr new_arg);
    void delete_call(CallId call);

    EditTransaction commit(Document& doc);
};
```

这些 API 是契约草案，不是最终 C++ 签名。重点是：调用方表达语义操作，framework 负责选择最小 source range 和格式。

### 10.3 基础编辑操作

每个基础操作都必须能说明 source binding 和 patch anchor。

| 操作 | 主要 source binding | patch 目标 |
|---|---|---|
| 插入 import | `source_file` | import 列表末尾或首个顶层 item 前 |
| 插入 scope | parent `scope_declaration.body` 或 `source_file` | body 插入点 |
| 重命名 scope | `scope_declaration.name` | name range |
| 修改 scope type | `scope_declaration.type` | type range；缺失时插入 `: Type` |
| 插入 object | parent scope body | body 插入点 |
| 重命名 object alias | `const_declaration.name` | name range |
| 修改 object type | `object_expression.type` | type range |
| 设置 property | `property_declaration.value` 或 object body | value range 或新增 property |
| 删除 property | `property_declaration` | 整个 property range |
| 添加 attribute | target declaration/field/property 前 | attribute list range |
| 修改 attribute 参数 | `attribute_argument.value` | value range |
| 插入 connect call | entry/scope body | body 插入点 |
| 修改 connect from | `call_expression.callee` | callee range |
| 修改 connect to | `argument_list[index]` | argument range |
| 删除 connect call | `call_statement` | 整个 statement range |

### 10.4 Patch 安全规则

生成 patch 时必须遵守：

- patch range 不允许重叠。
- 多个 patch 应按 source range 从后往前应用，或由统一 patch engine 保证稳定应用。
- 未修改区域的文本必须逐字保留。
- newline 风格沿用当前文件。
- 缩进优先从同级 sibling 推断；没有 sibling 时从父 scope 增加一级缩进。
- 删除节点时应保留必要空行，不制造连续三行以上空白。
- 插入 attribute 时默认紧贴目标声明上一行。
- 如果 source binding 指向 `ERROR` 或 missing node，优先在最近合法 parent body 中插入新 fragment，而不是重写整文件。
- patch 后必须重新 parse；如果语法诊断增加，EditTransaction 应标记为 failed 或 degraded。

### 10.5 错误文本下的编辑降级

错误文本仍应支持局部编辑，但需要降级策略：

```text
目标 range 完整:
  直接 patch 对应 range。

目标 value 缺失:
  patch MissingExpr 或插入默认表达式。

目标 property 损坏:
  在同一 object body 末尾插入新的 property fragment，并给旧 property 挂 diagnostic。

目标 call 损坏:
  如果 callee 可恢复，只 patch argument；否则插入新 call 并标记旧 call invalid。

目标 scope/object body 缺失:
  插入缺失 body 或创建新 fragment，避免整文件 emit。
```

### 10.6 EditTransaction 结果

每次编辑应返回可审计结果：

```text
EditTransaction:
  operation
  semantic_target
  patches
  touched_syntax_nodes
  diagnostics_before
  diagnostics_after
  projection_delta
  status: applied | degraded | rejected
```

AI native 的修复循环应优先消费 `diagnostics_after` 和 `projection_delta`，而不是只看文本 diff。

---

## 11. Lint Provider 模型

草案接口：

```cpp
class LintProvider {
public:
    virtual std::vector<Diagnostic> run(
        const Document& doc,
        const SemanticModel& sem,
        const ScopeSymbol& scope
    ) = 0;
};
```

诊断应当可以被机器消费：

```cpp
struct Diagnostic {
    Severity severity;
    std::string code;
    std::string message;
    SourceRange range;
    DiagnosticStage stage;
    std::vector<SourceRange> related_ranges;
    std::vector<std::string> expected_types;
    std::vector<std::string> candidate_symbols;
    std::vector<QuickFix> quick_fixes;
    std::string domain;
};
```

同一个 document 可以运行多个 provider：

```text
scope graph Plan: HTNGraph
  -> CoreAssetLint
  -> GraphLint
  -> HTNLint

scope table AbilityLevels: DataTable<AbilityRow>
  -> CoreAssetLint
  -> TableLint
```

### 11.1 DiagnosticStage

诊断必须标明来源阶段，方便 UI、AI 和构建系统决定是否继续处理：

```text
syntax
binding
command_lowering
domain_projection
lint
runtime_authoring
runtime_engine
```

示例：

```text
GS-SYN-001  syntax              缺少 ';'
GS-BND-014  binding             未知类型 ApplyDamage
GS-CMD-003  command_lowering    connect call 参数数量错误
GS-GPH-021  domain_projection   pin direction 不兼容
GS-HTN-010  lint                HTN root task 缺失
GS-RT-004   runtime_authoring   预览执行跳过 invalid node
```

### 11.2 Diagnostic code 规范

诊断 code 应稳定，供测试、AI 修复和 IDE 过滤使用。

推荐格式：

```text
GS-<DOMAIN>-<NUMBER>
```

其中：

```text
SYN  syntax/parser
BND  binding/semantic
CMD  command lowering
AST  AST/Text framework
REF  reference/import/dependency
GPH  graph projection
FLW  FlowGraph
HTN  HTN
LVL  LevelScript
TBL  Table
RUN  runtime/debug
```

code 一旦发布，不应随意改变含义。message 可以优化，code 语义要稳定。

### 11.3 QuickFix 契约

QuickFix 不应直接返回自然语言建议，而应返回可执行或可预览的结构化修复。

草案：

```cpp
struct QuickFix {
    std::string title;
    std::string code_action_kind; // quickfix, refactor, source.fixAll, source.organizeImports
    std::string diagnostic_code;
    std::vector<TextPatch> patches;
    std::string confidence;       // high | medium | low
    bool requires_user_choice;
};
```

示例：

```text
Diagnostic:
  GS-BND-014 unknown type "ApplyDamge"

QuickFix:
  title: Rename to "ApplyDamage"
  patches:
    replace range(type_ref ApplyDamge) with "ApplyDamage"
  confidence: high
```

涉及 schema/domain 语义的修复，应通过 `RewriteBuilder` 生成 patch，避免手写字符串替换。

### 11.4 AI 修复提示

Diagnostic 应包含足够信息，使 AI 能不依赖隐藏状态完成修复。

最低信息：

```text
code
severity
stage
message
primary range
related ranges
expected types
actual type
candidate symbols
quick fixes
domain
semantic target
```

示例：

```text
GS-GPH-021
stage: domain_projection
message: connect target expects exec input pin, got data field "amount".
expected_types: [ExecInputPin]
actual_type: FloatField
candidate_symbols:
  apply.enter
  apply.exit
quick_fixes:
  - Replace target with apply.enter
```

### 11.5 FixAll 和批量修复

允许 lint provider 暴露批量修复，但必须满足：

- 每个 patch range 不重叠。
- 修复前后都能重新 parse。
- 如果任一 patch 失败，整个 FixAll transaction 应 rejected 或回滚。
- FixAll 必须声明适用范围：document、scope、project。

示例：

```text
source.organizeImports
source.addMissingStableIds
source.convertLegacyPinsToAttributes
```

### 11.6 Lint Provider 分发

Lint provider 应按 scope kind、schema、asset type 和 project config 分发。

```text
source_file:
  SyntaxLint
  ImportDependencyLint

scope asset Fireball: Ability:
  CoreAssetLint
  AbilityLint

scope graph Execute: AbilityGraph:
  CoreAssetLint
  GraphLint
  FlowGraphLint
  AbilityGraphLint

scope table Tuning: DataTable<AbilityLevel>:
  CoreAssetLint
  TableLint
```

provider 不应修改文档。它只产出 diagnostics 和 quick fixes。实际修改必须通过 AST/Text Framework 的 edit transaction。

---

## 12. 外部参考

这些是设计参考，不是要直接复制的格式。

- Roslyn syntax model：语法树支撑编译、分析、绑定、重构、IDE 功能和代码生成。参考：https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/work-with-syntax
- Roslyn syntax trivia：空白和注释以 trivia 表示，保证源码保真。参考：https://github.com/dotnet/roslyn/blob/main/docs/wiki/Roslyn-Overview.md
- Unity text serialization/YAML：工业级文本化游戏资产序列化，以及直接修改资产文件的案例。参考：https://unity.com/blog/engine-platform/understanding-unitys-serialization-language-yaml
- Unity serialized file format：scene 会写成多个 YAML document，并带有对象身份。参考：https://docs.unity3d.com/6000.4/Documentation/Manual/FormatDescription.html
- Godot TSCN：人类可读的文本场景格式，包含资源、节点和连接。参考：https://docs.godotengine.org/en/4.4/contributing/development/file_formats/tscn.html
- LLVM IR：基于 SSA 的类型化 canonical IR，可作为稳定低层表示的参考。参考：https://llvm.org/docs/LangRef.html
- MLIR：可扩展 textual IR 和 dialect 模型，可作为多领域投影的参考。参考：https://mlir.llvm.org/docs/LangRef/

---

## 13. Roslyn 复用评估

Roslyn 是正确的架构参考，但大概率不适合作为 GraphScript 的核心依赖。

### 13.1 直接复用

直接使用 Roslyn 意味着解析 C# 语法，并基于 Roslyn 的 syntax tree、semantic API、diagnostics 和 workspace model 构建系统。

这与当前目标不太匹配：

- 宿主/runtime 侧是 C++。
- GraphScript 需要自定义静态资产语言，不是 C#。
- Roslyn 的 semantic model 围绕 C# 和 VB 语言规则设计。
- 可逆子集需要 `scope`、对象序列化、graph connection 等领域 command。
- 在引擎/编辑器集成里携带 .NET 编译器栈会比较别扭，尤其是 C++ 游戏引擎插件。
- Unreal 风格集成通常更偏好 native C++ 库，构建和部署行为更可控。

只有当源语言变成合法 C# 或 C# embedded DSL 时，直接复用 Roslyn 才比较自然。但这会削弱自定义资产格式的方向。

### 13.2 架构复用

这是推荐路线。

借用 Roslyn 的主要思想：

- lossless syntax tree
- trivia preservation
- immutable 或 persistent tree update
- syntax model 和 semantic model 分离
- tolerant parsing 和 missing node
- structured diagnostics
- workspace/document/project 抽象
- incremental parsing 和 binding
- code action 与 source patch API

但这些能力应该在 GraphScript 自己的 C++ 栈中实现，并服务自定义 grammar。

### 13.3 可选 Roslyn-side tooling

Roslyn 仍然可以在 C++ core 之外发挥作用：

- 原型验证 language-server 设计
- 对比 diagnostic model
- 为 C# 游戏项目做生成器/导入器
- 如果某个 C# 宿主需要，可以提供可选 .NET 工具

这应该是 adapter layer，而不是 canonical compiler implementation。

### 13.4 实际结论

核心引擎应该是 C++ 原生的 Roslyn-like compiler service，而不是 Roslyn-based compiler。

```text
推荐：
  C++ GraphScript compiler service
  Roslyn-inspired lossless CST and semantic APIs
  未来可选 C#/Roslyn adapters

避免：
  让 Roslyn 成为必需 parser/compiler 依赖
  强迫资产语言成为合法 C#
```

---

## 14. Parser 开源方案评估

目标不是只找一个能 parse 的库，而是找一个能支撑以下需求的起点：

```text
C++ 宿主
自定义语法
无损/近似无损 CST
source range
错误恢复
增量解析
编辑器/LSP 友好
后续可接 SemanticModel、lint、projection、TextPatch
```

### 14.1 Tree-sitter

仓库/文档：

- https://github.com/tree-sitter/tree-sitter
- https://tree-sitter.github.io/tree-sitter/

优点：

- 官方定位就是 parser generator + incremental parsing library。
- 能构建 concrete syntax tree。
- 支持源码编辑后的高效增量更新。
- 对语法错误有恢复能力，适合编辑器实时场景。
- runtime 是 C，可嵌入 C++ 项目。
- 生态里有很多语言 grammar 可参考。

缺点：

- 它只解决 CST，不提供 Roslyn-like SemanticModel、Workspace、CodeAction、TextPatch。
- grammar 通常用 `grammar.js` 定义，生成 C parser；构建链路会多一个生成步骤。
- CST 不是我们最终想暴露的强类型 AST，需要 C++ wrapper/facade。
- trivia/注释保留方式要通过 grammar 和原始 SourceText 配合设计，不是直接等同 Roslyn trivia model。

判断：

Tree-sitter 是最适合快速验证新语法和编辑器级 CST 的方案。它可以作为 Syntax Parser 模块的实现基础，但 AST/Text 框架仍要自己做。

### 14.2 ANTLR4

仓库/文档：

- https://github.com/antlr/antlr4
- https://www.antlr.org/

优点：

- 成熟 parser generator。
- grammar 表达清晰，适合快速原型。
- 可生成 parse tree，并通过 listener/visitor 处理。
- 支持 C++ target。

缺点：

- 更偏编译器/批处理 parser，不是编辑器增量解析优先。
- C++ runtime 集成比 header-only 方案重。
- 错误恢复可以做，但不如 Tree-sitter 天然贴近实时编辑器。
- 仍需要自己实现 lossless 文档模型、source patch、SemanticModel。

判断：

ANTLR 适合快速验证 grammar 和编译管线，但如果核心目标是 AI-native 编辑器和实时图文互转，不是首选。

### 14.3 lexy

仓库/文档：

- https://github.com/foonathan/lexy
- https://lexy.foonathan.net/

优点：

- C++17 parser combinator/DSL。
- 直接写在 C++ 中，和当前项目技术栈贴合。
- 可生成 parse tree，也有 parse error、input location、trace 等工具。
- 适合完全控制 AST 构建和错误信息。

缺点：

- 增量解析和编辑器级错误恢复不是它的主要卖点。
- grammar 写在 C++ template DSL 里，调试和学习成本不低。
- 要实现 Roslyn-like lossless CST，需要较多自建基础设施。

判断：

lexy 适合做 C++ 原生 parser，尤其适合 batch parse 和强控制错误信息；但若目标是快速得到编辑器级 CST，Tree-sitter 更快。

### 14.4 PEGTL

仓库/文档：

- https://github.com/taocpp/PEGTL

优点：

- C++ header-only。
- 零依赖。
- 基于 PEG，适合自定义语言。
- license 友好。

缺点：

- 更像底层 parser combinator 工具箱。
- 编辑器增量解析、CST、TextPatch、SemanticModel 都需要自己搭。
- 对 AI-native 编辑器需求来说，基础设施工作量较大。

判断：

PEGTL 适合想完全掌控 parser、且语法规模较小的场景。作为快速实现新一代编辑器级 parser，不如 Tree-sitter 直接。

### 14.5 手写递归下降 parser

当前项目已经有手写 lexer/parser。由于目标语法受限，手写 parser 也是现实选项。

优点：

- 最容易做成完全符合项目需求的 SyntaxTree/CST。
- source range、missing node、error recovery、diagnostic code 都能按我们的模型设计。
- 不引入外部生成链路。
- 容易和现有 C++ 数据结构整合。

缺点：

- 初期比 Tree-sitter 慢。
- 增量解析要自己做，难度较高。
- grammar 演进需要更多测试约束。

判断：

长期最可控，短期不一定最快。可以作为 Tree-sitter 原型验证后的第二阶段。

### 14.6 当前决策：使用 Tree-sitter

当前决策：新一代 AI Native 资产语法的 Syntax Parser 使用 Tree-sitter 实现。

选择理由：

- 它直接面向编辑器场景，支持增量解析。
- 它能产出 concrete syntax tree，适合做 lossless syntax tree 的基础。
- 它具备错误恢复能力，符合“错误文本也尽可能编译”的目标。
- runtime 是 C，能嵌入当前 C++ 项目。
- 它能快速验证新语法，而不需要先投入大量手写 parser 基础设施。

实现路线：

```text
第一阶段：Tree-sitter 语法仓库/目录
  定义 ai-native 语法 grammar
  生成 C parser
  在 C++ 中封装 SourceText + CST + SourceRange
  验证错误恢复、增量解析、scope/object/property/call 结构
  输出中间 JSON 供测试和 UI 使用

第二阶段：C++ Syntax Parser 封装
  封装 tree-sitter runtime
  提供 Document/SyntaxTree/SyntaxNode/SyntaxToken facade
  提供 diagnostics 和 source range 查询
  保留原始 SourceText，用于 trivia 和 patch

第三阶段：AST/Text Framework
  AST/Text Framework、SemanticModel、lint、projection 始终自研
```

也就是说，Tree-sitter 是正式的 Syntax Parser 技术选型，但它不替代整个 GraphScript compiler service。

具体 CST named node、field name、source patch anchor 和错误恢复要求以 [AI Native 资产语法](./ai-native-syntax-draft.md) 的 “Tree-sitter CST 契约” 为准。

```text
可以复用：
  parsing
  CST
  ranges
  incremental update
  editor-friendly error recovery

必须自研：
  domain-neutral AST facade
  SemanticModel
  Command/Object IR
  lint provider pipeline
  source binding
  TextPatch/CodeAction
  graph/table/HTN projection
  runtime/debug IR
```

---

## 15. AST/Text Framework 开源参考

这里关注的不是 parser，而是更接近 Roslyn 的“语法树 + 语义/编辑辅助框架”。

结论：目前没有一个适合 C++ 宿主、可直接复用、同时满足自定义语法、lossless tree、source patch、SemanticModel、lint、projection 的完整开源产品。更现实的路线是：

```text
Tree-sitter 负责 Syntax Parser/CST
借鉴 Rowan/SwiftSyntax/LibCST/Roslyn/JDT/PSI 的设计
自研 GraphScript AST/Text Framework
```

### 15.1 Rowan / rust-analyzer

参考：

- https://github.com/rust-analyzer/rowan
- https://github.com/rust-lang/rust-analyzer

Rowan 是 rust-analyzer 使用的 lossless syntax tree 库，采用 green/red tree 思路。它非常适合作为我们 AST/Text Framework 的结构参考：

- green tree 存不可变、无父指针、可共享的语法结构。
- red tree 是带 parent、offset 等信息的按需视图。
- typed AST 可以作为 lossless syntax node 之上的 wrapper。

优点：

- 设计非常贴近 IDE 场景。
- lossless syntax tree 与 typed AST facade 分层清楚。
- rust-analyzer 是真实大型语言服务验证。

缺点：

- Rust 实现，不是 C++。
- 只提供基础树模型，不提供我们需要的 game asset SemanticModel、projection、lint。

借鉴价值：

```text
最高。尤其是 green/red tree、untyped SyntaxNode + typed AST wrapper 的分层。
```

### 15.2 SwiftSyntax

参考：

- https://github.com/swiftlang/swift-syntax

SwiftSyntax 提供 Swift 源码的 source-accurate tree representation，并用于宏、重写、格式化等场景。

优点：

- source accurate。
- 强类型 syntax node API。
- 面向源码生成和转换。

缺点：

- 绑定 Swift 语言和 Swift 生态。
- 不适合直接嵌入 C++ 游戏工具链。

借鉴价值：

```text
高。适合作为强类型 syntax facade、源码生成和格式化 API 的参考。
```

### 15.3 LibCST

参考：

- https://github.com/Instagram/LibCST
- https://libcst.readthedocs.io/

LibCST 是 Python 的 concrete syntax tree 库，目标是保留所有格式细节，同时让树“看起来像 AST”。它主要服务 codemod、lint 和自动重构。

优点：

- lossless CST + AST-like API 的定位非常接近我们的需求。
- metadata、visitor、transformer、codemod 思路值得参考。
- 很适合研究“结构化修改后保持格式”的 API。

缺点：

- Python-only。
- 绑定 Python 语法。
- 不提供自定义语言框架。

借鉴价值：

```text
高。尤其适合参考 transformer/codemod API 和 metadata provider 设计。
```

### 15.4 Roslyn

参考：

- https://github.com/dotnet/roslyn
- https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/

Roslyn 是最完整的参考：SyntaxTree、SemanticModel、Analyzer、CodeFix、Workspace、Project/Document 模型都很成熟。

优点：

- 完整 compiler service 形态。
- Syntax/Semantic 分层非常清楚。
- analyzer/code fix/workspace 是我们 lint 和 quick fix 的直接参考。

缺点：

- C#/VB 专用。
- .NET 生态。
- 不适合作为 C++ core 直接依赖。

借鉴价值：

```text
架构最高，代码复用较低。
```

### 15.5 Eclipse JDT AST / ASTRewrite

参考：

- https://help.eclipse.org/latest/topic/org.eclipse.jdt.doc.isv/reference/api/org/eclipse/jdt/core/dom/AST.html
- https://help.eclipse.org/latest/topic/org.eclipse.jdt.doc.isv/reference/api/org/eclipse/jdt/core/dom/rewrite/ASTRewrite.html

Eclipse JDT 的 ASTRewrite 会收集对 AST node 的修改描述，并翻译成可应用到原始源码的 text edits。

优点：

- AST rewrite -> text edits 的思路与我们的 TextPatch API 非常接近。
- 适合参考 quick fix 和多个备选修复的实现方式。

缺点：

- Java 专用。
- Eclipse/Java 生态。

借鉴价值：

```text
高。尤其适合参考“不要直接改原 AST，而是收集 rewrite 描述再生成 text edit”。
```

### 15.6 IntelliJ PSI

参考：

- https://plugins.jetbrains.com/docs/intellij/psi-elements.html
- https://plugins.jetbrains.com/docs/intellij/implementing-parser-and-psi.html

IntelliJ PSI 是 JetBrains 平台的语法/语义模型接口，支持引用、重命名、补全、索引等 IDE 能力。

优点：

- 自定义语言插件经验丰富。
- PSI + reference + index 的模型值得参考。

缺点：

- JVM/IntelliJ 平台绑定。
- 更像 IDE 平台，不适合作为 GraphScript C++ core。

借鉴价值：

```text
中高。适合参考引用解析、rename、index、IDE extension model。
```

### 15.7 Clang LibTooling / ASTMatchers / Rewriter

参考：

- https://clang.llvm.org/docs/LibASTMatchersTutorial.html

Clang 提供 C/C++ AST、AST matcher、rewriter、source-to-source tooling。

优点：

- C++ 生态。
- source-to-source rewrite 经验成熟。
- AST matcher DSL 值得参考。

缺点：

- C/C++ 专用。
- Clang AST 不是自定义资产语言框架。
- 体量大，集成重。

借鉴价值：

```text
中。适合参考 matcher/rewrite，但不适合直接作为资产语言 AST 框架。
```

### 15.8 对 GraphScript 的建议

不要寻找一个“现成 Roslyn for C++ custom DSL”直接套用。建议实现自己的 AST/Text Framework，但吸收这些系统的分层：

```text
Tree-sitter:
  parsing、CST、增量解析、错误恢复

Rowan:
  green/red tree、lossless syntax node、typed AST wrapper

LibCST:
  AST-like CST、metadata provider、codemod/transformer API

Roslyn:
  SemanticModel、Analyzer/CodeFix、Workspace/Document/Project

JDT ASTRewrite:
  rewrite 描述 -> text edits

IntelliJ PSI:
  reference、rename、index、IDE integration

Clang:
  matcher、rewriter、source range discipline
```

推荐核心抽象：

```text
SourceText
SyntaxTree
SyntaxNode / SyntaxToken
TypedAstNode wrapper
SemanticModel
Document
Workspace
Diagnostic
CodeAction
TextPatch
RewriteBuilder
ProjectionProvider
LintProvider
```

---

## 16. 和当前 GraphScript 的关系

当前 GraphScript 已经有不少有价值的基础：

- `.gs` 文本作为图数据源
- `.d.gs` 声明宿主类型、节点和 schema
- parser/compiler/emitter pipeline
- AST node 已有 source range
- schema-driven graph validation
- edit session 和 graph model
- round-trip 测试

长期方向与当前实现的最大差异：

```text
当前 AST：
  Graph/Event/NodeInstance/Flow/Link 是语法概念。

目标 AST：
  Scope/Object/Property/Call/Reference 是语法概念。
  Graph/Event/Node/Flow/Link 是语义/领域概念。
```

这意味着更适合渐进迁移，而不是立即重写。

---

## 17. 模块拆分草案

长期系统可以拆成四层核心模块。模块之间应保持单向依赖，避免上层领域概念污染底层语法和文本编辑能力。

```text
Syntax Parser
  -> AST/Text Framework
  -> Graph Framework
  -> FlowGraph Framework
```

### 17.1 语法解析器模块

职责：

- 读取文本。
- 产出 lossless AST/CST。
- 保留 token、trivia、source range、missing node、error node。
- 尽可能在错误文本上恢复结构。

它只认识通用语法：

```text
module
import
scope
const/object declaration
property
call
assignment
reference
literal
array
attribute
missing/error node
```

它不应该认识：

```text
Graph
Event
NodeInstance
Flow
Link
HTN
LevelScript
Table
Dialogue
```

输出：

```text
SourceText + SyntaxTree + SyntaxDiagnostics
```

设计重点：

- parser 是容错的。
- AST/CST 是无损的。
- parser 不绑定类型，不解析 pin，不校验 graph。
- parser 不依赖任何 domain module。

### 17.2 AST/Text 辅助框架模块

这是最关键的基础设施模块。它提供类似 Roslyn Workspace/Document/SemanticModel/CodeAction 的能力，但用 C++ 原生实现。

职责一：扩展机制。

- 注册 scope kind。
- 注册 type/schema provider。
- 注册 binder。
- 注册 lint provider。
- 注册 projection provider。
- 注册 quick fix/code action。
- 注册 import resolver。
- 注册 unknown field preservation 策略。

职责二：操作机制。

提供完整 patch 方式操作文本资产。调用者不应手写字符串拼接，而是通过结构化 edit API 修改文档。

目标能力：

```text
insert scope
rename scope
insert object
delete object
set property
remove property
replace reference
insert call
delete call
replace call argument
move item
format changed range
apply quick fix
```

关键要求：

- 操作要尽可能精准映射回原有文本。
- 优先 patch 最小 source range。
- 保留未修改区域的注释、空白和风格。
- 对语法错误文件也能做局部 patch。
- patch 后可重新增量 parse/bind。

这里需要区分两个概念：

```text
AST edit:
  修改语法树结构，然后生成文本 patch。

Text patch:
  直接替换某个 source range。
```

对外 API 可以是结构化的，但底层最终必须能产生具体文本补丁：

```cpp
struct TextPatch {
    SourceRange range;
    std::string replacement;
};
```

框架还应提供 source binding：

```text
semantic object -> source range
property -> value range
call command -> callee/arg range
graph edge -> connect call range
```

输出：

```text
Document
SyntaxTree
SemanticModel
Command/Object IR
Diagnostics
CodeActions
TextPatch list
```

### 17.3 上层图框架模块

职责：

- 把通用 scope/object/call 投影成图模型。
- 管理 graph scope、entry scope、node object、pin reference、edge command。
- 提供图编辑器需要的 domain-agnostic graph API。
- 提供 graph lint 基础规则。
- 提供 graph edit -> source patch 的高层操作。

它消费 AST/Text 框架产出的 semantic model 和 command/object IR。

输入示例：

```ts
scope graph Execute: AbilityGraph {
    const apply = new ApplyDamage {
        editor.pos: [100, 100];
    }

    scope entry Start {
        context.start.connect(apply.enter);
    }
}
```

投影为：

```text
Graph Execute : AbilityGraph
Node apply : ApplyDamage
Edge context.start -> apply.enter
```

提供操作：

```text
add node
delete node
rename node
move node
set node property
connect pins
disconnect edge
replace edge endpoint
add entry
delete entry
validate graph
```

这些操作不直接改 Graph IR 后整文件 emit，而是通过 AST/Text 框架生成 source patch。

输出：

```text
GraphProjection
GraphDiagnostics
GraphEditOperations -> TextPatch
```

### 17.4 FlowGraph 框架模块

FlowGraph 是图框架之上的一个具体运行时/领域实现。

职责：

- 定义 FlowGraph 节点模型。
- 定义 exec/data pin 规则，但 pin 来源应是通用 object field + attribute/meta，而不是 parser 内建语法。
- 定义 `.connect(...)` 在 FlowGraph 中的语义。
- 定义 fan-in/fan-out、类型兼容、entry/context 规则。
- 提供 FlowGraph bake/runtime IR。
- 提供 FlowGraph 专属 lint。

它可以继续吸收当前 GraphScript 的已有概念：

```text
NodeDefinition
PinDefinition
GraphSchema
ConnectionPolicy
EditGraph
GraphRuntimeIR
Validator
```

但这些概念应属于 FlowGraph/domain 层，而不是 parser 层。

推荐 `.d.gs` 声明方式：

```ts
export declare object ApplyDamage {
    @flow.input
    target: Actor;

    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;

    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
}
```

FlowGraph binder/projection 将这些 attribute 解释为 pin definition。基础 AST 仍然只看到 attribute + field。

FlowGraph 之上还可以继续扩展：

```text
HTNGraph
TaskGraph
LevelScriptGraph
AbilityGraph
DialogueFlowGraph
```

这些是 FlowGraph schema/domain，而不是语法特例。

### 17.5 依赖方向

推荐依赖方向：

```text
Syntax Parser
  无依赖 domain

AST/Text Framework
  依赖 Syntax Parser
  不依赖 Graph/FlowGraph

Graph Framework
  依赖 AST/Text Framework
  不依赖具体 FlowGraph runtime

FlowGraph Framework
  依赖 Graph Framework
  依赖 AST/Text Framework 的扩展点
```

禁止方向：

```text
Syntax Parser -> Graph Framework
Syntax Parser -> FlowGraph Framework
AST/Text Framework -> FlowGraph Framework
```

这样才能保证未来新增 table、dialogue、quest 等模块时，不需要改 parser 或污染基础 AST。

---

## 18. 运行时与调试设计

一个重要原则：AST 本身不直接运行。

AST/CST 是源代码结构；真正可以执行、预览、验证或 bake 的对象，应当来自后续层：

```text
Syntax Tree / AST
  -> Semantic Model
  -> Command/Object IR
  -> Domain Projection
  -> Runtime/Baked IR
```

### 18.1 如果我现在有 AST，该怎么“跑”

运行流程应分成几步。

第一步：绑定。

输入 AST/CST，产出 Semantic Model：

```text
name binding
type binding
scope binding
reference resolution
schema lookup
partial symbol/error symbol
```

绑定结果不是必须全成功。未知类型、未知引用、缺失值都应保留为 error symbol/error type。

第二步：lower 到 Command/Object IR。

示例源代码：

```ts
const apply = new ApplyDamage {
    amount: 50;
}

context.start.connect(apply.enter);
```

lower 成：

```text
CreateObject apply : ApplyDamage
SetProperty apply.amount = 50
CallCommand connect(context.start, apply.enter)
```

这一步仍然是通用资产语义，不是具体 FlowGraph runtime。

第三步：领域投影。

Graph/FlowGraph projection 把 Command/Object IR 解释成图：

```text
Node apply : ApplyDamage
Default apply.amount = 50
Edge context.start -> apply.enter
```

Table projection 可以把同样的通用 object/property 解释成表行和单元格。

第四步：bake/runtime。

FlowGraph runtime 可以把 GraphProjection bake 成：

```text
GraphRuntimeIR
  flat node array
  flat pin array
  edge adjacency
  value/default table
  entry table
  source map table
```

HTN、LevelScript、AbilityGraph 等再根据 schema/domain 做自己的 bake 或 adapter。

所以“跑 AST”的准确说法应该是：

```text
Run = bind(AST) -> lower(commands) -> project(domain) -> bake/runtime
```

### 18.2 两类运行时

系统应区分 authoring runtime 和 engine runtime。

Authoring runtime：

- 在编辑器里运行。
- 支持错误文本和部分图。
- 用于 preview、lint、debug、AI 修复、图形高亮。
- 可以保留 invalid node/edge。
- 可以解释 Command IR 或 Domain Projection。

Engine runtime：

- 给宿主引擎执行。
- 要求通过更严格校验。
- 使用 baked IR。
- 可以丢弃 editor metadata。
- 可按领域优化布局和数据结构。

不要让 authoring runtime 和 engine runtime 共享同一套过早优化的数据结构。authoring 侧需要 source binding、diagnostics、invalid state；engine 侧需要快、稳定、可执行。

### 18.3 Runtime IR 应包含 SourceMap

为了调试和 AI 修复，baked/runtime IR 也应保留回源信息。

最低 bake 结果：

```cpp
struct BakeResult {
    std::shared_ptr<RuntimeIR> runtime_ir;
    RuntimeSourceMap source_map;
    std::vector<Diagnostic> diagnostics;
    ExecutionLevel execution_level;
    std::vector<SkippedRuntimeItem> skipped_items;
};
```

`BakeResult` 要求：

- authoring runtime 可以在 `execution_level < EngineExecutable` 时返回部分 runtime。
- engine runtime 只能接受通过严格校验的 `RuntimeIR`。
- `diagnostics` 必须能回到 source range。
- `skipped_items` 记录因为错误、缺失依赖或 schema 禁止而没有进入 runtime 的领域对象。
- `source_map` 必须能从 runtime id 找回领域对象和源文本范围。

Runtime item 示例：

```text
RuntimeNode {
  runtime_id
  source_object_id
  source_range
  type
}

RuntimeEdge {
  runtime_id
  from_pin
  to_pin
  source_call_range
  from_range
  to_range
}

RuntimeValue {
  runtime_id
  source_property_range
  value_range
}
```

SourceMap 建议结构：

```cpp
enum class RuntimeSourceKind {
    Graph,
    Entry,
    Node,
    Pin,
    Edge,
    Property,
    Value,
    Table,
    Row,
    Cell,
    Diagnostic
};

struct RuntimeSourceSpan {
    RuntimeItemId runtime_id;
    DomainItemId domain_item_id;
    SymbolId symbol_id;
    RuntimeSourceKind kind;
    SourceRange primary_range;
    std::vector<SourceRange> contribution_ranges;
};

struct RuntimeSourceMap {
    std::vector<RuntimeSourceSpan> spans;
    std::optional<RuntimeSourceSpan> findByRuntimeId(RuntimeItemId id) const;
    std::vector<RuntimeSourceSpan> findBySourceRange(SourceRange range) const;
};
```

Runtime id 不应直接使用行号或数组下标。推荐由稳定 object id、scope id、pin/property path 和 command identity 派生；当文本缺少稳定 id 时，可退化为 authoring-session-local id，并在下一次 formatter/code action 中提示补 `@id(...)`。

这允许运行时错误反向映射到文本：

```text
Runtime error:
  ApplyDamage.amount expected positive float, got -10

Source:
  Fireball.gs:12:17
  amount: -10;
```

### 18.4 Debug Target 契约

调试器、图编辑器和文本编辑器不应分别维护三套目标身份。断点、watch、step、高亮都应使用统一的 `DebugTargetRef`。

```cpp
enum class DebugTargetKind {
    Scope,
    Entry,
    Node,
    Pin,
    Edge,
    Property,
    Table,
    Row,
    Cell,
    Diagnostic
};

struct DebugTargetRef {
    DebugTargetKind kind;
    StableId stable_id;
    std::optional<RuntimeItemId> runtime_id;
    std::optional<DomainItemId> domain_item_id;
    SourceRange primary_range;
};
```

规则：

- 文本断点先绑定到 source range，再解析成 semantic/domain/runtime target。
- 图断点先绑定到 domain item，再通过 source binding 找回文本。
- runtime 重建后，断点应通过 stable id 和 source map 迁移；不能只靠旧 runtime id。
- invalid item 也允许作为 debug target，但只能在 authoring runtime 中高亮或解释，不能在 engine runtime 中执行。

### 18.5 调试模型

调试不应只发生在最终 runtime。至少需要四层可检查对象：

```text
Syntax debug:
  查看 CST/AST、missing nodes、error nodes、source ranges

Semantic debug:
  查看符号、类型、引用解析、scope 树、error symbols

Command debug:
  查看 CreateObject/SetProperty/CallCommand 序列

Domain debug:
  查看 GraphProjection/TableProjection/HTNProjection

Runtime debug:
  查看 baked graph、entry、edge adjacency、runtime values
```

建议 CLI/API 提供类似命令：

```bash
gs inspect syntax   Fireball.gs --json
gs inspect semantic Fireball.gs --json
gs inspect command  Fireball.gs --json
gs inspect graph    Fireball.gs --scope Execute --json
gs inspect runtime  Fireball.gs --scope Execute --json
gs lint             Fireball.gs --json
gs trace            Fireball.gs --scope Execute --entry Start
```

这些命令输出必须优先支持 JSON，供 AI、测试和编辑器消费；人类可读格式可以由 JSON 渲染得到。

### 18.6 图运行调试

FlowGraph 类图运行时应支持编辑器调试能力：

- entry 触发。
- step node。
- step edge。
- 查看当前 active node。
- 查看 pin value。
- 查看 data dependency。
- 查看 exec path。
- breakpoint on node。
- breakpoint on edge。
- watch pin/property。
- runtime diagnostic 映射到 source range。

调试事件可以统一成：

```cpp
enum class TraceEventKind {
    EnterEntry,
    EnterNode,
    ExitNode,
    TraverseEdge,
    ReadValue,
    WriteValue,
    Diagnostic,
    SkipInvalidItem
};

struct TraceEvent {
    RunId run_id;
    uint64_t sequence;
    TraceEventKind kind;
    DebugTargetRef target;
    std::optional<DebugTargetRef> cause;
    std::optional<ValueSnapshot> value;
    std::optional<Diagnostic> diagnostic;
    SourceRange source_range;
};
```

这套 trace event 同时服务：

- 可视化图高亮
- 文本编辑器高亮
- AI 自动解释错误
- 测试快照

Trace event 必须是确定性的：同一份 runtime IR、同一输入和同一 entry 下，事件顺序和 target id 应稳定，方便测试和 AI 复盘。

### 18.7 错误文本的运行策略

错误文本可以“尽可能编译”，但不能假装完全可运行。

建议运行等级：

```text
Level 0: SyntaxRecovered
  可显示结构和 syntax diagnostics

Level 1: SemanticPartial
  可显示对象、scope、部分引用、semantic diagnostics

Level 2: DomainPartial
  可显示部分图/表/HTN，非法节点和边被标红

Level 3: AuthoringExecutable
  可在编辑器里模拟/preview，但可能跳过 invalid commands

Level 4: EngineExecutable
  通过严格校验，可 bake 给宿主 runtime
```

最低执行策略：

- `SyntaxRecovered`：禁止运行，只允许 inspect syntax 和 syntax diagnostics。
- `SemanticPartial`：禁止运行，但允许 name/type/reference 的交互式解释。
- `DomainPartial`：禁止 engine bake，允许显示部分图/表/HTN。
- `AuthoringExecutable`：允许 preview/trace；遇到 invalid item 必须发出 `SkipInvalidItem` 或 diagnostic trace。
- `EngineExecutable`：允许导出或加载到宿主 runtime；不允许保留 unresolved symbol、invalid edge、missing required field。

这能避免“有错就什么都不能看”，也避免“有错还强行当作正式 runtime 执行”。

### 18.8 AST 操作和调试的关系

每个 AST/Text edit 最好能生成调试友好的变更记录：

```text
EditTransaction:
  operation = SetProperty
  semantic_target = apply.amount
  old_source_range = ...
  new_source_range = ...
  patches = [...]
  diagnostics_before = [...]
  diagnostics_after = [...]
```

这样 AI、编辑器和测试都能知道：

- 改了什么语义对象
- patch 了哪些文本范围
- 诊断是否减少
- runtime projection 是否变化

---

## 19. 阶段目标

### Milestone 1：记录目标模型

- 持续更新本文档。
- 增加 graph、HTN、level script、table、dialogue asset 的具体例子。
- 定义可逆语法子集。
- 定义非可逆/import-only 扩展点。

### Milestone 2：增加通用语法实验

- 原型支持 `scope`、object declaration、object property、call 和 missing node。
- 保留现有 `.gs` parser。
- 增加 broken/incomplete 文件 fixtures。
- 增加 CST/source-range 测试。

### Milestone 3：构建 Semantic Model 和 lint pipeline

- 实现 name binding。
- 实现 type binding。
- 实现 partial symbol/error type model。
- 实现 lint provider 注册。
- 增加 core asset lint。

### Milestone 4：Graph projection

- 把通用 object/call IR lower 成 graph IR。
- 在 graph scope 内把 `new NodeType { ... }` 映射为 graph node creation。
- 把 `.connect(...)` 映射为 graph edge。
- 为每个 node/property/edge 保留 source binding。
- 支持可视化编辑到 source patch。

### Milestone 5：领域模块

- HTN projection/lint。
- LevelScript projection/lint。
- Table projection/lint。
- Dialogue/Quest/Ability 实验。

### Milestone 6：Runtime adapters

- runtime/bake 保持 engine-specific。
- 提供通用 serialized asset IR。
- UE/Unity/Godot/custom adapter 可以选择自己的 runtime representation。

---

## 20. 开放问题

- stable id 应该是显式语法、生成 metadata，还是两者都支持？
- editor metadata 应该用 `editor { ... }`、`editor.pos`、attribute，还是专用 metadata channel？
- `input target: Actor;` 是否保留 directive-like statement，还是所有声明都表示成 object/command call？
- `scope graph Execute: AbilityGraph` 是否总是使用 `scope`，还是允许 `graph Execute: AbilityGraph` 作为语法糖？
- 精确的可逆子集是什么？
- 文件包含非可逆 macro 或生成逻辑时应如何处理？
- 类型未知时，是否仍要结构化保留未知字段？
- import 和跨资产引用如何与 stable id 交互？
- 图编辑器需要的最小 patch API 是什么？
- 当前 parser/compiler 应复用多少，还是应通过兼容层逐步替换？
- 是否需要可选 C# tooling 使用 Roslyn adapter，而 C++ core 仍保持 canonical？
- AST/Text 辅助框架是否应该采用 immutable tree，还是采用 mutable tree + edit transaction？
- 图编辑操作应优先生成 AST edit，还是直接生成 text patch 并重新 parse？
- FlowGraph 是否保留当前 `EditGraph` 名称，还是改成更明确的 runtime/domain 模块名？
- authoring runtime 是否直接解释 Domain Projection，还是先 bake 一个保留 source map 的 debug Runtime IR？
- trace event 是否作为所有领域 runtime 的统一调试协议？
- Tree-sitter 的 `grammar.js -> generated C parser` 构建链路应如何接入 CMake 和 CI？
- 是否需要长期迁移到手写 C++ parser 以获得完整 Roslyn-like trivia/source patch 控制？
- AST/Text Framework 是否采用 Rowan 风格 green/red tree？
- Rewrite API 是否采用 JDT ASTRewrite 风格，即收集 rewrite 描述再生成 TextPatch？
- FlowGraph 的 pin 是否完全通过 attribute/meta 表达，并把 `exec/data/input/output` 仅作为可选兼容语法糖？

---

## 21. 第一版实现边界

第一版 parser/contract 不需要解决所有开放问题。它需要固定最小可实现闭环：

```text
.gs / .d.gs
  -> Tree-sitter CST
  -> AST/Text typed wrapper
  -> SemanticModel
  -> ModuleGraph / ExportTable / SymbolResolution
  -> Command/Object IR
  -> DomainProjection
  -> Diagnostics / QuickFix / TextPatch
  -> Authoring Runtime / Debug SourceMap
```

第一版必须实现：

- `.gs` / `.d.gs` 文件识别。
- `import`、`export`、`declare module`、`declare type/enum/object/scope/command/schema/lint`。
- `scope`、`const alias = new Type { ... }`、property、literal、reference、attribute、restricted command call。
- Tree-sitter CST named node、field name、source range、错误恢复和增量 parse。
- AST/Text Framework 的 `Document`、`TextPatch`、`EditTransaction`、`RewriteBuilder`。
- `ModuleGraph`、`ExportTable`、`SymbolResolution`。
- `Diagnostic`、`QuickFix`、FixAll、AI 修复提示。
- `Command/Object IR`。
- `ProjectionProvider` 和至少一个 Graph/FlowGraph projection prototype。
- authoring runtime 的 partial model、source map、debug target 和 trace event。

第一版可以暂缓：

- `graph Execute` 这种省略 `scope` 的语法糖。
- `new Type(...)` constructor-like 写法。
- 非可逆 macro 或 import-time execution。
- FlowGraph 旧式 `exec/data/input/output` 声明作为核心语法。
- 完整 engine runtime 优化布局。
- 是否采用 green/red tree 的最终内部实现。
- 是否提供 C# Roslyn adapter。
- 是否长期迁移到手写 C++ parser。

这些暂缓项不会阻塞第一版，因为它们不影响核心真源文本、CST/source range、语义绑定、依赖分析、projection、lint、patch 和 debug source map 的闭环。

第一版推荐默认决策：

- stable id 使用声明前置 `@id(...)`；缺失时允许 authoring-session-local id，并提供 quick fix 添加稳定 id。
- editor metadata 允许先使用 `editor.pos`，同时把 `@editor.field` 作为 `.d.gs` 元信息；后续可以迁移到更统一的 metadata channel。
- directive-like statement 只作为兼容或 schema extension；核心可逆作者子集优先使用 property、attribute 和 command call。
- 图编辑操作优先通过 AST/Text Framework 生成 `TextPatch`；具体内部是 AST edit 还是 rewrite 描述由实现选择，但外部契约固定为 `EditTransaction`。
- trace event 作为 authoring runtime 的统一调试协议；engine runtime 可以选择剥离或压缩。

---

## 22. 当前临时结论

- 使用 Roslyn-like lossless syntax tree 作为编辑器 document model。
- C++ 核心实现不直接依赖 Roslyn。
- 新语法 Syntax Parser 使用 Tree-sitter；AST/Text 框架、SemanticModel、lint、projection、patch API 自研。
- AST/Text Framework 参考 Rowan、LibCST、Roslyn、JDT ASTRewrite、IntelliJ PSI、Clang Rewriter，但不直接依赖其中任何一个作为核心。
- AST 保持通用：scope/object/property/call/reference/literal。
- 图专属语义下沉到 binding/projection 层。
- pin 通过通用 field + attribute/meta 表达；FlowGraph projection 负责解释 pin 语义。
- `.connect(...)` 是受限 command call，不是 runtime invocation。
- object construction 是序列化语法，不是任意 constructor execution。
- partial compilation 和 domain-specific lint 是一等能力。
- 目标是通用游戏资产文本格式；图编辑是第一个 projection。
- AST/CST 本身不直接运行；运行应经过 binding、Command/Object IR、领域投影和 runtime/bake。
- runtime/debug IR 必须保留 source map，方便错误回源、图文高亮和 AI 修复。
