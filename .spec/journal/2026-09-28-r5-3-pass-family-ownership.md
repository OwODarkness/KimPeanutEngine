# R5.3 pass-family ownership journal

- Status: active; R5.3.1 implemented and R5.3.2 owner extraction implemented;
  acceptance evidence and R5.3.3–R5.3.7 remain
- Plan: [R5.3](../../docs/render/.plan/R5.3.md)
- Related prerequisite: [R5.2](../../docs/render/.plan/R5.2.md) remains in progress

## 2026-09-28 — R5.3.1 helper extraction

### Changes

- Added `SceneDrawRecorder` to own the per-frame `MeshProxy` snapshot, revision-
  stable visible-section packets, object/selection uniform keys, frame object
  state, material binding cache, draw recording helpers, and packet counters.
- `DeferredRenderer` now delegates shadow-caster and GBuffer object draws to
  the shared recorder. The helper borrows `FrameContext`, material system,
  resource resolver, and command recorder; it owns no GPU allocations.
- Added `FullscreenPassResources` to own the common fullscreen triangle and
  sampler, including partial-creation retry and cleanup. Existing fullscreen
  consumers read its handles through const accessors.
- Moved the selection uniform payload type and draw uniform-key constants next
  to the recorder, and moved fullscreen triangle vertex/index data next to its
  resource owner. Shader and Graphics ABI declarations remain in place.
- Added focused tests for snapshot replacement and partial fullscreen resource
  creation/retry/release in both mesh-fails and sampler-fails cases, plus
  section-packet reuse across repeated shadow-style queries. The resource tests
  also check ready initialization and cleanup are idempotent.

### Validation

- `tools/kp.ps1 -Configuration Debug build RenderSystemTest` — passed.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed.
- `build/engine/test/unit/render/Debug/RenderSystemTest.exe` — all 33 tests
  passed, including same-revision packet reuse, changed-revision invalidation,
  and both partial fullscreen resource retry cases.
- `ctest --test-dir build -C Debug -R RenderSystemTest --output-on-failure` —
  CTest reported no discovered tests; the target executable was run directly.
- Visible Debug Vulkan Sponza runtime: screenshot exported through
  `capture.screenshot`; Runtime stats showed path tracing active, 285 GBuffer
  sections, one point-shadow cache hit, and a completed 300-sample summary.
  Capture: `save/screenshots/validation/r53-helper-sponza-vulkan-warm.png`.

### Remaining work and risks

- R5.3.1's plan asks for existing static-reuse tests and matched before/after
  captures. The live run confirms current rendering and cache activity, but no
  same-condition baseline image/stat comparison was taken in this slice.
- OpenGL raster, shadow edit/reload/resize, and cleanup/failure runtime cases
  remain unverified.
- R5.3.2–R5.3.7 remain open. The facade still owns shadow, environment,
  deferred lighting, capture, tone-map, ray-tracing scene, and path-tracing
  implementations and their state.
- R5.2 acceptance is still incomplete; this user-directed helper-only slice did
  not change graph scheduling, transition, or transient-lease ownership.
- Pre-existing `.gitignore` and `asset/level/sponza.level` working-tree changes
  were preserved and are unrelated to this slice.

## 2026-09-28 — R5.3.2 ShadowPass extraction

### Changes

- Added `ShadowPass` to own directional/spot/point frame scheduling and draw
  recording, persistent shadow cache stamps and reuse flags, shadow pipeline,
  depth samplers, and resource cleanup.
- Moved the three shadow frame records into `shadow_frame_output.h`, the
  explicit borrowed-output contract used by lighting and capture recording.
- Kept pass order in `DeferredRenderer`; shadow record calls resolve their
  declared write target from the active graph pass context before delegating.
- Removed the shadow scheduler/recording implementations, sampler creation,
  pipeline creation, and cache fields from the facade. The `.cpp` fell from
  3,901 to 3,386 lines.

### Validation

- `tools/kp.ps1 -Configuration Debug build RenderSystemTest` — passed.
- `RenderSystemTest.exe --gtest_brief=1` — 33/33 passed.
- `tools/kp.ps1 -Configuration Debug build RenderPassScheduleTest` — passed.
- `RenderPassScheduleTest.exe --gtest_brief=1` — 94/94 passed. CTest reported
  no registered tests for this target, so the executable was invoked directly.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed.
