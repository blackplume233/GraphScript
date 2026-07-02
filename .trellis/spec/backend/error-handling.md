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

Compiler and EditSession validation must reject invalid graph references before
they enter runtime state.

| Condition | Expected behavior |
| --- | --- |
| Function references a graph node | Return an error like `function 'X': cannot reference 'Y'` |
| Event references an undefined name | Return an error like `event 'X': unknown reference 'Y'` |
| Source replay guard hash differs | Return `Source environment hash mismatch` |
| File read/write fails | Return a path-specific error |

Read `docs/spec/scope-rules.md` before changing these contracts.

## CLI Behavior

- CLI commands should report `Result` errors to the user without crashing the
  process for normal validation failures.
- Command functions in `CLIEditor` should keep validation close to the command
  boundary and delegate graph semantics to `EditSession` or compiler APIs.
- Add a help/usage error for malformed command arguments instead of silently
  guessing.

## Server/API Behavior

- HTTP command execution should preserve the CLI command contract: request a
  command string, execute through the same path as CLI editing, return output
  and current state when available.
- Frontend code must not treat a failed command as successful local state.
- If an API response carries diagnostics, keep enough target/range information
  for UI highlighting and tests.

## Scenario: Web API Command JSON Replay

### 1. Scope / Trigger

- Trigger: browser UI command replay through `POST /api/exec`.
- Scope: JSON request parsing in `cli/server.cpp`, command tokenization in
  `CLIEditor`, and browser command-log replay tests.

### 2. Signatures

- API: `POST /api/exec` with either plain text command body or JSON
  `{"command":"<cli command>"}`.
- CLI examples:
  - `set_init logger message "\"quoted value\""`
  - `set_param_default msg "\"fallback\""`

### 3. Contracts

- JSON command bodies must be decoded with the shared JSON string extraction
  helper, not ad-hoc quote searches.
- Decoded command strings must be passed to `CLIEditor.execute()` unchanged.
- `CLIEditor::tokenize()` must preserve escaped quotes inside quoted arguments:
  `"\"quoted value\""` becomes one token with value `"quoted value"`.
- Command log entries must retain the replayable command string.

### 4. Validation & Error Matrix

| Condition | Expected behavior |
| --- | --- |
| JSON body omits `command` or decodes empty | `/api/exec` returns `ok:false` with `Empty command` |
| Command contains escaped quotes | Execute normally; do not truncate at `\"` |
| CLI command has invalid args after tokenization | CLI command reports its normal usage/error output |

### 5. Good/Base/Bad Cases

- Good: `{"command":"set_init logger message \"\\\"hello\\\"\""}` stores
  initializer value `"hello"`.
- Base: `{"command":"add_node PrintString logger"}` behaves exactly like a
  plain-text body.
