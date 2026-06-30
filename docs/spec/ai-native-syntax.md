# AI Native 资产语法草案

> 状态：讨论草案。
> 目的：单独描述面向通用游戏资产序列化的源语言语法。本文只讨论 source syntax，不讨论完整 runtime 实现。

---

## 1. 设计定位

这门语言是一种静态、类型化、可错误恢复的游戏资产文本格式。

它的表层语法选择 TypeScript-flavored 方向：尽量借鉴 TypeScript 的块、类型标注、对象字面量、decorator 和调用表达式，但语义不是执行普通 TypeScript 程序，而是描述可序列化资产对象、scope、属性、引用和受限 command。

核心特点：

- 文本是唯一真源。
- 语法树必须可无损 round-trip。
- 错误文本也要尽可能恢复结构。
- parser 只理解通用语法，不内建 Graph/HTN/Table 等领域概念。
- 领域语义由 binder/projection/lint provider 解释。

当前语法风格决策：

```text
选择 TypeScript-flavored syntax。
不采用 Python 缩进语法。
不要求文件是合法 TypeScript。
不采用 TypeScript runtime semantics。
```

借鉴 TypeScript：

```text
name: Type
property: value
{ ... } block
@decorator / @attribute
qualified.member.access
call(arg)
array literal
object literal
import-like dependency declaration
```

GraphScript 自己定义：

```text
scope
declare object/scope/schema/command
const x = new Type { ... } 的静态资产语义
.connect(...) 的 command 语义
partial compilation / source binding / projection
```

### 1.1 Parser 技术选型

当前决策：新语法使用 Tree-sitter 实现 Syntax Parser。

Tree-sitter 负责：

- 解析源文本。
- 产出 concrete syntax tree。
- 提供 source range。
- 支持错误恢复。
- 支持增量解析。

Tree-sitter 不负责：

- 类型绑定。
- 名字解析。
- lint。
- graph/table/HTN projection。
- runtime/bake。
- TextPatch/CodeAction 高层语义操作。

这些能力由 GraphScript 自己的 AST/Text Framework 和上层 domain framework 实现。

语法设计应避免依赖 Tree-sitter 之外难以恢复的隐式规则。每个可视化编辑操作需要对应到明确、稳定的 CST node 或 source range。

### 1.2 Tree-sitter CST 契约

Tree-sitter grammar 的第一目标不是直接产出最终 AST，而是产出稳定、可查询、可增量更新的 CST。AST/Text Framework 会在 CST 之上提供 typed wrapper、SemanticModel、TextPatch 和 projection。

#### 1.2.1 基本约束

grammar 必须满足：

- 所有可被编辑器、AI、lint、projection 引用的结构都应是 named node。
- 标点 token 可以匿名，但关键语义片段必须有稳定 field name。
- whitespace 由 `SourceText` 保留；不依赖 CST 表示空白。
- 注释应作为 `line_comment` / `block_comment` named token 保留为 extras，并可通过 range 从 `SourceText` 取回。
- 错误文本应尽量恢复到最近的 `scope_declaration`、`object_expression`、`property_declaration`、`call_statement` 或 `declaration`。
- 不在 grammar 中编码 Graph/HTN/Table/FlowGraph 语义。

#### 1.2.2 最低 named node 集合

第一版 grammar 至少应产出这些 named node：

```text
source_file
import_declaration
export_declaration

scope_declaration
const_declaration
object_expression
property_declaration
call_statement
assignment_statement
directive_statement
empty_statement

declaration
module_declaration
type_declaration
enum_declaration
enum_member
object_declaration
field_declaration
scope_kind_declaration
command_declaration
schema_declaration
lint_declaration

attribute
attribute_argument
parameter_list
parameter_declaration
argument_list

type_ref
qualified_name
member_expression
ref_expression
asset_ref_expression
array_expression
inline_object_expression

string_literal
int_literal
float_literal
bool_literal
null_literal

line_comment
block_comment
ERROR
```

如果 Tree-sitter 的实际错误节点名为 `ERROR`，C++ facade 可以包装成 `error_node`；但原始 CST 不应隐藏错误节点。

#### 1.2.3 关键 field name

稳定 field name 比具体树深度更重要。C++ wrapper 和 source patch API 应优先依赖 field name，而不是子节点序号。

推荐 field：

```text
scope_declaration:
  attributes
  kind
  name
  type
  parameters
  body

const_declaration:
  attributes
  name
  value

object_expression:
  type
  body

property_declaration:
  attributes
  name
  value

field_declaration:
  attributes
  name
  type
  default_value

call_statement:
  expression

call_expression:
  callee
  arguments

assignment_statement:
  target
  value

attribute:
  name
  arguments

attribute_argument:
  name
  value

import_declaration:
  path

module_declaration:
  id
  body

export_declaration:
  declaration
  names
```

示例：

```ts
@id("apply")
const apply = new ApplyDamage {
    amount: 50;
}
```

应能稳定查询到：

```text
const_declaration.name -> apply
const_declaration.attributes -> @id("apply")
const_declaration.value -> object_expression
object_expression.type -> ApplyDamage
property_declaration.name -> amount
property_declaration.value -> 50
```

