# How to Write a Work Journal

A journal is a short, factual record for a large, multi-session, paused, or blocked task. Write one when the task reaches a meaningful checkpoint—not after every command.

The journal complements the completion evidence report. The evidence report proves the current change was validated; the journal preserves the reasoning and history that another engineer will need later.

## One task, one journal

Use the same kebab-case ID as the spec:

```text
.spec/specs/render-command-buffer-split.md
.spec/journal/render-command-buffer-split.md
```

If no spec was needed, create only the journal and link the parent TODO item.

## Required content

```markdown
# <Task title>

- Status: complete | partial | blocked | paused
- Date: YYYY-MM-DD
- Spec: [link to spec, if one exists]
- Parent TODO: [link]

## What was done
- ...

## What changed
- Architecture or behavior:
- Important files/modules:
- Public API or ownership changes:

## Validation
- Required level: L0 | L1 | L2 | L3 | L4
- Command: `...`
- Result: PASS | FAIL | NOT RUN
- Evidence: `...`

## Remaining risks and unverified areas
- ...

## Remaining work
- ...

## Documentation and follow-up
- ...
```

## Writing rules

- Record facts, not confidence: say `NOT RUN` when a check was skipped.
- Include exact commands and the meaningful result or failure reason.
- Separate code changes from documentation changes.
- Mention ownership, lifetime, threading, API, and compatibility impact when relevant.
- Keep remaining risks visible even when the status is `complete`.
- If later work changes the conclusion, add a dated `Correction` section instead of silently rewriting history.
- Link related specs, TODO items, validation evidence, and follow-up tasks.

For validation levels and targeted commands, use [`docs/validation_matrix.md`](../../docs/validation_matrix.md). For the final implementation report, use [`docs/agent_completion_evidence.md`](../../docs/agent_completion_evidence.md).

## Practical checkpoint format

During a long task, a compact checkpoint is enough:

```markdown
## Checkpoint — 2026-08-26
- Done: moved ownership of X into Y; updated Z.
- Validation: `tools/kp.ps1 validate` — PASS.
- Risk: runtime rendering path is not covered by automated tests.
- Next: add the targeted smoke check before closing the spec.
```
