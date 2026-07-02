# 语法草稿对比

> 状态：草稿对比文档。
>
> 目的：并列比较当前两个主要目标语法方向，辅助后续选择 canonical syntax。

---

## 对比对象

当前主要候选：

1. [面向节点的类型化表层语法](./node-oriented-syntax-draft.md)
2. [JSON 超集 DocumentNode 语法](./json-superset-documentnode-draft.md)

两者都不重新定义稳定设计理念。稳定原则以 `docs/spec/` 为准。

---

## 总览

### 面向节点的类型化表层语法

```gs
Fireball: AbilityGraph graph {
    target: Actor input;

    apply: ApplyDamage {
        amount: 50;
    }

    Start event {
        connect(context.start, apply.enter);
        bind(target, apply.target);
    }
}
```

特点：

- 接近 DSL。
- 人类手写和 code review 更舒服。
- `name: Type kind` 表达紧凑。
- command 保留 `connect(a, b)` 形式。
- 不是 JSON 超集，需要自定义 parser/formatter。

### JSON 超集 DocumentNode 语法

```json5
{
  assets: {
    Fireball: {
      kind: "graph",
      type: "AbilityGraph",
      children: {
        target: { kind: "input", type: "Actor" },
        apply: {
          kind: "node",
          type: "ApplyDamage",
          props: {
            amount: 50,
          },
        },
        Start: {
          kind: "event",
          commands: [
            { op: "connect", from: "context.start", to: "apply.enter" },
            { op: "bind", source: "target", target: "apply.target" },
          ],
        },
      },
    },
  },
}
```

特点：

- 接近 JSON/JSON5。
- 结构化 patch 和 editor state 更直接。
- command 必须 object/tuple 化。
- `kind/type` 字段重复较多。
- 人类手写图的噪声更大。

---

## 维度对比

| 维度 | 类型化表层语法 | JSON 超集 DocumentNode |
| --- | --- | --- |
| 人类手写 | 更短、更像 DSL | 更啰嗦、更像配置 |
| Code review | 图结构更直观 | 结构字段更多，diff 更机械 |
| Parser 难度 | 需要自定义 grammar | 可从 JSON5 grammar 起步 |
| Formatter | 需要 DSL formatter | 可接近 JSON formatter |
| Source patch | 需要语法节点 anchor | object path / JSON Pointer 更自然 |
| Command 表达 | `connect(a, b)` 自然 | 需 `{ op, from, to }` 或 tuple |
| 语法糖 | 支持空间大 | canonical 语法糖少 |
| JSON 兼容 | 不兼容 | 可以保持 JSON/JSON5-compatible |
| DOM/tree 渲染 | 需要 lowering | 天然 object tree |
| 声明文件 | 简洁 | 结构稳定但更长 |
| AI patch | 需要理解 DSL | 更适合结构化 patch |
| 视觉编辑回写 | 依赖 source binding | 路径定位更直接 |
| Runtime bake 输入 | 需要投影转换 | 更接近结构化输入 |

---

## Command 取舍

Command 是两条路线的核心分歧。

类型化表层语法：

```gs
connect(context.start, apply.enter);
bind(target, apply.target);
```

JSON 超集路线：

```json5
commands: [
  { op: "connect", from: "context.start", to: "apply.enter" },
  { op: "bind", source: "target", target: "apply.target" },
]
```

如果 JSON 路线允许：

```json5
commands: [
  connect(context.start, apply.enter),
]
```

它就不再是严格 JSON-compatible canonical syntax，而会变成 JSON-inspired
DSL。

---

## 语法糖取舍

类型化表层语法可以自然支持：

```gs
apply: ApplyDamage {
    amount: 50;
}
```

JSON 超集路线必须结构化：

```json5
apply: {
  kind: "node",
  type: "ApplyDamage",
  props: {
    amount: 50,
  },
}
```

如果 JSON 路线再加入 `apply: ApplyDamage` 这类糖，会破坏 JSON object/value
模型，需要额外 grammar 和 formatter 规则。

---

## 适合的主定位

### 类型化表层语法更适合

- 人类经常手写 graph。
- 代码审查要快速理解图结构。
- command 是核心 authoring 体验。
- 语言希望保留脚本/DSL 感。

### JSON 超集路线更适合

- 视觉编辑器和 AI patch 是主要入口。
- 源文件更像结构化资产。
- 希望最大化复用 JSON/JSON5 工具链。
- 希望 source path、stable id、schema validation 更直接。

---

## 可能的混合策略

不建议长期维护两个 canonical source。

可行混合方式：

1. **类型化表层语法作为 canonical source，JSON 作为 debug/editor/IR 格式。**
2. **JSON 超集作为 canonical source，类型化语法只作为未来可选 authoring
   frontend。**
3. **两者都作为草稿，先用同一个复杂示例和 patch 原型比较成本，再决定。**

如果选择双格式，必须明确谁是 source of truth，另一个只能是导出、中间表示
或兼容输入。

---

## 当前建议

短期建议保留两份草稿并行：

- 类型化表层语法用于验证人类 authoring 体验。
- JSON 超集路线用于验证 editor/AI patch/structured source 体验。

下一步应对同一组场景做原型比较：

- 添加节点。
- 修改属性。
- 添加 command。
- 重连 edge。
- 重命名 node。
- declaration 改名。
- 保留注释和局部格式。

比较结果再决定 canonical syntax。