- Visible Vulkan Sponza Runtime: Default desktop and foreground GLFW30 window
  verified; stats showed path tracing active, directional/spot/point passes
  executed, point-shadow cache hit 1/miss 0, no required graph failure, and a
  complete 300-sample summary. After a window/viewport resize request, stats
  reported 722×389 and a new SceneColor capture rendered successfully.
- `GraphicsSmoke.exe` — failed Vulkan validation on an indexed GBuffer draw:
  descriptor set 0 binding 5 was invalid. This is not currently attributed to
  the shadow extraction; the baseline cause is unknown.
- `git diff --check` — pending final pass after remaining owner work.

### Remaining work and risks

- OpenGL shadow parity, light edit/reload, and explicit shadow-resource failure
  and repeated-cleanup coverage remain open.
- The `GraphicsSmoke` failure needs a baseline/source diagnosis; do not claim
  the renderer-wide graphics smoke gate passed.
- R5.3.3–R5.3.7 and R5.2 acceptance remain open. The visible Sponza instance
  remains running for inspection.

## 2026-09-28 — shared DeferredRenderer schema extraction

### Changes

- Moved `EnvironmentBindingBundle` into `EnvironmentFrameBindings`, an adjacent
  shared environment-view contract used by deferred lighting and path tracing.
- Moved path tracing GPU scene/camera payloads, ABI assertions, and estimator,
  exposure, and RNG policy constants into
  `ray_tracing/path_tracing_scene_data.h`.
- Moved shader-content hash accumulation into `shader_signature.h` for the PT
  and tone-map shader cache signatures.
- Moved shadow and GBuffer uniform keys plus point-shadow face resolution into
  pass-specific constant headers; removed unused point-shadow atlas size
  constants.
- Extracted directional caster fitting, point/spot volume checks, and
  directional/point shadow cache-stamp calculations into `ShadowPassUtils`.
  The scheduler and GPU recording/state remain in the facade for the owner
  extraction slice.
- Removed three unused graph conversion helpers left in DeferredRenderer after
  R5.2 moved transition ownership to graph bindings/executor.

### Validation

- `tools/kp.ps1 -Configuration Debug build RenderSystemTest` — passed after
  the schema moves.
- `RenderSystemTest.exe --gtest_brief=1` — 33/33 passed.
- `tools/kp.ps1 -Configuration Debug build RenderPassScheduleTest` — passed;
  `RenderPassScheduleTest.exe --gtest_brief=1` — 94/94 passed, including four
  shadow utility contracts.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed.
- `git diff --check` — passed; Git emitted only working-copy line-ending
  normalization warnings for already modified files.

### Limits

- This only decouples shared contracts and shader-facing data definitions; the
  pass functions, their pipelines, and cache/history state still live in
  DeferredRenderer. It currently measures 3,901 `.cpp` lines and 379 header
  lines, so the planned pass-family owners remain the main work. R5.3.2–R5.3.7
  remain open, as does the R5.2 acceptance gate.

### Correction — R5.3.2 ShadowPass extraction

The preceding schema entry records the state before the ShadowPass extraction.
R5.3.2 now owns shadow scheduling/recording, cache state, pipeline, samplers,
and cleanup. See the R5.3.2 journal section above for its validation and open
acceptance; R5.3.3–R5.3.7 and R5.2 remain open.

## 2026-09-28 — R5.3.5 RayTracingScene extraction (in progress)

### Source changes

- Added `RayTracingScene` and `RayTracingSceneView` under `render/ray_tracing/`.
  Scene enumeration, retained section/material mapping, BLAS/TLAS state and
  recording, path-tracing light records, build-resource tokens, and reference-
  table packing/cleanup now live behind this owner.
- `DeferredRenderer` still selects the compiled graph variant and assembles
  graph bindings. It delegates scene/build operations and passes the owner's
  borrowed view to graph binding and path-tracing consumers.
- Removed the scene cache, AS, record, and build-resource fields from the
  DeferredRenderer declaration and registered the owner source in Render.

### Validation and remaining work

- The Debug `RenderSystemTest` target build passed after integration, and the
  direct executable passed 34/34 in the earlier validation turn. No
  RayTracingScene-specific owner tests or runtime checks were run; R5.3.5 is
  not accepted as complete.
- Review initialization/cleanup and build-token rollback, then validate
  retained section/material mapping, area lights, static reuse, and transform/
  material updates when testing is authorized.
