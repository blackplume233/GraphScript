# Database Guidelines

GraphScript currently has no database, ORM, migration system, or persistent
server-side datastore.

## Applicability

This file is intentionally marked not applicable for current backend work.

Do not introduce database assumptions into GraphScript specs, tests, or
implementation unless a future task explicitly adds a persistence layer.

## Current Persistence Model

- Graph source is represented as `.gs` text.
- Host declarations are represented as `.d.gs` text.
- Editor state lives in memory inside `EditSession`.
- Durable examples and regression data live in `tests/fixtures/`.
- CLI `load`/`save` operations read and write source files.

## If Persistence Is Added Later

A future persistence feature must add a new spec section before implementation
covering:

- Storage format and versioning.
- Load/save command signatures.
- Error behavior for missing, corrupt, or incompatible data.
- Round-trip expectations between storage, Module, and emitted `.gs`.
- Regression tests that prove old fixtures still compile and emit correctly.
