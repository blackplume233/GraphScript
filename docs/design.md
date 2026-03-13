# GraphScript 技术设计文档

> 版本: 0.3 draft | 基于 HumanStart.md 设计思路 + Domain/Schema + CLI + 测试策略 + Graph-as-Node + .d.gs 声明

---

## 1. 系统概述

GraphScript 是一个跨平台、跨引擎的 DSL 框架，核心能力：

1. **脚本形态**：用 `.gs` 文本描述节点图（Graph）
2. **声明文件**：`.d.gs` 文件声明宿主 native 类型、节点和 Schema，无需 C++ 编译
3. **Graph-as-Node**：Graph 自动推导为 NodeDefinition，可被其他 Graph 引用（节点即 Graph）
4. **解析器**：将 `.gs` / `.d.gs` 文本解析为内存中的图数据结构
5. **双向编辑**：文本编辑 ←→ 图形化编辑可互相转换
6. **宿主集成**：通过注册机制注入 Native 类型和 Native 节点
7. **多 Domain 支持**：通过 Schema 驱动不同场景（HTN / 任务编辑器 / 关卡脚本）的编辑约束——连接策略、可用节点、校验规则
8. **CLI 工具**：所有核心功能提供命令行入口，支持自动化调试和验证

系统**不包含**节点执行引擎——执行由宿主（如 UE）负责。
系统**不处理**不同 Domain 的运行时执行模型差异——Schema 仅约束编辑时行为。

---

## 2. 处理管线

```
.gs 文本
  │
  ▼
┌──────────┐
│  Lexer   │  文本 → Token 流
└──────────┘
  │
  ▼
┌──────────┐
│  Parser  │  Token 流 → AST（含 Graph : BaseType 继承声明）
└──────────┘
  │
  ▼
┌──────────┐
│ Compiler │  AST → Graph（存储结构）
└──────────┘     │
                 │ SchemaRegistry 查找 BaseType → GraphSchema
                 ▼
┌──────────────────────────────────────────────────────────┐
│ EditGraph（编辑器模型，持有 GraphSchema*）                   │
│   connect()  → 查 ConnectionPolicy                        │
│   add_node() → 查 allowed_node_tags                       │
│   validate() → GraphValidator（common + domain 特化）      │
└──────────┬───────────────┬──────────────────┬────────────┘
           │               │                  │
           ▼               ▼                  ▼
    图形编辑器交互    Emitter → .gs 文本    bake() → RuntimeGraph
                    （round-trip）        （+ domain_metadata）
                                                  │
                                                  └──→ 宿主执行引擎
```

---

## 3. 文件级数据结构

### 3.1 Module — 一个 .gs 文件的编译产物

一个 `.gs` 文件被加载后对应一个 `Module`：

```cpp
struct Module {
    std::string                   file_path;
    std::vector<ImportDecl>       imports;           // import 段
    std::vector<LetDecl>          top_level_lets;    // 文件级 let 定义
    std::vector<Graph>            graphs;            // Graph 块（Graph 也可作为节点被其他 Graph 引用）
};
```

### 3.2 ImportDecl — 导入声明

```cpp
struct ImportDecl {
    std::string path;       // "native.d.gs" 或 "other_base_node.gs"
    bool        is_native;  // 是否为 .d.gs（声明文件）
};
```

### 3.3 LetDecl — 顶层变量定义

对应 `let actor_1 = SoftObjectPath("actor_path_1");`

```cpp
struct LetDecl {
    std::string  name;           // "actor_1"
    std::string  type_name;      // "SoftObjectPath"
    std::string  constructor_arg; // "actor_path_1" (原始字符串)
};
```

### 3.4 `.d.gs` 声明文件规范

`.d.gs`（declaration GraphScript）文件用于声明宿主 native 提供的类型、节点和 Schema，不包含实际逻辑实现。
其作用等价于 C/C++ 头文件或 TypeScript `.d.ts`——为编辑器和编译器提供外部符号信息。

#### 3.4.1 语法

```bnf
d_gs_file     ::= declaration*
declaration   ::= declare_type | declare_node | declare_schema

declare_type  ::= "declare" "type" IDENTIFIER [ ":" "constructible" ] ";"
declare_node  ::= "declare" "Node" IDENTIFIER "{" pin_decl* "}"
declare_schema::= "declare" "Schema" IDENTIFIER "{" schema_body "}"

pin_decl      ::= pin_kind pin_dir IDENTIFIER ":" type_ref ";"
pin_kind      ::= "exec" | "data"
pin_dir       ::= "in" | "out"
type_ref      ::= IDENTIFIER

schema_body   ::= schema_field*
schema_field  ::= IDENTIFIER ":" value ";"
```

#### 3.4.2 示例

```gs
// ue_core.d.gs — UE 核心类型声明
declare type FName;
declare type FVector : constructible;
declare type FRotator : constructible;
declare type SoftObjectPath : constructible;
declare type AActor;
declare type UObject;

// 内置节点
declare Node PrintString {
    exec in  enter;
    exec out exit;
    data in  message : FString;
}

declare Node Delay {
    exec in  enter;
    exec out completed;
    data in  duration : float;
}

// HTN Domain Schema
declare Schema HTNGraph {
    max_exec_fan_out: unlimited;
    allow_exec_fan_in: false;
    strict_type_match: true;
    allowed_node_tags: ["htn_task", "htn_decorator", "htn_service", "common"];
}
```

#### 3.4.3 AST 节点

```cpp
struct DeclareTypeNode : ASTNode {
    std::string name;
    bool        constructible = false;  // 是否支持 TypeName("args") 构造语法
};

struct DeclareNodeNode : ASTNode {
    std::string                  name;
    std::vector<PinDeclNode>     pins;     // 每个 pin: kind, direction, name, type
};

struct PinDeclNode : ASTNode {
    PinKind      kind;       // exec / data
    PinDirection direction;  // in / out
    std::string  name;
    std::string  type_name;  // data pin 的类型（exec pin 可忽略）
};

struct DeclareSchemaNode : ASTNode {
    std::string                                    name;
    std::vector<std::pair<std::string, std::string>> fields;  // key-value 对
};
```

#### 3.4.4 与 C++ register API 的对应关系

| `.d.gs` 声明 | 等价 C++ API |
|:---|:---|
| `declare type FName;` | `env.types().register_type({"FName"});` |
| `declare type FVector : constructible;` | `env.types().register_type({"FVector", true});` |
| `declare Node PrintString { ... }` | `env.nodes().register_node(NodeDefinition{...});` |
| `declare Schema HTNGraph { ... }` | `env.schemas().register_schema(GraphSchema{...});` |

---

## 4. Domain/Schema 系统

不同使用场景（HTN / 任务编辑器 / 关卡脚本）的图在**编辑能力**上有本质差异：连接规则不同、可用节点不同、校验约束不同。Domain/Schema 系统将这些差异从核心代码剥离，通过可插拔的 Schema 注入。

DSL 语法上，通过继承声明 Graph 所属 Domain：

```gs
Graph my_plan : HTNGraph {
    event decompose() { ... }
}
Graph my_quest : TaskGraph {
    event on_start() { ... }
    event on_complete() { ... }
}
Graph my_level : LevelScriptGraph {
    event on_begin_play() { ... }
}
```

不指定 `: BaseType` 的 Graph 视为通用 Graph，使用默认策略。

### 4.1 ConnectionPolicy — 连接策略

Domain 体系的核心数据结构，定义什么样的连接是合法的：

```cpp
struct ConnectionPolicy {
    // Exec 连接规则
    uint32_t max_exec_fan_out = 1;        // 一个 exec output 最多连几个 exec input
    bool     allow_exec_fan_in  = false;  // 多个 exec output 连到同一个 exec input？

    // Data 连接规则
    uint32_t max_data_fan_out = UINT32_MAX; // 一个 data output 可供多少个 data input 读取
    bool     allow_data_fan_in  = false;    // 多个 data output 连到同一个 data input？

    // 类型兼容性
    bool     strict_type_match = true;  // true: 精确匹配或 Any; false: 允许隐式转换

    // 自定义校验钩子（通用规则检查之后调用，用于 Domain 特殊约束）
    using ConnectionValidateFn = std::function<Result<void, std::string>(
        const EditPin& from, const EditPin& to, const EditGraph& graph)>;
    ConnectionValidateFn custom_validate;  // nullptr = 无额外检查
};
```

三个 Domain 的 ConnectionPolicy 对比：

