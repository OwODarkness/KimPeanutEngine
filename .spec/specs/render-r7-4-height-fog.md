# R7.4 — Exponential height fog

- Status: R7.4 accepted on 2026-11-06; combined R7.5 storage exceeds its ceiling.
- Evidence: [journal](../journal/2026-11-06-render-r7-4-height-fog.md) and [review](../../docs/render/.review/R7.4.md).
- Parent: [R7 roadmap](../../docs/render/TODO.md) and [stage design](../../docs/render/.plan/R7.md#r74--exponential-height-fog).
- Baseline: `f1f2734` plus the existing owner-boundary and startup worktree changes.

## Placement

- Reuse the existing frame settings mailbox, compiled graph, transient pool,
  `DescribeSceneHdr`, fullscreen mesh/sampler, camera matrices and capture pass.
- HeightFogPass owns the shader pipeline and fog uniforms; a value-only settings
  contract serves Runtime and Render. DeferredRenderer only routes graph resources.
- Share the fullscreen pipeline description with TAA and keep graph variant key
  encoding beside the authored conditions instead of duplicating facade bit masks.
- Inspected gkNextEngine `4ba5b7cd106c282e7ed166ff853aeea87b392680`,
  `assets/shaders/Process.AtmosphereComposite.comp.slang`: use analytic optical
  depth and pre-temporal composition; retain a separate portable HDR target,
  rather than its Vulkan compute/bindless in-place execution. No source copied.

## Acceptance

- +Y height, perspective eye and orthographic camera-plane origins. Sky is
  unchanged by default; opt-in integrates to finite max distance.
- Disabled/zero-density bypass allocates no fog target and preserves exact output.
- Stable horizontal, homogeneous and steep-ray limits; graph orders lighting,
  fog, TAA and tone mapping. PT does not consume fog. Visible fog edits reset TAA.
- Capture transmittance from fogged HDR alpha without a second fog evaluation.
- Check Debug Render/Graphics, canonical Sponza on Vulkan/OpenGL, and indoor,
  terrain and numerical fixtures. Measure performance only in RelWithDebInfo.
- The distinct RGBA16F output costs 5.27 MiB payload at 1094×631 while live.
  Combined R7.5 storage must be measured against the existing 32 MiB gate.
