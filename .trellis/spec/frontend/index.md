# Frontend Agent Guide

This file is harness guidance for agents. It is not the product spec.

Frontend product behavior is defined by `docs/spec/`, especially the architecture
and graph-domain documents. `.trellis/spec` only tells agents what to read and
what to verify before changing UI code.

## Required Reading

Read these before frontend changes:

1. `docs/spec/index.md`
2. `docs/spec/architecture.md`
3. `docs/spec/graph-domain.md` for graph nodes, pins, edges,
   diagnostics, source-bound graph operations, or editor state.
4. `docs/spec/development-guide.md`
5. Backend harness guides when changing `/api/*`, command execution, or JSON
   state contracts.

## Harness Guides

| Guide | When to read |
| --- | --- |
| [Directory Structure](./directory-structure.md) | Locating `web/`, `webapp/`, API, and test files. |
| [Component Guidelines](./component-guidelines.md) | React/React Flow and legacy UI behavior. |
| [Hook Guidelines](./hook-guidelines.md) | Hook state and effect changes. |
| [State Management](./state-management.md) | Backend state, command replay, and UI refresh. |
| [Quality Guidelines](./quality-guidelines.md) | Frontend build, lint, smoke, and replay checks. |
| [Type Safety](./type-safety.md) | TypeScript API and state contracts. |

## Agent Checklist

- Determine whether the change targets `web/`, `webapp/`, or backend API.
- Find the backend operation behind any intended UI action.
- Do not add UI-only graph semantics that cannot be represented by backend
  document/domain operations.
- Check `webapp/src/api/types.ts` when JSON state assumptions change.
- Run the relevant frontend and backend checks before reporting completion.
