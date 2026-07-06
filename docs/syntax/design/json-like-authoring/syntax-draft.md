# JSON-like Authoring Syntax 草稿

> 状态：目标语法草稿。
>
> 目的：把 GraphScript 收敛为一种接近 JSON/TS 对象结构、但适合游戏关卡、
> 任务流程、对象声明和领域投影的手写 DSL。本文不是当前解析器的实现契约。

---

## 状态与范围

本文描述 `.gs` 和 `.d.gs` 的一条目标创作语法路线：

```text
JSON/TS-like object structure
+ dotted key
+ typed slot
+ command expression
+ assignment statement
+ annotation
```

它刻意不声称自己是严格 JSON5。JSON5 很适合数据结构，但对游戏流程资产有
两个明显缺口：

- command 不自然：`{ call: "patrol.start" }` 比 `patrol.start()` 噪声大。
- assignment / annotation 不自然：`patrol.route = route` 和
  `@comment("...")` 很难用纯 JSON5 友好表达。

本文因此采用 **JSON-like surface**：主体仍然是 object、array、scalar 和
path，但允许少量一等语法补足游戏创作体验。

本文只讨论语法形状，包括：

- asset 文件的表层结构。
- declaration 文件的表层结构。
- object、event、flow、callback、steps 的写法。
- 多入口、多出口的可调用对象如何表达为对象方法、回调和变量。
- assignment、command、annotation 的标准形式。
- 降级到结构化 IR / DocumentNode / 领域投影 fact 的方式。

不在本文范围内：

- 解析器实现。
- 当前 `.gs` parser 的迁移计划。
- runtime bake IR 的最终格式。
- Web editor JSON API 合约。
- 各游戏领域 schema 的完整语义。
- 资产热更、打包、Cook 或运行时加载策略。

本文约定：

- `.d.gs` 主要承载 declaration、schema、object type、domain type。
- `.gs` 主要承载具体 asset 实例，例如 level、quest、dialogue、table。
- 同一套表层语法同时服务声明文件和资产文件，差异由顶层字段和 domain/schema
  解释。
- 当前实现文件可以继续使用旧语法；本文只描述目标 authoring syntax。

---

## Spec 对齐

稳定设计理念以这些文档为准，本文不重新定义：

- `docs/spec/index.md`
- `docs/spec/architecture.md`
- `docs/spec/graph-domain.md`
- `docs/spec/development-guide.md`

本文需要对齐的稳定原则：

- 文本仍是 canonical source。
- Graph、任务、对话、表格等都是序列化文档之上的领域投影。
- 视觉编辑必须能回写到 source-bound document operation。
- 语法草稿不能暗示当前解析器已经实现该语法。

---

## 语法设计思路与方向

### 为什么不是严格 JSON5

严格 JSON5 中，所有对象成员都必须是 `key: value`。这会让流程写法变成：

```json5
steps: [
  { call: "patrol.start" },
  { set: "patrol.route", value: "$route" },
  { annotation: "comment", args: ["start patrol"] },
]
```

这对策划手写、code review 和 AI 局部 patch 都不够直观。

本文选择保留 JSON/TS 的对象结构，但允许：

```ts
steps: [
    @comment("start patrol")
    patrol.route = route
    patrol.start()
]
```

### 为什么不采用 `event OnStart {}`

`event OnStart {}` 是 DSL 声明头，不是 JSON object 语义。它把 kind 和 name
放在同一 header 中：

```text
kind = event
name = OnStart
body = {}
```

本文改用 path key：

```ts
events.OnStart: {
    steps: [
        patrol.start()
    ]
}
```

它等价于 object path：

```ts
events: {
    OnStart: {
        steps: [...]
    }
}
```

这样既减少嵌套，又保持 `key: value` 的结构心智。

### 核心取舍

- 不使用 `->`。
- 不把 `connect(...)` / `bind(...)` 作为策划主语法。
- `steps` 只表达线性主干，不替代完整领域图或运行时图。
- 多入口、多出口的可调用实体在文本中表现为对象实例：方法、回调、输入、
  输出。
