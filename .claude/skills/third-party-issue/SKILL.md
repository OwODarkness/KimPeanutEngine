---
name: third-party-issue
description: Troubleshoot a third-party library usage problem (imgui, assimp, glfw, sol2, ...) by querying the library's GitHub issue tracker first via the GitHub MCP, and only falling back to reading the vendored source when issues don't resolve it. Invoke by saying "third-party issue" or "/third-party-issue".
---

# Third-party usage issue

When something in a vendored library (under `third_party/`) misbehaves — an ImGui call draws/clips wrong, an assimp loader fails, a sol2 call errors — don't start reading the vendored source. **The library's GitHub issue tracker is almost always faster**: these are mature projects, the exact gotcha is usually already reported, and the resolution lives in the issue thread. Read issues first; read source only if the tracker doesn't answer.

## Step 0 — Pin down the target

1. Which library? Map the name to its upstream repo (table below).
2. Which symbol/behavior? Name the exact API involved (`ImGui::PushClipRect`, `sol::safe_script`, `aiImportFile`, ...) and the symptom ("button invisible", "clip", "throws", "crashes").
3. Check the vendored version — it anchors the search. e.g. `grep IMGUI_VERSION third_party/imgui/imgui.h`, `sol2` version in its header, etc.

| `third_party/` | GitHub repo |
|---|---|
| imgui | `ocornut/imgui` |
| assimp | `assimp/assimp` |
| glfw | `glfw/glfw` |
| googletest | `google/googletest` |
| httplib | `yhirose/cpp-httplib` |
| lua | `lua/lua` |
| magic_enum | `Neargye/magic_enum` |
| meshoptimizer | `zeux/meshoptimizer` |
| miniaudio | `mackron/miniaudio` |
| nlohmann | `nlohmann/json` |
| openssl | `openssl/openssl` |
| sol2 | `ThePhD/sol2` |
| stb_image | `nothings/stb` |
| glad / vulkan | SDK tooling — usually source-consult, not issue-tracker |

## Step 1 — Query GitHub issues first (via the MCP)

Use `mcp__github__search_issues`. The `q` field accepts GitHub's search syntax — scope it to the repo so results are on-point, and let `state:closed` surface threads that already contain a fix:

```
q: repo:ocornut/imgui "PushClipRect" "title bar" state:closed
q: repo:ocornut/imgui "button" "not visible" in:title is:issue
```

**Be token-lean — GitHub API responses are heavy.** Every `search_issues` hit carries the full issue body plus a ~25-field user object, so a single call can cost more than several source reads. The goal is **one precise query**, not a sweep.

Query strategy — **1–2 queries max, then decide**:

1. **One title-scoped query first** (highest precision per token): `repo:<lib> "<APIName>" <symptom> in:title` with `per_page` 3–5.
2. **Refine once only if empty or off-target** — drop the symbol, keep the symptom: `repo:<lib> <symptom> in:title`; append the version string from Step 0 if relevant; add `state:closed` to target already-resolved threads.
3. **Stop at the first high-precision hit.** If its title/body clearly matches the symptom, read that one and conclude — don't keep searching to "validate". Only search again if the hit is ambiguous.

Sort by `comments` and keep `per_page` at 3–5. Open the single best hit with `mcp__github__get_issue` and read for the resolution:

- **Prefer closed issues** — a close usually means "resolved"; the closing comment or a linked PR is the fix.
- Look for the **workaround / cause**, not just "me too". A maintainer's "this is by design, do X" is an answer.
- Note the library version the reporter used — if far older/newer than yours, the behavior may have changed.

## Step 2 — If issues don't resolve it, read the vendored source

Only after the tracker fails to answer:

1. Locate the vendored code: `Grep` the symbol under `third_party/<lib>/` (also search the whole tree if it's re-exported, e.g. `sol2` headers are included from vendored paths).
2. Read the implementation around the call — especially invariants the caller must uphold (clip rects, mutex, ownership, thread-affinity).
3. For headers-only libs (sol2, stb, httplib, magic_enum, nlohmann) the "source" is the header itself; read the relevant section, don't skim the whole file.

Optional intermediate (only if both above stall): `mcp__github__search_code` on the library repo for how other projects use the symbol — but issues → source is the primary spine.

## Step 3 — Report and fix

- Say **what the issue tracker said** (link the issue), then what you concluded. Be honest if the tracker had nothing relevant and you fell through to source.
- If the fix is a known workaround, apply it; if it's a version gap, flag it (the vendored copy may need bumping).
- **Don't re-read the vendored source just to confirm a tracker answer** — only go to source when verifying a version/caveat concern (e.g. an issue filed against an older release than the vendored copy).
- Build to verify (`/build`).

## Guardrails

- **Don't spam the API.** 1–2 targeted queries, then decide. Broad "everything about imgui" searches waste tokens and time.
- **This is authorized support, not a request to fix upstream.** We consume the tracker; we don't open issues/PRs unless the user asks.
- **The vendored copy may differ from tip.** A fix in a newer release may not apply without a version bump — say so instead of backporting blindly.
- If the MCP is unavailable, fall back straight to source and note it.
