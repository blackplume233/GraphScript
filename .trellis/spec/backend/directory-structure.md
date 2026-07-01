# Backend Directory Harness Guide

This file helps agents find code. It is not an architecture spec.

For architecture and product boundaries, read:

- `docs/spec/index.md`
- `docs/spec/architecture.md`
- `docs/syntax/current/serialization-syntax.md`
- `docs/spec/graph-domain.md`

## Common Locations

```text
include/graphscript/     Public C++ API headers
src/                     Implementations mirroring include/graphscript/
cli/                     `gs` executable, CLI editor, HTTP server entry points
tests/                   GoogleTest suites
tests/fixtures/          `.gs` and `.d.gs` fixtures
docs/spec/               Product, architecture, domain, and development docs
docs/syntax/             Syntax design artifacts
.trellis/spec/           Agent harness guidance only
web/                     Legacy/single-file browser UI
webapp/                  Vite/React/TypeScript graph editor
CMakeLists.txt           Build configuration
```

## Agent Rules

- Match existing header/source naming and ownership patterns.
- Put reusable engine behavior in backend libraries, not CLI or frontend glue.
- Put product/language decisions in `docs/spec/`.
- Put only agent workflow guidance in `.trellis/spec/`.
- Keep fixtures in `tests/fixtures/` and update focused tests with behavior
  changes.
