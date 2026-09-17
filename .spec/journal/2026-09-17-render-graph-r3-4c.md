# Render Graph R3.4c Execution Switch Journal

- Stage: [R3.4](../../docs/render/.plan/R3.md)
- Review: [R3.4 review](../../docs/render/.review/R3.4.md)
- Date: 2026-09-17

## Scope

Made the compiled plan the only pass scheduler, removed the fixed sequence and
its executor, and removed the parity scaffolding that existed to justify the
switch. No backend, resource-state, or ownership change: passes still record
through the same `ExecutePass` callbacks against the same persistent targets.

## Changes

- `DeferredRenderer` now compiles one plan per frame-start condition set during
  `Initialize` and executes the frame from it through `RenderGraphFrame`. The
  renderer sweep dispatches on the compiled pass's caller-owned key, which is
  the `FixedRenderPassId` the authored declaration assigned; an unkeyed pass
  cannot be dispatched and is reported as a failed visit rather than a
  successful one.
- Removed `FixedRenderPassSequence`, `FixedRenderPassFrame`, `RenderPassOutcome`,
  `render_pass.{h,cpp}`, `render_frame_parity.{h,cpp}`, and the two test files
  that covered them. The pass vocabulary the declaration needs moved into
  `render_pass_declaration.h`, so Render now has one pass-declaration header
  instead of a fixed model plus a parallel graph description.
- Replaced the parity counters with graph cost: `graph_compile_ms` (one-time per
  variant) and `cpu_graph_execute_ms`, aggregated as the `graph` CPU subphase.
  Both appear in the profile-complete log line, which is what the acceptance
  ledger requires to be observable.
- The compatibility proof no longer consults an oracle. It asserts the authored
  declaration is canonical and that each condition set compiles to the expected
  planned passes with correct keys, SSA versions, and a terminal last.

## Validation evidence

```text
cmake --build build --config Debug
  0 errors, 0 warnings

ctest --test-dir build -C Debug
  950/951 passed; the one failure is the pre-existing LevelLoaderTest
  asset-content failure (see below)

ctest -R "RenderGraph|RenderSystem|DeferredRenderer|RenderPassSchedule"
  36/36 passed

GraphicsSmoke (Vulkan + OpenGL, 6 frames each)
  "Graphics smoke (6 frames/API): passed", exit 0
```

Runtime fixtures, `level/sponza.level`, agent port 37373, both APIs. Each run
captured `scene_color` and the `linear_depth` conversion view, which is the path
that exercises the culled-versus-planned distinction in the compiled plan:

```text
Vulkan  captures: r34c-vulkan-scene-color-20260917.png     1094x619 RGBA
                  r34c-vulkan-linear-depth-20260917.png    1094x619 RGBA
        profile:  cpu_p50=17.788ms cpu_p95=25.777ms graph_p95=0.560ms
                  graph_compile_ms=0.132 draws=288 api=Vulkan

OpenGL  captures: r34c-opengl-scene-color-20260917.png     1094x619 RGBA
                  r34c-opengl-linear-depth-20260917.png    1094x619 RGBA
        profile:  cpu_p50=21.005ms cpu_p95=23.374ms graph_p95=0.580ms
                  graph_compile_ms=0.131 draws=288 api=OpenGL
```

All four captures decode as valid 8-bit RGBA PNGs at the fixture's 1094x619
viewport with non-trivial content, so none is a blank or single-colour frame.

Two independent comparisons were made by decoding the PNGs and diffing pixels:

- **Run-to-run determinism.** Re-running the same fixture on the same API
  produced frames identical to the previous run's, on both Vulkan and OpenGL:
  0 of 677186 pixels differ, maximum channel delta 0.
- **Cross-API parity.** Vulkan versus OpenGL on the same fixture and camera:
  the scene colour differs on 885 of 677186 pixels (0.131%), and the
  `linear_depth` views are pixel-identical apart from a maximum channel delta
  of 1.

## Cost

The compiled-plan sweep costs 0.560 ms p95 on Vulkan and 0.580 ms p95 on OpenGL
against 25.777 ms and 23.374 ms frames, so it is roughly two percent of the
CPU-bound frame. Compilation is 0.13 ms once per condition variant and is not
paid per frame.

## Not performed by design

- No resource-state, barrier, transient, subresource, or aliasing work; passes
  keep recording through the same backend-private target bracket.
- No removal of the shadow-recorded latches or other undeclared cross-pass
  state; execution order is unchanged, so they remain correct. Declaring or
  eliminating them is still required before any reordering or parallel
  recording.
- No change to the graphics queue count or the frame bracket.

## Remaining risk

**No visual A/B against a pre-switch binary was performed.** The switch's
justification is the R3.4b parity evidence, which compared the two schedulers
frame by frame on both APIs across hundreds of frames, plus the determinism and
cross-API comparisons above. That evidence covers scheduling and bookkeeping,
which is what the switch changed. It does not include a pixel diff between a
pre-switch and post-switch build of the same fixture, which would be the
strongest possible confirmation; a numeric R3.0 baseline cannot be reconstructed
from the repository, and the earlier `r15-stage60-*` captures are a different
view and are not a valid comparator.

The acceptance ledger's "raster output matches the R3.0 baseline" item stays
open on that basis rather than being claimed from capture presence alone.

## Pre-existing failure

`LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures` fails because
`asset/level/pbr_showcase.level` references the logical model
`model/rock1-bl/rock2`, which the asset archive does not contain. The test links
`AssetRuntime`, not `Render`, and the fixture is unmodified, so this is a
content/bake problem outside this stage.