- JSON-like DSL 是结构底座；Graph/FlowGraph 只是后续 domain projection。

### 层级边界与术语约束

本文当前讨论的是 **DSL 表层结构** 和它对应的 **DocumentNode 文档模型**。
Graph/FlowGraph 是重要目标领域，但它们的术语不应倒灌为当前 DSL 层的基础
概念。

本文使用的当前层术语：

| 当前层术语 | 含义 |
| --- | --- |
| `DocumentNode` | DSL 解析后的无损文档节点。 |
| `Entry` | `path: value` 形式的文档成员。 |
| `ObjectInstance` | `name: Type { ... }` 表达的类型化对象实例。 |
| `ObjectType` | `.d.gs` 中声明的对象类型。 |
| `Command` | `callee(args)` 形式的行为或构造调用。 |
| `Step` | `steps` 数组中的 command、assignment、let 或 keyed step。 |
| `Callback` | 对象实例暴露的生命周期或事件出口。 |
| `Assignment` | `target = value` 形式的数据绑定、配置或状态写入。 |

Graph/FlowGraph 术语只能出现在投影说明中，例如“某个 `ObjectInstance` 可以被
FlowGraph 投影为 GraphNode”。本文不把 `GraphNode`、`Pin`、`Edge` 作为 DSL
grammar 的基础概念。

Node-first 概念仍然可用，但它指的是内部 DocumentNode model，而不是
GraphNode-first authoring syntax：

```text
DSL source
  -> Lossless DocumentNode
  -> Semantic facts / Domain facts
  -> Graph / Quest / Dialogue / Table projection
```

---

## 核心心智模型

文件是一棵 `DocumentNode` 风格的对象树。每个可定位项目都有：

```text
DocumentItem {
    path: Path
    value: Value | Block
    annotations: Annotation[]
    sourceRange: SourceRange
}
```

其中 path 可以由嵌套 object 得到，也可以由 dotted key 得到：

```ts
events.OnStart: { ... }
```

等价路径：

```text
["events", "OnStart"]
```

领域投影层再把这些文档事实解释成：

- level / quest / dialogue / table / graph 等 asset root。
- object instance。
- event / flow / callback。
- command step。
- assignment / data binding。
- annotation / editor metadata。

---

## 文件类型与入口结构

### `.d.gs` Declaration 文件

声明文件是普通 object document，canonical 顶层通常包含 `declarations`：

```ts
import: [
    "ue_core.d.gs"
]

declarations: {
    AActor: type
    FVector: type

    PatrolController: object {
        inputs: {
            leader: AActor
            route: PatrolRoute
        }

        outputs: {
            currentTarget: FVector
        }

        methods: {
            start: {
                exits: [completed, failed]
            }
            pause: {}
            resume: {}
        }

        callbacks: {
            onReachedTarget: {}
        }
    }
}
```

其中 `AActor: type` 是 kind marker entry，表示 `AActor` 是一个类型声明，而
不是一个变量名为 `AActor`、类型也叫 `type` 的 asset input。

### `.gs` Asset 文件

资产文件可以直接以 asset root 作为顶层 entry：

```ts
XibeiNpcPatrol: level {
    inputs: {
        patrolLeader: AActor
    }

    events.OnStart: {
        steps: [
            Print("start")
        ]
    }
}
```

如果一个文件需要包含多个 asset，可以并列多个 root entry。formatter 不应把
多个 asset 自动合并到同一个隐式 `assets` object，除非迁移工具明确要求。

---

## 语法总览

最小关卡流程：

```ts
XibeiNpcPatrol: level {
    inputs: {
        patrolLeader: AActor
        route: PatrolRoute
        waypointDelay: float = 2.0
    }

    objects: {
        patrol: PatrolController {
            leader = patrolLeader
            route = route
        }
    }

    events.OnStart: {
        steps: [
            ResetRuntimeState()
            patrol.start()
        ]
    }

    callbacks.patrol.onReachedTarget: {
        steps: [
            Wait(waypointDelay)
            patrol.moveNext()
        ]
    }
}
```

特点：

