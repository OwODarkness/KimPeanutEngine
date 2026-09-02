# RenderSystem R1.2 Deferred Renderer Extraction

- Status: complete
- Owner: project maintainers / implementing agent
- Parent TODO: [Render R1.2](../../docs/render/TODO.md)
- Stage design: [R1.2](../../docs/render/.plan/R1.2.md)

## Objective

Extract the live deferred-PBR renderer and all pass-owned state from
`RenderSystem` into one concrete `DeferredRenderer`, preserving public behavior,
frame order, output, cross-backend contracts, and the R1.1 lifecycle baseline.

The authoritative ownership, interfaces, migration stages, reference
comparison, and rejected alternatives live in the linked stage design. This
spec is the execution and acceptance contract; it must not become a duplicate
architecture document or implementation journal.

## Current state

- R1.1 transactional lifecycle and direct `RenderSystem` characterization
  tests are complete.
- R1.2 extraction and review fixes are complete. `RenderSystem` retains the
  lifecycle/frame facade while `DeferredRenderer` owns named targets, schedule,
  pass recording/preparation, shadows, environment state, capture conversion,
  pass-private handles, and their cleanup.
- GP7 has removed legacy bootstrap scene/source transfer, but Render still has
  transitional synchronous Asset/Resource work that is explicitly deferred to
  R1.4.

## Scope

- Add the concrete renderer collaborator and transactionally initialize it.
- Move target/extent, schedule, shadow, environment, pass recording, capture
  conversion, and pass-private GPU state into that owner.
- Route a small immutable frame input and active `FrameContext` from the facade.
- Delegate SceneColor view, extent requests, capture target resolution, and
  pass recording without changing their public capabilities.
- Consolidate renderer cleanup before resolver and backend teardown.
- Add direct ownership/failure tests and preserve orchestration tests.
- Produce full cross-backend runtime and visual evidence before marking R1.2
  complete.

## Non-goals

- R1.3 fixed-sequence declaration/execution unification.
- R1.4 resource-ingestion and synchronous loading removal.
- Render graph, pass/plugin hierarchy, generic context or handle framework,
  shader/material/light/capture feature changes, or Graphics API changes.

## Invariants

- `RenderSystem` remains the only Runtime-facing facade and backend-frame owner.
- `DeferredRenderer` owns renderer policy/state but never owns Runtime,
  registries/worlds, MaterialSystem, ResourcePipeline, resolver, backend, or
  `FrameContext` slots.
- Renderer-owned handles are retired once before borrowed services and backend
  teardown; resolver-owned environment bindings are never double-destroyed.
- Manual pass order, target formats, shader ABI, descriptors, shadow budgets,
  output, and public APIs remain unchanged.
- Common Render/Graphics contracts expose no Vulkan/OpenGL implementation type.

## Stages

1. Strengthen backend-probe ownership/destruction assertions.
2. Introduce `DeferredRenderer`; move targets, extent, and schedule.
3. Move environment and shadow policy/state using immutable frame inputs.
4. Move pass preparation/recording and capture conversion without behavior
   changes.
5. Consolidate cleanup and remove moved facade members/methods/includes.
6. Run focused, full, smoke, and visual validation; write the factual R1.2
   journal and update status/roadmap.

Each stage maps directly to R1.2.A–R1.2.F in the stage design. If an
implementation needs to change the chosen owner, public facade, frame order,
or R1.3/R1.4 boundary, update and review the stage design before continuing.

## Acceptance criteria

- [x] The facade contains no pass-specific handles/state or pass
  recording/preparation methods.
- [x] The renderer contains all moved state and has transactional initialization
  plus idempotent cleanup.
- [x] Existing public call sites compile without capability broadening.
- [x] Normal, failure, resize, capture, editor, and shutdown behavior matches
  the R1.1 baseline.
- [x] Created renderer-owned handles are destroyed exactly once before backend
  cleanup; borrowed handles are not destroyed by the renderer.
- [x] Focused Render tests, full Debug build, and complete CTest pass.
- [x] Vulkan/OpenGL smoke passes and fresh deterministic captures are inspected.
- [x] A journal records exact commands/results, skipped evidence, and remaining
  R1.3/R1.4 risks.

## Validation plan

Selected level: Levels 2–4. This stage changes Render private/public headers,
CMake source wiring, renderer/RHI ownership, target lifetime, and every runtime
pass-recording path.

During implementation, use targeted checks after each stage:

```powershell
.\tools\kp.ps1 build RenderSystemTest
.\tools\kp.ps1 test RenderSystemTest
.\tools\kp.ps1 build RenderPassScheduleTest
.\tools\kp.ps1 test RenderPassScheduleTest
```

Before completion, run sequentially:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\tools\kp.ps1 smoke
```

Then use the checked-in PBR, directional-shadow, spot-shadow, and point-shadow
startup levels with the Runtime command transport to capture and inspect
SceneColor and the relevant semantic shadow views on Vulkan and OpenGL. Preserve
captures from unrelated tasks.

## Risks and open questions

- A mechanical move can accidentally change pass ordering, target selection,
  descriptor bindings, or lazy resource timing; characterization and visual
  evidence are mandatory.
- Environment binding handles are resolver-owned even though renderer state
  groups them; cleanup must distinguish logical binding state from GPU
  ownership.
- The C++17 shadow-validity capability must be non-owning and frame-scoped.
- Synchronous pass shader loading remains intentional R1.4 debt and must not be
  normalized as the final renderer interface.