#### 1.2.4 Source patch anchor

以下操作必须能从 CST 找到稳定 patch anchor：

```text
插入 import:
  source_file.import_declaration 列表末尾或第一个顶层 item 前

插入 declare module:
  import_declaration 列表之后，第一个 declaration/export/scope/const 前

插入 scope:
  source_file 或 parent scope 的 body

重命名 scope:
  scope_declaration.name

修改 scope kind:
  scope_declaration.kind

修改 scope type:
  scope_declaration.type

插入 object:
  scope_declaration.body

重命名 object alias:
  const_declaration.name

修改 object type:
  object_expression.type

插入/删除/修改 property:
  object_expression.body 或 scope_declaration.body 中的 property_declaration

修改 property value:
  property_declaration.value

插入/删除 connect call:
  scope_declaration.body 中的 call_statement

修改 connect 端点:
  call_expression.callee 或 argument_list 中对应 argument

添加/删除 attribute:
  对应 declaration/field/property/scope 的 attributes

修改 attribute 参数:
  attribute_argument.value
```

如果某个操作找不到对应 patch anchor，图编辑器应降级为插入新的 fragment，而不是重写整个文件。

#### 1.2.5 错误恢复要求

错误恢复的最低要求：

- 缺少 `;` 时保留前一个 declaration/property/call 的结构，并产生诊断。
- 缺少 property value 时生成可包装为 `MissingExpr` 的错误节点或空 value range。
- 未闭合 `{` 时尽量恢复 body 内已完成 item。
- 未闭合 call `)` 时保留 callee 和已解析 argument。
- 未闭合 attribute `)` 时保留 attribute name 和已解析 argument。
- 顶层出现未知 token 时生成 `ERROR`，但继续恢复后续 item。

示例：

```ts
const apply = new ApplyDamage {
    amount: 50
    target:
}

context.start.connect(apply.enter
```

CST facade 至少应能恢复：

```text
const_declaration apply
object_expression ApplyDamage
property_declaration amount value=50
property_declaration target value=MissingExpr
call_statement callee=context.start.connect args=[apply.enter] missing=')'
```

#### 1.2.6 Tree-sitter query 目标

第一版 grammar 应允许通过 query 找到常见语义结构。

示例 query 目标：

```scheme
; 找所有 scope
(scope_declaration
  kind: (identifier) @scope.kind
  name: (identifier) @scope.name)

; 找所有对象声明
(const_declaration
  name: (identifier) @object.alias
  value: (object_expression
    type: (type_ref) @object.type))

; 找 FlowGraph pin 字段
(field_declaration
  attributes: (attribute
    name: (qualified_name) @attribute.name)
  name: (identifier) @field.name)

; 找 connect call
(call_statement
  expression: (call_expression
    callee: (member_expression) @call.callee
    arguments: (argument_list) @call.args))
```

这些 query 不是最终 API，但能作为 parser 原型的验收样例。

---

## 2. 顶层结构

一个文件由 import 和顶层 item 组成。

```ebnf
File        ::= ImportDecl* Item*
Item        ::= ScopeDecl
              | ConstDecl
              | PropertyDecl
              | CallStmt
              | AssignStmt
              | DirectiveStmt
              | DeclStmt
              | ExportDecl
              | EmptyStmt
```

示例：

```ts
import "ue_core.asset";
import "ability_nodes.asset";

scope asset Fireball: Ability {
    const damage = new DamageEffect {
        amount: 50;
        type: DamageType.Fire;
    }
}
```

---

## 3. 文件类型

新语法建议区分资产文件和声明文件。

```text
.gs      source asset file，保存真实资产对象、scope、图、表、连接等内容
.d.gs    declaration file，保存类型、scope kind、对象类型、command、schema、导出符号和依赖信息
```

`.d.gs` 类似 TypeScript 的 `.d.ts` 或当前 GraphScript 的 `.d.gs`，但它服务的是通用资产格式，而不是只服务图 DSL。

### 3.1 `.gs` 资产文件

`.gs` 文件可以包含：

```text
import
scope
const object
property
command call
assignment
directive
attribute
```

示例：

```ts
import "ability_core.d.gs";

scope asset Fireball: Ability {
    const damage = new DamageEffect {
        amount: 50;
    }
}
```

### 3.2 `.d.gs` 声明文件

`.d.gs` 文件只声明接口和元信息，不创建真实资产实例。

`.d.gs` 可以包含：

```text
import
export
declare module
declare type
declare enum
declare object
declare scope
declare command
declare schema
declare lint
```

示例：

```ts
import "core.d.gs";

declare module "ability.core" {
    package: "Game.Ability";
    version: "1.0.0";
}

export declare type Ability;
export declare type Actor;
export declare type float;

export declare enum DamageType {
    Fire;
    Ice;
    Physical;
}

export declare object DamageEffect {
    amount: float;
    type: DamageType;
}

export declare object ApplyDamage {
    @flow.input
    target: Actor;

    @flow.input
    amount: float;

    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;

    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;

    @flow.pin(kind = "data", direction = "out")
    result: DamageResult;

    @editor.field
    pos: Vec2;
}

export declare scope graph AbilityGraph {
    allows object ApplyDamage;
    allows command connect;
    requires entry Start;
}

export declare command connect(from: PinRef, to: PinRef): EdgeRef;
```