- `XibeiNpcPatrol: level {}` 是 asset root。
- `inputs` 是开放块，内部使用 typed slot。
- `objects` 是对象实例表。
- `events.OnStart` 是 dotted key，减少 `events: { OnStart: ... }` 嵌套。
- `steps` 是线性流程糖。
- `patrol.start()` 是 command。
- `patrol.onReachedTarget` 是对象 callback path。

---

## 完整复杂配置示例

```ts
import: [
    "ue_core.d.gs"
    "patrol_nodes.d.gs"
]

declarations: {
    AActor: type
    FVector: type
    PatrolRoute: type
    PatrolState: type
    Exec: type

    PatrolController: object {
        inputs: {
            leader: AActor
            route: PatrolRoute
            moveSpeed: float = 300
        }

        outputs: {
            currentTarget: FVector
            state: PatrolState
        }

        methods: {
            reset: {}
            configure: {}
            start: {
                exits: [completed, failed]
            }
            pause: {}
            resume: {}
            moveNext: {}
            clearTarget: {}
        }

        callbacks: {
            onReachedTarget: {}
            onNoNextPoint: {}
            onAlert: {}
            onFinished: {}
        }

        defaultCall = start
        category = "Level/Patrol"
    }

    Wait: action {
        inputs: {
            duration: float = 0.2
        }

        methods: {
            start: {
                exits: [completed]
            }
        }

        defaultCall = start
        defaultExit = completed
    }

    GetActorLocation: pure {
        inputs: {
            actor: AActor
        }

        outputs: {
            location: FVector
        }
    }
}

XibeiNpcPatrol: level {
    inputs: {
        patrolLeader: AActor
        route: PatrolRoute
        waypointDelay: float = 2.0
        alertRegroupDelay: float = 3.0
    }

    objects: {
        @comment("巡逻队主控制对象")
        patrol: PatrolController {
            leader = patrolLeader
            route = route
            moveSpeed = 300
        }

        streaming: StreamingController {
            area = "PatrolArea"
        }
    }

    flows.CreatePatrolTeam: {
        inputs: {
            route: PatrolRoute
            leader: AActor = patrolLeader
        }

        steps: [
            let anchor = GetActorLocation(leader)

            parallel: [
                [
                    SpawnMonsters({
                        route = route
                        anchor = anchor.location
                    })
                ]
                [
                    SpawnNpcs({
                        route = route
                        anchor = anchor.location
                    })
                ]
                [
                    SpawnCart({
                        route = route
                    })
                ]
            ]

            WaitAllSpawned()
            BindTeam()
            ConfigureFormation()
        ]
    }

    events.OnStart: {
        steps: [
            @comment("BeginPlay 初始化状态和 streaming")
            ResetRuntimeState()

            streaming.load("PatrolArea") {
                completed: [
                    CreatePatrolTeam({
                        route = route
                        leader = patrolLeader
                    })
                    patrol.configure()
                    patrol.start()
                ]

                failed: [
                    Print("PatrolArea streaming failed")
                    Wait(1.0)
                    streaming.retry()
                ]
            }
        ]
    }

    callbacks.patrol.onReachedTarget: {
        steps: [
            if: RouteHasNext(route) {
                then: [
                    AdvanceRoute(route)
                    Wait(waypointDelay)
                    patrol.moveNext()
                ]

                else: [
                    patrol.clearTarget()
                    ResetRouteCursor(route)
                ]
            }
        ]
    }

    callbacks.patrol.onAlert: {
        steps: [
            patrol.pause()
            Wait(alertRegroupDelay)
            RegroupTeam()
            patrol.resume()
        ]
    }

    events.OnEndPlay: {
        steps: [
            StopTimers()
            DestroySpawnedEntities()
            ClearReferences()
        ]
    }
}
```

---

## Grammar 形状

非正式 grammar：

