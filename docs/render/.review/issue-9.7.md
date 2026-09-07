# Review — issue-9.7 Sponza Texture Aliasing and Frame Throughput

**Latest review: 2026-09-07.**

**Disposition: changes requested.**

**Implementation status: Stage 0 instrumentation and the Stage 1 runtime mip
path landed; native products and baseline evidence remain pending.**

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

The external examples are design evidence, not performance baselines. The
vkdf demo targets different hardware, resolution, content products, and frame
pacing.

## Local evidence map

| Concern | Evidence |
| --- | --- |
| One-level texture creation | [`DefaultTextureSettings`](../../../engine/runtime/render/render_resource_resolver.cpp) |
| Sampler LOD clamp | [`SamplerSettings`](../../../engine/runtime/graphics/backend/common/sampler.h) |
| Vulkan level-zero-only upload | [`VulkanUploadContext`](../../../engine/runtime/graphics/backend/vulkan/vulkan_upload_context.cpp) |
| Pool-per-binding-set allocation | [`VulkanDescriptorSetManager`](../../../engine/runtime/graphics/backend/vulkan/vulkan_descriptor_set_manager.cpp) |
| Per-draw transient material binding | [`FrameContext`](../../../engine/runtime/render/frame_context.cpp) |
| Proxy-only culling and section expansion | [`scene_visibility.cpp`](../../../engine/runtime/render/render_world/scene_visibility.cpp), [`scene_draw_list_builder.cpp`](../../../engine/runtime/render/render_world/scene_draw_list_builder.cpp) |
| Per-frame all-section shadow recording | [`DeferredRenderer`](../../../engine/runtime/render/deferred_renderer.cpp) |
| Material channel sampling | [`pbr_gbuffer.frag`](../../../asset/shader/pbr_gbuffer.frag) |
| Nine-tap shadow PCF | [`deferred_lighting.frag`](../../../asset/shader/deferred_lighting.frag) |

## Findings

### issue-9.7-F1 — P0: sampled textures have no minification hierarchy

**Runtime implementation status: addressed.** Native archive-product
serialization and Vulkan/OpenGL runtime capture evidence remain open.

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

The selected model references 72 4096×4096 images. Decoded RGBA8 base levels
occupy about 4.5 GiB; a complete uncompressed mip hierarchy is about 6 GiB.
This corrects the earlier directory-wide count of 135 images, which was not the
selected model's dependency closure.

Mip correctness must land with telemetry and a staged native texture-product
policy: semantic downsampling, a maximum-dimension profile, GPU-native
compression where supported, and an explicit fallback. Blindly resizing every
map to one resolution would lose author intent and still leave format waste.

### issue-9.7-F3 — P1: Vulkan allocates a descriptor pool per transient set

`VulkanDescriptorSetManager::CreateResourceBindingSet()` creates a
single-set `VkDescriptorPool`, allocates one set, and later destroys the whole
pool. `FrameContext::CreateMaterialBinding()` does this per draw and retires
the sets at the next frame begin. The Sponza G-buffer alone therefore drives
roughly 401 pool/set lifecycles per frame.

Descriptors need frame-slot arena ownership after fence completion, with
persistent bindings for stable material textures. Pool creation must not scale
with draw count.

### issue-9.7-F4 — P1: proxy-level visibility cannot reject Sponza sections

Scene visibility tests one `MeshProxy` AABB, then the draw-list builder expands
all visible proxy sections. Sponza is one proxy with 405 primitives; once its
aggregate bounds intersect the frustum, no section can be rejected.

Native model artifacts need per-section bounds, and Render needs section-level
visibility while retaining proxy-level ownership.

### issue-9.7-F5 — P1: an unchanged directional shadow map is rebuilt

The directional shadow pass snapshots the caster proxy and submits all of its
sections every frame. For the static fixture and unchanged sun, this duplicates
about 405 geometry draws without changing the shadow result.

Shadow validity needs an explicit stamp derived from light state, caster/world
bounds, and relevant geometry revisions. Reuse must remain conservative for
dynamic casters.

### issue-9.7-F6 — P1: FPS is not yet attributable

The engine target rate does not prove the active bottleneck. Debug Vulkan uses
validation, swapchain present mode may fall back to FIFO, and the renderer does
not yet expose pass GPU timestamps. The current FPS number cannot distinguish
CPU descriptor/recording work, GPU bandwidth/shading, residency pressure, or
missed present cadence.

Add frame-phase CPU timings, pass GPU timestamps, draw and descriptor counters,
texture resident bytes, selected present mode, and shadow-cache counters before
claiming a performance cause or improvement.

### issue-9.7-F7 — P2: later pass optimizations must remain measured choices

Per-section culling from the reference implementation applies directly. A
depth pre-pass may also pay when G-buffer fragments are the measured GPU limit,
and front-to-back order is a cheaper candidate. PCF kernel size, shadow
resolution, and post-process anti-aliasing are quality/performance controls,
not substitutes for mip correctness or CPU submission fixes.

## Positive observations

- The resolver already distinguishes sRGB color from linear data textures.
- The PBR shader renormalizes sampled tangent-space normals.
- OpenGL contains a mip-generation path, and common texture usage supports
  transfer operations needed by a backend-neutral upload contract.
- Existing frame contexts provide the natural ownership boundary for reusable
  per-frame descriptor arenas.

## Disposition

The current renderer is not acceptable for this fixture. Proceed with the
ordered design in `.plan/issue-9.7.md`; do not begin with shadow-quality knobs,
FXAA, or a debug/release comparison alone. Close each finding only with the
specified telemetry and cross-backend visual evidence.
