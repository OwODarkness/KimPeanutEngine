---
name: engine-reference
description: Study how another open-source game engine solves a KimPeanutEngine design problem, then map the pattern back to the repository. Invoke by asking how another engine does a subsystem or by using /engine-reference.
---

# Engine reference workflow

The durable reference knowledge and engine matrix live in [`docs/engine-reference/README.md`](../../../docs/engine-reference/README.md). Read that index and the relevant design note before beginning a study.

Use this skill when the question is "how should our module be shaped?" It is design study, distinct from `third-party-issue`, which concerns using a vendored library's API.

## Workflow

1. Name the subsystem and the concrete KimPeanutEngine design question.
2. Select a reference from `docs/engine-reference/README.md`.
3. Search the reference repository's actual source, scoped to the relevant module. Keep code-search queries focused and prefer headers for class shape.
4. Read only enough source to establish the pattern and its trade-offs. Record the branch or revision studied.
5. Write the result in two parts:
   - **How the reference does it:** a short source-backed description with repository-relative paths.
   - **What it suggests here:** a concrete owner, boundary, data flow, or trap for a KimPeanutEngine module.
6. Update the relevant document under `docs/` and, when the conclusion changes planned work, update the module TODO or status ledger.

## Guardrails

- Study, do not import. Do not copy source wholesale or inherit another engine's license and constraints.
- The default branch is a moving development branch, not necessarily a stable release.
- Prefer primary source and repository documentation over summaries.
- If source access is unavailable, use the existing local reference notes and state that limitation.