```text
File             := FileItem*
FileItem         := Import | Entry | Annotation* Entry | Empty | Comment
Import           := "import" ":" ArrayLiteral

Entry            := Path ":" EntryValue
EntryValue       := TypedBlock
                  | TypedSlot
                  | Block
                  | Expr

Path             := Identifier ("." Identifier)*
TypeRef          := Path
TypedBlock       := TypeRef Block
TypedSlot        := TypeRef Default?
Default          := "=" Expr
Block            := "{" BlockItem* "}"

BlockItem        := Entry
                  | Annotation* Entry
                  | Assignment
                  | Command
                  | Let
                  | KeyedStep
                  | Annotation
                  | Empty
                  | Comment

StepsArray       := "[" Step* "]"
Step             := Annotation* (Command | Assignment | Let | KeyedStep)

Command          := Callee "(" Args? ")" ExitBlock?
Callee           := Path
Assignment       := Path "=" Expr
Let              := "let" Identifier "=" Expr
KeyedStep        := Path ":" KeyedStepPayload
KeyedStepPayload := Expr Block? | Block
ExitBlock        := "{" ExitCase* "}"
ExitCase         := Identifier ":" StepsArray

Annotation       := "@" Path AnnotationArgs?
AnnotationArgs   := "(" Args? ")"
Args             := PositionalArgs | NamedArgs
PositionalArgs   := Expr (","? Expr)*
NamedArgs        := NamedArg (","? NamedArg)*
NamedArg         := Identifier "=" Expr
Expr             := Value | Path | Command | ObjectLiteral | ArrayLiteral
Value            := String | Number | Boolean | Null | IdentifierLiteral
IdentifierLiteral := Identifier
ObjectLiteral    := "{" ObjectField* "}"
ObjectField      := Identifier "=" Expr | Identifier ":" Expr
ArrayLiteral     := "[" Expr* "]"
Comment          := LineComment | BlockComment
```

本文使用逗号宽松规则：列表和参数中的逗号可由 formatter 插入或省略。实现
时可以先选择更严格的 grammar，再由 formatter canonicalize。

上面的 grammar 刻意区分了三类 `{ ... }`：

- `Block` 是 document / asset / declaration 的结构块。
- `ObjectLiteral` 只出现在 expression 位置，例如 command 参数。
- `ExitBlock` 只跟在 `Command` 后面，表示多出口 command。

`KeyedStepPayload := Expr Block? | Block` 用来覆盖两类复合 step：

```ts
parallel: [
    [SpawnMonsters()]
    [SpawnNpcs()]
]

if: RouteHasNext(route) {
    then: [patrol.moveNext()]
    else: [patrol.clearTarget()]
}
```

其中 `parallel` 的 payload 是 `ArrayLiteral`，`if` 的 payload 是
`Expr + Block`。

### 词法、注释和分隔符

本文建议的 lexical 规则：

- 标识符使用 ASCII identifier 作为 canonical 输出：`[A-Za-z_][A-Za-z0-9_]*`。
- domain/schema 可以后续放宽到引号字符串，但 formatter 默认输出 bare
  identifier。
- path 用 `.` 连接 segment：`callbacks.patrol.onReachedTarget`。
- string 使用双引号作为 formatter 输出。
- number、boolean、null 沿用 JSON/TS 心智。
- 行注释 `// ...` 和块注释 `/* ... */` 允许出现在 entry/step 之间。
- 文档注释 `/// ...` 可以作为 annotation 的兼容输入，但 formatter 应优先
  输出结构化 `@comment("...")` 或保留原注释而不强制重写。

分隔符规则：

- block entry 之间不要求 `,` 或 `;`。
- array item 之间允许换行分隔；formatter 默认不输出逗号。
- call 参数之间 formatter 可以输出逗号，也可以采用换行宽松风格；实现初期
  可以先要求逗号，再由 formatter 统一。
- `:` 表示 document entry / keyed step。
- `=` 表示 assignment、default value、named argument 或 object literal field。
- declaration、asset block 和普通 object literal 的上下文不同：`name: Type`
  在 `inputs` 中是 typed slot，在 `declarations` 中可以是 kind marker，在
  expression object literal 中则不是 source-level entry。

---

## 每种语法的详细解释

### Import

```ts
import: [
    "ue_core.d.gs"
    "patrol_nodes.d.gs"
]
```

