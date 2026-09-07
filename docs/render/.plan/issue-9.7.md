# issue-9.7 — Sponza Quality and Throughput Stage Design

**Status: Stages 0–5 and Stage 6.0–6.3 CPU submission slices have landed. A
supplied post-Stage-5 snapshot attributes the remaining approximately 30 FPS
result to CPU command submission; fixed-window performance-build proof remains
open. Block compression remains pending.**

Links: [issue](../issue/issue-9.7.md),
[formal review](../.review/issue-9.7.md),
[execution spec](../../../.spec/specs/sponza-render-quality-performance.md),
[journal](../../../.spec/journal/2026-09-07-sponza-render-quality-performance.md),
and [Render roadmap](../TODO.md).

## Objective

Remove Sponza's texture-frequency speckle and restore predictable frame
throughput without hiding the problem through blur or globally lowering
quality. The design must work through common Render/Graphics contracts on both
Vulkan and OpenGL.

The concrete design question is: how should render-ready texture detail,
descriptor lifetime, visibility granularity, and static-shadow validity be
represented so cost scales with visible work rather than imported source size
or draw count?

The Stage 6 refinement is: how can one prepared draw packet select frame-local
uniform ranges and stable resources without allocating/updating descriptors,
revalidating invariant state, or copying section ownership data per draw?

## Boundary and ownership

- **Asset and offline import** own source identity, decoding, semantic metadata,
  immutable native-product publication, model section bounds, and CPU asset
  lifetime.
- **Resource processing** validates accepted texture products and creates
  immutable render-ready artifacts, including explicit mip subresources and
  selected dimensions/formats. It owns no GPU object.
- **Render** chooses material sampling policy, persistent versus frame-local
  binding policy, section visibility, and shadow invalidation.
- **Graphics/RHI** uploads declared subresources, selects supported backend
  formats, owns descriptor pools/sets and synchronization, records timestamps,
  and releases resources only after submitted work is safe.

No common contract may expose Vulkan or OpenGL types.

## Measured CPU constraint

The supplied snapshot reports 32.0568 ms/frame, 30.924 ms Render CPU,
28.1182 ms record CPU, and 10.98 ms total GPU. Directional shadow consumes
9.127 ms CPU for 446 draws and the G-buffer consumes 16.232 ms CPU for 264
draws. Present is 0.2795 ms and pacing is zero.

With about 3.94 ms outside recording, the 60 Hz record budget is at most
12.73 ms. Stage 6 must reduce record CPU by at least 55% in the same scenario.
A shadow-cache hit saves about 9.13 ms but is insufficient alone; after that
win, the G-buffer must save at least 6.26 ms (about 39%) if other costs remain
fixed. The 8.33 ms stretch goal is deferred until GPU work drops below that
budget.

## Reference decision

- The Khronos [descriptor-management sample](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/performance/descriptor_management/README.adoc)
  matches the measured failure and recommends cached descriptors plus a small
  number of per-frame buffers addressed with dynamic offsets.
- The Khronos [dynamic uniform-buffer sample](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/api/dynamic_uniform_buffers/dynamic_uniform_buffers.cpp)
  demonstrates one aligned buffer and stable descriptor set with a changing
  per-draw offset. Adopt that data flow behind the common RHI contract.
- Godot's [RenderingDevice](https://github.com/godotengine/godot/blob/master/servers/rendering/rendering_device.h)
  caches layout compatibility to avoid costly redundant rebinds. Adopt the
  recorder-local state-cache principle without importing its render graph or
  RID architecture.
- Khronos' [command-buffer usage sample](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/performance/command_buffer_usage/README.adoc)
  supports multithreaded secondary recording only after enough draw work exists
  per buffer. Defer it for the current 713-draw frame until serial descriptor
  and state churn are removed and measured again.

## Chosen design

### Explicit semantic mip artifacts

The offline product carries semantic metadata and populated mip payloads;
Resource exposes them as a complete list of render-ready mip subresources.
Runtime texture creation derives the mip count from populated data and never
declares levels that have no payload. Both backends upload the same logical
chain. A temporary runtime-generation fallback may support authored loose
textures during migration; a backend-generation fast path may be added later
only if its output is equivalent and capability-gated.