设计原则：

- `.d.gs` 可被 parser 解析成同一套 CST。
- `.d.gs` 不包含 `const x = new Type { ... }` 这种实例创建。
- `.d.gs` 是 SemanticModel、补全、lint、projection 和依赖分析的主要输入。
- domain module 可以扩展 declaration 的语义，但不应要求 parser 内建具体 domain。

---

## 4. 词法

### 4.1 标识符

```ebnf
Identifier ::= Letter (Letter | Digit | "_")*
```

示例：

```text
Fireball
AbilityGraph
apply_damage
OnBeginPlay
```

### 4.2 关键字

基础关键字：

```text
import
scope
const
new
declare
export
true
false
null
ref
```

保留但暂不一定启用的关键字：

```text
let
var
as
from
```

声明相关词如 `type`、`enum`、`object`、`command`、`schema`、`lint`、`for` 由 declaration 语法使用。

领域词如 `asset`、`graph`、`entry`、`table`、`row` 不是 parser 关键字，而是 scope kind identifier。

### 4.3 字面量

```ebnf
Literal ::= StringLiteral
          | IntLiteral
          | FloatLiteral
          | BoolLiteral
          | NullLiteral
```

示例：

```ts
"Fireball"
50
3.0
true
null
```

### 4.4 注释

支持行注释和块注释。

```ts
// line comment

/* block
   comment */
```

注释属于 trivia，必须被 Syntax Tree/CST 保留。

---

## 5. Import

```ebnf
ImportDecl ::= "import" StringLiteral ";"
```

示例：

```ts
import "ue_core.asset";
import "levelscript_nodes.asset";
```

import 只声明依赖，不执行代码。

---

## 6. Export 与 Declaration

`.d.gs` 需要显式表达导出符号，方便依赖分析、补全和跨文件绑定。

### 6.1 Export

```ebnf
ExportDecl ::= "export" DeclStmt
             | "export" "{" ExportNameList "}" ";"

ExportNameList ::= Identifier ("," Identifier)* ","?
```

示例：

```ts
export declare type Ability;
export declare object ApplyDamage { ... }

export {
    Ability,
    ApplyDamage,
    AbilityGraph,
}
```

建议优先使用 `export declare ...`，因为它让声明和导出关系在同一处表达，利于 AI 修改。

### 6.2 Declare

```ebnf
DeclStmt ::= "declare" TypeDecl
           | "declare" EnumDecl
           | "declare" ObjectDecl
           | "declare" ScopeKindDecl
           | "declare" CommandDecl
           | "declare" SchemaDecl
           | "declare" LintDecl
           | "declare" ModuleDecl
```

### 6.3 Module 声明

`declare module` 是 `.d.gs` 的可选元信息声明，用于导出表、依赖图、包管理和版本诊断。它不创建 runtime module，也不改变文件内名字解析规则。

```ebnf
ModuleDecl ::= "module" StringLiteral "{" ModuleField* "}"
ModuleField ::= Identifier ":" Literal ";"
```

示例：

```ts
declare module "ability.core" {
    package: "Game.Ability";
    version: "1.0.0";
    owner: "Gameplay";
}
```

规则：

- 一个 `.d.gs` 最多声明一个 canonical module id。
- module id 推荐使用稳定字符串，不从文件路径隐式推导。
- `package`、`version`、`owner` 等字段是普通 metadata，具体含义由 package/build 系统解释。
- 没有 `declare module` 时，工具可以把文件路径作为 fallback module id，但应在导出表中标记为 inferred。
- `declare module` 只能出现在 `.d.gs`，`.gs` 资产文件不应声明 module。

### 6.4 类型声明

```ebnf
TypeDecl ::= "type" Identifier TypeBase? DeclBody? ";"?
TypeBase ::= ":" TypeRef
DeclBody ::= "{" DeclMember* "}"
```

示例：

```ts
export declare type Ability;
export declare type Actor;
export declare type Vec2;
export declare type DamageResult;
```

### 6.5 Enum 声明

```ebnf
EnumDecl ::= "enum" Identifier "{" EnumMember* "}"
EnumMember ::= Identifier ("=" Literal)? ";"
```

示例：

```ts
export declare enum DamageType {
    Fire;
    Ice;
    Physical;
}
```

### 6.6 Object 声明

object declaration 描述可序列化对象类型、属性和 metadata。它不应在基础语法层内建 `pin` 概念。

```ebnf
ObjectDecl ::= "object" Identifier TypeBase? "{" ObjectDeclMember* "}"
ObjectDeclMember ::= FieldDecl
                   | DirectiveStmt
```

基础字段：

```ebnf
FieldDecl ::= AttributeList? Identifier ":" TypeRef DefaultValue? ";"
```

示例：

```ts
export declare object DamageEffect {
    amount: float;
    type: DamageType;
}
```