`import` 是普通 path entry，值是字符串数组。它比 `import "x";` 更接近
object document。formatter 应把 import 放在文件开头。

### Declaration Block

```ts
declarations: {
    PatrolRoute: type

    PatrolController: object {
        inputs: {
            route: PatrolRoute
        }
    }
}
```

`declarations` 是声明表。每个成员的 key 是声明名，value 是 kind marker 或
typed block。常见 kind 包括：

```ts
type
object
action
pure
event
struct
enum
```

这些 kind 是 schema/domain 可扩展的 identifier literal，不是字符串。若
kind 名与普通引用冲突，后续实现可以要求在 declaration 位置按 kind 解析。

### Asset Root

```ts
XibeiNpcPatrol: level {
}
```

含义：

- key `XibeiNpcPatrol` 是 asset name。
- value type/kind `level` 是 asset domain。
- body 是 asset document。

如果某个领域需要图式资产，可以写成：

```ts
HelloWorld: graph {
}
```

如果需要 schema：

```ts
HelloWorld: LevelScriptGraph {
    domain = graph
}
```

开放问题：schema 是写在 typed block header，还是写成 block 内属性。

### Dotted Key

```ts
events.OnStart: {
}

callbacks.patrol.onReachedTarget: {
}
```

Dotted key 是本文的 canonical 扩展。它不符合严格 JSON5，但能减少嵌套并
保持 path 语义。

等价结构：

```ts
events: {
    OnStart: {}
}
```

formatter 可以保留 dotted key，尤其是 `events.*`、`flows.*`、
`callbacks.*` 这类常见 path。

### Inputs

```ts
inputs: {
    patrolLeader: AActor
    route: PatrolRoute
    waypointDelay: float = 2.0
}
```

`inputs` 是开放块。内部 entry 使用 typed slot：

```text
name: Type = default?
```

它降级为 graph/level parameter，或其他 domain 的输入字段。

### Objects

```ts
objects: {
    patrol: PatrolController {
        leader = patrolLeader
        route = route
    }
}
```

对象实例写成 `name: Type { ... }`。对象 body 中的 assignment 默认是配置
输入或 data binding。

### Object Type Declaration

多入口、多出口的可调用实体在当前 DSL 层表现为 object type：

```ts
PatrolController: object {
    inputs: {
        leader: AActor
        route: PatrolRoute
    }

    outputs: {
        currentTarget: FVector
    }

    methods: {
        start: {
            exits: [completed, failed]
        }
        pause: {}
        resume: {}
    }

    callbacks: {
        onReachedTarget: {}
        onAlert: {}
    }
}
```

当前层解释：

| 声明字段 | 当前 DSL / DocumentNode 解释 |
| --- | --- |
| `inputs` | 对象实例的可配置输入或数据依赖。 |
| `outputs` | 对象实例可暴露的数据输出。 |
| `methods` | 对象实例可调用的方法入口。 |
| `methods.*.exits` | 方法调用可能产生的命名出口。 |
| `callbacks` | 对象实例暴露的生命周期或事件出口。 |

FlowGraph 投影可以把这些字段解释为 data pins、exec pins、node callbacks
或 edges，但这些只是投影结果，不是当前 DSL grammar 的基础概念。

### Pure Function Declaration

```ts
GetActorLocation: pure {
    inputs: {
        actor: AActor
    }

    outputs: {
        location: FVector
    }
}
```

Pure declaration 不能独立作为 `steps` 中的 flow step，除非被 `let` 或其他
data expression 引用。领域投影可以把它解释为 pure node、query 或函数。

### Steps

```ts
steps: [
    ResetRuntimeState()
    Wait(1.0)
    patrol.start()
]
```

`steps` 是线性主干的语法糖，只能隐式连接具备默认继续出口的 step。

如果当前 step：

- 没有默认 exec output，且后面还有 step，应报错。
- 有多个 exec output，必须写 exit block 或声明 `defaultExit`。
- 是 callback 型输出，不应放在 `steps` 中，应写成 `callbacks.*` block。

### Command

```ts
patrol.start()
Wait(waypointDelay)
SpawnTeam({
    route = route
    leader = patrolLeader
})
```

