# GraphScript Frontend Guidelines

This layer covers GraphScript's browser editing surfaces:

- `web/`: legacy/single-file LiteGraph.js editor served by `gs serve`.
- `webapp/`: Vite, React, TypeScript, React Flow based editor.
- C++ server state/API contracts consumed by both UIs.

Frontend work must preserve the project axiom that every editing operation is
CLI-first and replayable. The browser UI is a command surface over the C++
`EditSession`; it must not create graph semantics that the core engine cannot
parse, validate, emit, and replay.

## Required Reading

Read these before frontend changes:

1. `docs/spec/architecture.md` for `EditSession`, `state_to_json()`, and server
   boundaries.
2. `docs/spec/development-guide.md` for CLI-first and test expectations.
3. `docs/spec/scope-rules.md` when rendering or editing flow/data links.
4. Backend specs when changing `/api/*`, command execution, or JSON state.

## Guidelines Index

| Guide | Description | Status |
| --- | --- | --- |
| [Directory Structure](./directory-structure.md) | `web/`, `webapp/`, panels, canvas, API, tests | Filled |
| [Component Guidelines](./component-guidelines.md) | React/React Flow and legacy LiteGraph UI patterns | Filled |
| [Hook Guidelines](./hook-guidelines.md) | Hook usage boundaries for the React webapp | Filled |
| [State Management](./state-management.md) | Backend state as source of truth and CLI replay | Filled |
| [Quality Guidelines](./quality-guidelines.md) | Build, lint, visual replay, backend smoke tests | Filled |
| [Type Safety](./type-safety.md) | TypeScript API contracts and JSON state typing | Filled |

## Pre-Development Checklist

- Determine whether the change targets `web/`, `webapp/`, or the C++ server API.
- Find the CLI command behind the intended UI action.
- If no CLI command exists, add or design the backend command before making a
  GUI-only operation.
- Check `webapp/src/api/types.ts` before changing JSON state assumptions.
- For visual graph behavior, identify the replay or smoke test that proves the
  UI can reconstruct state from backend output.

## Quality Check

- Run `npm run build` and `npm run lint` from `webapp/` for TypeScript UI work.
- Run relevant Python visual/smoke tests in `webapp/` when changing canvas,
  replay, diagnostics, source preview, or real backend integration.
- Rebuild and run backend tests when frontend changes require C++ API/state
  changes.
- Verify browser actions remain reproducible through command log replay.