FlowGraph、Table、Editor 等领域语义应通过 attribute/meta 标注字段，而不是让 parser 或通用 object declaration 内建专用字段语法。

FlowGraph 字段示例：

```ts
export declare object ApplyDamage {
    @flow.input
    target: Actor;

    @flow.input
    amount: float = 0.0;

    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;

    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;

    @flow.pin(kind = "data", direction = "out")
    result: DamageResult;

    @editor.field
    pos: Vec2;
}
```

说明：

- `target` 和 `amount` 是普通字段；FlowGraph projection 根据 `@flow.input` 把它们解释为输入 pin 或节点参数。
- `enter` 和 `exit` 使用 `Exec` marker type，再由 `@flow.pin(...)` 标注方向。
- `result` 是普通 typed field，再由 `@flow.pin(...)` 标注为 data output。
- `pos` 是普通字段，再由 `@editor.field` 标注为 editor-only metadata。
- parser 只需要识别 attribute 和 field，不需要知道 pin。

旧的 FlowGraph 专用声明形式可以作为兼容/语法糖由 declaration binder 解释，但不推荐作为基础语法：

```ebnf
PortDecl ::= ("input" | "output") Identifier ":" TypeRef DefaultValue? ";"
           | "exec" ("in" | "out") Identifier ";"
           | "data" ("in" | "out") Identifier ":" TypeRef DefaultValue? ";"
```

如果保留这类语法糖，也应在 Tree-sitter grammar 中作为通用 `DirectiveStmt` 或 declaration extension 处理，而不是进入核心 AST 概念。

### 6.7 Scope kind 声明

scope kind declaration 描述哪些 scope kind 存在，以及嵌套、允许内容、默认 lint/projection。

```ebnf
ScopeKindDecl ::= "scope" Identifier TypeBase? "{" ScopeDeclMember* "}"
ScopeDeclMember ::= DirectiveStmt
                  | PropertyDecl
```

示例：

```ts
export declare scope asset {
    allows scope graph;
    allows scope table;
    allows object any;
}

export declare scope graph: FlowGraphScope {
    allows object any;
    allows scope entry;
    allows command connect;
}

export declare scope row {
    allows property any;
}
```

### 6.8 Command 声明

command declaration 描述受限 call 的签名。

```ebnf
CommandDecl ::= "command" Identifier "(" ParamDeclList? ")" ReturnType? ";"
ReturnType ::= ":" TypeRef
```

示例：

```ts
export declare command connect(from: PinRef, to: PinRef): EdgeRef;
export declare command bind(target: PropertyRef, source: ValueRef): BindingRef;
```

`.connect(...)` 这种 member call 可以通过 binder 绑定到 command：

```ts
apply.exit.connect(log.enter);
```

等价语义：

```text
connect(from = apply.exit, to = log.enter)
```

### 6.9 Schema 声明

schema declaration 描述领域规则、默认 projection 和 lint。

```ebnf
SchemaDecl ::= "schema" Identifier TypeBase? "{" SchemaMember* "}"
SchemaMember ::= PropertyDecl
               | DirectiveStmt
```

示例：

```ts
export declare schema AbilityGraph: FlowGraphSchema {
    max_exec_fan_out: 1;
    allow_exec_fan_in: false;
    allowed_objects: [ApplyDamage, PrintString, Delay];
    required_entries: [Start];
    lint: AbilityGraphLint;
}
```

### 6.10 Lint 声明

lint declaration 用于声明某个 lint provider 的符号，让 project/module 能依赖它。

```ebnf
LintDecl ::= "lint" Identifier "for" TypeRef ";"
```

示例：

```ts
export declare lint AbilityGraphLint for AbilityGraph;
export declare lint HTNLint for HTNGraph;
```

具体 lint 实现通常在宿主 C++/插件中注册，`.d.gs` 只暴露符号和适用范围。

---

## 7. 依赖分析

`.d.gs` 的一个核心用途是静态依赖分析。依赖分析不执行代码，只读取 `import`、`export`、`declare module`、声明体中的 `TypeRef`、schema/command/lint 引用，以及 `.gs` 中的资产 `ref`。

最低产物：

```text
ModuleGraph
  ModuleRecord[]
  ImportEdge[]
  SymbolEdge[]
  AssetRefEdge[]
  Diagnostics[]

ExportTable
  module_id
  source_file
  exported_symbols[]

SymbolResolution
  local_symbols
  imported_symbols
  unresolved_symbols
  ambiguous_symbols
```

这些产物供 binder、autocomplete、lint、projection、build system、asset cooker 和 AI 修复共同使用。

### 7.1 文件依赖

直接依赖来自 import：

```ts
import "core.d.gs";
import "ability_core.d.gs";
```

语义规则：

- `import` 是静态依赖声明，不执行被导入文件。
- import path 推荐使用显式文件路径或虚拟包路径，解析规则由 `ImportResolver` 提供。
- `.gs` 可以 import `.d.gs` 或其他 `.gs`；`.d.gs` 不应依赖 `.gs` 实例文件。
- import cycle 允许被诊断和降级处理，但不应阻止 parser 产出 CST。

建议结果：