Command 是一等 step。它降级为：

```text
CallStep {
    target: Path
    args: Expr[]
    namedArgs: Assignment[]
}
```

方法调用：

```ts
patrol.start()
```

映射到对象实例 `patrol` 的 method `start`。如果投影到 FlowGraph，该 method
可以再被解释为 exec input。

### Object Literal 与 Named Arguments

Command 参数可以使用 object literal 承载命名参数：

```ts
SpawnTeam({
    route = route
    leader = patrolLeader
})
```

该写法等价于 named arguments：

```ts
SpawnTeam(route = route, leader = patrolLeader)
```

在 command 参数和 object literal 内，formatter 应优先输出 `name = expr`，
因为这里表达的是“把参数/字段绑定为某个表达式”，不是 document tree 的
`entry: value`。如果 value 本身是纯数据对象，兼容输入可以接受
`name: expr`，但 canonical 输出仍建议使用 `=`，以便和 asset document entry
区分。

Object literal 不创建新的 source-level declaration。它只属于当前 command
或 expression。

### Multi-exit Command

```ts
streaming.load("PatrolArea") {
    completed: [
        patrol.start()
    ]

    failed: [
        streaming.retry()
    ]
}
```

Exit block 的 key 是 method exit 名。每个 exit case 的值是一个 steps 数组。
如果投影到 FlowGraph，这些 exit 可以再被解释为 exec output。

### Assignment

```ts
patrol.route = route
patrol.moveSpeed = 300
blackboard.target = patrol.currentTarget
```

Assignment 的解释由 target domain 决定：

| 形式 | 解释 |
| --- | --- |
| `object.input = value` | data binding 或配置输入 |
| `object.property = literal` | initializer / property set |
| `state.x = value` | runtime state write action |
| `blackboard.x = value` | blackboard write action |

不建议允许普通临时变量反复赋值。可逆 authoring 子集应优先使用 `let`。

### Let

```ts
let anchor = GetActorLocation(patrolLeader)
let target = patrol.currentTarget
```

`let` 命名一个 data expression 或 pure declaration 的输出。它不是一般脚本
变量；它主要用于把数据依赖命名，便于后续绑定和领域投影显示。

多输出 pure declaration：

```ts
let anchor = GetActorTransform(patrolLeader)

SpawnTeam({
    location = anchor.location
    rotation = anchor.rotation
})
```

### Branch

```ts
if: RouteHasNext(route) {
    then: [
        AdvanceRoute(route)
        patrol.moveNext()
    ]

    else: [
        patrol.clearTarget()
        ResetRouteCursor(route)
    ]
}
```

`if` 是 keyed step，不是 general-purpose language statement。它降级为
领域控制结构；Graph/FlowGraph 投影可以再把它解释为 branch node。

### Parallel

```ts
parallel: [
    [
        SpawnMonsters({ route = route })
    ]
    [
        SpawnNpcs({ route = route })
    ]
    [
        SpawnCart({ route = route })
    ]
]
```

`parallel` 是复合 step。每个子数组是一条子 sequence。具体 join 语义由
domain/schema 决定，例如 all-complete join、race、或 fire-and-forget。

### Flow

```ts
flows.CreatePatrolTeam: {
    inputs: {
        route: PatrolRoute
    }

    steps: [
        SpawnTeam({ route = route })
        BindTeam()
    ]
}
```

`flow` 是命名可复用 steps。Inline `steps` 可以被编译为子图，但只有命名
`flows.*` 才是可复用 API。

调用：

```ts
CreatePatrolTeam({ route = route })
```

Flow 可以有输入、输出和本地对象，但它不自动成为事件。事件、callback 和
其他 flow 通过 command 调用它。

### Callback

```ts
callbacks.patrol.onReachedTarget: {
    steps: [
        Wait(waypointDelay)
        patrol.moveNext()
    ]
}
```

Callback 是对象生命周期出口。它不应伪装成当前 `steps` 的下一行，因为它
不是前一个 command 的默认流出。

### Annotation

