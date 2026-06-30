# Logging Guidelines

GraphScript does not currently use a structured logging framework. Output is
mostly CLI text, diagnostics, test failures, and HTTP command responses.

## CLI Output

- Use standard output for successful command results, summaries, emitted text,
  diagrams, and status displays.
- Use standard error or returned error strings for failures.
- Keep CLI output deterministic where tests or replay workflows may depend on
  it.
- Include the command context when an error would otherwise be ambiguous.

## Diagnostics

Diagnostics should be machine-usable when they cross into the web UI:

- Include source ranges when available.
- Preserve graph, declaration, node, pin, schema, or type identity when relevant.
- Avoid collapsing multiple diagnostics into one vague message.

## Server Output

- The web server should expose command results through API responses rather than
  hidden process-only logs.
- Browser-visible errors should remain tied to the command or state operation
  that caused them.
- If `gs serve` is running during rebuilds, stop it before rebuilding to avoid
  Windows linker errors such as `LNK1104: cannot open gs.exe`.

## What Not To Add

- Do not introduce logging dependencies just to satisfy generic backend
  patterns.
- Do not add noisy logs inside parser/compiler hot paths unless a task explicitly
  requires trace output.
- Do not let frontend-only logs become the only record of command failure; the
  command/API response must carry the failure.