- R5.3.3–R5.3.4 and R5.3.6–R5.3.7 remain open. R5.2 acceptance and all
  cross-backend/runtime evidence also remain open.

## 2026-09-29 — R5.3.3 deferred-lighting state ownership (partial)

### Changes

- `DeferredLightingPass` now composes `EnvironmentBindingsOwner`; level
  environment updates, fallback bindings, and active environment reads route
  through the pass owner. The resolver continues to own the backing GPU texture
  and sampler resources.
- Moved `FrameLightingBinding` from `DeferredRenderer` into
  `DeferredLightingPass`. The facade submits the binding prepared by its
  frame-context/light/shadow orchestration, while the pass clears it during
  cleanup and at frame boundaries.
- The facade continues to resolve declared graph targets and borrowed shadow
  and TLAS views, and to coordinate shared fullscreen/shadow resources. No pass
  schedule or graph resource ownership moved.

### Validation and remaining work

- `git diff --check` — passed; only line-ending normalization warnings were
  reported for previously modified files.
- `tools/kp.ps1 -Configuration Debug build RenderSystemTest` — passed.
- Tests and runtime were not run, per the current instruction not to test.
- This is only a partial R5.3.3 source extraction. Its owner-specific lifecycle,
  environment update/fallback behavior, hybrid-query behavior, Vulkan/OpenGL
  parity, and runtime acceptance remain unverified. R5.3.4 and R5.3.6 source
  ownership have since been implemented; their acceptance, R5.3.5 owner
  acceptance, R5.3.7 audit, and the R5.2 prerequisite remain open.

## 2026-09-29 — R5.3.4 output-policy ownership and R5.3.6 PathTracingPass

### Changes

- Added `ToneMapPass::OutputPolicy`; the fixed exposure, tone-map operator and
  output-transfer values now belong to `ToneMapPass.cpp`, and path-trace history
  signature construction consumes them from the owner. Removed those values
  from `path_tracing_scene_data.h` without changing the current policy.
- Added `PathTracingPass` to own path-tracing pipeline preparation, shader
  signature, persistent history/guide targets, history signature and commit
  state, binding cache, dispatch recording, activity/availability, failure
  injection, and scene-capacity diagnostic policy.
- `PathTracingPass::Record` resolves graph-declared inputs through the supplied
  `RenderGraphPassContext`. `DeferredRenderer` retains compiled-plan selection,
  graph import binding/execution, and profile aggregation.
- Added `PathTracingPass` as a `RenderTarget` friend solely so this Render owner
  can initialize and release its persistent logical targets; native allocation
  remains in Graphics. Removed pass-only state and forwarding adapters from
  the facade. The facade is now 1,323 `.cpp` lines and 178 header lines.
- `ToneMapPass`/`CaptureViewPass` already owned their respective recording and
  pipeline implementations; this change completed their output-policy
  boundary and records the source-level state accurately.

### Validation and remaining work

- `tools/kp.ps1 -Configuration Debug build RenderSystemTest` — passed after
  the owner extraction and facade adapter removal.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed after the
  owner extraction and shared-header cleanup. The affected target was rebuilt
  after the final logging-format edit.
- `git diff --check` — passed after source and documentation edits, with only
  Git line-ending normalization warnings for existing changed files.
- Tests and runtime were not run under the current instruction.
- Path-tracing history, descriptor reuse, dispatch failure, resize/cleanup,
  output parity, and runtime acceptance remain unverified. R5.3.3/3.4/3.5/3.6
  acceptance, R5.3.7 facade audit, and R5.2 acceptance remain open.

### R5.3.3 source-state correction and R5.3.7 audit — 2026-09-29

- The R5.3.3 source move is complete: `DeferredLightingPass` owns the GBuffer
  and lighting recorders/pipelines plus environment and frame-lighting state.
  Shared fullscreen/shadow/environment readiness stays in the facade because
  it coordinates resources owned by separate passes. Owner behavior remains
  unverified.
- Audited the facade declaration and implementation. It retains graph/frame
  composition and profile/settings state only; the pass pipelines, scene-table
  packer, BLAS/TLAS recorders, PT history/binding state, and draw algorithms
  are outside it. `RecordFrame` and `BuildFrameResourceBindings` remain long
  composition methods at about 376 and 165 lines respectively.