Downsampling rules are semantic:

- base/emissive color: linearize sRGB, filter, then encode for sRGB storage;
- normal: filter vectors in linear space and renormalize; preserve a route for
  variance-aware roughness when that material feature exists;
- metallic/roughness/occlusion: filter linear scalar channels without color
  transfer;
- opacity mask: preserve alpha coverage to avoid disappearing foliage and
  grilles at distance.

Sampler maximum LOD is compatible with the uploaded chain. Anisotropy remains
capability-clamped; it complements mipmaps and does not replace them.

### Bounded native texture products

Stage 0 records the current referenced texture set, source/decoded/resident
bytes, and device format support. AssetImport's native product pipeline then
selects a profile-specific maximum dimension and a capability-compatible block-
compressed format: high-quality color/packed data, two-channel normal storage
where appropriate, and RGBA8 fallback. The artifact records the choice; Render
does not infer it from filenames.

The initial Sponza reference profile has a 1.5 GiB resident-texture ceiling.
Stage 0 may revise that ceiling only by recording the device, quality impact,
and reason in the journal.

### Descriptor lifetime proportional to frames and materials

Each in-flight frame slot owns reusable descriptor pools reset only after its
fence completes. Stable material texture bindings are cached by immutable
material/resource identity and retired with the owning GPU resources. Per-draw
data uses the existing frame-local uniform/dynamic-offset path where compatible.

Acceptance forbids descriptor-pool creation proportional to section count.
After warm-up, a steady fixture frame creates no new pool unless a documented
arena growth event occurs.

### Section visibility and shadow validity

Native model sections carry local bounds. Render transforms and tests those
bounds after the proxy-level broad phase, then emits only visible sections.
The same section identities feed camera and shadow draw lists.

Directional shadow state carries a validity stamp over light parameters,
shadow projection inputs, caster membership/bounds, and geometry revisions. A
static unchanged frame reuses the prior map; a changed dependency invalidates
it before sampling. Dynamic casters remain conservatively dirty until a more
specific update policy is proven.

## Ordered stages

### Stage 0 — reproducible baseline and attribution

Implementation status: the checked-in scenario descriptor, Render/Graphics
profile snapshot, CPU phase timings, Vulkan/OpenGL pass timer-query hooks,
draw/section counters, descriptor counters, present-mode reporting, and
texture closure/resident-byte accounting are landed. Runtime baseline samples
and captures still require a successful native build and controllable window.

- Define a checked-in camera pose, viewport extent, graphics API, build type,
  warm-up duration, and sample window for Sponza.
- Add CPU phase timings; per-pass GPU timestamps; draw/section counters;
  descriptor pool/set counts; texture source, decoded, and resident bytes;
  selected present mode; and shadow-cache hits.
- Capture scene color, base color, world normal, material parameters, shadow
  visibility, and the baseline metrics on Vulkan and OpenGL.

Exit: p50/p95 frame time is split into CPU, GPU passes, and present behavior,
and the 72-texture dependency closure is reported by the runtime path.

### Stage 1 — mip correctness

Implementation status: the runtime slice is landed. `TextureData` carries
semantic metadata plus explicit initialized levels, the bounded loose-texture
fallback generates semantic-aware chains, samplers allow the populated LOD
range, and Vulkan/OpenGL upload every supplied level. Native archive texture
products remain the follow-up required for the offline-artifact portion.

- Define semantic texture metadata and explicit mip-subresource artifacts.
- Implement offline semantic-aware mip generation, a bounded loose-texture
  fallback, and alpha-coverage tests.
- Extend the common upload contract and both backends to upload every supplied
  level; derive sampler LOD from the populated chain.
- Add tiny deterministic texture tests for color, normal, packed data, and
  opacity masks.

Exit: G-buffer channels remain spatially stable under camera movement and both
backends sample only initialized mip levels.

### Stage 2 — texture budget

- Implementation status: the database-free `TextureImporter` →
  `TextureCooker` path now emits immutable `.texture` products with semantic
  metadata, explicit mip payloads, bounded dimensions, and portable RGBA8 or
  RGBA16F storage. Runtime `NativeTextureLoader` consumes those products;
  model import emits them for embedded and external material images, and the
  asset tool exposes `cook-texture` for direct sources.