| Domain | max_exec_fan_out | allow_exec_fan_in | 典型场景 |
|--------|-----------------|-------------------|---------|
| HTN | UINT32_MAX | false | 一个任务分解为多个并行子任务 |
| Task | 1 | true | 线性依赖 + 多前置任务汇聚到后续任务 |
| LevelScript | UINT32_MAX | false | 事件分支/广播 |

### 4.2 GraphSchema — Domain 定义

```cpp
class GraphSchema {
public:
    std::string domain_name;      // "htn", "task", "level_script"
    std::string base_graph_name;  // "HTNGraph", "TaskGraph", "LevelScriptGraph"

    // 核心：连接策略
    ConnectionPolicy connection_policy;

    // 必须事件
    struct RequiredEvent {
        std::string name;
        std::vector<GraphParameter> params;
        bool optional = false;   // true = 建议但不强制
    };
    std::vector<RequiredEvent> required_events;

    // 节点过滤（空 = 不限制；非空 = 只允许 tags 与此列表有交集的节点）
    std::vector<std::string> allowed_node_tags;

    // 自定义 Validator 工厂（nullptr = 仅通用校验）
    using ValidatorFactory = std::function<std::unique_ptr<GraphValidator>()>;
    ValidatorFactory create_validator;

    // 自定义 Bake 后处理（标准 bake 完成后调用，可往 domain_metadata 写入额外数据）
    using BakePostProcess = std::function<void(
        RuntimeGraph& rt, const EditGraph& edit, const Environment& env)>;
    BakePostProcess bake_post_process;  // nullptr = 无后处理
};
```

### 4.3 SchemaRegistry — Schema 注册表

```cpp
class SchemaRegistry {
public:
    void register_schema(GraphSchema schema);
    const GraphSchema* find_by_base(std::string_view base_graph_name) const;
    const GraphSchema* find_by_domain(std::string_view domain_name) const;
    std::vector<const GraphSchema*> all() const;

private:
    std::vector<GraphSchema>                     schemas_;
    std::unordered_map<std::string, size_t>      by_base_;
    std::unordered_map<std::string, size_t>      by_domain_;
};
```

### 4.4 Validator 系统 — 图校验

#### 4.4.1 Diagnostic — 校验结果

```cpp
struct Diagnostic {
    enum class Severity : uint8_t { Error, Warning, Info };
    Severity    severity;
    std::string rule_id;      // "common.type_mismatch", "htn.no_root_task" 等
    std::string message;
    NodeHandle  node;         // 关联节点（可选）
    PinHandle   pin;          // 关联 Pin（可选）
};
```

#### 4.4.2 GraphValidator — 校验器基类

```cpp
class GraphValidator {
public:
    virtual ~GraphValidator() = default;

    std::vector<Diagnostic> validate(
        const EditGraph& graph, const Environment& env) const
    {
        auto diags = validate_common(graph, env);
        auto domain_diags = validate_domain(graph, env);
        diags.insert(diags.end(), domain_diags.begin(), domain_diags.end());
        return diags;
    }

protected:
    // 通用校验（所有 Domain 共享）
    std::vector<Diagnostic> validate_common(
        const EditGraph& graph, const Environment& env) const;

    // Domain 特化校验（子类覆写）
    virtual std::vector<Diagnostic> validate_domain(
        const EditGraph& graph, const Environment& env) const { return {}; }
};
```

#### 4.4.3 通用校验规则（validate_common）

- 数据类型兼容性：Data 连接两端类型匹配（或源为 Any）
- 必须事件：Schema.required_events 中 `optional=false` 的事件必须存在
- 名称唯一性：节点实例名图内唯一
- 悬空 Pin：必要的 Exec Input 未连接时 Warning

#### 4.4.4 Domain 特化校验

- **HTNValidator**：根任务存在且唯一；分解关系无环（DAG）；复合任务至少一个分解方法
- **TaskValidator**：任务依赖关系为 DAG；`on_start` / `on_complete` 逻辑块非空
- **LevelScriptValidator**：`on_begin_play` 事件存在；触发器引用合法

### 4.5 预定义 Domain Schema

| Domain | base_graph_name | required_events | allowed_node_tags |
|--------|----------------|-----------------|-------------------|
| HTN | HTNGraph | `decompose` | `htn_task`, `htn_method`, `htn_condition`, `common` |
| Task | TaskGraph | `on_start`, `on_complete` | `task_node`, `task_condition`, `common` |
| LevelScript | LevelScriptGraph | `on_begin_play`; `on_trigger`(optional) | 不限制 |

---

## 5. 类型系统

### 5.1 TypeHandle — 类型引用

轻量的类型标识符，在内存中用于快速比较和查找：

```cpp
struct TypeHandle {
    uint32_t id;  // 由 TypeRegistry 分配的唯一 ID

    bool operator==(const TypeHandle& other) const { return id == other.id; }
    bool is_valid() const { return id != 0; }

    static const TypeHandle Invalid;  // {0}
};
```

### 5.2 TypeInfo — 类型定义

```cpp
struct TypeInfo {
    std::string name;        // "int", "float", "SoftObjectPath", "FType", "Any"
    bool        is_native;   // 是否由宿主注册
    bool        is_builtin;  // 是否为内置类型（int, float, string, bool, Any）

    // 宿主为 native 类型提供的回调
    using ConstructFn  = std::function<Value(std::string_view)>;
    using SerializeFn  = std::function<std::string(const Value&)>;

    ConstructFn  construct;   // 从文本构造值（可选）
    SerializeFn  serialize;   // 值序列化为文本（可选）
};
```

### 5.3 TypeRegistry — 类型注册表

```cpp
class TypeRegistry {
public:
    // 注册并返回 handle
    TypeHandle register_type(TypeInfo info);

    // 查找
    TypeHandle       find(std::string_view name) const;
    const TypeInfo*  get(TypeHandle handle) const;

    // 内置类型的快捷引用
    TypeHandle type_int() const;
    TypeHandle type_float() const;
    TypeHandle type_string() const;
    TypeHandle type_bool() const;
    TypeHandle type_any() const;

private:
    std::vector<TypeInfo>                        types_;      // index = handle.id
    std::unordered_map<std::string, TypeHandle>  name_map_;
};
```

### 5.4 Value — 类型擦除值容器

用于存储 DataPin 的默认值、字面量等：

```cpp
class Value {
public:
    Value();                                    // 空值
    explicit Value(int32_t v);
    explicit Value(float v);
    explicit Value(std::string v);
    explicit Value(bool v);
    Value(TypeHandle type, std::any data);      // 任意 native 类型值

    TypeHandle    type() const;
    bool          is_empty() const;

    // 安全取值（失败返回 nullopt）
    std::optional<int32_t>      as_int() const;
    std::optional<float>        as_float() const;
    std::optional<std::string>  as_string() const;
    std::optional<bool>         as_bool() const;

    // native 值的通用取值
    template<typename T>
    const T* as() const;

    // 序列化为文本表示（借助 TypeRegistry）
    std::string to_text(const TypeRegistry& registry) const;

private:
    TypeHandle type_;
    std::any   data_;
};
```

---

## 6. Pin 模型

### 6.1 PinKind / PinDirection — Pin 分类

```cpp
enum class PinKind : uint8_t {
    Exec,   // 执行流 pin（FFlowOutput / FFlowIn）
    Data    // 数据流 pin（TOutValue / TInValue）
};

enum class PinDirection : uint8_t {
    Input,
    Output
};
```

### 6.2 PinDefinition — Pin 定义（属于 NodeDefinition）

```cpp
struct PinDefinition {
    std::string   name;           // pin 名称
    PinKind       kind;           // Exec 或 Data
    PinDirection  direction;      // Input 或 Output

    // 以下仅 Data pin 有效
    TypeHandle    data_type;      // 数据类型（Data pin）
    Value         default_value;  // 默认值（仅 Data Input pin）

    // 以下仅 Exec Input pin 有效
    std::string   bound_handler;  // 绑定的处理函数名（FFlowIn 的第二个参数）
};
```

### 6.3 PinAddress — Pin 的全局寻址

用于在 Graph 中引用某个具体 pin：

```cpp
struct PinAddress {
    std::string node_name;   // 节点实例名，特殊值 "__context__" 代表上下文入口
    std::string pin_name;    // pin 名称

    bool operator==(const PinAddress& o) const;
};

// 提供 hash 支持，便于用作 map key
struct PinAddressHash {
    size_t operator()(const PinAddress& addr) const;
};
```

---

## 7. Node 模型

### 7.1 NodeDefinition — 节点类型定义

节点定义有两个来源：
- **Native 节点**：宿主 C++ 注册或 `.d.gs` 声明（`declare Node`）
- **Graph 节点**：编译器从 Graph 定义自动推导（见 §7.3）

