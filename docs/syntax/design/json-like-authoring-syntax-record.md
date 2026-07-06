# JSON-like Authoring Syntax 记录

> 日期：2026-07-06
>
> 状态：语法草稿决策记录。
>
> 对应草稿：[JSON-like Authoring Syntax 草稿](./json-like-authoring-syntax-draft.md)

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
- 支持多入多出 FlowGraph 节点，但不要把策划作者暴露在低层 pin 连接细节中。
- 后续语法不只服务 FlowGraph，也要能扩展到 quest、dialogue、table、ability
  等游戏资产。

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
- `events.OnStart: { ... }` 作为 dotted key 写法，等价于嵌套 object path。
- `patrol.start()` 作为 command，而不是 `{ call: "patrol.start" }`。
- `patrol.route = route` 作为 assignment，而不是 `{ set, value }`。
- `@comment("...")` 作为 annotation，而不是把注释全部塞进 meta object。
- `steps` 只表达线性主干语法糖，不替代完整 FlowGraph 语义。

---

## 多入多出节点的映射

多入多出的节点不直接映射成一堆裸 pin，而是视为一个对象实例：

```ts
patrol: PatrolController {
    leader = patrolLeader
    route = route
}
```

声明侧用 `inputs`、`outputs`、`methods`、`callbacks` 描述它：

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

语义映射：

| 文本形式 | 图语义 |
| --- | --- |
| `inputs` | data input pins / configurable fields |
| `outputs` | data output pins |
| `methods` | exec input pins |
| `methods.*.exits` | exec output pins |
| `callbacks` | lifecycle exec output pins |
| `object.input = value` | data binding / initializer |
| `object.method()` | exec call |

---

## `steps` 的定位

`steps` 不是通用脚本语言，也不是所有图结构的唯一表达。它只负责把“有默认
继续出口”的命令串成线性主干：

```ts
steps: [
    ResetRuntimeState()
    Wait(waypointDelay)
    patrol.start()
]
```

如果 command 没有默认出口，后续又接了下一步，应由 linter 报错。如果 command
有多个出口，需要写 exit block：

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

callback 不应被塞进当前 `steps` 的下一行，而应写成独立 block：

```ts
callbacks.patrol.onReachedTarget: {
    steps: [
        patrol.moveNext()
    ]
}
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

`let` 用于命名数据表达式或 pure node 输出，不作为一般脚本变量系统：

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
- 所有游戏资产都必须通过 FlowGraph 表达。
- 这套语法必须严格兼容 JSON5。

这些内容需要后续 prototype、fixture 和迁移计划验证。

---

## Linter 约束记录

本次讨论中特别记录了一类领域 linter 问题：重复语句和重复边不能只靠
serialization 解决。

例如：

```ts
steps: [
    patrol.start()
    patrol.start()
    patrol.start()
]
```

linter 应能识别重复 command，但删除时不能只说“删掉重复项”。必须依赖
`sourceRange`、stable id 或 steps item index，明确删除哪一条。否则视觉图
和文本 source 的双向编辑会产生误删风险。

---

## 后续验证项

后续实现前至少需要用 fixture 或 prototype 验证：

- dotted key 与嵌套 object 的 source patch 是否稳定。
- typed slot / typed block 的 parser 恢复能力。
- command、assignment、annotation 的 formatter 输出规则。
- `steps` 的默认出口检查。
- multi-exit command 到 graph projection 的 lowering。
- duplicate-step / duplicate-edge linter 的 source range 定位。
- `.d.gs` declaration 到 `.gs` asset 的跨文件引用诊断。
- 同一份资产在文本编辑和图编辑之间的增删改查是否可逆。

---

## 当前落点

本次记录对应的正式草稿文件是：

- [JSON-like Authoring Syntax 草稿](./json-like-authoring-syntax-draft.md)

语法路线对比文件是：

- [语法草稿对比](./syntax-draft-comparison.md)

这份记录只保存讨论和取舍背景。若某个决策进入实现指导阶段，应把提炼后的
稳定结论移动到 `../current/` 或 `docs/spec/` 中，而不是继续扩展本文。
