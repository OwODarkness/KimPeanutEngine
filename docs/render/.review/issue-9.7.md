# Review — issue-9.7 Sponza Texture Aliasing and Frame Throughput

**Latest review: 2026-09-07, CPU submission follow-up.**

**Disposition: changes requested.**

**Implementation status: the supplied post-Stage-5 snapshot attributes the
approximately 30 FPS result to CPU command construction/submission. The GPU is
inside a 60 Hz budget, but descriptor-set construction, repeated draw-state
resolution, and ineffective directional-shadow reuse remain open.**

Related records: [issue](../issue/issue-9.7.md),
[stage design](../.plan/issue-9.7.md),
[execution spec](../../../.spec/specs/sponza-render-quality-performance.md),
[journal](../../../.spec/journal/2026-09-07-sponza-render-quality-performance.md),
and [Render roadmap](../TODO.md).

## Scope and acceptance basis

This review covers the remaining visible speckle and approximately 30 FPS
Sponza result. It excludes the resolved black-frame/uniform-exhaustion defect.
Acceptance requires stable material detail in diagnostic captures, measured
frame-time attribution, bounded texture and descriptor costs, and fresh
Vulkan/OpenGL runtime evidence.

## Evidence inspected

- Diagnostic images under `save/screenshots/validation/`, including scene
  color, base color, world normal, material parameters, and shadow visibility.
- Texture creation, sampling, upload, descriptor, draw-list, visibility, and
  directional-shadow code in Render and Graphics.
- The selected `asset/level/sponza.level` model and its glTF manifest: 115
  meshes, 405 primitives, 28 materials, and 72 referenced 4K images. Primitive
  alpha modes are 401 opaque and 4 blend.