DSL 中**不提供** `export Node` 语法——节点即 Graph，Graph 即节点。

```cpp
struct NodeDefinition {
    std::string                  type_name;      // "PrintString", "MySubGraph" 等
    std::vector<PinDefinition>   pins;           // 全部 pin 定义
    bool                         is_native;      // true = 宿主注册/d.gs 声明; false = Graph 推导

    // Domain 节点过滤标签（供 GraphSchema.allowed_node_tags 过滤）
    std::vector<std::string>     tags;

    // Graph 推导节点专属：指向源 Graph 名称（is_native=false 时有效）
    std::string                  source_graph;

    // 便捷查询
    const PinDefinition* find_pin(std::string_view name) const;
    std::vector<const PinDefinition*> exec_inputs() const;
    std::vector<const PinDefinition*> exec_outputs() const;
    std::vector<const PinDefinition*> data_inputs() const;
    std::vector<const PinDefinition*> data_outputs() const;
};
```

### 7.2 NodeInstance — 图中的节点实例

对应 `FNodeType native_node{};`：

```cpp
struct NodeInstance {
    std::string  type_name;       // 引用 NodeDefinition
    std::string  instance_name;   // 实例名（图内唯一）
    std::string  initializer;     // {} 内的初始化文本（可为空）
};
```

### 7.3 Graph-as-Node 推导

每个 Graph 在编译后会自动生成一个 `NodeDefinition`，使其可被其他 Graph 作为节点引用。推导规则：

| Graph 参数方向 | 生成的 Pin |
|:---|:---|
| `in` 参数 | **Data Input Pin** |
| `out` 参数 | **Data Output Pin** |
| `event` 块名称 | **Exec Input Pin**（每个 event 对应一个入口） |
| Graph 内部的 `function` 出口连接 | **Exec Output Pin**（可选；如果 Graph 有显式对外调用点） |

推导过程由 Compiler 在每个 Graph 编译完成后自动执行，并将生成的 `NodeDefinition` 注册到 `NodeRegistry`。
后续 Graph 可以通过 `GraphName instance_name{};` 语法引用前面定义的 Graph 作为节点。

```cpp
// Compiler 伪代码
NodeDefinition derive_node_from_graph(const Graph& graph) {
    NodeDefinition def;
    def.type_name    = graph.name;
    def.is_native    = false;
    def.source_graph = graph.name;

    for (auto& param : graph.parameters) {
        PinDefinition pin;
        pin.name = param.name;
        pin.type = param.type;
        if (param.direction == ParamDirection::In)
            pin.direction = PinDirection::Input;
        else if (param.direction == ParamDirection::Out)
            pin.direction = PinDirection::Output;
        pin.kind = PinKind::Data;
        def.pins.push_back(pin);
    }

    for (auto& ev : graph.events) {
        PinDefinition pin;
        pin.name      = ev.name;
        pin.kind      = PinKind::Exec;
        pin.direction = PinDirection::Input;
        def.pins.push_back(pin);
    }

    return def;
}
```

---

## 8. 连接模型

### 8.1 FlowConnection — 执行流连接

对应 `context.start(native_node.in)` 或 `native_node.out(compose_node.in)`：

```cpp
struct FlowConnection {
    PinAddress from;  // 源：exec output pin
    PinAddress to;    // 目标：exec input pin
};
```

### 8.2 DataSource — 数据源

DataLink 的源端可以是三种东西之一：

```cpp
struct DataSource {
    enum class Kind : uint8_t {
        NodePin,     // 来自另一个节点的 data output pin
        Parameter,   // 来自 Graph 的 in/var 参数
        Literal      // 字面量值 int(1), "hello" 等
    };

    Kind kind;

    // Kind::NodePin
    PinAddress pin;           // 源节点的 output data pin

    // Kind::Parameter
    std::string param_name;   // Graph 参数名

    // Kind::Literal
    Value literal_value;      // 字面量值
};
```

### 8.3 DataLink — 数据连接

对应 `link(target.data_in, source)`：

```cpp
struct DataLink {
    PinAddress  target;   // 目标：data input pin
    DataSource  source;   // 数据来源
};
```

---

## 9. Graph 参数

对应 `in in_var_1 : FType;` / `out out_var_2 : FType;` / `var var_3 : FType;`：

```cpp
struct GraphParameter {
    enum class Direction : uint8_t {
        In,   // 图的输入参数
        Out,  // 图的输出参数
        Var   // 图的内部变量
    };

    Direction   direction;
    std::string name;
    TypeHandle  type;
};
```

---

## 10. 逻辑块：Event 和 Function

### 10.1 LogicBlock — 公共基类

event 和 function 内部结构相同——都是图的构造方法：

```cpp
struct LogicBlock {
    std::string                   name;
    std::vector<FlowConnection>   flow_connections;
    std::vector<DataLink>         data_links;
};
```

### 10.2 Event

对应 `event event_name() { ... }`。
Event 使用图级节点实例，隐式拥有 `FEventContext context`：

```cpp
struct Event : LogicBlock {
    // Event 不声明额外参数
    // Event 可以引用图级节点（Graph::nodes）
};
```

### 10.3 Function

对应 `function function_name_1(in value:FType, out value:FType) { ... }`。
Function 可以声明局部节点，有自己的参数列表：

```cpp
struct Function : LogicBlock {
    std::vector<GraphParameter>   params;       // 函数参数
    std::vector<NodeInstance>     local_nodes;  // 函数内局部节点
    // Function 也可以引用图级节点
};
```

---

## 11. Generate 块 — 编辑器元数据

对应 `generate() { ... }`。存储编辑器生成的布局信息（节点位置、注释分组）。
双向编辑时，图形编辑器 **仅修改此块**，其余脚本保持不变。

### 11.1 Comment — 注释对象

```cpp
struct CommentBlock {
    std::string id;       // 注释名（如 "comment_a"），在 generate 块内唯一
    std::string content;  // 注释文本
};
```

### 11.2 NodeMetadata — 节点编辑器属性

```cpp
struct NodeMetadata {
    std::string scope;        // "global" 或函数名（对应 global: / function_name: 前缀）
    std::string node_name;    // 节点实例名
    float       pos_x;
    float       pos_y;
    std::string comment_ref;  // 关联的 CommentBlock id（可为空）
};
```

### 11.3 GenerateBlock

```cpp
struct GenerateBlock {
    std::vector<CommentBlock>  comments;
    std::vector<NodeMetadata>  metadata;

    // 便捷查询
    const NodeMetadata* find_metadata(std::string_view scope, std::string_view node_name) const;
};
```

---

## 12. Graph — 完整图定义（存储结构）

这是 `.gs` 文件中一个 `Graph graph_name { ... }` 块编译后的直接产物：

```cpp
struct Graph {
    std::string                   name;
    std::string                   base_type;      // "HTNGraph", "TaskGraph" 等；空 = 通用 Graph

    // 声明段
    std::vector<GraphParameter>   parameters;     // in/out/var 参数
    std::vector<NodeInstance>     nodes;           // 图级节点实例

    // 逻辑段
    std::vector<Event>            events;
    std::vector<Function>         functions;

    // 编辑器段
    GenerateBlock                 generate;
};
```

---

## 13. 双模型架构：EditGraph + RuntimeGraph

`Graph`（§12）是编译器的直接输出，适合序列化。但两个核心使用场景有截然不同的需求：

| | EditGraph（编辑器模型） | RuntimeGraph（运行时模型） |
|---|---|---|
| **用途** | 图形编辑器交互、双向编辑 | 宿主执行引擎 |
| **可变性** | 可变：增删改节点/连接 | 不可变：bake 后冻结 |
| **标识符** | Stable Handle（uint32 index + generation） | 连续整数 ID（uint16） |
| **查找** | 名称索引 + handle O(1) 查找 | 无字符串查找，纯 index |
| **内存** | SlotMap + HashMap，支持碎片化操作 | Flat array，连续内存，cache-friendly |
| **连接** | 双向索引（出/入两个方向） | 单向邻接表（按执行方向） |
| **值存储** | `Value` 对象（类型擦除） | 连续 `uint8_t` buffer + offset |

```
.gs text ──Parser──> AST ──Compiler──> Graph(§12)
                                         │
                                    build_edit()
                                         │
                                         ▼
                                    ┌──────────┐
                     ┌──────────────│ EditGraph │──────────────┐
                     │              └──────────┘              │
                     │  图形编辑器读写     │  文本编辑器读写     │ 运行时请求
                     │                    │                    │
                     ▼                    ▼                    ▼
                 修改 Edit        Emitter → .gs text     bake() → RuntimeGraph
```

