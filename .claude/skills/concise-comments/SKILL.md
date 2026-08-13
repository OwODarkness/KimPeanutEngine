---
name: concise-comments
description: Keep comments inside source code short — 1–3 lines explaining why, not what. Push long-form rationale to docs/ or commit messages.
---

# Concise comments

When writing or editing source code in this repository, keep in-code comments short. Long explanations are a smell — in this repo, design rationale lives in `docs/`, not in the `.cpp`/`.h` files.

## Rules

- **Comment the *why*, not the *what*.** The code already says what it does. A comment should answer "why is this here / why is it written this way" in one or two sentences at most. If you catch yourself narrating the code, delete the comment.
- **Hard cap: 3 lines.** If a comment runs past three lines, it belongs in a doc file, a commit message, or the declaration-site doc comment — not inline in the implementation. Extract the one-sentence essence, then move the rest.
- **One-line beats multi-line.** Prefer `// short rationale` over a block comment when a single line carries it.
- **Don't re-state the obvious.** No `// increment i`, `// get the size`, `// default constructor`. No boilerplate headers repeating the file name or what the class is called.
- **No Chinese/English duplication.** Comments are written once, in whichever language the surrounding code uses. Do not add a second comment translating the first.
- **Doc comments are the one exception.** Public API declarations (`/** ... */` in headers) may carry fuller doc — that's the surface, not the implementation. Keep even those tight: signature intent, invariants, ownership, thread-safety, not prose.

## Where the detail goes instead

- Design rationale, trade-offs, and architecture notes → `docs/` (e.g. `docs/render/render_module.md`), following the existing per-module doc convention.
- "What I changed and why" → the commit message.
- Non-obvious decisions that must live near the code → one short line pointing at the doc: `// lifecycle: see docs/asset/asset_module.md`.

## Example

```cpp
// Bad — narrates the code, 6 lines of what
// This loop iterates over all shader artifacts in the program and,
// for each one, checks whether it is already present in the cache.
// If the artifact is not found in the cache, we then compile it,
// and afterwards insert the freshly compiled artifact back into
// the cache so that future requests can hit the content-addressable
// lookup instead of recompiling. This prevents duplicate work.
for (auto& art : program->artifacts()) {
    if (!cache.contains(art.key())) {
        cache.insert(art.key(), compiler.compile(art));
    }
}
```

```cpp
// Good — one line of why
// Content-addressed: compile only on first miss, never twice.
for (auto& art : program->artifacts()) {
    if (!cache.contains(art.key())) {
        cache.insert(art.key(), compiler.compile(art));
    }
}
```
