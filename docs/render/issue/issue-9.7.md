# Issue 9.7 — Sponza Texture Aliasing and Frame Throughput

**Status: active; Stage 5 and the Stage 6.0 telemetry implementation slice are landed; Stage 6.0 runtime proof is next.** The Sponza startup fixture renders,
but its material channels contain severe high-frequency speckle and the
observed frame rate falls to approximately 30 FPS.

The earlier black-frame/uniform-exhaustion defect is resolved and is explicitly
outside this issue.

## Verified diagnosis

- The selected model contains 405 primitives and references 72 distinct
  4096×4096 images. All are decoded to RGBA8.
- At investigation time, runtime sampled textures were created with one mip
  level and the sampler's maximum LOD was zero. Base color, world normal, and
  material-parameter captures already contained the speckle before deferred
  lighting; shadow visibility did not explain its distribution. The primary
  visual defect was texture-minification aliasing, not shadow noise.
- A naïve uncompressed RGBA8 upload is about 4.5 GiB for the base levels and
  about 6 GiB with complete mip chains. Correct mipmaps therefore need an
  explicit texture-product budget, not just a larger mip count.
- The former Vulkan path created a descriptor pool for each transient
  resource-binding set. Stage 3 now uses fence-safe reusable arenas per frame
  slot; Sponza's roughly 401 opaque G-buffer bindings share those arenas and
  pool creation is limited to warm-up or documented arena growth.
- Visibility is now tested at section granularity after the proxy broad phase.
  Native model version 2 products persist each section's local AABB; legacy
  version 1 products derive the same bounds while loading. Camera and punctual
  shadow passes reject section bounds independently, while directional shadows
  use the same section candidates for fitting and recording.
- Directional shadow maps now carry a conservative validity stamp over the
  light, camera fit input, caster transforms/bounds, materials, and section
  identities. An unchanged static frame reuses the previous depth target and
  records no directional shadow-caster draws; target resize and any stamp input
  change force a redraw.
- The supplied post-Stage-5 snapshot separates the approximately 30 FPS result:
  frame time is 32.0568 ms, Render CPU is 30.924 ms, command recording is
  28.1182 ms, GPU total is 10.98 ms, and present is 0.2795 ms. Geometry-pass
  recording is dominant: directional shadow is 9.127 ms CPU for 446 draws and
  G-buffer is 16.232 ms CPU for 264 draws.
- Frame-slot descriptor pools are reused, but every geometry draw still
  allocates and updates a descriptor set whose uniform offsets are unique to
  that draw. Pipeline/target compatibility and identical pipeline/mesh state
  are also resolved and bound repeatedly.
- The directional-shadow cache reports zero hits and one miss. Its stamp hashes
  raw camera position even when the camera remains inside identical fitted
  caster bounds, so camera motion can redraw an unchanged map.

## Resolution documents

- [Formal review](../.review/issue-9.7.md)
- [Stage design](../.plan/issue-9.7.md)
- [Execution spec](../../../.spec/specs/sponza-render-quality-performance.md)
- [Investigation journal](../../../.spec/journal/2026-09-07-sponza-render-quality-performance.md)
- [Render roadmap](../TODO.md)

Stage 0 instrumentation, the Stage 1 runtime mip slice, and the Stage 2
portable native texture cook slice are now landed: a
checked-in Sponza scenario, Render-owned telemetry, semantic-aware explicit
mip subresources, a bounded loose-texture fallback, sampler LOD range, and
Vulkan/OpenGL upload of every supplied level. Native archive texture products
now store semantic mips and are consumed read-only by runtime;
the portable profile bounds dimensions and records product semantics. Hardware
block compression and measured profile-specific resident-byte evidence remain
follow-up work because the current common `TextureFormat` contract exposes no
BCn/ASTC formats.

The Stage 3 Vulkan pool-lifetime slice is also landed: descriptor pools are
owned by frame-slot arenas, reset after the slot fence, and destroyed only at
backend teardown. Stable material-binding caching and dedicated lifecycle/
growth tests remain follow-up work.

Stage 4 is landed in the current working tree: section bounds are persisted and
validated, the camera and shadow paths perform section-level rejection after
proxy broad phase, and the directional shadow target uses conservative
dependency-stamped reuse. Focused render and runtime validation are recorded in
the execution journal.

Stage 5 adds measured-candidate front-to-back ordering for opaque G-buffer
sections. A depth pre-pass remains gated on a fresh GPU comparison.

Stage 6 targets the measured CPU bottleneck: effective shadow-map validity,
stable descriptor sets with dynamic uniform offsets, recorder-local redundant-
state suppression, lean reusable section packets, and one dirty-range OpenGL
uniform upload per active frame slot. Larger changes such as multithreaded
recording or indirect/GPU-driven submission remain gated on the resulting CPU
profile.

Stage 6.0 now exposes the required Render/Graphics subphase timers and counts,
including descriptor search/allocation/update work, requested versus emitted
native binds, and CPU-subphase p50/p95 summaries. The fixed-scenario runtime
profile still needs to be captured on Vulkan Debug, Vulkan performance, and
OpenGL performance builds before selecting the Stage 6.1+ optimization path.