---

### 13A. Handle 系统 — Stable 引用

传统数组索引在删除元素后会失效。EditGraph 使用 **generational handle** 保证引用稳定性：

```cpp
template<typename Tag>
struct Handle {
    uint32_t index;       // slot 位置
    uint32_t generation;  // 代数，检测 use-after-free

    bool is_valid() const { return generation != 0; }
    bool operator==(const Handle& o) const {
        return index == o.index && generation == o.generation;
    }
    static Handle invalid() { return {0, 0}; }
};

// 为 node / pin / connection 定义独立的 handle 类型，编译期防止混用
using NodeHandle       = Handle<struct NodeTag>;
using PinHandle        = Handle<struct PinTag>;
using ConnectionHandle = Handle<struct ConnectionTag>;

// hash 支持
template<typename Tag>
struct HandleHash {
    size_t operator()(const Handle<Tag>& h) const {
        return std::hash<uint64_t>{}(
            (uint64_t(h.index) << 32) | h.generation);
    }
};
```

底层用 `SlotMap<T, Tag>` 实现：空闲 slot 复用，generation 递增，O(1) 分配/释放/查找。

```cpp
template<typename T, typename Tag>
class SlotMap {
public:
    Handle<Tag>    insert(T value);
    void           remove(Handle<Tag> handle);
    T*             get(Handle<Tag> handle);        // nullptr if stale
    const T*       get(Handle<Tag> handle) const;
    size_t         size() const;                   // 活跃元素数

    // 遍历所有活跃元素
    template<typename Fn> void for_each(Fn&& fn);
    template<typename Fn> void for_each(Fn&& fn) const;

private:
    struct Slot {
        T        data;
        uint32_t generation;
        bool     occupied;
    };
    std::vector<Slot>     slots_;
    std::vector<uint32_t> free_list_;
};
```

---

### 13B. EditGraph — 编辑器模型

#### 13B.1 EditNode / EditPin / EditConnection

```cpp
struct EditNode {
    NodeHandle                 handle;
    std::string                instance_name;   // 图内唯一
    std::string                type_name;       // 引用 NodeDefinition
    const NodeDefinition*      definition;      // 已解析的类型定义指针

    // 编辑器元数据（来自 generate 块）
    float  pos_x = 0;
    float  pos_y = 0;

    // 所属作用域
    enum class Scope : uint8_t { Global, Function };
    Scope       scope = Scope::Global;
    std::string scope_name;   // scope == Function 时为函数名
};

struct EditPin {
    PinHandle       handle;
    NodeHandle      owner;          // 所属节点
    std::string     name;
    PinKind         kind;
    PinDirection    direction;
    TypeHandle      data_type;      // Data pin 的类型
    Value           default_value;  // Data Input pin 的默认值
    std::string     bound_handler;  // Exec Input pin 绑定的处理函数
};

struct EditConnection {
    ConnectionHandle  handle;
    PinHandle         from_pin;     // output pin (exec or data)
    PinHandle         to_pin;       // input pin (exec or data)

    // 对于数据连接，如果源是字面量或参数（无 from_pin）：
    bool              is_literal_source = false;
    Value             literal_value;
    bool              is_param_source = false;
    std::string       param_name;
};
```

#### 13B.2 EditGraph 类

```cpp
class EditGraph {
public:
    // ── 构造 ──
    // env 中的 SchemaRegistry 根据 Graph.base_type 自动查找 Schema
    static Result<EditGraph, std::string> build(
        const Graph& compiled,
        const Environment& env
    );

    const std::string& name() const;
    const GraphSchema* schema() const;  // 当前 Schema（可能为 nullptr = 通用 Graph）

    // ══════════════════════════════════
    //  节点操作
    // ══════════════════════════════════

    // add_node 内部检查 Schema.allowed_node_tags，不满足过滤条件时返回 invalid handle
    NodeHandle add_node(std::string_view type_name, std::string_view instance_name,
                        EditNode::Scope scope = EditNode::Scope::Global,
                        std::string_view scope_name = "");
    void       remove_node(NodeHandle handle);
    EditNode*  get_node(NodeHandle handle);

    // 按名称查找（编辑器/脚本端用）
    NodeHandle find_node(std::string_view instance_name) const;
    NodeHandle find_node(std::string_view scope, std::string_view instance_name) const;

    // 遍历
    template<typename Fn> void for_each_node(Fn&& fn) const;

    // ══════════════════════════════════
    //  Pin 查询（Pin 随节点创建/销毁，不单独增删）
    // ══════════════════════════════════

    EditPin*           get_pin(PinHandle handle);
    PinHandle          find_pin(NodeHandle node, std::string_view pin_name) const;
    std::vector<PinHandle> pins_of(NodeHandle node) const;
    std::vector<PinHandle> pins_of(NodeHandle node, PinKind kind, PinDirection dir) const;

    // ══════════════════════════════════
    //  连接操作
    // ══════════════════════════════════

    // 添加连接（自动验证类型兼容性 + ConnectionPolicy 约束）
    // 若 Schema 存在，检查 max_exec_fan_out / allow_exec_fan_in / data 规则 / custom_validate
    Result<ConnectionHandle, std::string>
        connect(PinHandle from, PinHandle to);

    // 数据输入 ← 字面量
    Result<ConnectionHandle, std::string>
        connect_literal(PinHandle data_input, Value literal);

    // 数据输入 ← Graph 参数
    Result<ConnectionHandle, std::string>
        connect_parameter(PinHandle data_input, std::string_view param_name);

    void disconnect(ConnectionHandle handle);

    // 查询某个 pin 的所有连接
    std::vector<ConnectionHandle> connections_of(PinHandle pin) const;
    // 查询连向某个 input pin 的连接（最多一个 data input 源）
    ConnectionHandle              connection_to(PinHandle input_pin) const;
    // 查询从某个 output pin 出发的所有连接
    std::vector<ConnectionHandle> connections_from(PinHandle output_pin) const;

    // ══════════════════════════════════
    //  参数
    // ══════════════════════════════════

    const std::vector<GraphParameter>& parameters() const;
    void add_parameter(GraphParameter param);
    void remove_parameter(std::string_view name);

    // ══════════════════════════════════
    //  逻辑块
    // ══════════════════════════════════

    const std::vector<std::string>& event_names() const;
    const std::vector<std::string>& function_names() const;

    // ══════════════════════════════════
    //  编辑器元数据
    // ══════════════════════════════════

    void set_node_position(NodeHandle node, float x, float y);
    void set_node_comment(NodeHandle node, std::string_view comment_id);
    const std::vector<CommentBlock>& comments() const;
    void add_comment(CommentBlock comment);

    // ══════════════════════════════════
    //  变更通知（观察者模式）
    // ══════════════════════════════════

    enum class ChangeType : uint8_t {
        NodeAdded, NodeRemoved, NodeMoved,
        ConnectionAdded, ConnectionRemoved,
        ParameterChanged, CommentChanged
    };

    struct ChangeEvent {
        ChangeType type;
        NodeHandle       node;        // 相关节点（如有）
        ConnectionHandle connection;  // 相关连接（如有）
    };

    using ChangeCallback = std::function<void(const ChangeEvent&)>;
    uint32_t on_change(ChangeCallback cb);
    void     remove_callback(uint32_t id);

    // ══════════════════════════════════
    //  Schema 感知查询
    // ══════════════════════════════════

    // 返回当前 Schema 允许的所有节点定义（无 Schema 或 tags 为空时返回全部）
    std::vector<const NodeDefinition*> available_node_types() const;

    // ══════════════════════════════════
    //  校验
    // ══════════════════════════════════

    // 执行完整校验：通用规则 + Domain 特化规则（若 Schema 提供了 Validator）
    std::vector<Diagnostic> validate() const;

    // ══════════════════════════════════
    //  Bake 到运行时
    // ══════════════════════════════════

    // bake 后若 Schema 有 bake_post_process，自动调用
    Result<RuntimeGraph, std::string> bake() const;

private:
    std::string          name_;
    const Environment*   env_ = nullptr;      // 持有环境引用
    const GraphSchema*   schema_ = nullptr;   // 持有 Schema 引用（可为 nullptr）

    // ── 数据存储（SlotMap） ──
    SlotMap<EditNode, struct NodeTag>             nodes_;
    SlotMap<EditPin, struct PinTag>               pins_;
    SlotMap<EditConnection, struct ConnectionTag> connections_;

    // ── 名称索引 ──
    std::unordered_map<std::string, NodeHandle>   node_by_name_;

    // ── 连接索引（双向） ──
    std::unordered_map<PinHandle, std::vector<ConnectionHandle>,
                       HandleHash<struct PinTag>> pin_connections_;

    // ── 图结构 ──
    std::vector<GraphParameter>  parameters_;
    std::vector<std::string>     event_names_;
    std::vector<std::string>     function_names_;
    std::vector<CommentBlock>    comments_;

    // ── 变更通知 ──
    std::unordered_map<uint32_t, ChangeCallback> callbacks_;
    uint32_t next_callback_id_ = 1;
};
```

