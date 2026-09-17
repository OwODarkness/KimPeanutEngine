# Render Graph R3.4b Authored Declaration and Dual-Path Parity Journal

- Stage: [R3.4](../../docs/render/.plan/R3.md)
- Review: [R3.4 review](../../docs/render/.review/R3.4.md)
- Date: 2026-09-17

## Scope

Collapsed the duplicated eight-pass declaration into one authored source, then
added a per-frame dry comparison between the fixed execution oracle and the
compiled plan. The fixed path stays authoritative: no command recording, no
resource binding, and no pass ordering changed in this slice.

## Changes

- `render_pass_declaration.{h,cpp}` owns the authored entries and derives both
  products: `CreateFixedRenderPassSequence()` and
  `CompileRenderFrameGraph(conditions)`. `DeferredRenderer::ConfigurePassSequence`
  and the compatibility test now consume it instead of restating the table, so
  the two cannot drift.
- The Editor terminal is compiled for every condition set, because whether it
  runs is not known when the frame declares. The compatibility proof's
  expectation changed accordingly: the planned set is what the oracle does not
  condition-skip, not what it executes.
- `render_frame_parity.{h,cpp}` replays the oracle's own per-pass verdicts
  through a `RenderGraphFrame` and compares order, outcomes, required failure,
  and the finalize verdict. Replaying rather than re-recording is what keeps the
  comparison free of duplicate GPU work.
- A culled conditional pass reports `NotInPlan` while the oracle reports
  `SkippedCondition`; the comparator maps those two states explicitly.
- The check runs once per frame in `FinalizeFrame`, where the oracle's ledger is
  complete, including whether the terminal ran or was skipped. Plans are cached
  per condition set, so each variant compiles once rather than per frame.
- `graph_parity_checks` and `graph_parity_mismatches` were added to the profile
  snapshot. `RenderSystem::EndFrame` refreshes them after finalize, because the
  snapshot is copied from the renderer in `BeginFrame`, before the comparison
  runs. A one-shot info line marks the first comparison so a runtime log proves
  the dual path is live rather than merely silent.

## Validation evidence

```text
cmake --build build --config Debug
  0 errors, 0 warnings

ctest --test-dir build -C Debug
  969/970 passed; the one failure is the pre-existing LevelLoaderTest
  asset-content failure (see below)

ctest -R "RenderGraph|RenderFrameParity|FixedRenderPass"
  35/35 passed

ctest -R "RenderSystemLifecycleTest.ComparesTheCompiledPlanWithTheFixedOracleEveryFrame"
  passed - one frame with the terminal, one frame with a capture request
  selecting the other variant; graph_parity_checks == 1 and
  graph_parity_mismatches == 0 on both

GraphicsSmoke (Vulkan + OpenGL, 6 frames each)
  "Graphics smoke (6 frames/API): passed", exit 0

KimPeanutEngine --graphics-api vulkan --startup-level level/sponza.level
  "Render graph parity check active: fixed oracle versus compiled plan"
  0 parity-mismatch lines; profile completed (scenario=sponza-stage-0)

KimPeanutEngine --graphics-api opengl --startup-level level/sponza.level
  parity check active, 0 parity-mismatch lines, profile completed
  cpu_p50_ms=22.186 cpu_p95_ms=26.665 draws=288
```

## Measured cost and its limits

The per-frame comparison performs no GPU work and no recording. It replays eight
passes and allocates a small outcome vector plus two order vectors per frame.

No numeric R3.0 baseline is recorded in the repository, so the Sponza figures
above cannot be compared against a stored comparator, and this slice does not
claim a no-regression result. The plan assigns that measurement to R3.4c, where
the graph becomes the only scheduler and build/compile/execute timings are
required to be visible.

## Not performed by design

- The fixed frame is still the only path that records commands; the graph frame
  never records.
- No graph-directed execution, no removal of the fixed path.
- No resource, barrier, transient, subresource, or backend change.

## Remaining risk

The comparison proves scheduling and bookkeeping parity, not that the graph
executor would invoke the callbacks identically: the planner is only ever
replayed, never driving real recording. R3.4c is what exercises that, and it
must rerun the same fixtures plus captures on both APIs before the fixed path is
removed. The per-frame allocations in the comparator should be removed or
measured when the scaffolding is retired.

## Pre-existing failure

`LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures` fails because
`asset/level/pbr_showcase.level` references the logical model
`model/rock1-bl/rock2`, which the asset archive does not contain. The test links
`AssetRuntime`, not `Render`, and the fixture is unmodified, so this is a
content/bake problem outside this stage.
