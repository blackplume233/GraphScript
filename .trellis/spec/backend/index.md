# GraphScript C++ Core Guidelines

This layer covers the C++17 GraphScript engine, CLI, build system, tests, and
runtime-facing contracts. It is the implementation-spec entry point for work in
`include/graphscript/`, `src/`, `cli/`, `tests/`, `tests/fixtures/`, and CMake.

Detailed product and language specifications stay in `docs/spec/`. The Trellis
spec summarizes the executable engineering rules future agents must apply before
editing code.

## Required Reading

Read these files before backend/core changes:

1. `docs/spec/index.md` for project axioms and command overview.
2. `docs/spec/architecture.md` for layer boundaries and data structures.
3. `docs/spec/development-guide.md` for build, test, extension, and anti-pattern rules.
4. `docs/spec/scope-rules.md` before touching compiler, EditSession, flow, or data links.
5. The specific guideline files below for the touched area.

## Guidelines Index

| Guide | Description | Status |
| --- | --- | --- |
| [Directory Structure](./directory-structure.md) | C++ API, implementation, CLI, tests, fixtures, and docs layout | Filled |
| [Database Guidelines](./database-guidelines.md) | Database applicability for this project | N/A |
| [Error Handling](./error-handling.md) | `Result<T, std::string>`, diagnostics, CLI/server error flow | Filled |
| [Quality Guidelines](./quality-guidelines.md) | Test gates, round-trip rules, doc comments, forbidden patterns | Filled |
| [Logging Guidelines](./logging-guidelines.md) | CLI/server output and diagnostic visibility | Filled |

## Pre-Development Checklist

- Identify the touched pipeline stage: lexer, parser, compiler, emitter, edit,
  runtime, CLI, web server, or tests.
- Search for the existing pattern before adding a new helper, command, or data
  field.
- If the change affects syntax or IR shape, plan updates across parser,
  compiler, emitter, tests, and fixtures.
- If the change affects graph references, read `docs/spec/scope-rules.md`.
- If the change affects GUI behavior, also read the frontend specs because GUI
  actions must remain replayable through CLI commands.

## Quality Check

- Build with `cmake --build build --config Release`.
- Run `./build/Release/gs_tests.exe`; all tests are expected to pass.
- Add or update focused GoogleTest coverage for changed behavior.
- For syntax changes, include a round-trip test: parse, compile, emit, reparse,
  recompile, then compare structural behavior.
- Do not commit domain-specific behavior into the core engine; domain knowledge
  belongs in `.d.gs` schema declarations and fixtures.