---

### 13C. RuntimeGraph — 高性能运行时模型

RuntimeGraph 从 EditGraph 经过 **bake** 产生。所有字符串名称解析为整数索引，所有连接预构建为邻接表，值存储为连续 buffer。**一旦 bake 完成，结构不可变。**

#### 13C.1 运行时标识符

```cpp
using RNodeId = uint16_t;    // 最多 65534 个节点
using RPinId  = uint16_t;    // 最多 65534 个 pin
using RTypeId = uint16_t;    // 类型索引

static constexpr RNodeId INVALID_RNODE = 0xFFFF;
static constexpr RPinId  INVALID_RPIN  = 0xFFFF;
```

#### 13C.2 运行时节点和 Pin

```cpp
struct alignas(8) RNode {
    uint16_t  definition_id;   // 节点定义索引（NodeRegistry 中的位置）
    uint16_t  first_pin;       // pins[] 中的起始索引
    uint16_t  pin_count;       // pin 数量
    uint16_t  _pad;
};

struct alignas(8) RPin {
    RNodeId       owner_node;    // 所属节点
    PinKind       kind;          // Exec / Data
    PinDirection  direction;     // Input / Output
    RTypeId       data_type;     // 数据类型索引
    uint32_t      value_offset;  // value_buffer 中的偏移量
    uint16_t      value_size;    // 值大小（字节）
    uint16_t      _pad;
};
```

#### 13C.3 运行时连接

```cpp
// 执行流边
struct RFlowEdge {
    RPinId from_pin;   // exec output pin
    RPinId to_pin;     // exec input pin
};

// 数据边
struct RDataEdge {
    RPinId   target_pin;       // data input pin
    uint8_t  source_kind;      // 0 = NodePin, 1 = Parameter, 2 = Literal
    uint8_t  _pad;
    union {
        RPinId   source_pin;   // source_kind == 0: data output pin
        uint16_t param_index;  // source_kind == 1: 参数索引
        // source_kind == 2: literal 值在 value_buffer[target_pin.value_offset]
    };
};
```

#### 13C.4 预构建邻接表 — 执行流快速遍历

执行引擎的核心操作：「从某个 exec output pin 出发，找到所有目标 exec input pin」。
用 offset + count 的 flat array 邻接表实现 O(1) 查找 + cache-friendly 遍历：

```cpp
struct RFlowAdjacency {
    // 对每个 exec output pin，存储目标列表
    // adjacency_offset[pin_id] = targets[] 中的起始位置
    // adjacency_count[pin_id]  = 目标数量
    // 如果该 pin 不是 exec output，offset 和 count 都为 0

    std::vector<uint32_t> adjacency_offset;  // 长度 = 总 pin 数
    std::vector<uint16_t> adjacency_count;   // 长度 = 总 pin 数
    std::vector<RPinId>   targets;           // 所有目标 pin，紧密排列

    // O(1) 查找
    const RPinId* targets_of(RPinId output_pin, uint16_t& out_count) const {
        out_count = adjacency_count[output_pin];
        if (out_count == 0) return nullptr;
        return &targets[adjacency_offset[output_pin]];
    }
};
```

#### 13C.5 预构建查找表 — 数据输入快速读取

执行节点时需要读取所有 data input 的值。为每个 data input pin 预构建直接映射：

```cpp
struct RDataLookup {
    // data_source[pin_id] = 该 data input pin 的数据源
    // 如果该 pin 不是 data input，或无连接，source_kind = 0xFF
    std::vector<RDataEdge> data_source;  // 长度 = 总 pin 数
};
```

#### 13C.6 RuntimeLogicBlock — 逻辑块

event 和 function bake 后的运行时表示：

```cpp
struct RuntimeLogicBlock {
    RPinId          entry_pin;       // context.start 的目标 pin
    RPinId          complete_pin;    // context.complete pin（function 专用）
    RFlowAdjacency  flow;           // 执行流邻接表
    RDataLookup     data;           // 数据输入查找表
};
```

#### 13C.7 RuntimeGraph — 完整运行时结构

```cpp
class RuntimeGraph {
public:
    // ── 只读访问 ──

    // 节点 / Pin（flat array，连续内存）
    const RNode* nodes() const;
    const RPin*  pins() const;
    uint16_t     node_count() const;
    uint16_t     pin_count() const;

    // 某个节点的全部 pin
    const RPin* node_pins(RNodeId node_id, uint16_t& out_count) const;

    // 值 buffer（默认值 + 字面量，执行时用于读写 pin 当前值）
    const uint8_t* value_buffer() const;
    size_t         value_buffer_size() const;

    // 参数
    const RPin* params() const;
    uint16_t    param_count() const;

    // 逻辑块
    const RuntimeLogicBlock* find_event(uint16_t event_index) const;
    const RuntimeLogicBlock* find_function(uint16_t func_index) const;

    // ── 名称 → 索引映射（仅初始入口时用，非热路径） ──
    uint16_t event_index(std::string_view name) const;
    uint16_t function_index(std::string_view name) const;
    RNodeId  node_id(std::string_view instance_name) const;

    // ── Domain 标识（宿主据此选择执行器） ──
    const std::string& domain_name() const;            // 空 = 通用 Graph
    const std::vector<uint8_t>& domain_metadata() const; // 宿主自行解释的 opaque 数据

    // ── Bake 构造 ──
    static Result<RuntimeGraph, std::string> bake(
        const EditGraph& edit,
        const Environment& env
    );

private:
    // ── Flat 存储 ──
    std::vector<RNode>   nodes_;
    std::vector<RPin>    pins_;
    std::vector<uint8_t> value_buffer_;

    // ── 参数 ──
    std::vector<RPin>    params_;

    // ── 逻辑块 ──
    std::vector<RuntimeLogicBlock> events_;
    std::vector<RuntimeLogicBlock> functions_;

    // ── 名称映射（sorted vector, binary search） ──
    struct NameEntry {
        uint32_t hash;
        uint16_t index;
    };
    std::vector<NameEntry> event_names_;
    std::vector<NameEntry> function_names_;
    std::vector<NameEntry> node_names_;

    // ── Domain 信息 ──
    std::string          domain_name_;       // 由 bake 时从 Schema 填入
    std::vector<uint8_t> domain_metadata_;   // 由 Schema.bake_post_process 填充
};
```

#### 13C.8 性能特性

| 操作 | 复杂度 | 说明 |
|---|---|---|
| 从 exec output 查找目标 | O(1) | adjacency_offset + targets 直接索引 |
| 读取 data input 值 | O(1) | data_source[pin_id] 直接索引 → value_buffer[offset] |
| 访问节点的所有 pin | O(1) | first_pin + pin_count 连续切片 |
| 值读写 | O(1) | value_buffer + offset 直接内存访问 |
| 按名称查事件/函数 | O(log n) | sorted array + binary search（仅入口时用，非热路径） |

内存布局特征：
- `RNode[]` 和 `RPin[]` 紧密排列，同一节点的 pin 连续存储 → 空间局部性
- `value_buffer` 单次分配，pin 值通过 offset 直接访问 → 零额外分配
- `RFlowAdjacency::targets` 按逻辑块聚集 → 遍历时缓存命中率高
- 所有结构体 8 字节对齐 → 避免跨缓存行访问

---

### 13D. Bake 流程 — EditGraph → RuntimeGraph

```
EditGraph
│
├─ 1. 分配 RNodeId: 遍历所有节点，按 slot 顺序分配连续 ID
│
├─ 2. 分配 RPinId: 遍历所有节点的 pin，按节点顺序分配连续 ID
│     保证同一节点的 pin 连续排列
│
├─ 3. 构建 value_buffer:
│     - 为每个 Data pin 的默认值分配空间
│     - 为每个字面量连接分配空间并写入值
│     - 记录每个 pin 的 value_offset 和 value_size
│
├─ 4. 构建 RFlowAdjacency（每个逻辑块独立）:
│     - 遍历 Exec 连接，收集 from_pin → to_pin 对
│     - 按 from_pin 排序
│     - 构建 offset + count + targets flat array
│
├─ 5. 构建 RDataLookup（每个逻辑块独立）:
│     - 遍历 Data 连接，为每个 target_pin 记录数据源
│
├─ 6. 构建名称映射:
│     - 计算 hash(name) → index 的 sorted array
│
├─ 7. 写入 domain_name_（从 Schema.domain_name 获取，无 Schema 时为空）
│
├─ 8. 若 Schema 有 bake_post_process，调用之:
│     - 可往 domain_metadata_ 写入宿主需要的额外数据
│     - 如 HTN 可写入分解方法表，Task 可写入拓扑排序结果
│
└─ 9. 输出 RuntimeGraph（冻结，不可变）
```