```cpp
struct ImportEdge {
    ModuleId from_module;
    ImportPath path;
    std::optional<ModuleId> resolved_module;
    SourceRange source_range;
    ImportStatus status; // resolved | unresolved | cyclic | forbidden
};
```

### 7.2 符号依赖

声明和资产中的 TypeRef、RefExpr、Command、Schema 都会形成符号依赖：

```ts
export declare object ApplyDamage {
    @flow.input
    target: Actor;

    @flow.input
    amount: float;
}
```

依赖：

```text
ApplyDamage -> Actor
ApplyDamage -> float
```

建议结果：

```cpp
struct SymbolEdge {
    SymbolId from_symbol;
    QualifiedName referenced_name;
    std::optional<SymbolId> resolved_symbol;
    SourceRange reference_range;
    SymbolDependencyKind kind; // type | base_type | field_type | command | schema | lint | attribute
};
```

符号解析规则：

- 先解析当前文件 local declarations。
- 再解析直接 import 的 exported symbols。
- 如果多个 import 导出同名符号，必须产生 ambiguous diagnostic，不能任意选择。
- 未解析符号以 `UnresolvedSymbol` 保留，允许继续 partial semantic/projection。
- AI quick fix 可以基于候选 module 生成 `import` patch。

### 7.3 ModuleRecord

每个 `.d.gs` 应生成一个 `ModuleRecord`。

```cpp
struct ModuleRecord {
    ModuleId module_id;
    std::string source_file;
    bool module_id_inferred;
    std::optional<std::string> package;
    std::optional<std::string> version;
    std::vector<ImportEdge> imports;
    ExportTable exports;
};
```

示例：

```ts
declare module "ability.core" {
    package: "Game.Ability";
    version: "1.0.0";
}
```

导出为：

```text
module_id: ability.core
package: Game.Ability
version: 1.0.0
module_id_inferred: false
```

没有 `declare module` 时：

```text
module_id: path://ability_core.d.gs
module_id_inferred: true
```

### 7.4 资产依赖

`.gs` 中的 `ref` 表达式形成资产依赖：

```ts
icon: ref "/Game/UI/Icons/Fireball";
```

依赖：

```text
current asset -> /Game/UI/Icons/Fireball
```

建议结果：

```cpp
struct AssetRefEdge {
    AssetId from_asset;
    AssetPath referenced_asset;
    SourceRange reference_range;
    AssetRefKind kind; // hard | soft | editor_only
};
```

`AssetRefKind` 可由 schema、attribute 或 host resolver 决定。基础 parser 只识别 `ref` 表达式，不理解资产系统。

### 7.5 导出表

每个 `.d.gs` 应能被分析成导出表：

```text
module ability.core
exports:
  type Ability
  enum DamageType
  object DamageEffect
  object ApplyDamage
  scope graph
  schema AbilityGraph
  command connect
  lint AbilityGraphLint
```

这张表供 binder、autocomplete、lint、projection 和构建系统使用。

建议结构：

```cpp
struct ExportedSymbol {
    SymbolId symbol_id;
    SymbolKind kind;
    std::string name;
    SourceRange declaration_range;
    SourceRange name_range;
    bool is_deprecated;
};

struct ExportTable {
    ModuleId module_id;
    std::string source_file;
    std::vector<ExportedSymbol> symbols;
};
```

导出规则：

- `export declare ...` 同时声明并导出符号。
- `export { A, B }` 只导出当前文件已声明或已 re-export 的符号。
- `declare module` 不需要 `export`，它描述当前声明文件自身。
- `.gs` 默认不导出类型符号；资产是否可被其他文件引用由 asset id、package/build 系统和 `@id(...)` 决定。

### 7.6 ModuleGraph 诊断

依赖分析至少产生这些诊断：

```text
GS-REF-001 unresolved import
GS-REF-002 cyclic declaration import
GS-REF-003 import from instance file in .d.gs
GS-REF-004 unresolved symbol
GS-REF-005 ambiguous symbol
GS-REF-006 duplicate export
GS-REF-007 module id mismatch
GS-REF-008 incompatible module version
```

诊断必须包含：

- import/source reference range。
- 期望符号或 module id。
- 候选 module/symbol 列表。
- 可选 quick fix，例如 add import、rename symbol、qualify symbol、remove duplicate export。

### 7.7 Source Patch 要求

依赖相关编辑必须走 AST/Text Framework：

| 操作 | patch 目标 |
| --- | --- |
| add import | import 列表末尾或首个顶层 item 前 |
| remove unused import | 对应 `import_declaration` range |
| organize imports | 只重排 import block，不移动其他 item |
| add export | 声明前加 `export`，或更新 `export { ... }` 列表 |
| rename exported symbol | declaration name range，并更新同文件引用 |
| add declare module | `.d.gs` 的 import block 后、第一个 declaration 前 |

AI 修复不应凭字符串搜索插入 import。它应消费 `ModuleGraph` 和 `ExportTable`，再通过 `RewriteBuilder` 生成 `TextPatch`。

---

## 8. Scope

scope 是最重要的结构边界。所有领域结构都应先表达成通用 scope。

