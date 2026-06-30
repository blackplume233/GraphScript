# Quality Guidelines

Frontend quality means the editor remains visually usable, type-correct,
backend-compatible, and replayable through CLI commands.

## Build And Lint

For `webapp/` changes:

```powershell
cd webapp
npm run build
npm run lint
```

The build script runs `tsc -b` and Vite build. Lint uses the repository ESLint
configuration.

## Visual And Replay Tests

`webapp/test_*.py` contains visual, replay, backend, and smoke tests. Choose the
smallest relevant subset for the changed area, for example:

| Area | Tests to inspect/run |
| --- | --- |
| React Flow node rendering | `test_visual_node_rendering_metadata.py`, `test_node_rendering.py` |
| Edge reconnect/delete/redirect | `test_visual_edge_*`, `test_visual_connection_feedback.py` |
| Command replay | `test_*_replay.py`, `test_real_backend_replay_smoke.py` |
| Source ranges | `test_state_source_ranges.py`, `test_source_range_preview.py` |
| Diagnostics | `test_diagnostic_highlights.py`, `test_real_backend_diagnostic_target_smoke.py` |
| Large graph behavior | `test_real_backend_large_graph_smoke.py`, `test_large_graph_canvas_perf.py` |

## Backend Compatibility Gate

If a frontend change depends on backend state, command, validation, or source
range behavior, also rebuild the C++ project and run backend tests. Frontend
passing alone is not enough for cross-layer changes.

## UI Invariants

- A graph edit must be replayable through the command log.
- The UI must render from backend state after edits.
- Visual previews must not contradict backend validation.
- Diagnostics and source previews must point to the backend-provided target.
- Large graphs must remain navigable; avoid unnecessary full-layout churn.

## Common Mistakes

### Treating UI Preview As Authority

Compatibility previews help users, but backend validation decides. Keep tests
for both preview behavior and real command rejection/acceptance.

### Forgetting Real Backend Tests

A mocked UI test can pass while `/api/exec` or `state_to_json()` has drifted.
Run real backend smoke tests when the contract changes.

### Breaking Command Replay

If visual state changes cannot be reproduced from logged commands and backend
state, the feature breaks GraphScript's CLI-first design.
