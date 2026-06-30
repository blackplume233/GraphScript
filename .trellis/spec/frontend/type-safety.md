# Type Safety

The React webapp is TypeScript-first and runs with `strict: true`. Backend JSON
contracts should be represented in `webapp/src/api/types.ts` and used by API
clients and UI components.

## TypeScript Configuration

Important current settings from `webapp/tsconfig.json`:

- `target`: `ES2020`
- `moduleResolution`: `bundler`
- `jsx`: `react-jsx`
- `strict`: `true`
- `noFallthroughCasesInSwitch`: `true`
- path alias: `@/*` -> `./src/*`

Do not weaken strictness to get a feature through. Fix the contract or narrow
the data correctly.

## API Contract Rules

- Add new backend response/request fields to `webapp/src/api/types.ts`.
- Update `webapp/src/api/client.ts` when adding or changing endpoints.
- Keep optional fields optional only when the backend can truly omit them.
- Prefer exact discriminated shapes for diagnostics/actions when the backend
  supports them.
- Do not use `any` for graph, node, edge, source range, diagnostic, or command
  response payloads.

## Backend Compatibility

When C++ JSON state changes:

- Update TypeScript types in the same task.
- Update every renderer/panel that consumes the changed field.
- Add or update frontend smoke/replay tests.
- Run backend tests if the state change reflects compiler/EditSession behavior.

## Source Ranges And Identifiers

Source ranges, stable IDs, graph names, node instance names, pin names, schema
names, and declaration names are contract fields. Treat them as typed data, not
display-only strings.

Tests should cover:

- stable IDs across state refresh,
- source range preview/navigation,
- diagnostic targets,
- replay behavior after rename or reconnect operations.

## Wrong Vs Correct

Wrong:

```ts
const node = response.graphs[0].nodes[0] as any
renderPin(node.pins[0])
```

Correct:

```ts
import type { GSState, NodeInst } from '@/api/types'

function firstNode(state: GSState): NodeInst | undefined {
  return state.graphs[0]?.nodes[0]
}
```