- Iago Toral's [Sponza frame analysis](https://blogs.igalia.com/itoral/2018/04/17/frame-analysis-of-a-rendering-of-the-sponza-model/)
  and its linked [vkdf implementation](https://github.com/itoral/vkdf/blob/master/demos/sponza/main.cpp).
- Khronos' [Vulkan descriptor-management sample](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/performance/descriptor_management/README.adoc).
- Khronos' [dynamic uniform-buffer sample](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/api/dynamic_uniform_buffers/dynamic_uniform_buffers.cpp),
  Godot's [RenderingDevice compatibility/cache contract](https://github.com/godotengine/godot/blob/master/servers/rendering/rendering_device.h),
  and Khronos' [command-buffer usage sample](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/performance/command_buffer_usage/README.adoc).

The external examples are design evidence, not performance baselines. The
vkdf demo targets different hardware, resolution, content products, and frame
pacing.

## CPU submission follow-up

### Supplied profile attribution

| Measure | Observed | Consequence |
| --- | ---: | --- |
| Frame | 32.0568 ms | Approximately 31.2 FPS; above the 16.67 ms acceptance budget. |
| GPU total | 10.98 ms | The measured GPU workload fits 60 Hz with about 5.69 ms of headroom. |
| Render CPU | 30.924 ms | The frame is CPU-bound, not GPU-bound. |
| Record CPU | 28.1182 ms | 90.9% of Render CPU and 87.7% of the complete frame. |
| Directional shadow | 9.127 ms CPU / 5.56 ms GPU, 446 draws | About 20.5 microseconds CPU per draw; the cache missed. |
| G-buffer | 16.232 ms CPU / 4.62 ms GPU, 264 draws | About 61.5 microseconds CPU per draw. |
| Geometry-pass CPU | 25.359 ms | 90.2% of command-recording time. |
| Present | 0.2795 ms | Present pacing is not the limiting wait in this snapshot. |

The non-record portion of the measured frame is about 3.94 ms. Meeting 60 Hz
therefore requires record time at or below about 12.73 ms, a 55% reduction.
Making the directional-shadow cache hit would remove about 9.13 ms from this
sample but would still leave a frame near 22.93 ms. The G-buffer path must then
save at least another 6.26 ms, or about 39%, assuming other costs remain
unchanged. The 8.33 ms stretch target is not currently feasible on the GPU
because measured GPU time alone is 10.98 ms; it is not the first optimization
target.

This is a decisive bottleneck sample, but it is not yet the plan's required
warm p50/p95 evidence. The next measurement must also identify API, build type,
validation-layer state, viewport, present mode, descriptor set updates, native
state-bind calls, and cache hits across the fixed sample window.

## Local evidence map

| Concern | Evidence |
| --- | --- |
| Original one-level texture creation (resolved) | [`DefaultTextureSettings`](../../../engine/runtime/render/render_resource_resolver.cpp) |
| Original sampler LOD clamp (resolved) | [`SamplerSettings`](../../../engine/runtime/graphics/backend/common/sampler.h) |
| Original Vulkan level-zero-only upload (resolved) | [`VulkanUploadContext`](../../../engine/runtime/graphics/backend/vulkan/vulkan_upload_context.cpp) |
| Frame-slot pool reuse but per-draw set allocate/update | [`VulkanDescriptorSetManager`](../../../engine/runtime/graphics/backend/vulkan/vulkan_descriptor_set_manager.cpp), lines 134–279 |
| Per-draw transient material binding | [`FrameContext`](../../../engine/runtime/render/frame_context.cpp), lines 138–263 |
| Section candidate copies and repeated expansion | [`scene_visibility.cpp`](../../../engine/runtime/render/render_world/scene_visibility.cpp), [`scene_draw_list_builder.cpp`](../../../engine/runtime/render/render_world/scene_draw_list_builder.cpp) |
| Shadow cache and repeated caster preparation | [`DeferredRenderer`](../../../engine/runtime/render/deferred_renderer.cpp) |
| Per-draw pipeline compatibility and native state binds | [`VulkanCommandRecorder`](../../../engine/runtime/graphics/backend/vulkan/vulkan_command_recorder.cpp), lines 67–153 |
| OpenGL whole-buffer upload on every binding | [`OpenglCommandRecorder`](../../../engine/runtime/graphics/backend/opengl/opengl_command_recorder.cpp), lines 183–207 |
| Material channel sampling | [`pbr_gbuffer.frag`](../../../asset/shader/pbr_gbuffer.frag) |
| Nine-tap shadow PCF | [`deferred_lighting.frag`](../../../asset/shader/deferred_lighting.frag) |

## Findings

### issue-9.7-F1 — P0: sampled textures have no minification hierarchy

**Runtime implementation status: addressed.** Native archive-product
serialization is now landed; Vulkan/OpenGL runtime capture evidence remains
open.

The former `DefaultTextureSettings()`/sampler path fixed `mip_levels` and
`max_lod` at zero. The runtime path now creates explicit initialized semantic
mip subresources, derives the image level count from that payload, and permits
sampling across the populated chain. The G-buffer shader therefore no longer
has to sample only the full-resolution base level at arbitrary projected
sizes.

OpenGL uploads every supplied level explicitly. Vulkan packs every supplied
level into one staging upload and copies each level with its own subresource
region; merely increasing `mip_levels` without payload data is rejected.

### issue-9.7-F2 — P0: the current texture product has no viable memory budget

**Implementation status: bounded portable product landed; hardware compression
remains open.**

The selected model references 72 4096×4096 images. Decoded RGBA8 base levels
occupy about 4.5 GiB; a complete uncompressed mip hierarchy is about 6 GiB.
This corrects the earlier directory-wide count of 135 images, which was not the
selected model's dependency closure.

The database-free importer/cooker now applies semantic downsampling, a
2048-dimension default profile, and canonical native `.texture` products with
RGBA8/RGBA16F portable storage. Runtime consumes these products without source
decode. The current RHI exposes no BCn/ASTC formats, so block compression is an
explicit follow-up rather than an unrecorded fallback. Resident-byte ceiling
evidence for the Sponza profile is still required.

### issue-9.7-F3 — P0: Vulkan descriptor work still scales with draws

**Implementation status: partially addressed; promoted to P0 by the supplied
profile.** Frame-slot arenas removed pool-per-draw creation, but every geometry
draw still allocates and updates a descriptor set.

At the original review, `VulkanDescriptorSetManager::CreateResourceBindingSet()`
created a single-set `VkDescriptorPool`, allocated one set, and later destroyed
the whole pool. Stage 3 corrected pool ownership, but
`FrameContext::CreateMaterialBinding()` still creates a set per draw and retires
the handles at the next frame begin.

Descriptors need frame-slot arena ownership after fence completion, with
persistent bindings for stable material textures. Pool creation must not scale
with draw count.

The remaining path is now the primary CPU finding. `RecordMeshProxy()` creates
fresh per-pass, per-object, selection, and material-constant allocations for
each section, then `FrameContext::CreateMaterialBinding()` reconstructs a
binding vector and calls `CreateResourceBindingSet()`. Vulkan counts bindings,
searches an arena, allocates a set, builds three temporary write/info vectors,
resolves every handle, and calls `vkUpdateDescriptorSets()` for every draw.
The shadow path repeats the same set allocate/update operation for two uniform
bindings per caster. Khronos documents per-draw descriptor updates as capable
of costing more CPU time than the draws themselves; this local profile has the
same shape.

Replace offset-specific per-draw descriptor construction with frame-slot
binding sets backed by the existing aligned frame uniform buffer and dynamic
uniform offsets. Persistent image/sampler identities remain cached by material
or in the existing bindless table. The steady-state acceptance condition is
zero descriptor allocations and zero descriptor updates per geometry draw.

### issue-9.7-F4 — P1: proxy-level visibility cannot reject Sponza sections

**Implementation status: addressed by Stage 4.** The current profile reports
264 visible G-buffer sections versus 446 directional-shadow caster sections,
which is consistent with pass-specific section rejection. Packet construction
overhead is tracked separately in F9.

Scene visibility tests one `MeshProxy` AABB, then the draw-list builder expands
all visible proxy sections. Sponza is one proxy with 405 primitives; once its
aggregate bounds intersect the frustum, no section can be rejected.

Native model artifacts need per-section bounds, and Render needs section-level
visibility while retaining proxy-level ownership.

### issue-9.7-F5 — P1: an unchanged directional shadow map is rebuilt

**Implementation status: cache mechanism landed, but the supplied profile
shows zero hits and one miss; the performance finding remains open.**

At the original review, the directional shadow pass submitted all caster
sections every frame. Stage 4 added a cache, but the current miss still submits
446 geometry draws for a map whose effective inputs may be unchanged.

Shadow validity needs an explicit stamp derived from light state, caster/world
bounds, and relevant geometry revisions. Reuse must remain conservative for
dynamic casters.

The landed stamp hashes the raw camera position at
`deferred_renderer.cpp:232-276`, while the fitted shadow bounds only change
when that position expands the caster bounds at lines 118–146. Camera movement
inside the already-covered Sponza caster bounds therefore invalidates an
identical map. Compute the effective fitted bounds/matrices first and stamp the
inputs that can actually change recorded depth. Retain light, caster identity,
geometry/material revision, transform, visibility, and target-generation
dependencies. Any camera/receiver contribution must be stabilized and included
only when it changes the fitted projection.

### issue-9.7-F6 — P1: FPS is not yet attributable

**Implementation status: addressed for bottleneck class by the supplied
snapshot; complete fixed-window p50/p95 evidence remains open.**

At the original review, the engine target rate could not prove the active
bottleneck: Debug Vulkan used validation, present mode could fall back to FIFO,
and pass GPU timestamps were not exposed. Stage 0 added the required telemetry,
and the supplied snapshot now identifies CPU recording as the limiting class.

Add frame-phase CPU timings, pass GPU timestamps, draw and descriptor counters,
texture resident bytes, selected present mode, and shadow-cache counters before
claiming a performance cause or improvement.

The snapshot now rules out GPU execution and present pacing as the 30 FPS
limiter. It does not yet distinguish time inside material resolution,
descriptor allocation/update, command validation/binds, draw-list building,
or heap allocation, so those sub-phases must be instrumented before choosing
between the two first-order CPU changes.

### issue-9.7-F7 — P2: later pass optimizations must remain measured choices

Per-section culling from the reference implementation applies directly.
Front-to-back opaque G-buffer ordering is now the cheaper candidate and uses
section bounds already produced by Stage 4. A depth pre-pass may also pay when
G-buffer fragments are the measured GPU limit. PCF kernel size, shadow
resolution, and post-process anti-aliasing are quality/performance controls,
not substitutes for mip correctness or CPU submission fixes.

The supplied GPU time reinforces this priority. Front-to-back sorting may help
the 4.62 ms G-buffer GPU pass, but it cannot close a 16.23 ms G-buffer CPU
recording cost. Do not add a depth pre-pass until CPU submission is below the
60 Hz budget and a new GPU A/B shows a net frame win.

### issue-9.7-F8 — P1: the command recorder repeats invariant validation and binds

`RecordMeshProxy()` and `RecordShadowCaster()` bind pipeline and mesh for every
section (`deferred_renderer.cpp:1984-2067`). Vulkan's recorder then reconstructs
a partial `PipelineDesc`, allocates a diagnostic `std::string`, and validates
target compatibility on every `BindPipeline()` call. It also resolves and
binds the same pipeline, bindless set, vertex buffer, and index buffer even when
the preceding draw used identical state (`vulkan_command_recorder.cpp:67-153`).

Validate pipeline/target compatibility once when beginning a target or when the
pipeline actually changes. Add recorder-local last-bound state and counters for
requested versus emitted pipeline, mesh, and descriptor binds. Keep stale-
handle validation at the public boundary; do not remove correctness checks in
exchange for speed.

### issue-9.7-F9 — P1: section preparation performs avoidable copies and repeated work

`SceneVisibility::BuildSections()` copies a full `MeshProxy`, including its
`section_materials` vector, for every section and then clears that copied vector
(`scene_visibility.cpp:30-81`). Directional-shadow scheduling builds all
section candidates for fitting/stamping, and the directional pass builds them
again before recording (`deferred_renderer.cpp:730-790` and 936–989). The
G-buffer builds another section list and repeats material readiness, template,
pipeline, and draw-class lookups in the draw-list builder and again while
creating material bindings.

Build one immutable, lean per-frame section packet array from one RenderWorld
snapshot. Store proxy/section identity, resolved mesh/material/pipeline, world
bounds, and compact sort keys without owning a copied section-material vector.
Derive camera and shadow views from those packets and refresh them only when
RenderWorld or resource/material revisions change.

### issue-9.7-F10 — P1: OpenGL uploads every mapped uniform arena on every bind

The OpenGL recorder loops over all mapped uniform buffers and issues a complete
`glBufferSubData()` for each one every time a resource-binding set is bound
(`opengl_command_recorder.cpp:183-207`). The configured frame arena is 4 MiB,
so this policy scales uploaded bytes with draw count rather than dirty data.
`OpenglBackend::BindResourceBindingSet()` contains the same behavior on its
legacy direct path.

Track the written range of the active frame slot and upload it once before its
first draw, or use persistent/coherent mapping when supported and measured.
Binding a draw should only select buffer ranges and dynamic offsets. This is a
cross-backend correctness/performance requirement of the new common binding
contract, even if the supplied snapshot was captured on Vulkan.

## Reference comparison and decisions

- Khronos' descriptor-management sample directly matches the local failure:
  it warns against allocation/update on the critical path and recommends a
  small number of per-frame buffers with dynamic offsets plus cached stable
  descriptors. KimPeanutEngine already has the aligned per-frame buffer, so the
  missing piece is the binding contract, not another allocator.
- Khronos' dynamic-uniform sample demonstrates one descriptor set and one
  aligned large buffer with a different offset per draw. Adopt the pattern
  through API-neutral dynamic uniform bindings; do not expose Vulkan types.
- Godot caches layout compatibility and avoids redundant descriptor rebinds.
  Adopt the recorder-local fast-path principle, but keep KimPeanutEngine's
  simpler handles and fixed-pass architecture.
- Khronos' command-buffer sample reports a benefit for multithreaded recording
  at roughly 1,800 draws and also warns that too few draws per secondary buffer
  can lose state reuse. With 713 draws and obvious serial descriptor/state
  churn, multithreaded recording is deferred until the single-thread path is
  fixed and re-profiled.

## Positive observations

- The resolver already distinguishes sRGB color from linear data textures.
- The PBR shader renormalizes sampled tangent-space normals.
- OpenGL contains a mip-generation path, and common texture usage supports
  transfer operations needed by a backend-neutral upload contract.
- Existing frame contexts provide the natural ownership boundary for reusable
  per-frame descriptor arenas.

## Disposition

The current renderer is not acceptable for this fixture. Proceed with the
CPU-submission stage in `.plan/issue-9.7.md`: first make the shadow cache reuse
the unchanged effective map, then eliminate per-draw descriptor allocation/
updates, then suppress redundant state work and compact the section packets.
Do not begin with multithreaded command buffers, indirect drawing, a depth pre-
pass, shadow-quality knobs, or FXAA. A performance-build comparison is required
evidence, but it is not itself a fix. Close each finding only with the specified
telemetry and cross-backend visual evidence.