- Current size is 1,323 `.cpp` lines and 178 header lines versus the plan's
  4,587/379 baseline. The remaining `.cpp` is 123 lines above the soft upper
  planning target; no pass algorithm was left behind just to hit that number.
- `git diff --check` passes with line-ending normalization warnings. Debug
  engine and affected target builds passed earlier in this turn. Tests and
  runtime remain unrun under the current instruction; R5.3 owner acceptance
  and the R5.2 prerequisite remain open.

### RayTracingScene view lifetime and cleanup correction — 2026-09-29

- `BuildFrameResourceBindings` previously kept a `RayTracingSceneView` across
  `PrepareBuildResources`, even though preparation rebuilds the owner's frame
  vectors that back the view's spans. It now checks pending build declarations
  first and reacquires the view after preparation before assembling graph
  bindings.
- Reference-table creation/upload was still called by `PathTracingPass::Record()`.
  It now runs in the graph-import binding phase against the declared geometry
  group; the PT pass consumes `RayTracingSceneView::reference_table` read-only.
- Consolidated `RayTracingScene::Cleanup` so resource destruction is conditional
  on a live Graphics ray-tracing owner while all CPU records, signatures,
  capacities, counters, and build flags reset on either cleanup path.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed after
  all three source corrections. No tests or runtime were run, per instruction.
- The facade now measures 1,346 `.cpp` lines and `BuildFrameResourceBindings`
  is 201 lines because it coordinates pre-record table preparation. The
  Debug `RenderSystemTest` target also compiled after the PathTracingPass API
  change; its executable was not run.
- The new lifetime and repeat-cleanup paths still need focused owner tests and
  Vulkan/OpenGL runtime acceptance before R5.3 can close. R5.2 acceptance also
  remains open.

### PathTracingPass target rollback correction — 2026-09-29

- `EnsureHistoryTargets` now rolls back both history targets and the guide
  target when any target is invalid or allocation throws. It resets the PT
  sample count, history parity, and history signature with the target set.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed. No tests
  or runtime were run, per instruction.
- Partial target creation, resize, binding release, and safe-close behavior
  still need direct owner/runtime verification.

### RayTracingScene failed-BLAS cache retry correction — 2026-09-29

- When a BLAS allocation fails, the current scene may still proceed with that
  section omitted, preserving the existing fallback. The scene cache now stays
  invalid so preparation retries the omitted section next frame instead of
  permanently caching an incomplete scene until content changes.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed. No tests
  or runtime were run, per instruction. Recovery and scene-completeness
  behavior remain runtime-unverified.

### Draw-cache identity correction — 2026-09-29

- Replaced the draw recorder's lossy XOR cache keys with typed keys. The
  object-state key compares renderable identity and pass. The material-binding
  key compares renderable/material identity, pass, and the complete per-pass
  buffer allocation (handle, offset, range).
- Repacked stable object/selection uniform keys as domain tag + full handle ID +
  generation, so the domain tag does not overlap handle bits.
- `tools/kp.ps1 -Configuration Debug build RenderSystemTest` — passed
  (compile/link only); executable not run.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed.
  No tests or runtime were run, per instruction.
- Before the frame-input cleanup, the facade measured 1,346 `.cpp` lines and
  178 header lines. It now measures 1,370 `.cpp` lines and 179 header lines.
  Cache behavior and collision regression assertions remain unverified until
  owner tests run.

### Aborted-frame input cleanup — 2026-09-29

- Centralized release of the facade's borrowed frame/world pointers, active
  capture/view requests and frame-lighting binding. All `RecordFrame` preparation
  failure returns clear these inputs; a rejected second call preserves inputs
  for an already-active graph. `Cleanup` clears them immediately after
  aborting graph execution and before pass-owner teardown; `FinalizeFrame` uses
  the same cleanup path after releasing graph state.
- This removes stale frame-scoped borrows after a failed frame and makes owner
  teardown ordering explicit. No GPU ownership or pass scheduling changed.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed.
- Tests and runtime remain unrun per instruction; failure-path lifecycle behavior
  still needs focused verification.

### Pass-profile scope cleanup — 2026-09-29

- `ExecutePass` now resets the active profile-pass marker through scope cleanup,
  including exception unwinding from an owner recorder.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — passed.
- Tests and runtime remain unrun per instruction; exception-path profiling
  remains unverified.
