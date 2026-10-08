# Render R7 raster image quality

- Status: active; R7.0 baseline recorded; R7.1 implementation complete with Vulkan runtime evidence; R7.2 implementation and Vulkan/OpenGL runtime capture complete; R7.1 and combined cross-backend acceptance remain open.
- Owner: Render module.
- Parent TODO: [R7 roadmap](../../docs/render/TODO.md#r7--raster-image-quality-r72-implementation-in-progress-acceptance-open).
- Canonical design: [R7 stage plan](../../docs/render/.plan/R7.md).
- Journal: [planning checkpoint](../journal/2026-10-03-render-r7-raster-image-quality.md).

## Objective

Improve opaque deferred raster quality through spatial SSAO, motion/history
contracts, native-resolution TAA, and analytic height fog, with independently
observable quality, cost, and lifecycle evidence on Vulkan and OpenGL.

## Current state

The renderer has sampled GBuffer depth/normals/material AO, environment IBL,
compiled graph execution, fullscreen resources, and separate raster/PT outputs.
Source inspection found no SSAO, fog, or scene temporal AA. R5 graph/pass
acceptance and recorded Vulkan validation failures remain open; R6 PT temporal
reconstruction remains separate work. See the canonical plan for source paths.
R7.0 has a deterministic source-backed fixture, Sponza raster captures, and
direct-source Cornell/PBR showcase raster captures. The user directed closing
the baseline milestone without expanded R5 validation; those R5 gates remain
open. Extra scene variants depend on checkout-local ignored model sources.

R7.1 has graph-declared full-resolution AO estimate/filter passes, bounded
world-position sampling, edge-guided filtering, ambient-diffuse-only
composition, Low/Medium/High tap-count settings, requested/effective runtime
stats, and settings-off graph pruning. PT skips the raster AO producers.
RelWithDebInfo Vulkan Sponza runtime captures verified enabled and disabled
behavior, and raw/filtered semantic views were inspected. High quality measured
0.434 ms combined AO GPU time at 1094x631. OpenGL runtime, AO-material overlap,
and focused silhouette/planar-darkening checks remain unverified; R7.1 stage
acceptance is therefore open.

R7.2 adds previous-submitted camera and rigid-object transforms, a fifth GBuffer
attachment carrying previous-minus-current UV motion, positive view depth, and
history validity, plus a semantic motion capture. CPU contracts define top-left
normalized UV on both APIs, API-specific clip-depth validation, and one-time
jitter removal. Current raster jitter is zero until TAA consumes it. Contract
tests pass, and Debug Vulkan/OpenGL Sponza runtime captures exercise base color,
scene color, and motion vectors with path tracing disabled. Runtime shader logs
show the checkout's `asset/shader` sources using their compiled shader-cache
entries. Shader programs are loaded as source assets, not cooked through the
AssetTool import providers; a separate archive publication step does not apply.

## Scope and non-goals

R7.0–R7.5 are the core deliverable. Reflection probes, SSR, and volumetric fog
are subsequent design gates, not core completion requirements. Full PT receives
neither SSAO nor raster TAA. No new GI, upscaler, frame-generation, compute,
transparency, or physical-medium subsystem is selected by this spec.

## Invariants

- The compiled render graph exclusively schedules every effect/filter/resolve;
  typed declarations cover transients, motion outputs, persistent history
  imports/writes, composition, and captures. No second post-process schedule.
- Render owns policy, copied settings, logical histories, and graph declarations.
- Graphics owns physical GPU allocation, execution, synchronization, retirement.
- Asset/Resource owns file/product preparation; common contracts stay API-neutral.
- Distinct subresources support filtering/history; submission success commits
  previous-frame state. Failed required producers cannot publish stale output.
- Disabled effects preserve the baseline; requested/effective mode is explicit.
- R5/R6 acceptance is not relabeled by this work.

## Stages

| ID | Deliverable |
| --- | --- |
| R7.0 | Close affected correctness gates; freeze fixtures and quality/cost budgets |
| R7.1 | Spatial SSAO, edge-aware filtering, ambient-diffuse integration |
| R7.2 | Jitter, motion, previous-submitted transforms, validity contract |
| R7.3 | Native-resolution TAA with rejection and safe history lifecycle |
| R7.4 | Stable integrated exponential height fog before TAA/tone mapping |
| R7.5 | Combined acceptance and explicit reflection/volumetric follow-up decisions |

Detailed algorithms, dependency flow, gates, and stage exits belong solely in
the [R7 plan](../../docs/render/.plan/R7.md).

## Acceptance criteria

- [x] R7.0 baseline and numeric quality/GPU/memory budgets recorded.
- [ ] R7.1–R7.4 stage exits passed on both raster backends.
- [ ] Effect-off parity and unchanged full PT behavior verified.
- [ ] Motion/disocclusion/cuts, edits, resize/reload/toggle, injected failure,
  rejected submission, and safe teardown have evidence.
- [ ] Combined effects preserve selection/UI and semantic diagnostic capture.
- [ ] Matched RelWithDebInfo results satisfy the frozen budgets.
- [ ] R7.5 records subsequent design decisions without claiming implementation.

## Validation plan

R7.0 was closed at the user’s direction without expanded R5 validation. R7.1 implementation follows the matrix and canonical plan:
targeted builds/contracts, Debug Vulkan validation and visible Vulkan/OpenGL
captures, broad validation for shared ABI changes, then matched performance
measurements. Compilation alone cannot accept image quality or GPU lifetime.

## Risks and open questions

Recorded R5 validation failures need current reproduction. SSAO can overlap
authored material AO; motion/jitter errors can create trails; TAA can lose fine
detail; fog needs stable numerical limits and defined sky behavior. Numeric
budgets await R7.0 measurement. Reflection implementation and PT fog integration
remain unselected; no runtime result is asserted by the planning checkpoint.