```ebnf
ScopeDecl ::= AttributeList?
              "scope" ScopeKind Identifier TypeAnnotation? ParamList?
              "{" Item* "}"

ScopeKind  ::= Identifier
```

示例：

```ts
scope asset Fireball: Ability {
}

scope graph Execute: AbilityGraph {
}

scope entry Start {
}

scope table Tuning: DataTable<AbilityLevel> {
}

scope row Level1 {
}
```

`asset`、`graph`、`entry`、`table`、`row` 的含义由 semantic/domain 层解释。

### 8.1 Scope 类型标注

```ebnf
TypeAnnotation ::= ":" TypeRef
```

示例：

```ts
scope asset Fireball: Ability {
}

scope graph Execute: AbilityGraph {
}
```

### 8.2 Scope 参数

scope 可以带参数，主要用于 entry/event/function-like scope。

```ebnf
ParamList ::= "(" ParamDeclList? ")"
ParamDeclList ::= ParamDecl ("," ParamDecl)* ","?
ParamDecl ::= Identifier ":" TypeRef DefaultValue?
DefaultValue ::= "=" Expr
```

示例：

```ts
scope entry OnDamage(amount: float, instigator: Actor) {
}
```

---

## 9. 类型引用

```ebnf
TypeRef ::= QualifiedName TypeArgs?
TypeArgs ::= "<" TypeRef ("," TypeRef)* ","? ">"
QualifiedName ::= Identifier ("." Identifier)*
```

示例：

```ts
Ability
AbilityGraph
DataTable<AbilityLevel>
Game.Actor
Map<string, float>
```

类型是否存在、泛型参数是否合法，不由 parser 判断，由 binder 判断。

---

## 10. 对象声明

对象声明用于创建当前 scope 内的类型化序列化对象。

```ebnf
ConstDecl ::= AttributeList?
              "const" Identifier
              "=" ObjectExpr ";"?
```

示例：

```ts
const damage = new DamageEffect {
    amount: 50;
    type: DamageType.Fire;
}

@id("01J2FIREBALLAPPLY")
const apply = new ApplyDamage {
    effect: damage;
    editor.pos: [100, 100];
}
```

说明：

- `const` 名称是 source alias。
- `@id(...)` 是稳定身份，推荐作为声明前置 attribute。
- `new Type { ... }` 是序列化对象构建，不是运行时 constructor execution。
- 允许省略对象声明结尾分号，formatter 可统一输出。

---

## 11. 对象表达式

```ebnf
ObjectExpr ::= "new" TypeRef ObjectBody
ObjectBody ::= "{" ObjectMember* "}"
ObjectMember ::= PropertyDecl
               | CallStmt
               | AssignStmt
               | DirectiveStmt
               | EmptyStmt
```

示例：

```ts
new PrintString {
    message: "done";
    editor.pos: [360, 100];
}
```

这是 TS-flavored 但不是合法 TypeScript。它借鉴 TypeScript/C# 的对象构建心智，但 `new Type { ... }` 在 GraphScript 中表示静态资产对象构建。

未来也可以考虑支持更接近 TS 的构造形式作为语法糖：

```ts
const log = new PrintString({
    message: "done",
    editor: {
        pos: [360, 100],
    },
});
```

但初始草案优先使用 `new Type { ... }`，因为它更容易做字段级 source patch，并且与声明文件中的 object body 风格一致。

对象体内允许属性，也允许受限 command。具体允许哪些成员由 schema/binder 决定。

---

## 12. 属性声明

```ebnf
PropertyDecl ::= AttributeList? PropertyPath ":" Expr ";"
PropertyPath ::= Identifier ("." Identifier)*
```

示例：

```ts
amount: 50;
type: DamageType.Fire;
target: context.target;
editor.pos: [100, 100];
```

属性语义由当前对象类型或当前 scope schema 决定。

重要限制：

- 属性值是静态表达式。
- 不允许任意函数调用作为基础可逆子集的属性值。
- 未知属性应尽可能保留，并由 lint 报告。

---

## 13. 表达式

基础表达式：

```ebnf
Expr ::= Literal
       | RefExpr
       | AssetRefExpr
       | ArrayExpr
       | InlineObjectExpr
       | MissingExpr

RefExpr ::= QualifiedName
AssetRefExpr ::= "ref" StringLiteral
ArrayExpr ::= "[" (Expr ("," Expr)* ","?)? "]"
InlineObjectExpr ::= "{" PropertyDecl* "}"
```

示例：

```ts
50
"done"
DamageType.Fire
damage
context.target
ref "/Game/Abilities/Fireball"
[100, 200]
{ x: 100; y: 200; }
```

### 13.1 引用

引用可以指向：

- 当前 scope 内对象 alias
- scope 参数
- context 成员
- enum value
- 外部 schema/type 中的符号

引用是否有效由 binder 判断。

### 13.2 外部资产引用

```ts
icon: ref "/Game/UI/Icons/Fireball";
montage: ref "/Game/Anim/Cast_Fireball";
```

`ref` 表示外部资产引用，具体 hard/soft/reference policy 由 engine adapter 或 schema 决定。

---

## 14. Command Call

