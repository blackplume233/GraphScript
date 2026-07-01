# Cross-Layer Thinking Guide

> **Purpose**: Think through data flow across layers before implementing.

This is harness guidance for agents. Product and syntax artifacts live outside
`.trellis/spec/`.

---

## The Problem

Most bugs happen at layer boundaries, not within layers.

Common cross-layer bugs:

- API returns format A, frontend expects format B
- Storage keeps X, service transforms to Y, but loses data
- Multiple layers implement the same rule differently

---

## Before Implementing Cross-Layer Features

### Step 1: Map The Data Flow

Draw out how data moves:

```text
Source -> Transform -> Store -> Retrieve -> Transform -> Display
```

For each arrow, ask:

- What format is the data in?
- What could go wrong?
- Who is responsible for validation?

### Step 2: Identify Boundaries

| Boundary | Common Issues |
| --- | --- |
| API <-> Service | Type mismatches, missing fields |
| Service <-> Storage | Format conversions, null handling |
| Backend <-> Frontend | Serialization, state shape drift |
| Component <-> Component | Props shape changes |

### Step 3: Define Contracts

For each boundary:

- What is the exact input format?
- What is the exact output format?
- What errors can occur?

---

## Common Cross-Layer Mistakes

### Mistake 1: Implicit Format Assumptions

Bad: Assuming a format without checking.

Good: Explicit format conversion at boundaries.

### Mistake 2: Scattered Validation

Bad: Validating the same thing in multiple layers.

Good: Validate once at the authoritative entry point, then share diagnostics.

### Mistake 3: Leaky Abstractions

Bad: UI or domain code depends on private parser internals.

Good: Each layer depends on its neighbor's public contract.

---

## GraphScript Cross-Layer Triggers

GraphScript changes often look local but cross parser, document model,
projection, patch/format path, CLI, frontend, tests, and docs. Use this checklist
before editing.

### Serialization Syntax Or Document Model

- [ ] Current syntax doc checked: `docs/syntax/current/serialization-syntax.md`
- [ ] AI Native design guide checked: `.trellis/spec/guides/ai-native-design-guide.md`
- [ ] Parser/CST/AST source ranges updated where needed
- [ ] Document operations or formatter preserve unrelated comments, blank lines,
      and local formatting
- [ ] Round-trip or patch tests updated
- [ ] Fixtures updated if user-facing behavior changes

### Graph Domain Or Visual Editing

- [ ] Current graph doc checked: `docs/spec/graph-domain.md`
- [ ] AI Native design guide checked: `.trellis/spec/guides/ai-native-design-guide.md`
- [ ] Backend projection and frontend preview use the same rules
- [ ] Graph edits remain source-bound or return clear diagnostics
- [ ] Tests cover accepted and rejected graph operations

### CLI/API/Frontend

- [ ] Command/API shape updated intentionally
- [ ] `webapp/src/api/types.ts` updated when JSON state changes
- [ ] UI renders from backend state instead of inventing unsupported semantics
- [ ] Replay, source-range, and diagnostic tests updated when relevant

---

## Cross-Platform Template Consistency

In Trellis, command templates can exist in multiple platforms with identical or
near-identical content. This is a cross-layer boundary.

### Checklist: After Modifying Any Command Template

- [ ] Find all platforms with the same command:
      `find src/templates/*/commands/trellis/ -name "<command>.*"`
- [ ] Update all platform copies
- [ ] Adapt platform-specific syntax when needed
- [ ] Run the relevant cross-layer check

---

## Generated Runtime Template Upgrade Consistency

Some generated files are both documentation and runtime input. Template changes
must be validated against both fresh init and upgrade paths.

### Checklist: After Modifying A Runtime-Parsed Template

- [ ] Identify every runtime parser that reads the template
- [ ] Check whether relevant syntax lives outside managed regions
- [ ] Verify fresh init output
- [ ] Verify an upgrade scenario from an older template
- [ ] Update the harness spec that owns the runtime contract

---

## Mode-Detection Probe Checklist

When a CLI auto-detects a mode by probing a resource:

### Before Implementing

- [ ] Probe runs in all code paths that use the result
- [ ] 404 vs transient error are distinguished
- [ ] Transient errors abort or retry, never silently switch modes
- [ ] Shared state is reset when context changes
- [ ] Shortcut paths have the same error-handling quality as the probed path

### After Implementing

- [ ] Trace every path from probe result to the decision branch
- [ ] External format contracts are tested or documented
- [ ] Metadata reads consume a complete response
- [ ] Composite identifiers include all fields in the correct order
- [ ] Action functions do not call old catch-all helpers internally

---

## When To Create Flow Documentation

Create detailed flow docs when:

- Feature spans 3+ layers
- Multiple teams or agents are involved
- Data format is complex
- Feature has caused bugs before