---

### 13E. 内存布局示例（HumanStart.md 的 graph_name）

**EditGraph 视角**（编辑器持有）:

```
EditGraph "graph_name"
│
├─ nodes_ (SlotMap):
│   [handle_0] EditNode "native_node"   type="FNodeType"  scope=Global  pos=(10,20)
│   [handle_1] EditNode "compose_node"  type="FNodeType"  scope=Global  pos=(7,8)
│   [handle_2] EditNode "native_node_1" type="FNodeType"  scope=Function("function_name_1")
│
├─ pins_ (SlotMap):
│   [pin_0] "in"           ExecIn   owner=handle_0
│   [pin_1] "out"          ExecOut  owner=handle_0
│   [pin_2] "complete"     ExecOut  owner=handle_0
│   [pin_3] "in_value_1"   DataIn   owner=handle_0  type=int
│   [pin_4] "in_value_2"   DataIn   owner=handle_0  type=FType
│   [pin_5] "out_value"    DataOut  owner=handle_0  type=...
│   [pin_6] "in"           ExecIn   owner=handle_1
│   ... (compose_node 的 pins)
│
├─ connections_ (SlotMap):
│   [conn_0] ctx.start       → pin_0 (native_node.in)       [event_name]
│   [conn_1] pin_1           → pin_6 (native_node.out → compose_node.in)
│   [conn_2] pin_3 ← Literal(int, 1)
│   [conn_3] pin_4 ← Parameter("in_var_1")
│   [conn_4] compose_node.in_value ← pin_5 (native_node.out_value)
│   ...
│
├─ pin_connections_ (双向索引):
│   pin_0 → [conn_0]
│   pin_1 → [conn_1]
│   pin_3 → [conn_2]
│   ...
│
└─ node_by_name_:
    "native_node"   → handle_0
    "compose_node"  → handle_1
    "native_node_1" → handle_2
```

**RuntimeGraph 视角**（bake 后，执行引擎持有）:

```
RuntimeGraph "graph_name"
│
├─ nodes_[]:     [RNode{def=0, first_pin=0, count=6}, RNode{def=0, first_pin=6, count=5}, ...]
├─ pins_[]:      [RPin, RPin, RPin, ...]  (连续，同一节点的 pin 相邻)
├─ value_buffer: [00 00 00 01 | ...default values... | ...literal values...]
│                 ↑ pin_3 的字面量 int(1)
│
├─ events_[0] (event_name):
│   ├─ entry_pin: 0  (native_node.in)
│   ├─ flow.adjacency_offset: [_, 6, 8, ...]  // pin_1(out)→offset 6, pin_2(complete)→offset 8
│   ├─ flow.adjacency_count:  [_, 1, 1, ...]
│   ├─ flow.targets: [..., 6, ..., 7, ...]    // pin 6 = compose_node.in, pin 7 = compose_node.in_2
│   └─ data.data_source[3]: {target=3, kind=Literal, value_offset=0}
│      data.data_source[4]: {target=4, kind=Param, param_index=0}
│      data.data_source[10]: {target=10, kind=NodePin, source_pin=5}
│
└─ node_names_: [{hash("native_node"),0}, {hash("compose_node"),1}, ...]  (sorted)
```

---

## 14. NodeRegistry — 节点类型注册

### 14.1 NodeRegistry

```cpp
class NodeRegistry {
public:
    // 注册 native 节点定义（宿主 C++ 或 .d.gs 声明）
    void register_node(NodeDefinition def);

    // 注册 Graph 推导的节点（Compiler 在编译 Graph 后自动调用）
    void register_graph_node(NodeDefinition def);

    // 查找
    const NodeDefinition* find(std::string_view type_name) const;

    // 列出所有已注册节点
    std::vector<const NodeDefinition*> all() const;

private:
    std::unordered_map<std::string, NodeDefinition> nodes_;
};
```

### 14.2 Environment — 组合注册入口

```cpp
class Environment {
public:
    TypeRegistry&   types();
    NodeRegistry&   nodes();
    SchemaRegistry& schemas();
    const TypeRegistry&   types() const;
    const NodeRegistry&   nodes() const;
    const SchemaRegistry& schemas() const;

    // 加载一个 Module 的所有定义到注册表
    Result<void, std::string> load_module(const Module& module);

private:
    TypeRegistry   type_registry_;
    NodeRegistry   node_registry_;
    SchemaRegistry schema_registry_;
};
```

---

## 15. AST 节点类型概览

AST 节点与语法一一对应。每个节点携带 `SourceRange` 用于 round-trip 编辑定位。

### 15.1 SourceLocation / SourceRange

```cpp
struct SourceLocation {
    uint32_t line;     // 1-based
    uint32_t column;   // 1-based
};

struct SourceRange {
    SourceLocation begin;
    SourceLocation end;
};
```

### 15.2 AST 节点层级

```
ASTNode (base)
├── ModuleNode              // 整个文件
├── ImportNode              // import 语句
├── LetDeclNode             // let 定义
├── DeclareTypeNode         // declare type Name [: constructible];  (.d.gs)
├── DeclareNodeNode         // declare Node Name { ... }             (.d.gs)
├── DeclareSchemaNode       // declare Schema Name { ... }           (.d.gs)
├── GraphNode               // Graph 块（含可选 base_type: "HTNGraph" 等）
│   ├── ParamDeclNode       //   in/out/var 参数
│   ├── NodeInstanceNode    //   节点实例声明
│   ├── EventNode           //   event 块
│   ├── FunctionNode        //   function 块
│   │   ├── FlowStmtNode   //     执行流连接语句
│   │   └── LinkStmtNode   //     数据连接语句
│   └── GenerateNode        //   generate 块
│       ├── CommentNode     //     Comment 声明
│       └── MetadataNode    //     scope:node.property() 语句
└── ExprNode                // 表达式（字面量、构造器调用等）
    ├── LiteralNode         //   int(1), "hello" 等
    ├── IdentifierNode      //   变量/参数引用
    └── ConstructorNode     //   TypeName("args")
```

`GraphNode` 新增字段：

```cpp
struct GraphNode : ASTNode {
    std::string                    name;
    std::optional<std::string>     base_type;  // "HTNGraph" 等; nullopt = 通用 Graph
    // ... 其余字段省略（ParamDecl, NodeInstance, Event, Function, Generate）
};
```

每个 AST 节点的公共基类：

```cpp
struct ASTNode {
    SourceRange   range;          // 源码位置
    std::string   leading_trivia; // 前置空白和注释（round-trip 保留用）

    virtual ~ASTNode() = default;
};
```

---

## 16. Token 定义

```cpp
enum class TokenType : uint8_t {
    // ── 关键字 ──
    KW_import,
    KW_let,
    KW_declare,       // declare（.d.gs 声明文件专用）
    KW_type,          // type（declare type ...）
    KW_Node,          // Node（declare Node ... 和 Graph 中节点实例类型名）
    KW_Schema,        // Schema（declare Schema ...）
    KW_Graph,
    KW_event,
    KW_function,
    KW_generate,
    KW_in,
    KW_out,
    KW_var,
    KW_link,
    KW_Comment,
    KW_constructible, // constructible（declare type Name : constructible）

    // ── 字面量 ──
    IntLiteral,       // 42
    FloatLiteral,     // 3.14
    StringLiteral,    // "hello"
    BoolLiteral,      // true / false

    // ── 标识符 ──
    Identifier,       // 变量名、类型名等

    // ── 符号 ──
    LeftBrace,        // {
    RightBrace,       // }
    LeftParen,        // (
    RightParen,       // )
    LeftAngle,        // <
    RightAngle,       // >
    Comma,            // ,
    Semicolon,        // ;
    Colon,            // :
    Dot,              // .
    Assign,           // =

    // ── 注释 ──
    LineComment,      // // ...
    BlockComment,     // /* ... */

    // ── 控制 ──
    EndOfFile,
    Error
};

struct Token {
    TokenType      type;
    std::string    text;      // 原始文本
    SourceLocation location;
};
```

