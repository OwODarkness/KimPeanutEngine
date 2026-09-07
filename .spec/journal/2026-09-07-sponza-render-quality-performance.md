# 2026-09-07 — Sponza Render Quality and Performance

**Status: Stage 0 instrumentation landed; native baseline evidence pending.**

Links: [issue](../../docs/render/issue/issue-9.7.md),
[review](../../docs/render/.review/issue-9.7.md),
[plan](../../docs/render/.plan/issue-9.7.md), and
[spec](../specs/sponza-render-quality-performance.md).

## Investigation checkpoint

- Inspected scene-color, base-color, world-normal, material-parameter, and
  shadow-visibility captures. Speckle is already present in sampled G-buffer
  material channels and is not spatially explained by shadow visibility.
- Traced texture creation to one mip level and sampler LOD zero. Confirmed the
  OpenGL mip-generation branch and that Vulkan's current upload copies only
  level zero.
- Parsed the exact selected glTF dependency closure: 115 meshes, 405
  primitives, 28 materials, and 72 referenced images; all referenced images are
  4096×4096. Primitive alpha modes are 401 opaque and 4 blend.
- Calculated approximately 1.89 GiB compressed source bytes, 4.5 GiB decoded
  RGBA8 base levels, and 6 GiB for complete uncompressed mip chains.
- Traced Vulkan material bindings to a pool-per-set lifecycle and G-buffer
  submission to roughly 401 transient material sets per frame.
- Traced visibility to one whole-model proxy bound and directional shadows to
  all-section redraw every frame, for roughly 806 geometry draws before
  fullscreen passes.
- Reviewed the linked Sponza frame analysis and source implementation. Adopted
  its applicable per-mesh culling and measured depth-prepass lessons; rejected
  its hardware/resolution result as a project performance baseline.
- Reviewed Khronos' descriptor-management sample as supporting evidence for
  pool reuse/caching, then mapped lifetime to this engine's frame slots and
  fences.

## Changes in the investigation checkpoint

The initial checkpoint added the formal review, stage design, execution spec,
issue entry, roadmap links, and project-status entry. The Stage 0 checkpoint
below adds the first runtime instrumentation slice.

## Validation

- Read-only source and manifest inspection completed.
- Relative Markdown link scan passed for all changed records.
- Trailing-whitespace scan passed for all changed records.
- `git diff --check` passed for tracked documentation changes. New records were
  covered by the explicit trailing-whitespace scan.
- Native CMake build and runtime capture are not claimed for this checkpoint;
  the build is blocked before compilation by the machine's Windows SDK probe.

## Remaining evidence and risk

- CPU/GPU/present timing attribution is implemented but has not yet been
  collected from the native runtime.
- Device-reported resident memory, format support, and descriptor counts are
  instrumented but have not yet been captured.
- Current diagnostic captures are static images; camera-motion stability needs
  a reproducible path or sampled poses.
- The proposed memory ceiling and 16.67 ms target require measurement on the
  recorded reference machine; neither is a current result.
- All seven review findings remain open.

## Stage 0 implementation checkpoint

- Added the checked-in `sponza-stage-0` scenario descriptor: `level/sponza.level`,
  `main_camera`, 1920×1080, Vulkan as the initial API, Debug build, 120 warm-up
  frames, and a 300-frame sample window.
- Added a Render-owned value profile snapshot with CPU phase timings, per-pass
  CPU timings, draw/section counts, descriptor set/pool counts, present mode,
  shadow baseline counters, and texture source/decoded/resident bytes.
- Added common backend-neutral profiling hooks and native timer-query
  implementations for Vulkan timestamp queries and OpenGL timer queries.
  GPU timings are consumed only after the backend's safe frame completion
  point; unavailable timer support leaves CPU telemetry active.
- Added prepared-catalog texture closure accounting and resolver resident-byte
  accounting. The closure count is captured before renderer-owned built-ins so
  the Sponza dependency count remains attributable to the selected level.
- Added Render profile contract tests and surfaced the key Stage 0 readouts in
  the Editor profile bar. The profile window also emits one structured
  completion log with p50/p95 CPU/present values and the measured counters;
  OpenGL samples are finalized after the external `SwapBuffers()` timing is
  recorded.

## Stage 0 validation

- `git diff --check` — passed.
- MinGW C++17 syntax checks — passed for Render, Runtime, Editor, Vulkan, OpenGL,
  and `render_profile_test.cpp`.
- `cmake --build build --config Debug --target Render Graphics
  RenderPassScheduleTest RenderSystemTest` — blocked before compilation by
  MSBuild's denied access to `C:\Users\17519\AppData\Local\Microsoft SDKs`
  while evaluating the Windows SDK probe.
- Vulkan/OpenGL runtime baseline samples and image captures — not claimed;
  they require the blocked native build and a controllable GLFW window.