call 语句用于表达受限 command。

```ebnf
CallStmt ::= CallExpr ";"
CallExpr ::= MemberExpr "(" ArgList? ")"
MemberExpr ::= RefExpr ("." Identifier)+
ArgList ::= Expr ("," Expr)* ","?
```

示例：

```ts
context.start.connect(apply.enter);
apply.exit.connect(log.enter);
apply.result.connect(log.message);
```

在基础可逆子集中，call 不是普通函数调用，而是 command。

例如：

```ts
apply.exit.connect(log.enter);
```

可以 lower 为：

```text
CallCommand kind=connect from=apply.exit to=log.enter
```

允许的 command 名称由当前 scope/domain 注册。基础语法只负责记录 call 结构。

---

## 15. Assignment

assignment 可以用于 patch 后的局部更新，也可以作为可选 source 语法。

```ebnf
AssignStmt ::= RefExpr "=" Expr ";"
```

示例：

```ts
apply.amount = 75;
apply.editor.pos = [120, 240];
```

是否允许 assignment 出现在某个 scope 或 object 内，由 binder/domain 决定。

对于可逆图编辑，推荐 formatter 把稳定默认值写回对象属性，而不是到处生成 assignment。

---

## 16. Directive

directive 用于表达小型声明型命令，例如 graph input/output。

```ebnf
DirectiveStmt ::= Identifier DirectiveBody ";"
DirectiveBody ::= DirectiveParamDecl
                | ArgList
                | /* empty */

DirectiveParamDecl ::= Identifier ":" TypeRef DefaultValue?
```

示例：

```ts
input target: Actor;
input amount: float = 10.0;
output result: bool;
```

parser 可把它记录成通用 directive。`input`、`output` 的含义由 graph projection 解释。

开放问题：未来是否把 directive 统一改成 command call，例如：

```ts
input("target", Actor);
```

当前草案保留 directive，因为它更适合人类和 AI 阅读。

---

## 17. Attribute

attribute 用于附加稳定 id、编辑器提示或 domain metadata。

```ebnf
AttributeList ::= Attribute+
Attribute ::= "@" QualifiedName ("(" AttributeArgList? ")")?
AttributeArgList ::= AttributeArg ("," AttributeArg)* ","?
AttributeArg ::= Identifier "=" Expr
               | Expr
```

示例：

```ts
@id("01J2...")
const damage = new DamageEffect {
    amount: 50;
}

@deprecated("Use ApplyDamageV2")
const apply = new ApplyDamage {
}

@flow.pin(kind = "exec", direction = "in")
enter: Exec;
```

attribute 应被 CST 保留，并由 semantic/domain 层解释。

Attribute 是基础语法的统一 metadata 机制。领域模块应优先通过 attribute 标注通用语法节点，而不是引入 parser 专用语法。

典型用途：

```text
@id(...)               稳定身份
@for(...)              当前 scope/object fragment 描述的目标对象
@fragment(...)         fragment 名称或用途
@editor.field          editor-only 字段
@flow.input            FlowGraph 输入字段
@flow.pin(...)         FlowGraph pin 描述
@table.key             Table row key
@deprecated(...)       弃用提示
```

---

## 18. 多 Scope 描述同一对象

可以允许不同 scope 描述同一个对象，但必须通过稳定身份显式关联，不能依赖同名合并。

推荐模型：

```text
Object = Fragment*
Fragment = 某个 source range 内对目标 object 的一组属性、命令或领域数据描述
```

示例：

```ts
@id("ability.fireball")
scope asset Fireball: Ability {
    name: "Fireball";
    icon: ref "/Game/UI/Icons/Fireball";
}

@for("ability.fireball")
scope tuning FireballBalance: AbilityTuning {
    damage: 50;
    cooldown: 3.0;
}

@for("ability.fireball")
scope graph FireballExecute: AbilityGraph {
    const apply = new ApplyDamage {
        amount: damage;
        editor.pos: [100, 100];
    }

    scope entry Start {
        context.start.connect(apply.enter);
    }
}
```

这里三个 scope 都参与描述同一个逻辑对象 `ability.fireball`，但它们的职责不同：

- `scope asset` 描述资产主体。
- `scope tuning` 描述平衡参数 fragment。
- `scope graph` 描述执行图 fragment。

绑定层应把它们聚合成同一个 logical object：

```text
LogicalObject ability.fireball : Ability
  fragments:
    asset Fireball
    tuning FireballBalance
    graph FireballExecute
```

### 18.1 合并规则

基础规则：

- `@id(...)` 创建或声明稳定对象身份。
- `@for(...)` 表示当前 scope/object fragment 贡献给已有目标对象。
- 未写 `@for(...)` 的 scope 默认描述自己。
- 同名 scope 不自动合并。
- 同一属性在多个 fragment 中重复定义时，应由 schema 决定是允许合并、覆盖还是报错。
- 默认策略应是报 conflict diagnostic，而不是 last-writer-wins。

示例冲突：

```ts
@for("ability.fireball")
scope tuning A: AbilityTuning {
    damage: 50;
}

@for("ability.fireball")
scope tuning B: AbilityTuning {
    damage: 60;
}
```

