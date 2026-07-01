# JSON5 Graph Asset 路线 handoff

> 目的：把当前 session 关于“GraphScript 是否改用 JSON/JSON5 承载脚本 graph”的讨论交接给新 worktree。本文不是最终规范，而是供新分支验证和决策的任务背景。

## 背景

当前主线任务是 `tree-sitter-gs-migration`。既有迁移方向是从旧手写 graph DSL 迁移到 tree-sitter asset 新语法，并统一使用 `.gs` / `.d.gs` 后缀。旧 parser/compiler/emitter 不需要保留语法兼容，但必须保留编辑器产品能力：

- CLI editor
- Web editor
- source diagnostics
- 可视化编辑
- patch/apply 流程
- 后端/前端 API 合约
- runtime-facing 能力

本轮讨论提出一个可能改变方向的问题：是否把 graph 的 canonical source 从 `.gs` DSL 改为纯 JSON / JSON5 / JSONC 这类结构化资产格式。

## 已形成的共识

纯 JSON/JSON5 技术上可以承载 graph 目标。Graph、node、pin、edge、schema、diagnostic、patch、runtime IR 都可以表达为结构化数据。

之前认为 JSON 不适合的点需要修正：

- 标准 JSON 没有注释，但 JSON5 有 `//`、`/* */` 注释，也支持尾逗号、unquoted keys、单引号等更适合手写的能力。
- `source diagnostics`、可视化编辑回写、AI patch 精确改源码，并不是 JSON5 做不到。
- 只要保留 lossless parse tree / source span / stable id / semantic binder，JSON5 也能给每个字段、数组元素、对象成员和语义对象建立 source binding。

真正的差异不是“能不能实现”，而是产品定位和实现代价：

- `.gs` DSL 更像脚本语言，适合人类频繁手写、阅读、review，并且表达密度更高。
- JSON5 更像结构化资产，适合可视化编辑器、程序化改写、AI patch 和运行时序列化。

## 关键判断

不要把 JSON5 路线理解为：

```text
JSON.parse -> runtime
```

可行路线应是：

```text
JSON5 source
  -> lossless JSON5 CST / AST
  -> Graph asset schema parser
  -> declaration/import binder
  -> graph semantic validator
  -> editor model
  -> graph runtime IR bake
```

也就是说，仍然需要一个完整的 GraphScript semantic 层。JSON5 只替换源码表面语法和结构化承载方式，不替代 binder、validator、projector、runtime bake。

## JSON5 可以满足的能力

### Source diagnostics

可以实现。要求 parser 为每个 JSON5 value 和 object member 保留 `TextSpan`。语义诊断可以定位到：

- graph 对象
- node 对象
- property 字段
- edge 数组元素
- schema/type/import 字段
- pin/path 字符串内部或对应 member

示例诊断定位：

```text
graphs[0].nodes[2].type
graphs[0].edges[5].from
declarations.nodes.Print.inputs.message.type
```

### 可视化编辑回写

基础版本比 DSL 更直接。编辑器可以通过 JSON Pointer / stable id 找到目标结构，修改对象后再写回。

高质量版本仍需要 lossless rewrite：

- 尽量只替换局部字段
- 保留注释
- 保留邻近字段顺序
- 保留用户分组
- 避免每次保存格式化整个文件

### AI patch 精确改源码

可以实现，且机器侧更简单。AI 可以输出 JSON Patch、JSON Pointer 或项目自定义 patch op。

示例：

```json
{
  "op": "replace",
  "path": "/graphs/0/nodes/spawn_projectile/properties/speed",
  "value": 1200
}
```

但如果要保证“只改一小段源码并保留注释/格式”，patcher 仍要基于 lossless CST，而不是普通 JSON serializer。

## JSON5 路线的风险

- 人类手写 graph 会比 DSL 啰嗦。
- GraphScript 会从“脚本 DSL”转向“结构化资产格式”。
- 如果直接用 formatter 重写文件，review diff 可能变大。
- JSON Pointer 对数组下标敏感，长期 patch API 应优先使用 stable id，而不是依赖 `nodes[3]`。
- 注释归属需要设计。比如注释属于 object、字段、数组元素，还是仅作为 trivia 保留。
- declaration/import/schema 仍需要专门语义模型，不能只靠 JSON Schema。
- 如果 canonical source 改成 JSON5，现有 tree-sitter `.gs` 迁移计划需要重新评估，尤其是 grammar、formatter、CLI 命令和 docs 的目标。

## 候选路线

### 方案 A：继续 `.gs/.d.gs` DSL

定位：脚本语言和可读资产文档。

优点：

- 手写和 review 体验更好。
- graph/domain 表达更紧凑。
- 已经贴合当前 tree-sitter asset 迁移计划。
- `.d.gs` 声明语法可以自然承载 domain contract。

