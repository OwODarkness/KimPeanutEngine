---
name: git-commit
description: Commit the working tree with best-practice messages — split unrelated changes into logical commits, write scoped conventional-style messages that explain the why. Invoke by saying "git commit".
---

# Git commit

Turn the current working tree into clean, well-described commits. Triggered by "git commit" or `/git-commit`.

## Before anything

1. **Understand the changes first.** Run `git status --short` and `git diff --stat`. For new directories, list them with `git status --porcelain --untracked-files=all` so you know every file you're about to stage (catch stray build artifacts).
2. **Read before staging when unsure.** If a diff hunk's purpose isn't obvious, read the actual file. Never commit code you don't understand.

## Rules

- **One commit per purpose.** Split unrelated changes (e.g. "log window + profile bar + window config") into separate logical commits — stage only the files for one purpose at a time with `git add <paths>`, verify with `git diff --cached --stat`, then commit. Each commit must leave the tree compiling. Only combine into one commit when the changes genuinely form a single unit.
- **Leave the third-party submodule alone.** `third_party/googletest` is a gitlink whose checkout has local modifications (e.g. a patched `CMakeLists.txt`). Its recorded commit is unchanged — never stage it. Same rule for any other submodule whose pointer hasn't moved.
- **Don't push, don't branch, don't tag.** The user said commit. If they want a push or a branch, they'll say so. Do commit on the current branch (this project's workflow is commit-to-main).
- **Don't `git add -A` / `.` blindly.** Explicit paths keep mixed trees splittable.
- **Don't add generated build output.** `build/`, `*.exe`, `*.lib` etc. are ignored; if you see untracked artifacts, leave them out.

## Message format

- **Subject ≤ ~50 chars, imperative, scoped** using the repo's conventional-style prefixes: `editor:`, `log:`, `runtime:`, `audio:`, `graphics:`, `render:`, `script:`, `asset:`, `resource:`, `build:`, `test:`, `docs:`, `platform:`. Example: `editor: lock toggle + viewport-fraction geometry for windows`.
- **Body explains the *why*, not the *what*.** The diff shows what changed; the message says why it's written this way — the non-obvious decisions, the trade-offs, the seam. This repo's convention (see the `concise-comments` skill) is: rationale lives in the commit message or `docs/`, never in verbose in-code comments.
- **Cap subject + body at a few lines each.** If a commit needs a long essay, it's probably two commits.
- Reference concrete files when it helps (`editor_ui.cpp`, `memory_stats_sampler.h`), but don't paste diff content.
- **End every commit with:**

```
Co-Authored-By: Claude <noreply@anthropic.com>
```

## Workflow

For each logical group:

1. `git add <paths>` — only that group's files.
2. `git diff --cached --stat` — confirm you're committing exactly the intended scope.
3. `git commit -F - <<'EOF' ... EOF` with the message (scoped subject, blank line, body, blank line, Co-Authored-By line).

Finish by showing `git status --short` and `git log --oneline -5` so the result is visible. The LF→CRLF warnings on this repo are normal — ignore them.

## Example

A tree touching the editor log window, the profile bar, and docs should become:

```
log: snapshot API; editor log window virtualization
editor: profile status bar with metric seam + platform memory sampler
docs: window config, log snapshot, and profile bar
```

not one "update editor stuff" commit.
