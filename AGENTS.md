# KimPeanutEngine Agent Guide

This is the shared project contract for Codex, Claude Code, and other coding agents working in KimPeanutEngine. User instructions take precedence. Keep durable subsystem design in `docs/`, execution records in `.spec/`, and tool-specific behavior in native skill/config directories.

## Project map

KimPeanutEngine is a C++17 engine focused on rendering and infrastructure:

```text
Editor → Runtime → Asset / Resource → Render → Graphics RHI → OpenGL / Vulkan
```

- `engine/runtime/core` — common types, math, config, logging, async, and resources
- `engine/runtime/asset` — asset identity, loading, ownership, and dependencies
- `engine/runtime/graphics` — API-neutral graphics contracts and backends
- `engine/runtime/render` — render policy, materials, passes, frame data
- `engine/editor` — editor shell and ImGui tools
- `engine/test/unit` — GoogleTest unit and contract tests
- `docs/status.md` — current project state and milestone ledger
- `docs/dead_code.md` — intentionally inactive code; do not repair it as live code

## Start of task

For implementation work:

1. Inspect `git status --short`, `docs/status.md`, and the affected module docs.
2. Define the design boundary, concrete question, and acceptance criteria before non-trivial edits.
3. Read `README.md` when the subsystem or project convention is unfamiliar.
4. Follow [the validation matrix](docs/validation_matrix.md) and preserve unrelated user changes.
5. Review the final diff for accidental ownership, dependency, or API changes.

Do not start a broad refactor from a feature checklist alone. Diagnose first, then make the smallest coherent change.

## Shared skills

Shared user-global skills use this portable layout:

- Canonical source: `%USERPROFILE%\.agents\skills\<name>\SKILL.md`
- Codex discovery root: `%USERPROFILE%\.codex\skills\<name>`
- Claude Code discovery root: `%USERPROFILE%\.claude\skills\<name>`

The native entries are Windows directory junctions to the canonical source. The global setup is machine-local and optional; if a shared skill is missing, continue with the project contract and report the limitation rather than blocking. Keep tool-specific skills in the corresponding native directory. Do not edit a shared skill through a junction path.

## Architecture boundaries

- Asset owns asset identity, loading, dependency tracking, and CPU-side asset lifetime. Render must not load arbitrary files.
- Resource processing converts CPU asset data into render-ready artifacts; it does not own GPU objects.
- Render owns scene policy, pass scheduling, materials, pipeline descriptions, and frame-local data.
- Graphics/RHI owns GPU resources, API execution, synchronization, and backend translation.
- Common render/RHI contracts must not expose Vulkan or OpenGL implementation types.
- Backend-specific code stays below the common graphics contract.
- Runtime must not depend on Editor. The existing RuntimeLib ↔ EditorLib CMake cycle is known; do not expand it.
- Every GPU resource has one documented owner and is released only after submitted work is safe.
- Add abstractions only when a current data flow and consumer justify them.

## Design references

Use `docs/engine-reference/README.md` and the global `engine-reference` skill for cross-engine design questions. For complex architectural work, inspect a small amount of actual source from relevant reference repositories before finalizing the plan. Compare ownership, lifetime, data flow, synchronization, and performance assumptions; do not copy source. If repository access fails, record that limitation and use the local studies.

Relevant studies include Sakura (RHI/render graph), Piccolo (asset/resource layering), bgfx (cross-API contracts), and gkNextEngine (Vulkan-first submission and validation).

## Build and validation

Use `tools/kp.ps1` for targeted validation when possible. The standard fallback is:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

Do not start concurrent CMake/MSBuild builds. If compilation is blocked by the environment, report the first environment error separately from source failures.

For runtime rendering changes, compilation is insufficient: use the Runtime command registry and a checked-in startup fixture. Launch `build/Debug/KimPeanutEngine.exe --graphics-api <api> --startup-level <fixture> --agent-port 37373`, send JSON-lines to `127.0.0.1:37373`, and use `capture.screenshot` followed by `poll`. Captures must stay under `save/screenshots/validation/`, end in `.png`, and preserve captures from other tasks. Do not access backend objects or scrape the Editor console. `KimPeanutCommand` alone does not bootstrap Render.

## Documentation ownership

- Use the `modular-documentation` skill before creating, moving, splitting, or substantially reorganizing module documentation.
- `PLANS.md` describes durable module architecture; `TODO.md` is the roadmap and acceptance ledger.
- Stage designs belong in a module's `.plan/` directory.
- `.spec/specs/*.md` records objectives, scope, invariants, stages, acceptance, and validation.
- `.spec/journal/*.md` records dated investigation, implementation, validation, corrections, skipped checks, and remaining risks.
- `docs/status.md` summarizes current state and links to the detailed roadmap or journal. Do not turn it into an execution log.
- For large, paused, or risky work, follow [.spec/README.md](.spec/README.md) and keep the roadmap, spec, and journal separate.

## Completion report

Every implementation response must state:

1. What changed and why.
2. Files changed.
3. Architecture or ownership impact.
4. Validation commands and results.
5. Known blockers, unverified paths, and follow-up work.

Follow [the completion evidence template](docs/agent_completion_evidence.md). Do not claim runtime, resource-lifetime, or visual work is complete from compilation alone.

## Repository hygiene

- Do not modify vendored code under `third_party/` unless explicitly requested.
- Do not commit build directories, binaries, logs, or temporary captures.
- Runtime text logs are generated under `save/logs/`; keep them out of commits.
- Do not use destructive Git commands without explicit user instruction.
- Preserve unrelated uncommitted changes.
- Use conventional commit subjects: `<type>(<scope>): <imperative description>`.
- Keep source comments short and explain non-obvious reasons; put extended rationale in `docs/`.