- The compression policy is explicit: `Portable` is supported by the current
  RHI contract, while `RequireBlockCompression` fails clearly because BCn/
  ASTC formats are not yet represented by `TextureFormat` or both backends.
- Product parsing validates dimensions, format, semantic value, mip extents,
  byte ranges, and integrity before accepting a product.

Exit: the reference profile meets its recorded memory ceiling without visible
loss of material identity or alpha coverage, with a measured native-product
resident-byte report. Block-compression selection is a follow-up capability
stage rather than a silent fallback.

### Stage 3 — descriptor and submission lifetime

- Implementation status: partially landed. Vulkan descriptor allocation now
  uses reusable,
  fence-safe frame-slot arenas. Each slot resets its arenas only after the
  matching in-flight fence completes; a full arena grows by adding a reusable
  arena for that slot. Destroying a transient set releases only its generational
  handle, so it no longer destroys a Vulkan pool per draw. The supplied profile
  shows that set allocation/update per draw is still open and is promoted into
  Stage 6.
- Cache stable material texture bindings and retain transient binding objects
  only for genuinely frame-local resources. Offset changes within the frame
  uniform arena must not create a new descriptor set.
- Add lifecycle, arena-growth, and deferred-destruction tests.

Exit: steady-state pool creation is zero and descriptor work is bounded by
changed materials/frame slots, not Sponza draw count.

### Stage 4 — visibility granularity and static shadows

- Implementation status: native model version 2 persists section local AABBs and
  validates every indexed vertex against its section bounds. Version 1 products
  remain loadable by deriving section bounds from their indexed vertices.
- Implementation status: camera, directional, spot, and point shadow paths
  reject individual section world bounds after the proxy broad phase and issue
  section-indexed draws.
- Implementation status: directional shadow targets use conservative validity
  stamps over light, effective fitted bounds/matrices, caster transform/bounds,
  material, and section identity inputs. The initial fit is caster-driven and
  includes the camera only when it lies outside that fit; camera motion inside
  the unchanged fitted volume no longer invalidates the map. Resize and changed
  dependencies still invalidate the stamp. Stage 6.1 focused tests cover
  inside-fit reuse, outside-fit camera invalidation, and light/caster changes.

Exit: hidden Sponza sections do not produce camera or shadow draws, and an
unchanged static frame records zero shadow-caster draws after warm-up.

### Stage 5 — measured pass optimization

Implementation status: the G-buffer opaque list now has a Render-owned
front-to-back ordering policy based on section world-bound centers and the
active camera direction. Equal-depth items retain the existing deterministic
pipeline/material/mesh/section tie-break. This is the low-risk candidate from
the measured pass options; a depth pre-pass and `EQUAL` G-buffer depth testing
remain deferred until a valid GPU comparison shows that the extra pass pays.

Re-profile the fixed Sponza scenario after startup has completed. Only if GPU
evidence still identifies G-buffer overdraw, compare front-to-back sorting with
a depth pre-pass and `EQUAL` G-buffer depth testing.
Only if shadow sampling is material, evaluate kernel/resolution changes. Add
post-process anti-aliasing only for remaining geometry/shader edges.

Exit: every retained option has before/after pass timings and inspected image
comparisons; rejected options and their cost are journaled.

### Stage 6 — CPU submission path

#### Stage 6.0 — subphase proof

- Implementation status: Render/Graphics now publish per-frame subphase timers
  and counters for section preparation, shadow scheduling, material lookup,
  uniform writes, descriptor work, pipeline validation, requested/emitted binds,
  and native draws. The fixed profile window reports CPU-subphase p50/p95 and
  the completion log includes the diagnostic counters. Runtime Vulkan Debug,
  Vulkan performance, and OpenGL performance captures remain pending.
- Add CPU timers and counts for section-packet build, shadow stamp/fit,
  material resolution, uniform writes, descriptor search/allocation/update,
  pipeline validation, requested/emitted native binds, and draw calls.
