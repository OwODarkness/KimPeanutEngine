# Issue 9.7 — Sponza Texture Aliasing and Frame Throughput

**Status: active; remediation proposed.** The Sponza startup fixture renders,
but its material channels contain severe high-frequency speckle and the
observed frame rate falls to approximately 30 FPS.

The earlier black-frame/uniform-exhaustion defect is resolved and is explicitly
outside this issue.

## Verified diagnosis

- The selected model contains 405 primitives and references 72 distinct
  4096×4096 images. All are decoded to RGBA8.
- Runtime sampled textures are created with one mip level, while the sampler's
  maximum LOD is zero. Base color, world normal, and material-parameter captures
  already contain the speckle before deferred lighting; shadow visibility does
  not explain its distribution. The primary visual defect is texture
  minification aliasing, not shadow noise.
- A naïve uncompressed RGBA8 upload is about 4.5 GiB for the base levels and
  about 6 GiB with complete mip chains. Correct mipmaps therefore need an
  explicit texture-product budget, not just a larger mip count.
- Vulkan creates a descriptor pool for each transient resource-binding set.
  Sponza contributes about 401 opaque G-buffer material bindings per frame,
  before fullscreen passes.
- Visibility is tested at whole-model proxy granularity. Once Sponza's single
  proxy is visible, all of its sections are submitted. The unchanged
  directional shadow map is also redrawn every frame, producing roughly 806
  geometry draws before fullscreen work.
- The 30 FPS observation has not yet been separated into CPU recording, GPU
  execution, present pacing, or validation overhead. Measurement is a required
  gate before pass-level tuning.

## Resolution documents

- [Formal review](../.review/issue-9.7.md)
- [Stage design](../.plan/issue-9.7.md)
- [Execution spec](../../../.spec/specs/sponza-render-quality-performance.md)
- [Investigation journal](../../../.spec/journal/2026-09-07-sponza-render-quality-performance.md)
- [Render roadmap](../TODO.md)

The first implementation slice is Stage 0 instrumentation: a checked-in
Sponza scenario, Render-owned CPU/pass/draw telemetry, Vulkan/OpenGL pass timer
queries, descriptor/present counters, and texture closure/resident-byte
accounting. Semantic-aware mip artifacts and Vulkan/OpenGL subresource upload
remain Stage 1; texture product compression and size policy follow because
full-resolution RGBA8 mip chains are not an acceptable steady-state solution.
