# Error Handling

GraphScript uses explicit result values for ordinary fallible operations.
Exceptions are not the normal error path for parsing, compiling, editing, or
file operations.

## Core Convention

- Fallible operations return `Result<T, std::string>`.
- Fallible void operations return `Result<void, std::string>`.
- Use `Result<void, std::string>::ok()` for success. Do not write
  `Result<void, std::string>::ok({})`.
- Error strings should identify the failed operation and the relevant graph,
  block, node, pin, path, or identifier when available.

```cpp
Result<void, std::string> EditSession::add_node(
    const std::string& type,
    const std::string& name);

auto added = session.add_node("PrintString", "logger");
if (added.is_err()) {
    std::cerr << added.error() << std::endl;
    return;
}
```

## Validation Errors

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