- Record one warm fixed-window profile on Vulkan Debug with validation, Vulkan
  performance build without validation, and OpenGL performance build. Keep the
  supplied snapshot as the diagnostic baseline, not the final comparator.

Gate: descriptor/update and state-bind counters explain the majority of the
25.359 ms geometry-pass CPU time before changing the common contract. If they
do not, use a sampling CPU profiler and revise the stage rather than guessing.

#### Stage 6.1 — effective directional-shadow validity

- Implementation status: landed. The directional shadow stamp now hashes the
  effective caster fit and fitted matrices rather than raw camera position.
  Cameras inside the existing orthographic fit reuse the depth target; cameras
  outside it expand the effective fit and force a redraw. Light and caster
  changes remain conservative invalidation inputs. Repeated editor viewport
  extent requests now preserve the cache when the extent is unchanged; actual
  target-size changes still invalidate it.
- Build the effective caster bounds and fitted matrices before computing the
  validity stamp. Do not hash raw camera position when it lies inside the same
  fitted volume and produces identical matrices.
- Stamp light/shadow identity, target generation, effective fit, caster
  membership, mesh/material revisions, visibility, and transforms. Add focused
  invalidation tests for every dependency.
- For a moving camera that changes receiver coverage, compare a stable texel-
  snapped/quantized fit with exact refits. Retain the exact path unless the
  stable fit has inspected image and invalidation evidence.

Exit: an unchanged static Sponza scene, including camera motion inside the same
effective fitted volume, records zero directional-shadow caster draws after
warm-up. Any effective map change forces a miss.

#### Stage 6.2 — stable binding sets plus dynamic uniform offsets

- Implementation status: landed. Stable per-frame-context geometry bindings use
  an API-neutral dynamic-uniform
  intent and extend binding commands with ordered dynamic offsets. Preserve
  the existing uniform alignment and range validation.
- Pack per-pass data once per pass, per-object data once per proxy/revision,
  material constants once per material instance/revision per frame slot, and
  selection constants from shared selected/unselected allocations where
  practical.
- Allocate one compatible geometry binding set per pipeline/frame slot (or a
  small cache keyed by stable buffer/image/sampler identities). Vulkan maps the
  dynamic intent to `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`; OpenGL selects
  the same ranges with `glBindBufferRange`.
- Keep bindless image/sampler bindings in the existing table. On non-bindless
  devices, cache the stable material texture set separately from the dynamic
  object/frame set so texture identity does not force per-draw reconstruction.
- Key cache retirement by pipeline layout generation, resource generations,
  frame slot, and completed submission/fence epoch. Never reuse a set across an
  incompatible layout or before its slot is safe.

Exit: after warm-up, each geometry draw performs zero descriptor allocation
and zero descriptor update. Descriptor counts scale with pipelines, changed
stable materials, and frame slots—not sections. Vulkan and OpenGL render the
same frame data through the common contract. Focused stable-binding coverage
passes; fixed-scenario runtime counters and visual comparison remain pending.

#### Stage 6.3 — recorder state cache and lean draw packets

- Implementation status: landed. Recorder-local compatibility, pipeline, mesh,
  and binding state is reused within each target/frame boundary. Deferred
  rendering now snapshots `RenderWorld` once per frame and prepares one shared
  section-packet array; camera visibility filters that array, and section
  packets do not copy `section_materials`.

- Validate target/pipeline compatibility when the active target or pipeline
  changes, not for repeated requests of the same pair. Keep generational-handle
  checks and failure diagnostics at state transitions.
- Track recorder-local last-bound pipeline, mesh, descriptor set, and bindless
  table. Suppress only native calls proven redundant for the active command
  buffer; reset the cache at target/frame boundaries as required by each API.
- Build one lean immutable section-packet array from one RenderWorld snapshot.
  A packet carries identities, resolved handles, world bounds, section range,
  dynamic offsets, and sort keys; it must not own a copied
  `section_materials` vector.
- Reuse those packets for directional fitting/stamping, shadow filtering, and
  G-buffer visibility. Cache static resolution by explicit RenderWorld,
  mesh-section, and material revisions; do not infer immutability from pointer
  stability.
