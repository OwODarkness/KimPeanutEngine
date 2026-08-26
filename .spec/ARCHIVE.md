# Spec and Journal Archive

This file explains how to find historical work. It is an index and search guide, not a reason to delete old journals.

## Directory policy

- Active and recent specs live in `.spec/specs/`.
- Journals live in `.spec/journal/`.
- A completed, paused, or blocked journal remains readable in place.
- “Archived” means the task is no longer active and is indexed here; it does not mean its evidence is removed.

Do not move or rename a record just to make the directory look tidy. Links from TODOs, commits, and other documents are more valuable than a perfectly sorted folder.

## How to find a journal

Search by task ID or subsystem:

```powershell
rg -n -i "render-command|asset cache|tts|Status: blocked|Remaining risks" .spec docs
```

List the newest records first:

```powershell
Get-ChildItem .spec\journal -File -Recurse |
  Sort-Object LastWriteTime -Descending |
  Select-Object LastWriteTime, FullName
```

Find the journal linked by a TODO item:

```powershell
rg -n -i "journal/|\.spec/specs/" docs .spec
```

Find all unfinished work:

```powershell
rg -n -i "Status: (proposed|active|paused|blocked)|- \[ \]" .spec docs
```

## Optional archive index

For a larger project, add one row here whenever a task leaves active work:

| ID | Area | Status | Spec | Journal | Parent TODO | Last updated |
| --- | --- | --- | --- | --- | --- | --- |
| `<task-id>` | `<area>` | `complete` | [`spec`](specs/<task-id>.md) | [`journal`](journal/<task-id>.md) | [`TODO`](../docs/graphics/TODO.md) | `YYYY-MM-DD` |

The row should be added for `complete`, `paused`, and `blocked` tasks. A blocked journal is especially important because it records the exact external dependency or unanswered question.

## What to read when resuming

Read in this order:

1. The parent TODO item for the requested outcome.
2. The spec for scope, invariants, and acceptance criteria.
3. The latest journal for actual progress and remaining risk.
4. The validation matrix and completion evidence for the last verified state.

If the spec and journal disagree, trust the journal for what actually happened, then update the spec before continuing.