缺点：

- formatter、patcher、source binding 的设计复杂度更高。
- 可视化编辑回写需要更精细的 source rewrite。
- AI patch 需要理解 DSL 结构。

### 方案 B：JSON5 作为 canonical source

定位：结构化资产格式，编辑器和工具优先。

优点：

- 编辑器状态和源码结构更接近。
- AI patch 可以更稳定地使用 JSON Pointer / stable id。
- runtime bake 输入天然结构化。
- source diagnostics 和 schema validation 可以直接围绕对象路径设计。

缺点：

- 手写表达啰嗦。
- GraphScript 的“脚本语言”属性下降。
- 仍要实现完整 binder/validator/projector。
- 当前 tree-sitter `.gs` 迁移计划需要大幅改写。

### 方案 C：双格式

定位：DSL 作为 authoring frontend，JSON5 作为结构化 asset/editor/cooked source。

优点：

- 保留人类手写 DSL。
- 编辑器和 runtime 可以消费 JSON5/IR。
- 允许逐步验证 JSON5 资产模型。

缺点：

- 双 canonical 风险高，必须明确谁是 source of truth。
- 需要双向转换或导出策略。
- 测试矩阵和文档成本最高。

如果选双格式，建议只允许一个 canonical source，另一个是导出/缓存/中间表示，避免长期双源漂移。

## 建议在新 worktree 验证的问题

1. JSON5 canonical source 是否真的降低 patch/apply 和 visual editor round-trip 成本。
2. 是否能用 stable id 避免数组下标 patch 不稳定。
3. 是否能为注释和局部格式建立可维护的 lossless rewrite 规则。
4. `.d.gs` 声明系统是否也改为 `.d.json5`，还是保留 DSL 声明。
5. runtime IR bake 应从 JSON5 graph model 直接生成，还是仍经过统一 DSL semantic model。
6. CLI 命令面如何变化：`parse/lint/project/patch/edit/serve` 是否仍成立。
7. 现有 Web editor state JSON 是否可以直接提升为 source schema，还是需要分离 source schema 和 UI state schema。
8. 当前 Phase 1-6 迁移计划是否需要暂停并改写。

## 最小可行实验

新 worktree 可以先做一个文档和原型级实验，不必立刻改主线实现。

建议输出：

```text
docs/plans/json5-graph-asset-prototype.md
tests/fixtures/json5/minimal.graph.json5
tests/fixtures/json5/diagnostics.graph.json5
```

建议 fixture 覆盖：

```json5
{
  version: 1,
  graphs: [
    {
      id: "graph.execute",
      name: "Execute",
      schema: "AbilityGraph",

      params: [
        { id: "param.target", name: "target", direction: "input", type: "Actor" },
      ],

      nodes: [
        {
          id: "node.log",
          name: "log",
          type: "PrintString",
          properties: {
            message: "done",
          },
          editor: {
            pos: [100, 200],
          },
        },
      ],

      edges: [
        {
          id: "edge.begin.log",
          kind: "exec",
          from: "context.start",
          to: "node.log.enter",
        },
      ],
    },
  ],
}
```

实验成功标准：

- 能从 JSON5 解析出 graph model。
- 每个 graph/node/edge/property 都有 stable id 和 source span。
- 能生成一条语义诊断，并定位到 JSON5 源码字段。
- 能应用一个局部 patch，并保留未修改区域文本。
- 能说明 runtime IR bake 输入和现有/目标 graph model 的关系。

## 对当前主线的影响

如果 JSON5 只是 editor state、API payload、runtime dump 或 cooked artifact，则当前 `.gs/.d.gs` tree-sitter 迁移方向不需要改变。

如果 JSON5 要成为 canonical source，则需要更新：

- `AGENTS.md` 当前迁移方向
- `docs/plans/tree-sitter-gs-migration-plan.md`
- `docs/spec/architecture.md`
- `docs/spec/dsl-reference.md`
- CLI help 和 package metadata
- tree-sitter grammar 目标
- tests/fixtures 策略
- editor/server/webapp state contract

不要在没有明确决策前删除当前 tree-sitter asset 路线；先用新 worktree 做原型和对比。

## 推荐给新 session 的初始 prompt

```text
请在这个 worktree 中评估 GraphScript 是否应该从 `.gs/.d.gs` DSL canonical source 改为 JSON5 canonical graph asset source。先阅读 `docs/plans/json5-graph-asset-handoff.md`、`docs/plans/tree-sitter-gs-migration-plan.md`、`.trellis/tasks/06-30-tree-sitter-gs-migration/prd.md`。不要直接重写主线实现，先产出一个 JSON5 graph asset 原型设计文档和最小 fixture，重点验证 source diagnostics、visual editor round-trip、AI patch、stable id、runtime IR bake 的可行性和代价。
```