```ts
@comment("巡逻控制器实例")
patrol: PatrolController {
}

steps: [
    @editor.position(x = 100, y = 200)
    patrol.start()
]
```

Annotation 可以附着到后续 entry 或 step。它降级为结构化 metadata：

```text
Annotation {
    name: Path
    args: Expr[]
    namedArgs: object
}
```

常见 annotation：

```ts
@comment("...")
@editor.position(x = 100, y = 200)
@id("stable-id")
@deprecated("use patrol.start")
```

### Reference

```ts
route
patrol.currentTarget
anchor.location
inputs.patrolLeader
```

Reference 使用 path 表达。字符串仍然是字符串，不自动解释为引用：

```ts
"PatrolArea"        // string literal
patrol.currentTarget // reference path
```

---

## Command 表达

本文 canonical command 是 call expression：

```ts
patrol.start()
SpawnTeam({ route = route })
```

不是 JSON object command：

```json5
{ call: "patrol.start" }
```

也不是低层连接命令：

```ts
connect(context.start, patrol.start)
bind(route, patrol.route)
```

低层 `connect` / `bind` 可以作为 debug/IR 或高级兼容输入，但不作为策划
canonical authoring syntax。

---

## 结构化等价形式

Surface syntax：

```ts
events.OnStart: {
    steps: [
        @comment("start patrol")
        patrol.route = route
        patrol.start() {
            completed: [
                Print("started")
            ]
            failed: [
                Print("failed")
            ]
        }
    ]
}
```

结构化等价：

```js
{
  path: ["events", "OnStart"],
  value: {
    steps: [
      {
        kind: "assignment",
        target: ["patrol", "route"],
        value: { ref: ["route"] },
        annotations: [
          { name: ["comment"], args: ["start patrol"] },
        ],
      },
      {
        kind: "call",
        target: ["patrol", "start"],
        exits: {
          completed: [
            { kind: "call", target: ["Print"], args: ["started"] },
          ],
          failed: [
            { kind: "call", target: ["Print"], args: ["failed"] },
          ],
        },
      },
    ],
  },
}
```

该结构是中立 IR / debug shape，不是 canonical authoring syntax。

---

## Formatter 与 Canonicalization 规则

Formatter 应输出：

```ts
AssetName: level {
    inputs: {
        name: Type = default
    }

    objects: {
        objectName: ObjectType {
            input = value
        }
    }

    events.EventName: {
        steps: [
            Command()
        ]
    }
}
```

Formatter 可以保留 dotted key：

```ts
events.OnStart: {}
flows.CreateTeam: {}
callbacks.patrol.onAlert: {}
```

Formatter 不应输出：

```ts
event OnStart {}
flow CreateTeam {}
connect(a, b)
bind(a, b)
a -> b
{ call: "patrol.start" }
```

这些可以作为迁移输入、debug IR 或其他草稿路线，但不是本文 canonical 输出。

Canonicalization 还应处理：

- `events: { OnStart: ... }` 可以保留，也可以折叠为 `events.OnStart: ...`。
- 一行 object literal 可以在参数较多时展开成多行。
- `/// comment` 可以保留原样；如果需要结构化 metadata，输出
  `@comment("...")`。
- 旧语法 `connect` / `bind` 在迁移时应 lowering 成 command、assignment、
  callback 或 explicit relation IR；formatter 不应直接把旧语法原样作为目标
  canonical 输出。

---

## 诊断与 Linter 边界

语法本身只规定可解析结构，领域 linter 负责检查投影语义。目标 linter 至少应
覆盖：

- import 解析失败：例如 `ue_core.d.gs` 找不到时，应在 import 字符串所在行
  标红。
- 未声明类型或对象：typed slot、typed block、command target 无法解析时，
  指向对应 token。
- `steps` 连续性错误：没有默认 flow output 的 step 后面又接了下一步。
- 多出口 command 缺少必要 exit case，或 exit case 名称不存在。
- callback 被写进普通 `steps`，例如把生命周期输出当作当前流程下一步。
- 重复语句：同一个 `steps` block 中出现完全相同的 command/assignment 且
  没有 annotation 或稳定 id 区分时，应报 duplicate diagnostic。
