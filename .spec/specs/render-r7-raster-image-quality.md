# Render R7 raster image quality

- Status: R7.0 and R7.2–R7.4 are complete. The R7.5 combined budgets pass. Core sign-off still requires R7.1 focused acceptance.
- Owner: Render module.
- Parent TODO: [R7 roadmap](../../docs/render/TODO.md).
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
0.434 ms combined AO GPU time at 1094×631. R7.5 verified OpenGL runtime and
AO-off parity on both APIs. Material overlap and focused silhouette and planar
checks remain open.

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

R7.3 is accepted on 2026-10-22. The graph-scheduled scene-linear resolve owns
ping-pong HDR/packed-normal-depth/identity history, submitted-frame Halton
jitter, current-frame reconstruction, Catmull-Rom history sampling, YCoCg
bounds, and same-surface normal-footprint rejection. Active TAA selects a
half-mip finer PBR texture footprint; disabled raster and PT keep their mip
policy. History resources and shader replacement prepare before frame
recording. Vulkan idle collection no longer retires unsubmitted AS scratch.

Matched Vulkan/OpenGL Sponza comparisons at 1094×631 pass all frozen quality
gates against a 2188×1262 reference: shimmer error decreases 35.42% / 35.32%,
static error 28.25% / 29.65%, and edge-gradient error 34.22% / 34.02%.
Composited-window orientation, cut/resize/minimize/restore/reload, rigid-object
inspection, rejected-history and shader/allocation lifecycle contracts pass.
PT excludes TAA/jitter and preserves accumulation when TAA settings toggle.
Three uncontended RelWithDebInfo Vulkan windows (120 warm-up, 300 samples)
measure resolve p95 0.099–0.102 ms and total GPU p95 3.215–3.340 ms. Active R7
Vulkan allocator ranges measure 32,440,320 bytes (30.94 MiB), within 32 MiB;
OpenGL native allocation measurement remains unsupported and absent.
Render/Graphics contracts pass 251/251, and GraphicsSmoke passes both APIs.
Nine unrelated Asset/Terrain broad-suite failures remain recorded separately.
See the [review](../../docs/render/.review/R7.3.md) and
[acceptance journal](../journal/2026-10-03-render-r7-raster-image-quality.md#r73-acceptance--2026-10-22).

R7.4 is accepted on 2026-11-06. Exponential height fog runs on scene-linear
HDR before TAA/tone mapping. Vulkan/OpenGL agree with the analytic sky cases
within 0.003 linear transmittance at the measured pixel, and zero density
preserves byte-identical captures. Debug Sponza, indoor, and procedural terrain
preview checks pass; Vulkan PT bypasses fog. RelWithDebInfo Vulkan fog p95 is
0.024 ms in the R7.4 build. See [R7.4 evidence](../journal/2026-11-06-render-r7-4-height-fog.md).

R7.5 reuses the lit HDR target for fog, reducing combined active Vulkan
storage from 36.29 MiB to 30.94 MiB at 1094×631. Independent and combined
effect captures, shadow modes, PT bypass and return, and combined performance
have evidence. The R7.1 focused material and artifact checks remain open.
See the [R7.5 review](../../docs/render/.review/R7.5.md).

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
- [x] Matched RelWithDebInfo results satisfy the combined GPU and texture budgets.
- [x] R7.5 records subsequent design decisions without claiming implementation.

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
