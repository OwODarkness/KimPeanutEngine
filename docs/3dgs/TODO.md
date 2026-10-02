# 3DGS Rendering Roadmap

**Status: planned, no implementation begun.** See [architecture](PLANS.md),
[source and dependency study](references.md), and [GS1 design](.plan/GS1.md).

## GS0 — Reference and fixture gate

- [ ] Select a small, redistributable GraphDeco-layout PLY fixture and a
  deterministic camera. Record its source and content license; do not commit
  the downloaded GraphDeco repository or a large pretrained model.
- [ ] Pin a PLY parser revision/license after a binary little-endian, arbitrary
  property-order, large-count, and malformed-input probe. Prefer `tinyply` if
  the probe passes; retain a parser-free procedural fixture for render tests.
- [ ] Record a reference image or numeric pixel/projection check, model axes,
  SH convention, color space, and expected active splat count.

## GS1 — First integrated raster renderer

- [ ] Create optional `engine/module/3dgs` targets and an SDK-free build-off
  path. Register the importer through the existing Asset extension seam.
- [ ] Validate and convert PLY fields into an immutable, versioned Gaussian
  asset and renderer-specific packed artifact; add focused malformed and
  activation/SH-order tests.
- [ ] Submit one scene instance through the ordinary scene/camera path. Sort
  visible splats back-to-front with a deterministic key and upload through
  frame-safe RHI buffers.
- [ ] Project anisotropic splats and render an ordered, premultiplied HDR
  accumulation. Reject fragments behind raster mesh depth and composite into
  the Render graph before tone mapping.
- [ ] Capture checked-in fixture on Debug Vulkan validation and OpenGL with
  the same camera; verify front/behind mesh cases, alpha order, camera movement,
  empty model, resize, unload, and clean shutdown. Log active RT mode.

## GS2 — Scene and lifecycle

- [ ] Support multiple independently transformed/visible instances while
  sharing one immutable source. Define stable ordering across models.
- [ ] Verify reimport/reload failure retention and frame-safe GPU retirement.
  Expose count, sort time, upload bytes, draw time, and memory in Runtime stats.

## GS3 — Measured performance

- [ ] Measure GS1 in RelWithDebInfo at a pinned camera/viewport/model count;
  separate decode, sort, upload, projection, and blend/fill costs.
- [ ] Choose GPU sort/culling, LOD, or compressed format only from the measured
  bottleneck. Compare portable graphics compute, optional CUDA/CUB, and the
  CPU baseline. If CUDA is chosen, test Vulkan/GL interop, device matching,
  synchronization, fallback, and teardown. Compare quality and cost against
  the GS1 reference.

## GS4 — Path-tracing interaction

- [ ] Specify depth/guide source, mixed mesh/splat occlusion, and history reset
  on camera/model changes. Test raster and PT modes separately before enabling
  a mixed PT presentation claim.

## Open decisions

- Which redistributable fixture can be checked in for visual regression?
- Whether GS1's CPU sort is adequate for the first target model size is a
  measurement, not a premise. Its cost decides the GS3 implementation.
- Whether splats need to cast/receive game lighting or participate in PT is
  outside the render-only first release and requires a separate design.
