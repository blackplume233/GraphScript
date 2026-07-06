# JSON-like Authoring Syntax 记录

> 日期：2026-07-06
>
> 状态：语法草稿决策记录。
>
> 对应草稿：[JSON-like Authoring Syntax 草稿](./syntax-draft.md)

---

## 背景

这次讨论的目标是为 GraphScript 选择一条更适合游戏关卡、任务流程和策划手写
的目标语法路线。现有低层图语法可以表达 `connect(...)`、`bind(...)`，但
直接暴露这些连接命令会让关卡流程难读，也会让视觉图和文本之间的语义落差
变大。

讨论中明确希望：

- 尽量降低文本语法复杂度。
- 不使用 `->` 作为流程连接语法。
- 大体保留 JSON 或 TypeScript 风格的 object、array、path 心智。
- 不把严格 JSON5 当作唯一目标，因为 command、assignment、annotation 在
  纯 JSON5 中会变得啰嗦。
- 支持多入口、多出口的可调用对象，但不要把策划作者暴露在某个领域投影的
  低层连接细节中。
- 后续语法不只服务 Graph/FlowGraph 投影，也要能扩展到 quest、dialogue、
  table、ability 等游戏资产。

---

## 本次结论

本次草稿选择 **JSON-like Authoring Syntax** 作为折中路线：

```text
JSON/TS-like object structure
+ dotted key
+ typed slot
+ command expression
+ assignment statement
+ annotation
```

核心判断：

- 主体仍是 `key: value`、object、array、scalar、path。
- `event OnStart {}` 这类双头 DSL header 不作为 canonical 写法。
- `Start: [...]`、`patrol.onReachedTarget: [...]` 这类 named entry 作为
  canonical 写法；是否表示事件或回调由 schema 决定。
- `patrol.start()` 作为 command，而不是 `{ call: "patrol.start" }`。
- `patrol.route = route` 作为 assignment，而不是 `{ set, value }`。
- `@comment("...")` 作为 annotation，而不是把注释全部塞进 meta object。
- 数组本身只是 `DocumentArray`；schema 可以把某些数组解释为 executable
  statement list。

本文记录的是 DSL 表层结构和 DocumentNode 结构，不把 Graph/FlowGraph 的
`node`、`pin`、`edge` 作为当前层基础术语。Graph 是目标投影之一，而不是
当前 DSL grammar 的语义层。

同理，`event`、`flow`、`steps`、`on`、`inputs`、`outputs`、`methods`、
`callbacks` 都不是 grammar 关键字。它们只是普通字段名，含义由 schema、
linter 和 projection 决定。

---

## 多入口多出口对象的映射

多入口、多出口的可调用实体不直接映射成某个领域投影的一堆裸连接点，而是
视为一个对象实例：

```ts
patrol: PatrolController {
    leader = patrolLeader
    route = route
}
```

声明侧可以用普通字段描述它；这些字段名由 schema 约定：

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
    }

    callbacks: {
        onReachedTarget: {}
    }
}
```

schema 语义映射示例：

| 文本形式 | schema 可解释为 |
| --- | --- |
| `inputs` | 对象实例的可配置输入或数据依赖 |
| `outputs` | 对象实例可暴露的数据输出 |
| `methods` | 对象实例可调用的方法入口 |
| `methods.*.exits` | 方法调用可能产生的命名出口 |
| `callbacks` | 对象实例暴露的生命周期或事件出口 |
| `object.input = value` | data binding / initializer |
| `object.method()` | exec call |

FlowGraph 可以把这些结构投影成 node、pin、edge，但这是投影层解释，不是当前
DSL 层的基础语法。

---

## 数组与 named entry 的定位

`steps` 不再作为语法层概念。可执行列表就是普通 array value；哪个 entry
的 array value 表示流程入口，由 schema 决定：

```ts
Start: [
    ResetRuntimeState()
    Wait(waypointDelay)
    patrol.start()
]
```

如果 schema 把某个 array 解释为 executable statement list，而 command
没有默认出口，后续又接了下一项，应由 linter 报错。如果 command 有多个
出口，需要写 trailing object：

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

callback-like entry 不应被塞进当前 array 的下一项，而应写成独立 named
entry：

```ts
patrol.onReachedTarget: [
    patrol.moveNext()
]
```

---

## 参数传递

参数传递保留两种 authoring 形状：

```ts
Wait(waypointDelay)
SpawnTeam(route = route, leader = patrolLeader)
```

复杂参数可以写成 object literal：

```ts
SpawnTeam({
    route = route
    leader = patrolLeader
})
```

`let` 用于命名数据表达式或 pure declaration 输出，不作为一般脚本变量系统：

```ts
let anchor = GetActorLocation(patrolLeader)

SpawnTeam({
    location = anchor.location
})
```

---

## 非目标

本次记录不把以下内容定为规范：

- 当前 parser 已支持该语法。
- 当前 formatter 已能输出该语法。
- 当前 graph editor 已能完整读写该语法。
- 低层 `connect(...)` / `bind(...)` 立即废弃。
- 所有游戏资产都必须通过 Graph/FlowGraph 表达。
- 这套语法必须严格兼容 JSON5。

这些内容需要后续 prototype、fixture 和迁移计划验证。

---

## Linter 约束记录

本次讨论中特别记录了一类领域 linter 问题：重复语句和重复关系不能只靠
serialization 解决。

例如：

```ts
Start: [
    patrol.start()
    patrol.start()
    patrol.start()
]
```

linter 应能识别重复 command，但删除时不能只说“删掉重复项”。必须依赖
`sourceRange`、stable id 或 array item index，明确删除哪一条。否则视觉图
和文本 source 的双向编辑会产生误删风险。

---

## 后续验证项

后续实现前至少需要用 fixture 或 prototype 验证：

- dotted key 与嵌套 object 的 source patch 是否稳定。
- typed slot / typed block 的 parser 恢复能力。
- command、assignment、annotation 的 formatter 输出规则。
- executable array 的默认出口检查。
- multi-exit command 到领域投影的 lowering。
- duplicate-step / duplicate-relation linter 的 source range 定位。
- `.d.gs` declaration 到 `.gs` asset 的跨文件引用诊断。
- 同一份资产在文本编辑和图编辑之间的增删改查是否可逆。

---

## 当前落点

本次记录对应的正式草稿文件是：

- [JSON-like Authoring Syntax 草稿](./syntax-draft.md)

语法路线对比文件是：

- [语法草稿对比](../syntax-draft-comparison.md)

这份记录只保存讨论和取舍背景。若某个决策进入实现指导阶段，应把提炼后的
稳定结论移动到 `../current/` 或 `docs/spec/` 中，而不是继续扩展本文。
