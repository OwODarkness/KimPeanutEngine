---
name: engine-reference
description: Study how another open-source game engine (Sakura, Piccolo, bgfx, Godot, ...) solves a design problem — RHI, render graph, asset pipeline, shader cache, ECS, editor — by reading its actual source on GitHub via the MCP, then map the pattern back to KimPeanutEngine. Invoke by saying "how does <engine> do <X>" or "/engine-reference".
---

# Engine design reference

When deciding how a subsystem should work — or reconstructing one — don't design in a vacuum. This repo's architecture (Editor → Engine → Resource → RHI) descends from Piccolo/Unreal, and several open-source engines have solved the same problems in production. Read their real code, extract the pattern, and map it back here.

This is **design study**, distinct from `third-party-issue` (which is about *using* a vendored library's API). Use this when the question is "how should *our* module be shaped".

## Step 1 — Pin the question to a subsystem

Name the design question, then map it to this repo's module and a reference engine:

| Question (this repo's concern) | Reference engine | Repo (branch) | Search anchor |
|---|---|---|---|
| RHI backend abstraction, render graph | **Sakura Engine** | `SakuraEngine/SakuraEngine` (`engine`) | `path:engine/modules/render` |
| ECS, reflection, core containers | **Sakura Engine** | `SakuraEngine/SakuraEngine` (`engine`) | `path:engine/modules/core` |
| Editor / tool UI | **Sakura Engine** | `SakuraEngine/SakuraEngine` (`engine`) | `path:engine/modules/editor` |
| Asset → RHI pipeline (the closest lineage) | **Piccolo** (games104) | `BoomingTech/Piccolo` (`main`) | `path:engine/source/runtime` |
| Cross-API backend (the RHI contract itself) | **bgfx** | `bkaradzic/bgfx` (`master`) | `RendererType` / `backend/` |
| Production-scale servers, shader cache | **Godot** | `godotengine/godot` (`master`) | `path:servers/rendering` |
| RenderSystem / shader-manager abstractions | **Ogre** | `Ogre3D/ogre` (`master`) | `HighLevelGpuProgramManager` |
| Compact C++ engine — graphics + resource cache | **Urho3D** | `urho3d/urho3d` (`master`) | `Graphics` / `ResourceCache` |
| ECS / scene as a different paradigm | **Bevy** | `bevyengine/bevy` (`master`) | `path:src` |

Notes:
- **Sakura and Piccolo are the two closest references** — both are C++ engines whose module boundaries resemble this repo's layering. Sakura additionally ships `agent_docs/` and a `CLAUDE.md` in its repo; start there when they exist, they describe intent faster than code.
- Unreal itself is not public on GitHub (needs an Epic grant); Piccolo is the Unreal-inspired open-source stand-in.
- When the question touches the render module or the RHI (the current reconstruction), Sakura's `engine/modules/render` and bgfx are the first stops.

## Step 2 — Read the reference code

1. `mcp__github__search_code`, always scoped to the repo, else all-of-GitHub noise:
   ```
   q: repo:SakuraEngine/SakuraEngine RenderGraph language:C++ path:engine/modules/render
   q: repo:BoomingTech/Piccolo "RHI" language:C++ path:engine/source/runtime
   ```
   GitHub **code search is the most rate-limited MCP tool** — budget ≤3 queries per question. The `path:` qualifier narrows to a module and buys precision.
2. The snippets in results are cheap and usually enough to identify the right file. Read whole files only for the top 1–2 hits, via `mcp__github__get_file_contents`:
   - **Prefer headers (`.h`) for class shape**; read a `.cpp` only for one specific behavior.
   - Stop when the pattern is clear — you're extracting a design, not reading an engine.

## Step 3 — Synthesize

Report in two parts, tightly:

1. **How X does it** — 3–6 lines, citing the repo-relative path (+ line when precise). e.g. "Sakura splits render into a frame graph (`engine/modules/render/...`) with a `RenderBackend` seam, so API-specific code never leaks into the graph."
2. **What it suggests for KimPeanutEngine** — the transferable idea *or* the trap to avoid, tied to an actual module. Tie it to the current state, e.g. the RHI leaks in `CLAUDE.md` or `ResourcePipeline::ProcessShader` having no callers yet.

## Guardrails

- **Study, not import.** Copying another engine's source wholesale brings their license and their design constraints in; abstract the idea and cite the source.
- **Rate limits.** `search_code` is the tightest budget — once you know a path, prefer `get_file_contents` (fewer, bigger calls) over repeated searches.
- **Default branch ≠ stable release.** You're reading tip; note it, and don't assume a pattern matches the release the user knows.
- **We don't open issues/PRs in reference repos** unless the user asks.
- **MCP unavailable** → fall back to local vendored code or `docs/`, and say so.
