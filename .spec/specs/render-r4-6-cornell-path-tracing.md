# R4.6 Cornell Box Path-Tracing Validation

The stage-level implementation design is [R4.6 plan](../../docs/render/.plan/R4.6.md).
This spec owns the acceptance contract; the plan owns execution order and stop
gates.

## Acceptance revision — 2026-09-22

The user clarified that `save/cornell_box_ref.jpeg` was downloaded from the
internet and is a visual guide. Exact source settings and a pixel match are not
required. R4.6 acceptance now rests on a clearly ray-traced Cornell capture:
correct wall/box/emitter geometry, plausible neutral/red/green diffuse color,
soft area-light shadows, visible red/green bounce light, stable progressive
convergence, and validated reset/resource lifetime. The older manifest and
pixel-threshold requirements below are retained as historical design context;
they do not block visual tuning or completion. Record capture extent, samples,
active RT passes, and known visual differences for review.

- Status: implementation in progress; visual and lifecycle
  acceptance remain open. See the
  [formal review](../../docs/render/.review/R4.6.md)
- Owner: Render policy and validation; Graphics owns physical RT resources and
  Vulkan execution
- Parent TODO: [Render Graph roadmap](../../docs/render/render_graph/TODO.md)
- Stage plan: [R4](../../docs/render/.plan/R4.md#r46--dedicated-cornell-box-path-tracing-validation)
- Predecessor: R4.5 ray-query shadow integration and evidence

## Objective

Produce a dedicated, progressively accumulated Cornell Box path-traced image
that can be compared meaningfully with `cornell_box_ref.jpeg`. The path must
generate primary rays, reconstruct scene hits, sample a rectangular area light,
and integrate at least one diffuse secondary bounce. This validates light
transport that the hybrid R4.5 hard-shadow query cannot exercise: soft
area-light penumbrae, indirect illumination, and red/green color bleeding.

## Current state

- R4.5 ray-query lighting remains a separate hybrid path. R4.6 has a dedicated
  Vulkan RT pipeline with raygen, miss, and closest-hit stages, TLAS traversal,
  Cornell hit reconstruction, rectangular area-light sampling, one cosine
  secondary bounce, and graph-declared ping-pong HDR history.
- The camera UBO is pinned by C++ size/offset assertions; raygen and closest-hit
  now declare the same field offsets. Four samples are traced per dispatch, and
  the profile count reflects actual accumulated samples.
- The latest Vulkan capture is 1094×619 at 10,636 samples. The emitter, neutral,
  red, and green surfaces, boxes, penumbrae, and indirect color are visible. It
  is a qualitative guide comparison under the user's 2026-09-22 clarification.
- Resize was exercised through `window.resize`: the history count reset from
  91,312 to 276 in the first follow-up report at a 560×302 render extent. A
  repeated run restored the original composition and accumulated 10,636 samples.
- The SBT owner checks handle/base alignment, max stride, recursion, buffer
  address alignment and total dispatch invocation count. On the tested RTX 4070
  Laptop GPU, the runtime diagnostic reported 32-byte handle/handle alignment,
  64-byte base alignment, 4096 max stride, 1/31 recursion, and a
  1,073,741,824 invocation limit; raygen/miss/hit addresses were aligned.
- OpenGL fallback was run after the SBT changes: the level loaded and captured,
  RT sample count remained zero, RT passes were absent, and raster tone mapping
  was active. The `stats` response now reports `path_tracing_available` and
  `path_trace_active` explicitly. Vulkan and OpenGL application windows both
  closed on a normal window-close request; no `VUID-` or `Validation Error`
  appeared in the captured validation-run logs.
- The Runtime command surface has no camera motion, in-place scene reload, or
  deliberate RT-failure operation. Camera-motion reset, in-place reload, and
  failed-dispatch recovery remain open. Fresh `GraphicsContractTest` passed
  29/29 and full Debug CTest passed 974/974.
- The local reference is `save/cornell_box_ref.jpeg`; the user's qualitative
  guide clarification means exact pixel comparison is not required. Its tracked
  manifest remains provenance context rather than an acceptance gate.

## Original reference manifest gate (superseded by acceptance revision)

Before implementation tuning, verify the canonical local reference hash and
record one immutable tracked manifest containing:

- reference path, 1920×2030 dimensions, provenance, license/redistribution
  status, and image hash;
- width, height, crop, pixel aspect, and origin convention;
- camera position/orientation or view matrix, projection/FOV, near plane, and
  ray-generation convention at pixel centers;
- rectangular light center, two extent axes, normal, dimensions, emitted
  radiance/color, and sidedness;
- Cornell geometry scale/orientation and material diffuse reflectances;
- exposure, tone mapper, white point if any, transfer function, and output color
  space;
- fixed comparison sample budget, bounce count, RNG seed policy, and metric
  thresholds.

Do not adjust the reference transform or thresholds after viewing the candidate
output without recording a reviewed manifest revision.

## Scope

### Dedicated execution path

- Select a graph variant or dedicated validation mode that replaces the normal
  raster lighting output with an RT storage output for the Cornell fixture.
- Reuse the R4 graph-scheduled BLAS/TLAS ownership and revision rules.
- Keep the path separate from the R4.5 deferred-lighting shader. It may resolve
  into the existing tone-map/capture path, but it must not depend on a raster
  G-buffer for primary visibility or shading data.
- Return an explicit unavailable result when the active backend lacks the full
  RT-pipeline/storage-image contract. OpenGL raster output is not path-tracing
  validation evidence.

### RT pipeline and scene lookup data

- Extend shader assets/import for ray-generation, miss, and closest-hit stages.
- Implement Vulkan RT pipeline creation, shader groups, shader-binding-table
  layout/storage/alignment, descriptor bindings, dispatch, and submission-safe
  retirement behind the existing common contract.
- Publish copied Render-owned geometry/material/instance lookup records keyed by
  stable shader-visible indices. Include vertex/index ranges, transforms and
  normal transforms, material ID, and the data needed for Cornell diffuse
  shading.
- Define how instance custom index, geometry index, primitive index, and
  barycentrics map to those records. Multi-section meshes must not guess a
  material from BLAS or descriptor creation order.

### Integrator

For every pixel and sample:

1. Generate a camera ray using the frozen camera and resolution contract.
2. Trace the TLAS and evaluate the miss environment as black unless the manifest
   explicitly defines another value.
3. At a hit, reconstruct world position, geometric normal, shading normal,
   material/instance identity, and orientation-safe normal handling.
4. Sample one point on the rectangular emitter with a documented area/solid-
   angle PDF, cast a bounded shadow ray, and evaluate emitted radiance and the
   Lambertian BRDF with the correct geometry term.
5. Sample at least one cosine-weighted diffuse secondary direction, update
   throughput, trace it, and include its returned direct/emissive contribution.
6. Avoid counting the emitter through both next-event estimation and an
   unweighted emissive hit. If MIS is omitted, state the chosen exclusion rule.

The minimum accepted depth is one diffuse secondary bounce after the primary
hit. Russian roulette is unnecessary at that depth; if deeper paths are added,
their termination and maximum depth must be explicit.

### Progressive accumulation

- Accumulate linear HDR radiance and a monotonically increasing sample count in
  graph-declared storage resources. Ping-pong images or explicit read/write
  versions must obey graph hazards; in-place history must not rely on implicit
  backend ordering.
- Use a reproducible per-pixel/per-sample RNG sequence. A fixed seed must
  reproduce the same image on the same supported implementation.
- Reset accumulation when any semantic input changes: camera, extent, scene or
  instance revision, geometry, materials, area light, exposure/tone map, shader
  or pipeline revision, integrator settings, or RNG policy.
- A capture/report must include accumulated samples per pixel. A pending or
  partially reset image cannot be labelled as the final comparison.

### Output and comparison

- Apply the frozen exposure, tone map, and color transform exactly once after
  averaging accumulated linear radiance.
- Compare only identically sized/aligned display-space images. Do not compare
  linear HDR values directly with JPEG samples and do not require byte equality.
- Lock the global metrics and thresholds in the reference manifest before
  tuning. At minimum report a perceptual/structural score and luminance/chroma
  error.
- Add local regions or probes that separately cover the red-wall bounce,
  green-wall bounce, neutral interior, emitter, and soft-shadow penumbra. The
  comparison must fail when color bleeding or the penumbra is removed even if a
  global score remains acceptable.
- Export the candidate, reference-aligned difference image, metric report,
  sample count, and render settings under stable validation paths.

## Non-goals

- Production path-tracing renderer mode or editor workflow.
- Denoising, temporal reprojection, adaptive sampling, spectral rendering,
  participating media, depth of field, or motion blur.
- General BSDF/material-graph support beyond the diffuse Cornell materials and
  rectangular emitter.
- Dynamic/skinned geometry, multiple queues, or OpenGL RT emulation.
- Matching the reference by hand-authored postprocessing that is not part of the
  frozen manifest.

## Invariants

- Render owns scene/material/light policy and copied shader lookup records.
- Graphics owns native AS resources, RT pipeline/SBT, storage allocation,
  synchronization, submission, and retirement.
- No Vulkan handle, device address, SBT record, or native shader-group index
  enters Render/Gameplay/Asset contracts.
- The render graph remains the authority for AS consumption, accumulation
  history, storage output, resolve, and capture ordering.
- The dedicated validation path cannot silently fall back and still report a
  successful R4.6 comparison.
- Reference metadata, sample budget, and thresholds are versioned inputs, not
  values selected after inspecting the result.

## Stages

1. **R4.6a — reference contract:** verify the JPEG, freeze the manifest, update
   the Cornell fixture to the matching rectangular emitter/camera contract, and
   complete a scoped review.
2. **R4.6b — RT pipeline backend:** add shader-stage ingestion and complete the
   Vulkan pipeline, SBT, descriptor, dispatch, and retirement implementation.
3. **R4.6c — hit reconstruction:** publish and bind geometry/material/instance
   lookup data, then validate primary/miss/normal/material debug outputs.
4. **R4.6d — lighting and bounce:** implement area-light next-event estimation,
   visibility, one diffuse bounce, and deterministic RNG.
5. **R4.6e — accumulation lifecycle:** add graph-authored HDR history, reset
   keys, sample reporting, resize/reload behavior, and capture readiness.
6. **R4.6f — reference evidence:** render the frozen budget, run global and local
   comparisons, inspect the difference image, record performance/lifetime data,
   and verify orderly close.

## Acceptance criteria (read with the 2026-09-22 revision)

- [x] A captured Cornell scene has reviewable camera framing, geometry,
  diffuse colors, emitter, soft shadows, and secondary color bleeding.
- [ ] Vulkan creates and retires the RT pipeline, SBT, bindings, accumulation
  resources, and AS dependencies without validation errors or leaks.
- [ ] Primary/miss/hit reconstruction is proven independently before indirect
  lighting is evaluated.
- [x] The renderer samples a rectangular emitter and visibly produces a soft
  penumbra.
- [ ] Disabling the diffuse secondary bounce makes red/green local probes fail.
- [ ] Fixed seed plus fixed inputs is reproducible, while accumulation resets on
  every documented semantic revision.
- [x] The final capture reports its extent and sample count and visibly
  converges; the downloaded JPEG is a qualitative guide.
- [x] Unsupported/OpenGL execution reports R4.6 unavailable and keeps the normal
  raster renderer operational without claiming reference evidence.
- [ ] Resize, shader/pipeline failure, capture, cleanup, and orderly application
  close preserve correct lifetime and observable failure behavior.

## Validation plan

- Build and run shader asset/import tests for all RT stages.
- Add Graphics contract tests for pipeline descriptors, SBT alignment/regions,
  resource binding, invalid handles, dispatch dimensions, and retirement.
- Add Render graph tests for AS-read/storage-write/accumulation/resolve ordering,
  history reset, missing physical bindings, and required-pass failure.
- Add deterministic CPU/reference tests for camera rays, barycentric attribute
  reconstruction, rectangle sampling/PDF, cosine hemisphere sampling,
  throughput, and reset-key computation.
- Run the focused Render/Graphics tests, full Debug build and CTest suite, then
  an RT-capable Vulkan runtime session with validation enabled.
- Render the frozen Cornell sample budget, capture the final image and sample
  report, generate the difference artifact, and evaluate every locked global and
  local threshold.
- Exercise camera/light/material revision, resize, failed pipeline/dispatch,
  repeated start/stop, and orderly close. Run OpenGL once to prove explicit
  unavailability and normal raster fallback health.

## Risks and open questions

- The reference metadata is incomplete, but exact agreement is not an acceptance
  gate under the user's visual-guide clarification. Treat the authored Cornell
  camera and rectangular emitter as validation-scene settings, not recovered
  reference parameters.
- The current model/material path may not expose enough per-primitive lookup
  information for closest-hit reconstruction; section-to-geometry identity must
  be designed without backend leakage.
- One sample per frame can make validation prohibitively slow. Batched samples
  per dispatch are allowed if sample indexing and cancellation/reset remain
  deterministic and responsive.
- Self-intersection offsets must scale with scene units and ray direction;
  hardcoded large epsilons can erase contact shadows and bias the comparison.
- A superficially similar exposure can hide transport errors. Debug outputs and
  local probes are required before the final reference score is accepted.