---

## 17. 双向编辑策略

### 17.1 文本 → 图（正向）

```
.gs text ──Lexer──> tokens ──Parser──> AST ──Compiler──> Graph(§12)
                                                           │
                                                    EditGraph::build()
                                                           │
                                                      EditGraph（可编辑）
                                                           │
                                                      bake()（按需）
                                                           │
                                                      RuntimeGraph（可执行）
```

### 17.2 图 → 文本（逆向 / round-trip）

关键原则：**仅修改 generate 块和逻辑块中的连接语句，其余文本原样保留**。

```
编辑器操作
    │
    ▼
修改 EditGraph 的连接 / 节点位置 / 注释
    │
    ▼
定位对应 AST 节点（通过 SourceRange）
    │
    ▼
替换该 AST 子树
    │
    ▼
Emitter 输出修改后的文本
（未修改的 AST 节点保留原始 leading_trivia + 源文本）
```

### 17.3 generate 块特殊处理

图形编辑器移动节点 / 修改注释时：
1. 直接修改 `EditGraph` 中节点的 pos_x/pos_y 和 comments
2. 重建 `GenerateNode` AST 子树
3. Emitter 输出 generate 块的新文本
4. 替换原 .gs 文件中 generate 块的文本范围

脚本的 import、let、Node、Graph 参数、event/function 逻辑 —— 全部保持用户手写的原始文本。

### 17.4 EditGraph 修改后的 RuntimeGraph 更新

EditGraph 变更后，旧的 RuntimeGraph 失效，需要重新 bake：

```
EditGraph 修改
    │
    ▼
标记 RuntimeGraph 为 dirty
    │
    ▼
下次执行请求时 → EditGraph.bake() → 新的 RuntimeGraph
    │
    ▼
替换旧 RuntimeGraph（原子交换）
```

bake 是一次性批处理，不支持增量更新。这是有意为之：
- bake 的时间开销可接受（毫秒级，图规模通常 < 1000 节点）
- 增量更新会破坏 flat array 布局，丧失运行时性能优势
- dirty 标记避免每次 edit 都触发 bake

---

## 18. 宿主集成接口

宿主（如 UE 项目）通过 C++ API 注入 native 类型和节点：

```cpp
// 宿主侧初始化示例
GraphScript::Environment env;

// 注册 native 类型
env.types().register_type({
    .name = "SoftObjectPath",
    .is_native = true,
    .construct = [](std::string_view text) -> Value {
        return Value(TypeHandle{...}, FSoftObjectPath(text));
    },
    .serialize = [](const Value& v) -> std::string {
        return v.as<FSoftObjectPath>()->ToString();
    }
});

// 注册 native 节点
NodeDefinition print_node;
print_node.type_name = "PrintString";
print_node.is_native = true;
print_node.pins = {
    {.name = "In",     .kind = PinKind::Exec, .direction = PinDirection::Input},
    {.name = "Out",    .kind = PinKind::Exec, .direction = PinDirection::Output},
    {.name = "String", .kind = PinKind::Data, .direction = PinDirection::Input,
     .data_type = env.types().type_string(),
     .default_value = Value("Hello")},
};
env.nodes().register_node(std::move(print_node));

// 注册 Domain Schema（HTN 为例）
GraphScript::GraphSchema htn_schema;
htn_schema.domain_name = "htn";
htn_schema.base_graph_name = "HTNGraph";
htn_schema.connection_policy.max_exec_fan_out = UINT32_MAX;
htn_schema.required_events = {{.name = "decompose"}};
htn_schema.allowed_node_tags = {"htn_task", "htn_method", "common"};
env.schemas().register_schema(std::move(htn_schema));

// 加载 .gs 文件 → 编辑器模型
auto ast = GraphScript::parse_file("my_graph.gs");
auto module = GraphScript::compile(ast, env);
auto edit = GraphScript::EditGraph::build(module.graphs[0], env);
// build() 自动根据 Graph.base_type 查找 Schema

// 编辑器操作（图形编辑器调用）
edit.set_node_position(some_node, 100.f, 200.f);

// 校验（编辑器侧可随时调用）
auto diags = edit.validate();

// Bake 到运行时模型（执行前调用）
auto runtime = edit.bake();

// 宿主执行引擎使用 runtime（根据 domain_name 选择执行器）
if (runtime.domain_name() == "htn") {
    // 使用 HTN planner
}
uint16_t event_idx = runtime.event_index("decompose");
auto* block = runtime.find_event(event_idx);
// ... 用 block->flow / block->data 驱动执行 ...
```

---

## 19. 错误处理

全局使用 `Result<T, Error>` 模式，不使用异常：

```cpp
template<typename T, typename E = std::string>
class Result {
public:
    static Result ok(T value);
    static Result err(E error);

    bool     is_ok() const;
    bool     is_err() const;
    T&       value();
    const E& error() const;
};
```

---

## 20. 附录

### 20.1 完整数据流示意

以加载一个带 Domain 继承的 `.gs` 文件为例：

```
┌──────────────────────────────────────────────────────┐
│  .gs 文件文本                                         │
│  Graph my_plan : HTNGraph { ... }                     │
└───────────────────────┬──────────────────────────────┘
                        │ Lexer
                        ▼
┌──────────────────────────────────────────────────────┐
│  Token 流: [KW_Graph, Ident("my_plan"), Colon,        │
│            Ident("HTNGraph"), LBrace, ...]             │
└───────────────────────┬──────────────────────────────┘
                        │ Parser
                        ▼
┌──────────────────────────────────────────────────────┐
│  AST: GraphNode                                       │
│   base_type = "HTNGraph"                              │
│   ├─ EventNode("decompose")                           │
│   │   ├─ FlowStmtNode(...)                           │
│   │   └─ LinkStmtNode(...)                           │
│   └─ GenerateNode(...)                                │
└───────────────────────┬──────────────────────────────┘
                        │ Compiler + SchemaRegistry 查找
                        ▼
┌──────────────────────────────────────────────────────┐
│  Graph (§12, base_type="HTNGraph")                    │
└───────────────────────┬──────────────────────────────┘
                        │ EditGraph::build(graph, env)
                        │ → SchemaRegistry.find_by_base("HTNGraph")
                        ▼
┌──────────────────────────────────────────────────────┐
│  EditGraph (§13B)                                     │
│   schema_ = HTNSchema*                                │
│   connect()  → 查 ConnectionPolicy (fan-out unlimited)│
│   add_node() → 查 allowed_node_tags                   │
│   validate() → HTNValidator (common + domain)         │
└────────┬───────────────┬──────────────┬──────────────┘
         │               │              │
         ▼               ▼              ▼
  Emitter → .gs    validate()      bake()
  (round-trip)     → Diagnostics   → RuntimeGraph
                                     domain_name_="htn"
                                     domain_metadata_=[...]
                                          │
                                          └──→ 宿主执行引擎
```

### 20.2 项目目录结构