默认应诊断：

```text
conflict: property ability.fireball.damage defined by two fragments
```

### 18.2 Source Binding

每个 fragment 和每个属性都必须保留 source binding。

```text
logical object -> fragment list
fragment -> source scope range
property contribution -> source property range
graph edge contribution -> source connect call range
```

这样属性面板修改 `damage` 时，系统可以知道应该 patch 哪个 fragment，而不是重写整个 logical object。

### 18.3 适用场景

这种机制适合：

- 一个资产由多个领域视图共同描述。
- 表格参数和图行为共同组成一个 ability。
- editor metadata 与 runtime data 分开存放。
- 插件对已有对象做扩展 fragment。
- 同一个对象在不同文件中被分段描述。

跨文件 fragment 需要 import 和依赖分析支持：

```ts
import "Fireball.gs";

@for("ability.fireball")
scope tuning FireballBalance: AbilityTuning {
    damage: 50;
}
```

---

## 19. 可逆作者子集

为了保证文本和可视化编辑互转，基础可逆子集必须受限。

允许：

```text
scope
const = new Type { ... }
property: static value
ref "asset/path"
array literal
inline object literal
qualified reference
directive
restricted command call
assignment
attribute
comment
```

不允许进入基础可逆子集：

```text
if
for
while
lambda
user function
spread
computed property
dynamic type selection
arbitrary function call in expression
operator expression such as a + b
import-time execution
```

这些能力未来可以作为非可逆宏层或生成层存在，但不能破坏基础资产文件的图/文本双向编辑能力。

---

## 20. 错误恢复语法

parser 应尽可能插入 missing node/token，而不是直接失败。

示例输入：

```ts
const apply = new ApplyDamage {
    amount: 50
    target:
}

context.start.connect(apply.enter
```

恢复结果应包含：

```text
Property amount = 50
  diagnostic: expected ';'

Property target = MissingExpr
  diagnostic: expected expression

Call context.start.connect(apply.enter)
  diagnostic: expected ')'
```

错误恢复原则：

- scope 不完整时尽量恢复 scope body。
- object body 不完整时尽量恢复已有 property。
- call 参数不完整时保留 partial call。
- 缺少表达式时生成 MissingExpr。
- 无法归类的 token 放入 ErrorAst/SkippedToken。

---

## 21. 领域映射示例

### 21.1 FlowGraph

源文本：

```ts
scope graph Execute: AbilityGraph {
    input target: Actor;

    const apply = new ApplyDamage {
        target: target;
        editor.pos: [100, 100];
    }

    scope entry Start {
        context.start.connect(apply.enter);
    }
}
```

Graph projection：

```text
Graph Execute : AbilityGraph
Input target : Actor
Node apply : ApplyDamage
Default/link apply.target = target
Editor position apply = [100, 100]
Entry Start
Edge context.start -> apply.enter
```

### 21.2 HTN

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

HTN projection/lint 负责解释 root、method、task、decorator、分解关系等语义。

### 21.3 表

```ts
scope table AbilityLevels: DataTable<AbilityLevel> {
    scope row Level1 {
        damage: 50;
        cooldown: 3.0;
    }

    scope row Level2 {
        damage: 75;
        cooldown: 2.5;
    }
}
```

Table projection：

```text
Table AbilityLevels : DataTable<AbilityLevel>
Row Level1
  damage = 50
  cooldown = 3.0
Row Level2
  damage = 75
  cooldown = 2.5
```

---

## 22. 最小完整示例

```ts
import "ue_core.asset";
import "ability_nodes.asset";

scope asset Fireball: Ability {
    @id("damage")
    const damage = new DamageEffect {
        amount: 50;
        type: DamageType.Fire;
    }

    scope graph Execute: AbilityGraph {
        input target: Actor;
        input amount: float = 50.0;

        @id("apply")
        const apply = new ApplyDamage {
            effect: damage;
            target: target;
            amount: amount;
            editor.pos: [100, 100];
        }

        @id("log")
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

---

## 23. 开放问题

- `@id(...)` 是否只允许作为声明前置 attribute，还是兼容旧的声明内后缀写法？
- 对象体属性采用 TypeScript 风格 `property: value` 是否作为最终决策？
- 是否允许 `new Type(...)` 形式，还是统一使用 `new Type { ... }`？
- directive 是否保留，还是统一成 command call？
- `editor.pos` 是否应改成 `editor { pos: [...] }`？
- scope kind 是否全部作为普通 identifier，还是为常见 kind 提供语法糖？
- object declaration 是否必须带 stable id？
- call expression 是否只允许作为 statement，不允许作为 value？
- 缺分号的 formatter 策略是什么？
- 和现有 `.gs` 语法如何共存：新扩展名、模式开关，还是逐步迁移？
- `.d.gs` 中 FlowGraph 的 `exec/data/input/output` 声明是否完全移除，还是只保留为兼容语法糖？
- `declare schema` 里的数组和类型引用如何区分符号引用与普通值？
- 多 fragment 描述同一对象时，属性冲突默认是否一律报错，还是允许 schema 指定覆盖策略？
