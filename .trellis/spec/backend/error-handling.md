# Error Handling Harness Guide

This file is an agent checklist. Product-level diagnostic contracts belong in
`docs/spec/`.

## Required Product Docs

- `docs/spec/architecture.md`
- `docs/syntax/current/serialization-syntax.md`
- `docs/spec/graph-domain.md`

## Agent Rules

- Use explicit result values or structured diagnostics for expected invalid user
  input.
- Keep parser, projection, edit, CLI, and server failures actionable.
- Preserve source range or source-bound graph context when available.
- Do not collapse backend/API failures into generic UI-only messages.
- Add or update tests when changing error behavior.

## Common Checks

- CLI errors should be readable and non-crashing.
- API errors should carry enough context for UI highlighting and tests.
- Diagnostic changes should remain machine-usable for AI repair workflows.