```
GraphScript/
├── CMakeLists.txt              // 顶层构建
├── README.md
├── CLAUDE.md                   // AI 助手指引
├── HumanStart.md               // 原始设计思路
├── docs/
│   └── design.md               // 本文档
├── include/graphscript/        // 公共头文件
│   ├── core/                   // 核心数据结构
│   │   ├── types.h             // TypeHandle, TypeInfo, TypeRegistry, Value
│   │   ├── pin.h               // PinKind, PinDirection, PinDefinition, PinAddress
│   │   ├── node.h              // NodeDefinition, NodeInstance
│   │   ├── connection.h        // FlowConnection, DataSource, DataLink
│   │   ├── graph.h             // Graph, GraphParameter, LogicBlock, GenerateBlock
│   │   └── result.h            // Result<T, E>
│   ├── schema/                 // Domain/Schema 系统
│   │   ├── connection_policy.h // ConnectionPolicy
│   │   ├── graph_schema.h      // GraphSchema
│   │   ├── schema_registry.h   // SchemaRegistry
│   │   ├── validator.h         // Diagnostic, GraphValidator
│   │   └── domains/            // 预定义 Domain
│   │       ├── htn_schema.h
│   │       ├── task_schema.h
│   │       └── levelscript_schema.h
│   ├── edit/                   // 编辑器模型
│   │   ├── handle.h            // Handle<Tag>, SlotMap
│   │   └── edit_graph.h        // EditNode, EditPin, EditConnection, EditGraph
│   ├── runtime/                // 运行时模型
│   │   └── runtime_graph.h     // RNode, RPin, RuntimeGraph
│   ├── parse/                  // 解析
│   │   ├── lexer.h
│   │   ├── token.h
│   │   ├── parser.h
│   │   └── ast.h
│   ├── compile/                // 编译
│   │   └── compiler.h
│   ├── emit/                   // 输出
│   │   └── emitter.h
│   ├── registry/               // 注册表
│   │   ├── node_registry.h
│   │   └── environment.h
│   └── graphscript.h           // 统一入口头文件
├── src/                        // 实现文件（与 include 结构对应）
├── cli/                        // CLI 工具
│   ├── main.cpp
│   ├── cmd_parse.cpp
│   ├── cmd_compile.cpp
│   ├── cmd_validate.cpp
│   ├── cmd_emit.cpp
│   ├── cmd_bake.cpp
│   ├── cmd_info.cpp
│   └── cmd_schema.cpp
└── tests/                      // 单元测试
    ├── test_main.cpp
    ├── test_lexer.cpp
    ├── test_parser.cpp
    ├── test_compiler.cpp
    ├── test_type_registry.cpp
    ├── test_node_registry.cpp
    ├── test_slotmap.cpp
    ├── test_editgraph.cpp
    ├── test_connection_policy.cpp
    ├── test_node_filter.cpp
    ├── test_validator.cpp
    ├── test_htn_validator.cpp
    ├── test_task_validator.cpp
    ├── test_levelscript_validator.cpp
    ├── test_schema_registry.cpp
    ├── test_emitter.cpp
    ├── test_bake.cpp
    ├── test_runtimegraph.cpp
    ├── test_value.cpp
    └── fixtures/               // 测试用 .gs 文件
        ├── minimal.gs
        ├── htn_basic.gs
        ├── task_basic.gs
        ├── levelscript_basic.gs
        ├── invalid_connection.gs
        └── round_trip.gs
```

---

## 21. CLI 工具

### 21.1 设计目标

所有核心功能通过 `gs` CLI 工具暴露，支持 AI Agent 通过 Shell 直接调用来验证功能正确性。

### 21.2 子命令体系

统一入口 `gs <subcommand> [options]`：

| 子命令 | 功能 | 典型用法 |
|--------|------|---------|
| `gs parse` | 解析 .gs → AST | `gs parse -i test.gs -f json` |
| `gs compile` | 编译 .gs → Module | `gs compile -i test.gs -n native.d.gs -f json` |
| `gs validate` | 校验 Graph（含 Schema） | `gs validate -i htn.gs -s HTNGraph -f table` |
| `gs emit` | EditGraph → .gs 文本 | `gs emit -i original.gs -o roundtrip.gs` |
| `gs bake` | Bake → RuntimeGraph 统计 | `gs bake -i graph.gs -f json` |
| `gs info` | Module 概要 | `gs info -i graph.gs` |
| `gs schema` | 列出已注册 Schema | `gs schema -f table` |

### 21.3 通用选项

```
gs <subcommand> [options]

  --input, -i <path>       输入 .gs 文件路径
  --output, -o <path>      输出文件路径（默认 stdout）
  --format, -f <fmt>       输出格式: json | text | table (默认 text)
  --native, -n <path>      加载 native 声明文件 (.d.gs)
  --schema, -s <name>      指定 Schema（validate/compile 时用）
  --verbose, -v            详细输出
  --quiet, -q              仅输出错误
```

### 21.4 典型调试场景

**验证 Lexer/Parser**：
```bash
gs parse -i test.gs -f json | jq '.graphs[0].base_type'
```

**验证 Schema 连接策略**：
```bash
gs validate -i htn_test.gs -s HTNGraph -f table
# SEVERITY  RULE_ID              MESSAGE                NODE
# error     common.type_mismatch  int vs float mismatch  native_node
# warning   htn.no_root_task      no root task found     -
```

**Round-trip 验证**：
```bash
gs emit -i original.gs -o roundtrip.gs
diff original.gs roundtrip.gs
```

**Bake 性能验证**：
```bash
gs bake -i large_graph.gs -f json
# {"node_count": 150, "pin_count": 620, "value_buffer_bytes": 2048, "bake_ms": 3.2}
```

**查看可用 Schema**：
```bash
gs schema -f table
# DOMAIN        BASE_TYPE          MAX_EXEC_FAN_OUT  ALLOW_EXEC_FAN_IN  REQUIRED_EVENTS
# htn           HTNGraph           unlimited         no                 decompose
# task          TaskGraph          1                 yes                on_start, on_complete
# level_script  LevelScriptGraph   unlimited         no                 on_begin_play
```

### 21.5 CMake 集成

```cmake
add_executable(gs
    cli/main.cpp
    cli/cmd_parse.cpp
    cli/cmd_compile.cpp
    cli/cmd_validate.cpp
    cli/cmd_emit.cpp
    cli/cmd_bake.cpp
    cli/cmd_info.cpp
    cli/cmd_schema.cpp
)
target_link_libraries(gs PRIVATE graphscript_core)
```

---

## 22. 测试策略

### 22.1 框架选型

- **Google Test (gtest)** — CMake 原生支持（`FetchContent`），UE 生态常见
- **Google Mock (gmock)** — mock callback / host 注册接口

### 22.2 CMake 集成

```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(gs_tests
    tests/test_main.cpp
    tests/test_lexer.cpp
    tests/test_parser.cpp
    tests/test_compiler.cpp
    tests/test_type_registry.cpp
    tests/test_node_registry.cpp
    tests/test_slotmap.cpp
    tests/test_editgraph.cpp
    tests/test_connection_policy.cpp
    tests/test_node_filter.cpp
    tests/test_validator.cpp
    tests/test_htn_validator.cpp
    tests/test_task_validator.cpp
    tests/test_levelscript_validator.cpp
    tests/test_schema_registry.cpp
    tests/test_emitter.cpp
    tests/test_bake.cpp
    tests/test_runtimegraph.cpp
    tests/test_value.cpp
)
target_link_libraries(gs_tests PRIVATE graphscript_core GTest::gtest GTest::gmock)

include(GoogleTest)
gtest_discover_tests(gs_tests)
```

### 22.3 每模块测试清单

**Lexer** (`test_lexer.cpp`)：关键字识别、字面量、符号、注释、错误 token、SourceLocation 正确性

**Parser** (`test_parser.cpp`)：import/let/Node/Graph 解析、`Graph name : BaseType` 继承语法、错误恢复、SourceRange 准确性

**Compiler** (`test_compiler.cpp`)：Module 结构、NodeDefinition 生成、Graph 连接解析、类型解析、错误（未声明类型/节点/重复名称）

**ConnectionPolicy** (`test_connection_policy.cpp`) — 核心测试：
- 默认 policy（exec 1:1, data N:1）
- HTN policy（exec fan-out unlimited，连 3 个成功）
- Task policy（exec fan-out 1，第二个连接被拒）
- Task policy（exec fan-in allowed，多入一出成功）
- Data fan-out / fan-in 各种组合
- Custom validate 钩子调用和拒绝
- 无 Schema 时使用默认 policy

**节点过滤** (`test_node_filter.cpp`)：tagged 节点通过/被拒、无 tags 限制时全部可用、`available_node_types()` 返回正确过滤结果

**Validator**：
- `test_validator.cpp`：通用规则（类型不匹配、名称重复、必须事件缺失、悬空 pin）
- `test_htn_validator.cpp`：无根任务、分解环路、方法缺失
- `test_task_validator.cpp`：依赖环路、逻辑块为空
- `test_levelscript_validator.cpp`：事件缺失、触发器无效

**EditGraph** (`test_editgraph.cpp`)：节点 CRUD、Pin 查询、连接 CRUD、参数操作、变更通知、Handle 稳定性

**Emitter** (`test_emitter.cpp`)：Round-trip（parse → compile → build → emit → diff = 无差异）、generate 块修改、`: BaseType` round-trip

**Bake** (`test_bake.cpp`)：RNode/RPin 数量、value buffer、邻接表、数据查找表、bake_post_process 钩子、domain_name 传递

### 22.4 测试执行

```bash
cmake --build build --target gs_tests
cd build && ctest --output-on-failure

# 运行特定测试
./build/gs_tests --gtest_filter="ConnectionPolicyTest.*"

# 详细输出
./build/gs_tests --gtest_filter="*" --gtest_print_time=1
```

### 22.5 测试约定

- 每个功能实现后，先写测试、再提交代码
- 测试命名：`TEST(ModuleName, TestCaseName)`
- 负面测试与正面测试比例 >= 1:1
- `fixtures/` 目录的 `.gs` 文件同时供 CLI 和单元测试使用
- CI 中每次提交自动运行 `ctest`
