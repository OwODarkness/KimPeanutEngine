# Task Specs and Journals

`.spec/` is the lightweight execution record for work that is too large or too long-lived for a single TODO checkbox.

It does not replace the roadmap in [`docs/graphics/TODO.md`](../docs/graphics/TODO.md). The roadmap says *what* should be done; a spec and journal record *how the work was planned and what actually happened*.

## When to use it

- Small change: use the normal completion report in [`docs/agent_completion_evidence.md`](../docs/agent_completion_evidence.md).
- Medium change: use the completion report and update the relevant TODO item.
- Large refactor, multi-session task, or risky architecture change: create a spec before implementation and a journal when the work is completed, paused, or blocked.

The journal is not a second TODO list. It is a factual report: what was done, what changed, what was validated, and what risk remains.

## Recommended lifecycle

```text
Roadmap item -> Spec -> Implementation -> Targeted validation -> Journal -> TODO/status update
```

1. Find the parent item in `docs/graphics/TODO.md` or another project document.
2. Create `.spec/specs/<area>-<short-task>.md` when the task needs a durable plan.
3. Write the objective, scope, invariants, stages, acceptance criteria, and validation plan.
4. Keep the spec focused on decisions and intended outcomes. Update it when the plan changes materially.
5. During or after implementation, write `.spec/journal/<area>-<short-task>.md`.
6. Link the journal from the parent TODO item when useful, then record the final status.

## Naming

Use lowercase kebab-case and include the main subsystem:

```text
.spec/specs/render-command-buffer-split.md
.spec/specs/asset-cache-rework.md
.spec/specs/tts-runtime-integration.md
```

Use one spec per coherent outcome. Do not create a spec for every small edit.

## Minimum spec structure

Every spec should answer these questions:

```markdown
# <Task title>

- Status: proposed | active | paused | complete | blocked
- Owner: <person or agent>
- Parent TODO: [link]

## Objective
What user or engineering outcome is required?

## Current state
What exists now, including relevant files and constraints?

## Scope and non-goals
What will change, and what is explicitly out of scope?

## Invariants
What architecture, API, ownership, threading, or compatibility rules must remain true?

## Stages
1. ...
2. ...

## Acceptance criteria
- [ ] ...

## Validation plan
Which level from [`docs/validation_matrix.md`](../docs/validation_matrix.md) is required?

## Risks and open questions
What may still be wrong or undecided?
```

## Finding active work

```powershell
rg -n -i "render|asset|tts|Status: active|Status: blocked" .spec docs
Get-ChildItem .spec\specs,.spec\journal -File -Recurse |
  Sort-Object LastWriteTime -Descending
```

The archive guide contains more search and indexing patterns: [`ARCHIVE.md`](ARCHIVE.md).

## Completion rule

A large task is not complete merely because code was edited. Its journal should state the exact changes, validation commands and results, skipped checks, and remaining risks. Use [`journal/README.md`](journal/README.md) for the format and the standard evidence report linked by [`AGENTS.md`](../AGENTS.md).