- 重复关系：同一 source 到同一 target 的重复绑定或重复投影关系应报错，而
  不是静默合并。

重复语句的删除不能只靠语义相等。编辑器和自动修复必须使用 `sourceRange`、
stable id 或同一 `steps` 数组中的 item index 来定位具体要删哪一条。也就是
说，linter 可以说“这三条相同”，但 quick fix 必须能指定“删除第 2 条”。

诊断输出建议包含：

```text
Diagnostic {
    code: "duplicate-step"
    severity: "error" | "warning" | "info"
    message: string
    sourceRange: SourceRange
    relatedRanges: SourceRange[]
    fix?: TextPatch
}
```

这只是目标 shape，不表示当前编辑器已经实现行内标红或 quick fix。

---

## 领域扩展性

本文语法不把所有游戏资产都塞进某一种 graph domain。`steps` 只是 flow-like
domain 的字段。其他领域可以用同一 object/path/value 结构表达自己的内容：

```ts
RescueNpc: quest {
    objectives: [
        ReachArea("Village")
        TalkTo("Npc_01")
        CollectItem("Medicine", count = 3)
        ReturnTo("Npc_01")
    ]

    rewards: {
        exp = 500
        items = ["Potion"]
    }
}
```

```ts
GuardGreeting: dialogue {
    entries.start: {
        text = "站住。"
        choices: [
            { text = "我只是路过", next = pass }
            { text = "我要进去", next = reject }
        ]
    }
}
```

```ts
MonsterSpawnTable: table {
    rows.goblin: {
        prefab = "/Game/Enemy/Goblin"
        hp = 100
        exp = 12
    }
}
```

核心语言只提供结构。领域语义由 `level`、`quest`、`dialogue`、`table`、
`ability` 等 domain/schema 解释。

---

## 与严格 JSON5 的关系

本文不是严格 JSON5。以下能力均不属于 JSON5：

- dotted key：`events.OnStart: {}`
- typed slot：`route: PatrolRoute`
- typed block：`patrol: PatrolController {}`
- command：`patrol.start()`
- assignment statement：`patrol.route = route`
- annotation：`@comment("...")`

但本文仍保留 JSON/JSON5 的核心心智：

- object block。
- array list。
- key/value entry。
- string/number/bool/null scalar。
- path 可降级为 object path。
- 注释和尾逗号可作为 grammar 允许项。

---

## 实现状态

当前仓库实现还不等于本文目标草稿。

当前已实现或已有样例覆盖：

- 当前 `.gs/.d.gs` parser。
- `graph` / `node` / `event` / `function` blocks。
- `connect(...)` / `bind(...)` 低层图命令。
- graph projection、diagnostics、source-backed patch 的部分能力。

本文目标但尚未实现：

- `AssetName: level {}` asset root。
- dotted key path。
- typed slot / typed block 的 JSON-like grammar。
- command expression 作为 canonical step。
- assignment step 到 data binding / runtime action 的 lowering。
- annotation 附着到任意 entry/step。
- object method/callback 声明模型。
- `steps` 的 linear sugar、multi-exit、parallel、branch lowering。
- formatter 输出本文 canonical syntax。

---

## 开放问题

- 顶层 asset root 应写 `AssetName: level {}`，还是 `level: AssetName {}`？
- schema/type 与 domain/kind 同时存在时，header 如何保持不歧义？
- dotted key 是否允许任意深度，还是只允许特定字段如 `events.*`？
- `steps` 中逗号是完全可选，还是 formatter 输出时统一省略？
- `parallel` 的 join 语义由语法指定，还是完全交给 schema？
- assignment 到 data input 与 runtime state write 如何在 declaration 中声明？
- `let` 是否允许解构，例如 `let { location } = GetActorTransform(actor)`？
- annotation 是否允许夹在 array item 之间并附着到下一 step？
- 低层 `connect` / `bind` 是否保留为高级模式，还是只出现在 IR/debug 中？
- 该语法应作为 `.gs` canonical source，还是先作为 `.gsx`/experimental 输入？
