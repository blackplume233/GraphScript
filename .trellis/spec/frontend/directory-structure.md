# Directory Structure

GraphScript has two browser editing surfaces. Treat them as clients of the C++
server and `EditSession`, not as independent graph engines.

## Legacy UI: `web/`

```text
web/index.html    Single-file LiteGraph.js editor
```

The legacy UI talks to the server at the current host, calls `/api/state` and
`/api/exec`, registers LiteGraph node types from backend state, and maps GUI
actions to CLI command strings.

## React UI: `webapp/`

```text
webapp/
  package.json
  vite.config.ts              Vite dev proxy for `/api` to localhost:8080
  src/
    App.tsx                   Main editor shell and command orchestration
    api/
      client.ts               HTTP client helpers
      types.ts                Backend JSON contracts
    canvas/                   React Flow canvas, node rendering, diagnostics
    panels/                   Palette, properties, diagnostics, command log
    components/               Shared UI primitives and error boundary
    lib/                      Small utilities
  test_*.py                   Visual, replay, backend, and smoke tests
```

## Placement Rules

- Put HTTP request/response helpers in `webapp/src/api/client.ts`.
- Put backend JSON type definitions in `webapp/src/api/types.ts`.
- Put graph rendering and interaction code under `webapp/src/canvas/`.
- Put side panels and command surfaces under `webapp/src/panels/`.
- Put shared visual primitives under `webapp/src/components/`.
- Keep generated or static assets under `webapp/public/` or `webapp/src/assets/`.

## Server Boundary

- `/api/state` is the source for graph/editor state.
- `/api/exec` executes CLI command strings and returns output/state.
- The Vite dev server proxies `/api` to `http://localhost:8080`.
- Any new UI operation must be expressible through a backend command or a
  documented API contract with equivalent replay behavior.

## Cross-Layer Files To Check

When changing state shape or command behavior, inspect:

- `cli/editor.h` and `cli/editor.cpp`
- C++ server route handling
- `EditSession::state_to_json()`
- `webapp/src/api/types.ts`
- `webapp/src/api/client.ts`
- UI components that render the changed fields
- relevant `webapp/test_*.py` replay or smoke tests