- Bad: searching for the next `"` after `"command":` truncates the command to
  `set_init logger message \` when the argument contains escaped quotes.

### 6. Tests Required

- GoogleTest coverage for escaped quoted CLI args in `CLIEditor`.
- Real backend web smoke coverage that sends a browser command through
  `/api/exec`, asserts command-log replay, state JSON, and emitted source.

### 7. Wrong vs Correct

Wrong:

```cpp
auto q2 = body.find('"', q1);
cmd = body.substr(q1, q2 - q1);
```

Correct:

```cpp
cmd = extract_json_string_field(body, "command").value_or("");
```

## Scenario: Source-Backed CLI Graph Commands

### 1. Scope / Trigger

- Trigger: graph edits issued by web canvas/properties or CLI while a session
  was loaded from `.gs` source text.
- Scope: `CLIEditor` command handlers, `EditSession::asset_source()`,
  tree-sitter asset CST ranges, and `/api/exec` command replay.

### 2. Signatures

- CLI commands remain the public edit surface:
  - `add_node <Type> <instance>`
  - `remove_node <instance>`
  - `rename_node <old> <new>`
  - `set_init <node> <field> <value>`
  - `event <name>` / `fn <name>` followed by `flow`, `unflow`, `link`, `unlink`
  - `annotate node <instance> Position X=<x> Y=<y>`
- Session query:
  - `const std::optional<std::string>& EditSession::asset_source() const`

### 3. Contracts

- If `asset_source()` is present, graph-edit CLI commands must patch source text
  first using CST ranges, then call `EditSession::load_source()` to reproject the
  in-memory graph.
- Web graph operations must still call `/api/exec` with CLI command strings.
  They must not introduce a parallel browser-only semantic source patch API.
- If `asset_source()` is absent, the command may use the existing in-memory
  `EditSession` mutation path.
- Source-backed command failure must report an error and leave the previous
  source/session intact; it must not silently fall back to memory mutation,
  because that drops comments, blank lines, and ordering.

### 4. Validation & Error Matrix

| Condition | Expected behavior |
| --- | --- |
| Source-backed command can find CST target | Patch text and `load_source()` |
| CST target/range is missing | CLI error; do not mutate memory graph |
| Patched source fails parse/compile | CLI error from `load_source()`; previous state remains |
| Command is malformed | Existing usage error |
| Session has no `asset_source()` | Existing memory graph command path |

### 5. Good/Base/Bad Cases

- Good: dragging a node issues `annotate node N Position X=.. Y=..`; CLI patches
  only the Position attribute line and reloads source.
- Base: a graph created purely in memory can still use `EditSession::add_node`.
- Bad: web canvas calls a separate `/api/source_command_patch` endpoint or
  refreshes Source by `/api/emit` after every graph operation.

### 6. Tests Required

- GoogleTest for source-backed graph commands that asserts comments, blank
  lines, initializer values, node renames, and connection text survive without a
  full emit rewrite.
- Web smoke tests should assert graph operations still replay through
  `/api/exec` command logs.

### 7. Wrong vs Correct

Wrong:

```text
GUI edit -> browser computes source patch -> backend loads source
```

Correct:

```text
GUI edit -> /api/exec CLI command -> CLI patches CST-backed text -> load_source()
```

## Scenario: Source Diagnostics Import Resolver Root

### 1. Scope / Trigger

- Trigger: browser Source diagnostics or guarded source apply checks a source
  buffer that contains `import` declarations.
- Scope: `webapp/src/App.tsx` `sourceDiagnosticsOptions()`,
  `webapp/src/api/client.ts`, `/api/diagnostics`, `/api/source`,
  `/api/source_patch`, and `cli/source_diagnostics.cpp`.

### 2. Signatures

- API JSON request fields:
  - `source: string`
  - `resolve_imports: boolean`
  - `source_path?: string`
  - `base_dir?: string`
  - `environment_hash?: string` for guarded apply paths.

### 3. Contracts

- If `resolve_imports=true`, the backend resolves only `.d.gs` imports inside
  the configured resolver root.
- `base_dir` is the resolver root. If omitted, backend current working directory
  is used.
- `source_path` may imply `base_dir` from its parent directory when explicit
  `base_dir` is absent.
- Absolute import paths are allowed only when their normalized target remains
  inside the resolver root. Absolute paths outside the root must stay blocked.
- Unsaved Web sessions may have empty `file_path`; in that case the frontend
  should derive `base_dir` from loaded import `normalized_path` values before
  calling import-aware diagnostics.

### 4. Validation & Error Matrix

| Condition | Expected behavior |
| --- | --- |
| Relative `.d.gs` import inside `base_dir` | Load into dry-run Environment. |
| Absolute `.d.gs` import inside `base_dir` | Load into dry-run Environment. |
| Relative import escaping `base_dir` | `GS_IMPORT_PATH_BLOCKED`. |
| Absolute import outside `base_dir` | `GS_IMPORT_PATH_BLOCKED`. |
| Non-`.d.gs` import | `GS_IMPORT_UNSUPPORTED_EXTENSION`. |
| Missing import file | `GS_IMPORT_READ_FAILED`. |

### 5. Good/Base/Bad Cases

- Good: unsaved Web source emitted from a session with native preset imports
  passes `base_dir` derived from loaded imports, so absolute preset imports do
  not appear as compile errors.
- Base: saved source passes `source_path`, and the backend derives the parent
  directory as resolver root.
- Bad: frontend sends import-aware diagnostics for an unsaved source with no
  `base_dir`, causing backend-emitted absolute imports to be treated as blocked
  user imports.

### 6. Tests Required

- `SourceDiagnostics.ResolveImportsAllowsAbsoluteDeclarationInsideBaseDir`
  asserts absolute imports under `base_dir` load successfully.
- `SourceDiagnostics.ResolveImportsRejectsAbsoluteDeclarationOutsideBaseDir`
  asserts absolute imports outside `base_dir` stay blocked.
- Frontend build/type-check must cover `sourceDiagnosticsOptions()` when adding
  or changing request fields.

### 7. Wrong vs Correct

Wrong:

```typescript
fetchDiagnostics(source, { resolveImports: true })
```

Correct:

```typescript
fetchDiagnostics(source, {
  resolveImports: true,
  baseDir: sourceDiagnosticsBaseDir(state),
})
```

## Anti-Patterns

### Do Not Throw For Expected User Input Errors

Parser, compiler, CLI, and edit operations should report normal invalid input
through `Result` or diagnostics.

### Do Not Lose Scope Context

Errors involving graph logic must name whether the failing block is an event or
function, because the allowed reference sets differ.

### Do Not Convert All Server Failures To Generic UI Errors

The browser editor needs actionable messages for replay, diagnostics, and source
range navigation tests.