- Preserve front-to-back ordering within a bounded pipeline/material strategy.
  Measure requested versus emitted binds so GPU overdraw and CPU state locality
  can be compared rather than assumed.

Exit: compatibility checks and native pipeline/mesh binds scale with actual
state changes, section preparation performs no per-section heap allocation,
and the same packet identity drives profiling and drawing.

#### Stage 6.4 — OpenGL uniform upload correction

- Track the dirty range of the active frame-slot uniform arena. Upload that
  range once before its first consumer, or use a capability-gated persistent
  mapping path only when measured.
- Remove the all-mapped-buffer `glBufferSubData()` loop from descriptor bind
  paths. A draw bind may select ranges/offsets but may not upload every frame
  arena.

Exit: OpenGL uploaded uniform bytes are bounded by the active slot's written
range rather than `draw_count × total_mapped_capacity`.

#### Stage 6.5 — re-profile before larger submission features

- Repeat the exact fixed window after each substage and retain only changes
  with improved p50/p95 CPU record time and no GPU/visual regression.
- If record p95 remains above 12.73 ms, capture a sampling CPU profile. Consider
  multithreaded secondary command recording, indirect/multi-draw, or a GPU-
  driven path only when the remaining trace proves command encoding itself is
  dominant. These are not prerequisites for 60 Hz.

Exit: performance-build p95 record time is at most 12.73 ms and total frame
p95 is at most 16.67 ms in the fixed scenario.

### Stage 7 — closure evidence

- Run focused tests, full validation, and Vulkan/OpenGL smoke/captures.
- Measure the same baseline scenario in a non-validation performance build;
  keep validation builds as correctness evidence.
- Update the review finding status, roadmap, journal, and project status.

## Acceptance criteria

- Scene-color, base-color, world-normal, and material-parameter captures show
  no temporally unstable speckle at the baseline pose or during the camera path.
- Vulkan and OpenGL produce equivalent material detail and valid alpha-masked
  geometry from the same explicit mip artifacts.
- The reference fixture reports 72 texture dependencies and stays within the
  recorded resident-memory ceiling.
- No per-draw descriptor allocation or update occurs in steady state, and
  lifetime tests prove cache reset/destruction happens only after the owning
  frame fence. Counts scale with pipelines/material revisions/frame slots.
- Visibility counters demonstrate section rejection for camera views that do
  not contain the complete model.
- An unchanged effective static sun/caster map reuses its shadow target even
  when raw camera motion remains inside the same fitted volume; all declared
  dependency changes invalidate it.
- Performance-build p95 record time is at most 12.73 ms for the supplied
  hardware/scenario, with descriptor, bind, packet-build, and uniform-upload
  counters attached to the result.
- On the reference machine and fixed scenario, the performance build reaches
  p95 total frame time at or below 16.67 ms after warm-up. The stretch target is
  8.33 ms. CPU, GPU-pass, and present measurements accompany the result.
- No regression appears in existing PBR fixtures, capture views, resize,
  teardown, or resource-lifetime validation.

## Non-goals

- Revisiting the resolved black-frame/uniform defect.
- Introducing a render graph, virtual texturing, mesh shaders, or bindless as a
  prerequisite.
- Solving transparent material ordering or replacing the material model.
- Globally reducing shadow quality, texture resolution, or viewport extent
  without profile data and inspected comparisons.

## Risks

- CPU-generated mip results can differ from driver generation; golden tests
  need tolerance and semantic invariants rather than byte identity.
- Compression support differs across devices; fallback accounting must be
  included in the budget.
- Alpha-coverage preservation and normal filtering are quality-sensitive.
- Descriptor caching can retain textures or race deferred destruction if
  ownership keys and fence epochs are incomplete.
- Dynamic uniform offsets require layout-order and alignment agreement across
  shaders, common RHI validation, Vulkan, and OpenGL; a mismatch can select the
  wrong object's constants without an obvious lifetime failure.
- Incorrect shadow stamps can reuse stale depth, so invalidation tests must
  cover light, transform, load/unload, and bounds changes.
- Recorder state suppression is valid only inside a known command-buffer and
  target lifetime; external/editor recording must invalidate or isolate the
  cache.
