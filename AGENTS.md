# KimPeanutEngine Agent Guide

This file is the shared project contract for coding agents working in KimPeanutEngine, including Codex and Claude Code. User instructions take precedence over this file. Detailed subsystem design belongs in `docs/`; tool-specific commands and skills belong in `.claude/` or `.codex/`.

## Project overview

KimPeanutEngine is a C++17 game engine focused on rendering and engine infrastructure.

```text
Editor → Runtime → Asset / Resource → Render → Graphics RHI → OpenGL / Vulkan
```

Important modules:

- `engine/runtime/core` — common types, math, config, logging, async, resources
- `engine/runtime/asset` — asset identity, loading, ownership, and dependency tracking
- `engine/runtime/graphics` — API-neutral graphics contracts and OpenGL/Vulkan backends
- `engine/runtime/render` — render policy, materials, render world, passes, frame data
- `engine/editor` — editor shell and ImGui tools
- `engine/test/unit` — GoogleTest unit and contract tests
- `docs/status.md` — current project state and milestone ledger
- `docs/dead_code.md` — code that is intentionally not part of the live build

Read `README.md`, `docs/status.md`, and the module document relevant to the change before editing. Treat `docs/status.md` as the current state unless code or the user explicitly shows it is stale.

## Architecture invariants

- Asset loading owns asset identity and CPU-side asset lifetime; render code does not load arbitrary files directly.
- Resource processing converts CPU asset data into render-ready artifacts; it does not own GPU objects.
- Render owns scene policy, pass scheduling, materials, pipeline descriptions, and frame-local render data.
- Graphics/RHI owns GPU resources, API execution, synchronization, and backend translation.
- Common render and RHI contracts must not expose Vulkan or OpenGL implementation types.
- Backend-specific code belongs below the common graphics contract.
- Runtime must not depend on Editor. This is the target boundary; the current CMake RuntimeLib ↔ EditorLib cycle is a known issue and must not be expanded.
- GPU resources have one documented owner and are released only after submitted GPU work is safe.
- Do not add an abstraction only because another engine has one; identify its current data flow and consumer first.

## Reference-engine studies

Use `docs/engine-reference/README.md` when a design question needs comparison with another engine. The `.claude/skills/engine-reference/SKILL.md` file contains only the Claude workflow for conducting a study. Study patterns and map them to KimPeanutEngine; do not copy source.

- Sakura — RHI, render graph, and backend-independent renderer shape
- Piccolo — asset/resource/runtime layering
- bgfx — cross-API contract design
- gkNextEngine — Vulkan-first runtime rendering, modern GPU submission, complete render workflows, and validation

Relevant conclusions are indexed under `docs/engine-reference/` and recorded in the appropriate design document under `docs/graphics/` or another module directory.

## Build and test

The project uses CMake with the Visual Studio 17 2022 generator and builds into `build/`. Use `tools/kp.ps1` as the standard command wrapper for targeted validation; use raw CMake/CTest commands when diagnosing the wrapper or a build-system failure.

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

Follow the [validation matrix](docs/validation_matrix.md). It maps changed paths to the minimum build, unit, runtime-smoke, or full-integration evidence required. Do not start concurrent CMake/MSBuild builds. If the environment prevents compilation, report the first environment error and distinguish it from source failures.

## Change workflow

For a non-trivial change:

1. Inspect `git status`, `docs/status.md`, and the affected module docs.
2. State the design boundary and acceptance criteria before editing.
3. Make the smallest coherent change; preserve unrelated user work.
4. Run impact-appropriate build/tests and inspect the first failure.
5. Update status/design documentation when behavior or ownership changes.
6. Review the diff for accidental dependency, ownership, or API changes.

Do not begin a broad refactor from a feature checklist alone. Define the current problem, invariant, migration stages, and validation evidence first.

For large, multi-session, paused, or risky work, follow the [spec and journal workflow](.spec/README.md). Keep the roadmap in `docs/graphics/TODO.md`; use a spec for the intended plan and a journal for factual progress, validation, and remaining risk.

## Completion report

Follow the [agent completion evidence template](docs/agent_completion_evidence.md). Every implementation task should report:

1. What changed and why.
2. Files changed.
3. Architecture or ownership impact.
4. Validation commands and results.
5. Known blockers, unverified paths, and follow-up work.

Never claim a task is complete based only on compilation if the task changes runtime rendering, resource lifetime, or visual output.

## Repository hygiene

- Do not modify vendored code under `third_party/` unless explicitly requested.
- Do not commit build directories, generated binaries, logs, or temporary captures.
- Do not use destructive Git commands without explicit user instruction.
- Preserve existing uncommitted changes unless the user asks for cleanup or replacement.
- Keep comments short and explain non-obvious reasons; put extended rationale in `docs/`.
