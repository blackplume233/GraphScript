# Backend Quality Harness Guide

This file is an agent verification checklist. Product quality standards belong
in `docs/spec/`; this file tells agents what to run and inspect.

## Build And Test Gates

Use the existing CMake build and GoogleTest suite:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
.\build\Release\gs_tests.exe
```

## Change-Type Checks

| Change type | Expected verification |
| --- | --- |
| Parser or syntax | Parser tests, invalid-input cases, fixtures, and current syntax docs. |
| Document patch/format | Round-trip tests and preservation checks for unrelated text. |
| Projection/domain | Projection tests, diagnostics tests, and graph-domain docs. |
| EditSession/API | Edit/replay tests and API state checks. |
| CLI | Dispatch/help updates and smoke tests. |
| Fixtures | Integration parse/project test and round-trip coverage where relevant. |

## Agent Rules

- Do not report completion without saying which checks ran.
- If a relevant check cannot run, state why.
- Keep code, fixtures, and `docs/spec` aligned when behavior changes.
- Do not add product decisions to `.trellis/spec`; update `docs/spec` instead.
