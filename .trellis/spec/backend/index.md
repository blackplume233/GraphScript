# Backend Agent Guide

This file is harness guidance for agents. It is not the product spec.

Product and architecture authority lives in `docs/spec/`; syntax artifacts live
in `docs/syntax/`. Use this Trellis file only as the Agent work checklist.

## Required Reading

Read these before backend/core changes:

1. `docs/spec/index.md`
2. `docs/spec/architecture.md`
3. `docs/syntax/current/serialization-syntax.md` for parser, CST/AST,
   source patch, or persisted syntax work.
4. `docs/spec/graph-domain.md` for graph projection, pins, edges,
   source bindings, or visual graph edit work.
5. `docs/spec/development-guide.md`

Then read the specific harness guide below only if it applies to the files you
will touch.

## Harness Guides

| Guide | When to read |
| --- | --- |
| [Directory Structure](./directory-structure.md) | Locating backend files and ownership boundaries. |
| [Database Guidelines](./database-guidelines.md) | Only when a persistence feature is proposed. |
| [Error Handling](./error-handling.md) | Parser/projection/edit/CLI/server errors. |
| [Quality Guidelines](./quality-guidelines.md) | Test and verification expectations. |
| [Logging Guidelines](./logging-guidelines.md) | CLI/server output and diagnostics visibility. |

## Agent Checklist

- Identify the touched layer before editing: serialization syntax, document
  model, projection, patch/format, edit, runtime, CLI, server, tests, or docs.
- Search for existing patterns before adding helpers, commands, fields, or
  fixtures.
- If a change crosses layers, update code, tests, fixtures, and `docs/spec`
  together.
- Keep product decisions in `docs/spec`, not `.trellis/spec`.
- Run the relevant build/test checks before reporting completion.
