# R7.5 combined raster acceptance

The combined Vulkan raster stack at 1094×631 exceeded its 32 MiB texture cap by 4.29 MiB. R7.4's separate RGBA16F fog output accounted for 5,611,520 allocated bytes. Preserve the independently scheduled and profiled fog pass while removing this allocation.

## Ownership

The fixed pass table and graph versioning already own attachment intent and producer dependencies. Reuse the pooled SceneHdr target and publish a preserving write. The graph executor opens the target with a per-pass color load. The command recorder translates it into the existing Vulkan attachment begin and image barrier, or skips the OpenGL clear. The target lease and store policy remain with their current owners.

HeightFogPass owns the fog equation, descriptors and blend state. Its shader computes fog color and transmittance, with the blend unit weighting the existing HDR color by transmittance. The alpha write still feeds the transmittance diagnostic. Temporal resolve and tone mapping consume the latest SceneHdr version.

The local gkNextEngine study at 4ba5b7cd uses one HDR image in its atmosphere compute composite. Here the raster load and blend avoid both a second HDR target and shader feedback without importing storage-image requirements.

## Acceptance

- Verify the new HDR version reaches temporal resolve, tone mapping and the fog diagnostic. Reject a preserving load of an unproduced transient.
- Verify exact fog-off versus zero-density output and compare matched R7.4 fog-on captures, allowing at most two 8-bit levels per channel for blending roundoff on a static fixture.
- Exercise raster shadow maps, hybrid ray queries and switching to and from path tracing. Check the independently enabled and combined effects on the supported APIs.
- Measure Vulkan RelWithDebInfo after warm-up at 1094×631. Combined GPU p95 must be at most 8.5 ms, fog GPU p95 at most 0.5 ms, and incremental active texture allocation at most 32 MiB.
