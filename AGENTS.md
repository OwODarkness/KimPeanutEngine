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

For complex design work, reference-repository discovery is a required planning gate. After inspecting the local architecture and defining the concrete design problem, search the existing reference studies and use the available GitHub MCP read/search tools to identify and inspect a small number of relevant open-source repositories before finalizing the implementation plan. Examine actual source, documentation, or history; do not rely on memory. Compare ownership, lifetime, data flow, synchronization, performance assumptions, and tradeoffs against KimPeanutEngine. Record why a reference applies or does not apply. If GitHub access fails or no relevant repository exists, record that limitation and proceed using the strongest available local or authoritative evidence.

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

## Render-debug capture for agents

Use the Runtime command registry to capture rendered output; do not access
backend objects or scrape the Editor console. Select a checked-in startup
fixture at launch with `--startup-level` and enable the loopback transport with
`--agent-port 37373`, for example:

`build/Debug/KimPeanutEngine.exe --graphics-api vulkan --startup-level level/point_shadow_validation.level --agent-port 37373`

The selector is launch-scoped and does not modify `config/bootstrap.json`; omit
it to use the validated Bootstrap default. Then send JSON-lines to
`127.0.0.1:37373`, for example
`{"op":"execute","command":"capture.screenshot","arguments":{"path":"save/screenshots/validation/agent-debug.png","view":"scene_color"}}`.
Poll the returned `request_id` with `{"op":"poll","request_id":<id>}` until
terminal. On success, inspect `data.output_path` to debug the PNG. Paths must
stay under `save/screenshots/validation/` and end in `.png`; preserve captures
created by other tasks. `KimPeanutCommand` tests the headless protocol but does
not bootstrap Render, so real capture requires the Runtime host and its
explicitly enabled local transport.

## Change workflow

For a non-trivial change:

1. Inspect `git status`, `docs/status.md`, and the affected module docs.
2. State the design boundary, concrete design question, and acceptance criteria before editing.
3. If the change is architectural or otherwise complex, complete the reference-repository discovery gate above before finalizing the plan.
4. Make the smallest coherent change; preserve unrelated user work.
5. Run impact-appropriate build/tests and inspect the first failure.
6. Update status/design documentation when behavior or ownership changes.
7. Review the diff for accidental dependency, ownership, or API changes.

Do not begin a broad refactor from a feature checklist alone. Define the current problem, invariant, migration stages, and validation evidence first.

For large, multi-session, paused, or risky work, follow the [spec and journal workflow](.spec/README.md). Keep the roadmap in the relevant `docs/**/TODO.md`; use a spec for the intended plan and a journal for factual progress, validation, and remaining risk.

### Documentation ownership

#### Mandatory modular documentation workflow

- For any task that creates, moves, splits, or substantially reorganizes
  module or submodule documentation, agents **must use the
  `modular-documentation` skill** before editing. This includes Render and its
  submodules, even when the immediate request mentions only a TODO, plan, or
  journal.
- Follow that skill's canonical structure and ownership rules. Do not invent a
  parallel layout or combine architecture, actionable work, agent rules, and
  execution history into one document.
- The skill is not required for a small correction to an existing standalone
  note, generated documentation, or a one-off prose typo. If the skill is
  unavailable for a task that needs it, stop before restructuring and report the
  missing workflow dependency.

- A `TODO.md` is a roadmap and acceptance ledger: goals, ordered work items,
  checkboxes, concise landed markers, and links to design/spec/journal
  documents. It must not become a chronological implementation log.
- A design or plan document records durable architecture and policy decisions;
  it is not a substitute for execution evidence.
- `.spec/specs/*.md` records the objective, scope, invariants, stages,
  acceptance criteria, and validation plan for a substantial task.
- `.spec/journal/*.md` is the factual execution record: dated changes,
  investigations, validation commands/results, reference gates, corrections,
  skipped checks, and remaining risks. Put detailed landed notes here.
- When a TODO starts accumulating dated implementation narratives, move those
  narratives into the matching `.spec/journal/` file, then leave only the
  checklist/status and a journal link in the TODO. Do not duplicate the same
  landing report in both files.
- A modular subsystem should keep `AGENTS.md`, `PLANS.md`, and `TODO.md` at its
  module root. `PLANS.md` describes module architecture and links its
  submodules; `TODO.md` indexes current work; `AGENTS.md` defines local agent
  usage and boundaries. A substantial submodule follows the same pattern.
- Concrete stage designs belong in the module's hidden `.plan/` directory,
  using names such as `D1.md`, `D2.md`, or the submodule's stage prefix. Link
  those files from `PLANS.md`; do not turn the architecture plan into a
  stage-by-stage implementation dump.
- `docs/status.md` should summarize the current project state and link to the
  roadmap or journal when more detail is needed; it must not become a second
  execution journal.

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
