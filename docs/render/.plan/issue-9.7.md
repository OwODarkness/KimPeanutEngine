# issue-9.7 — Sponza Quality and Throughput Stage Design

**Status: Stage 0 instrumentation and the Stage 1 runtime mip path landed
2026-09-07; native-product and runtime baseline evidence remain pending.**

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

- Add maximum-dimension and compression policy to AssetImport's native texture
  processing.
- Select formats from declared device capabilities with a portable fallback.
- Expose artifact and resident-byte accounting; reject products that violate
  declared dimensions or subresource sizes.

Exit: the reference profile meets its recorded memory ceiling without visible
loss of material identity or alpha coverage.

### Stage 3 — descriptor and submission lifetime

- Replace pool-per-set creation with fence-safe frame-slot arenas.
- Cache stable material texture bindings and retain transient bindings only for
  genuinely frame-local resources.
- Add lifecycle, arena-growth, and deferred-destruction tests.

Exit: steady-state pool creation is zero and descriptor work is bounded by
changed materials/frame slots, not Sponza draw count.

### Stage 4 — visibility granularity and static shadows

- Persist section bounds in native model artifacts and validate them at load.
- Add section-level frustum rejection after proxy broad phase.
- Add directional-shadow validity stamps and conservative invalidation tests.

Exit: hidden Sponza sections do not produce camera or shadow draws, and an
unchanged static frame records zero shadow-caster draws after warm-up.

### Stage 5 — measured pass optimization

Re-profile. Only if GPU evidence still identifies G-buffer overdraw, compare
front-to-back sorting with a depth pre-pass and `EQUAL` G-buffer depth testing.
Only if shadow sampling is material, evaluate kernel/resolution changes. Add
post-process anti-aliasing only for remaining geometry/shader edges.

Exit: every retained option has before/after pass timings and inspected image
comparisons; rejected options and their cost are journaled.

### Stage 6 — closure evidence

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
- No per-draw descriptor pool creation occurs in steady state, and lifetime
  tests prove reset/destruction happens only after the owning frame fence.
- Visibility counters demonstrate section rejection for camera views that do
  not contain the complete model.
- An unchanged static sun/caster frame reuses its shadow map; all declared
  dependency changes invalidate it.
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
- Incorrect shadow stamps can reuse stale depth, so invalidation tests must
  cover light, transform, load/unload, and bounds changes.
