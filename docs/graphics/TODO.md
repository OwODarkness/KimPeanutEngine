# Graphics (RHI) Work TODO

**Snapshot: 2026-09-04.** The working task list and ordered roadmap for the RHI leak-fixes and the render-module reconstruction. State-of-the-world lives in [status.md](../status.md) and [graphics_module.md](graphics_module.md); this page is the ledger that drives them — tick items here as they land, then fold the result into those two.

Items marked **← sakura** are ideas taken from [sakura_reference.md](sakura_reference.md) — *learned, not copied*.

## Build encapsulation

- [x] Keep `Graphics` native SDK dependencies and backend include root private
  (2026-08-26). `glad`, `VulkanSDK`, GLFW, and `backend/` no longer propagate
  from the `Graphics` target; the explicit `EditorUILib` Vulkan bridge links
  `VulkanSDK` itself.
- [x] Make `USE_OPENGL` and `USE_VULKAN` select backend sources and factory
  availability (2026-08-26). An isolated OpenGL-only `Graphics` build passes;
  a disabled API returns no backend rather than constructing an unavailable
  implementation.
- [ ] Split common public headers from implementation headers physically. The
  present broad `engine/runtime` include root still lets an internal target name
  `graphics/backend/vulkan/...` directly, even though CMake no longer exports
  that path or its native dependencies.

## Capability contract

- [x] Publish immutable common `GraphicsCapabilities` from `RenderBackend`
  (2026-08-26). It reports the initialized engine path, not raw native feature
  structs: both backends report their sampled-texture-stage limit, and bindless
  textures remain false until descriptor indexing/GL bindless is enabled behind
  one common resource-table design.
- [ ] Define the common bindless texture-table lifetime and binding contract,
  then enable the required Vulkan descriptor-indexing and OpenGL paths only
  where that contract is implemented.

## Bindless textures — planned, optional path

**Goal:** let material shaders address a large RHI-owned sampled-texture table
by a common generational slot, while retaining the current per-draw bound
resource path as a correct fallback. This is not a request to expose Vulkan
descriptor sets, `VkDescriptorIndexing*`, or `GLuint64` texture handles above
Graphics.

### B0 — establish the common contract before enabling a backend

- [x] Limit V1 to sampled textures. Explicitly defer bindless buffers, storage
  images, acceleration structures, and generic descriptor arrays.
- [x] Define a common `BindlessTextureHandle`/slot with generation validation,
  invalid value semantics, and an explicit maximum table capacity.
- [x] Define the table owner: Graphics owns GPU/native table state; Render owns
  material selection and requests slot allocation/release using ordinary
  `TextureHandle` and `SamplerHandle` inputs.
- [x] Define the shader-visible table layout, binding/set convention, shader
  preprocessor contract, and versioning rule. A material uses a table index,
  never a backend-native handle.
- [x] Specify fallback behavior: unsupported devices and capacity exhaustion
  use ordinary `ResourceBindingSetDesc` bindings; no draw may silently sample a
  different texture.
- [x] Expand `GraphicsCapabilities` only with effective, common-path facts
  needed by the decision (for example supported mode and usable table capacity),
  never with native API structs or raw extension lists.

### B1 — lifetime, update, and frame safety

- [x] Design slot allocation, replacement, release, and generation validation.
- [x] Define when a texture/sampler table write becomes visible to recording
  and when it is legal to overwrite a descriptor or make a resident handle
  non-resident.
- [x] Defer slot reuse until all frame slots/submitted GPU work that could read
  the old entry are complete; integrate this with the existing backend frame
  lifecycle rather than adding a global `WaitIdle`.
- [x] Decide table growth policy and telemetry (requested capacity, allocated
  slots, exhaustion), including deterministic test limits.

### B2 — Vulkan backend implementation

**Completion rule:** B2 is one atomic Vulkan milestone. Do not mark any B2
item complete, expose a usable table capacity, or set
`GraphicsCapabilities::bindless_textures` until all six requirements below are
implemented and validated together. A descriptor pool/table that no compatible
pipeline can bind and sample is not an implementation milestone.

- [x] Query and enable the required descriptor-indexing feature subset during
  logical-device creation; report bindless unavailable if the enabled device
  path cannot satisfy the B0 contract.
- [x] Create backend-private descriptor-set layouts, pools, allocation, and
  update strategy with the required binding/layout flags and variable-count
  policy where chosen.
- [x] Implement a backend-private slot allocator and deferred retirement tied
  to frame completion. Do not publish `VkDescriptorSet`, `VkImageView`, or
  descriptor-indexing feature structs.
- [x] Update Vulkan pipeline layout/shader compilation inputs for the stable
  bindless table convention, without breaking the ordinary bound-material path.
- [x] Bind the global table for every compatible Vulkan pipeline through
  `VulkanCommandRecorder`, and drive B1 retirement from the exact per-frame
  submission serial whose fence completed.
- [x] Enable the common capability only after the device feature path, table,
  pipeline layout, recorder binding, and submission-safe reuse path all have
  runtime validation.

### B3 — OpenGL backend implementation

- [x] Select and validate the required OpenGL bindless-texture extension path;
  report unavailable when it is absent.
- [x] Own resident texture-handle creation/non-residency and the GPU-visible
  table privately; match B1 deferred-reuse guarantees.
- [x] Apply the same common shader table convention and material fallback as
  Vulkan. Do not expose `GLuint64` handles to Render or Editor.

### B4 — render and material adoption

- [x] Add material-template metadata that opts a compatible shader/pipeline
  into the bindless convention; keep the template data serializable and API
  neutral.
- [x] Let the material resolver cache common bindless slots for ready texture
  bindings and invalidate/retire them when source handles change.
- [x] Let `FrameContext` select the bindless or ordinary binding path from
  `GraphicsCapabilities`; `MeshProxy` remains unaware of native GPU bindings.
- [x] Add one real multi-material scene that demonstrates fewer per-draw
  texture-binding updates while preserving identical output on the fallback.

### B5 — validation and rollout

- [ ] Add headless tests for generational slots, capacity exhaustion, fallback
  selection, and deferred reuse scheduling.
- [ ] Add Vulkan conditional contract tests for unsupported and enabled paths;
  retain explicit skip reporting when the machine lacks the required feature.
- [ ] Extend `GraphicsSmoke` with multiple material textures and run it through
  Vulkan and OpenGL. Compare the bindless path with the bound fallback where
  both are available.
- [x] Keep bindless opt-in until both backend behavior, lifetime rules, shader
  convention, and fallback behavior have runtime evidence.

## 1. Pipeline seams — shaders become `ShaderData`

- [x] **`PipelineDesc` shaders are `data::ShaderData*` directly** — no `graphics::Shader` wrapper. Landed 2026-08-15: `Shader`/`ResourceShader`/`ShaderLoader` retired (the single-impl seam's `api` dispatch was redundant — each backend reads the field its own API needs: Vulkan `byte_code`, OpenGL `source`).
- [x] **Retire the `ShaderModule` seam** — landed 2026-08-16. `common/shader_module.h` + `vulkan_shader_module.*`/`opengl_shader_module.*` deleted: the seam's two impls read `shader->glsl`/`shader->spirv` (fields `ShaderData` doesn't have), `GetHandle() → const void*` was a leaky abstraction (Vulkan's module and GL's compiled-shader object have different lifetimes), and the work already lives inline in the pipeline bakes — `VulkanPipelineManager::CreateShaderModule(device, bytes, size, &module)` for Vulkan, `glCreateShader/glCompileShader/glAttachShader/glLinkProgram` in `OpenglPipeline::Initialize` for OpenGL. Each backend wraps the raw-data → API-object step where the shader is needed.
- [x] **Pipeline baking stops building `PipelineDesc`** — landed 2026-08-15 and exposed as `CreatePipelineResource` on 2026-08-20. The backend takes the desc and bakes it (completing attachment formats from the swapchain format, an RHI-owned invariant). Building the desc is render-module/example work.

## 2. Ownership — retire the path-keyed `ShaderManager`

- [x] **Remove the `ShaderManager` member from `RenderBackend`** — landed 2026-08-15, the whole manager is gone (the RHI owning a shader cache was the leak; caching belongs to the render module / resource pipeline).
- [x] **Retire `ShaderManager::CreateShader<API>(type, path)`**, which read the file itself — landed 2026-08-15. `shader_manager.*`, `shader_factory.h`, `vulkan_shader.*`, `opengl_shader.*` deleted; shaders arrive as `ShaderData` bytes.
- [x] **Remove the build-time `glslc` → `.spv` step** in [Graphics/CMakeLists.txt](../../engine/runtime/graphics/CMakeLists.txt) — landed 2026-08-16 (TODO 2.3). The `Shaders` custom target and the `glslc` build requirement are gone; `glslc` is no longer findable-at-configure. The `rhi_example` demo now bakes its shaders at runtime through `ResourcePipeline::ProcessShader` (`asset.LoadSync(.shader)` → `ProcessShader` → `ShaderData`), the same flow the asset example proves — `ProcessShader`'s first graphics-end caller.

## 3. `RenderBackend` facade cleanup

- [x] **Drop the `GLFWwindow *window_` test seam** — landed 2026-08-16. `window_` and the dead public `CameraData camera_data` are gone from [render_backend.h](../../engine/runtime/graphics/backend/common/render_backend.h); `RenderBackend::Initialize` now takes the native window handle (`WindowHandle` = `void*`) as an explicit parameter, and each backend casts it back to `GLFWwindow*` internally (the editor's `EditorImguiGLFWWSI` pattern). The common facade no longer knows GLFW.
- [x] ← sakura **Split `RenderDevice` from frame lifecycle — decided 2026-08-16: keep the frame loop.** `RenderBackend` stays the facade with `BeginFrame`/`EndFrame`/`Present`; the device/frame split already lives *inside* the backend (`VulkanDevice` = pure device + queues, `VulkanSwapchain`/`VulkanFrameContext` = frame lifecycle). A frame executor above the facade would re-fuse what Phases 1–3 separated, with no consumer that needs it — the render module talks to the facade today.
- [x] **Replace the public editor `GraphicsContext` escape hatch in Render
  R1.5:** expose one typed common editor-presentation capability, keep native
  Vulkan operations inside the approved `VulkanEditorBridge`/EditorUILib pair,
  and make backend `GraphicsContext` helpers private implementation detail. →
  [R1.5 stage design](../render/.plan/R1.5.md)

## 4. Pipeline cache — `PipelineDesc` as a *key* (← sakura)

- [ ] ← sakura **RHI exposes "bake this `PipelineDesc`", not "find me a cached shader"** — cache keyed by `(program AssetID, api)` on the render side, per the asset-module design note. Sakura's `pso_map`/`shader_map` are hash→resource tables; `PipelineDesc` is a *key* and the renderer dedupes by hashing it.

## 5. Wiring

- [x] **`Render` links `Graphics`** — landed 2026-08-15, extended 2026-08-20: `Render` links `Graphics` PRIVATE. `RenderSystem` owns the API-neutral `RenderBackend` facade, default-pipeline warmup/cache, and the frame bracket; the demo scene (`render::RenderScene`) still records through the Vulkan-specific stopgap.
- [x] **Render-side queue drain + bootstrap preload → render cache** landed 2026-08-13 ([async_resource_queue.md](../async/async_resource_queue.md)) — this is the caller the reconstruction builds on.

## 6. Testing (headless, no GPU)

- [ ] Unit tests: `ShaderCache`, `GenerateShaderHash`, `HandleSystem`, asset manager — listed as next up in [status.md](../status.md).

## 7. Next roadmap — turn the backend into a reusable RHI

The Vulkan split and shader-boundary work are complete. The remaining work is
not another Vulkan rewrite: it is moving *render intent* out of both backends
and making `RenderSystem` the sole caller of the RHI. Complete these milestones
in order; do not start a render graph before milestone 4 is stable.

### Milestone 1 — decouple backend startup from one pipeline

**Goal:** a backend initializes a device/swapchain/frame context, then can bake
and destroy any number of independently owned pipelines.

- [x] Change `RenderBackend::Initialize` to take only `WindowHandle`. Removed
  `PipelineDesc` from backend startup, 2026-08-20.
- [x] Add common `CreatePipelineResource(const PipelineDesc&) -> PipelineHandle`
  and `DestroyPipelineResource(PipelineHandle)` operations. Vulkan and OpenGL
  now implement the same lifecycle, 2026-08-20.
- [x] Replace `VulkanBackend::pipeline_handle_` and `OpenglBackend::pipeline_`
  as the single implicit pipeline with manager-owned, handle-addressed storage,
  2026-08-20.
- [x] Keep swapchain attachment formats RHI-owned: the Vulkan backend fills
  omitted swapchain color/depth formats while baking, but does not choose shader
  stages, vertex layout, descriptor layout, or material state.
- [x] Remove the redundant `Present()` facade call. `EndFrame()` is the sole
  submit/present operation for both backends, 2026-08-20.

**Done:** `rhi_example` initializes the backend without a pipeline, creates two
caller-built `PipelineHandle`s, destroys both before cleanup, and neither
backend contains a single implicit demo-pipeline member (2026-08-20).

### Milestone 2 — make `RenderSystem` own pipeline requests and cache policy

**Goal:** render, not graphics, decides which pipelines exist and when they are
prepared.

- [x] Give `RenderSystem` ownership of the `RenderBackend`, created for the
  selected `GraphicsAPIType`, and make it the only engine-level frame-loop
  caller (`BeginFrame` / record / `EndFrame`). Landed 2026-08-20; recording is
  empty until milestone 3 supplies a common command surface.
- [x] Add a fixed render-side default pipeline builder that builds
  `PipelineDesc`: stages, vertex layout, raster/blend/multisample state,
  descriptor bindings, and non-swapchain attachment intent. This is the
  material-system placeholder, landed 2026-08-20.
- [x] At startup, turn bootstrap shader-program requests into
  `LoadSync` → `ProcessShader` → `PipelineDesc` → `CreatePipelineResource`.
  Landed 2026-08-20.
- [x] Add a render-side pipeline cache keyed by `(program AssetID, API, render
  state/layout signature)`. The first cache is keyed by packed `AssetID`; one
  `RenderSystem` owns one API and one fixed default-state signature. The cache
  owns `PipelineHandle`s; the RHI only owns the GPU objects addressed by them.
  Landed 2026-08-20.
- [ ] On shader compile failure, do not create a pipeline. First finish the
  resource-side `CompileFailed` status/error text so this decision is explicit.

**Done when:** `RenderSystem`, rather than `rhi_example` or a backend, owns the
startup warmup and can request the same program twice without creating a second
GPU pipeline. The ownership/warmup/cache slice landed 2026-08-20; retain this
milestone until compile errors are retained as data rather than logs.

### Milestone 3 — replace the Vulkan-only demo recording seam

**Goal:** `RenderScene` expresses rendering work without including Vulkan or
depending on `VulkanBackend`.

#### 3.1 — move static GPU-resource ownership to `RenderSystem`

**Goal:** make the scene a consumer of render-ready resources before changing
how it records commands.

- [x] Add render-cache entries for baked `MeshHandle`, `TextureHandle`, and
  `SamplerHandle`, alongside the existing cached `PipelineHandle`. The mesh and
  texture caches dedupe by packed asset ID; a shared default sampler is created
  lazily. `RenderCacheEntry` carries one typed result variant rather than
  optional fields for every handle kind, 2026-08-20.
- [x] Move asset loading and mesh/texture GPU upload out of `RenderScene`.
  `RenderSystem`'s request path now owns the production cache/create/destroy
  flow; `rhi_example` creates the same resources through the common backend API
  until the demo scene itself is owned by `RenderSystem`, 2026-08-20.
- [x] Change `RenderScene::Initialize` to receive non-owning mesh, texture,
  sampler, and pipeline handles; raw Vulkan recording remains temporary,
  2026-08-20.
- [ ] Keep dynamic per-scene/per-object UBOs in `RenderScene` for now. They are
  scene-local state, not shared asset-cache entries.

**Done:** `RenderScene` has no `AssetManager`, asset path, loader, or GPU-upload
call; it stores copied handles and never destroys static resources (2026-08-20).

#### 3.2 — define common resource-binding descriptions

**Goal:** describe draw resources with common types before exposing commands.

- [x] Define small common descriptions for uniform-buffer, texture/sampler,
  and descriptor-set/resource bindings. `ResourceBindingSetDesc` contains
  handles, binding/set indices, offsets, and ranges—not `Vk*` or `GLuint`
  values, 2026-08-20.
- [x] Let the Vulkan backend allocate/update the descriptor-set implementation
  for those descriptions. `VulkanDescriptorSetManager` privately owns pools and
  native descriptor sets behind `DescriptorSetHandle`, 2026-08-20.
- [x] Keep this deliberately sufficient for the demo's two UBOs + one sampled
  texture. Push constants, arrays, bindless resources, and materials wait for a
  demonstrated caller.

**Done:** render code describes every demo binding using only common handles and
descriptors, while Vulkan owns every `VkDescriptor*` object (2026-08-20).

#### 3.3 — add the minimal cross-API command recorder

**Goal:** replace raw command-buffer access with render intent.

- [x] Expose a recorder only during the `BeginFrame`/`EndFrame` interval.
- [x] Support only the demo operations: bind pipeline, bind mesh, bind resource
  bindings, set viewport/scissor, and draw indexed.
- [x] Add `BeginRenderTarget` / `EndRenderTarget` so render owns scene-output
  attachment selection. Vulkan records dynamic rendering into API-private
  color/depth textures; OpenGL binds the matching framebuffer. Landed 2026-08-20.
- [x] Implement the recorder in Vulkan by issuing `vkCmd*`; keep the native
  command buffer private to the backend.
- [x] Convert `RenderScene::Record` to take/use this common recorder and remove
  all `Vk*`, `vkCmd*`, and `VulkanBackend` references.

**Done when:** `RenderScene` describes a draw entirely with RHI handles and the
Vulkan backend is the only layer that sees native command-buffer types.

**Landed:** `CommandRecorder` is the API-neutral vocabulary. Vulkan translates
it to `vkCmd*`; OpenGL translates it to state changes and draw calls. The
recorder is available only during an active frame (2026-08-20).

#### 3.4 — introduce frame contexts for transient render data

**Goal:** centralize per-frame GPU allocation and lifetime without returning
scene-specific camera/object state to `RenderSystem`.

`RenderSystem` owns one logical `FrameContext` per backend frame slot. The
backend remains authoritative for fences and slot reuse; the render context owns
only transient render data for that safe-to-reuse slot.

- [x] Add `FrameContext`, owned and rotated by `RenderSystem`: one context per
  frame in flight, carrying frame index, frame-global CPU data, and transient
  RHI allocation services.
- [x] Add a common dynamic-uniform allocator. `FrameContext::AllocateUniform<T>`
  returns a buffer/range valid only for its frame slot; the allocator resets or
  recycles only after that slot's GPU work is complete.
- [x] Add transient resource-binding-set allocation through `FrameContext`.
  Binding sets that reference frame UBO ranges are released with the frame;
  scenes must not retain their handles across frames.
- [x] Move `RenderScene`'s persistent `UniformBuffer` storage, mapped pointers,
  descriptor-set vector, and manual destruction out of the scene. The scene
  retains logical camera/renderable/material state and writes frame data through
  its supplied context.
- [x] Put truly global dynamic data (frame number, elapsed time, global
  lighting/environment) in `FrameContext`; keep camera data in a scene-created
  render view and per-object transforms in scene draw data.
- [x] Preserve static ownership: `RenderSystem` keeps cached mesh, texture,
  sampler, and pipeline handles; a future material system may cache immutable
  texture/sampler binding sets separately.
- [x] Enforce the frame sequence: `BeginFrame` → acquire/begin `FrameContext`
  → scenes build draw data and record through the common recorder → end context
  → `EndFrame`.
- [x] Verify destruction order: scene logical state → frame contexts/transient
  allocations → render cached static resources/pipelines → backend/device.

**Done when:** scenes own no transient `BufferHandle` or per-frame
`DescriptorSetHandle`; all such handles are valid only inside the supplied
`FrameContext`, while the same scene-facing code remains Vulkan/OpenGL-neutral.

**Landed:** `RenderSystem` creates one `FrameContext` for each backend frame
slot and schedules registered scenes between `BeginFrame`/`EndFrame`. Each
context owns a 64 KiB persistently mapped uniform-buffer arena and its transient
descriptor sets. Allocations return an aligned buffer/offset/range/mapped-pointer
record; both the arena cursor and binding sets recycle only after
`RenderBackend::BeginFrame` has made that slot reusable. Vulkan uses
`minUniformBufferOffsetAlignment`; OpenGL uses
`GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT` and `glBindBufferRange`. `RenderScene`
now retains only logical state and static RHI handles (2026-08-20).

#### 3.5 — make Vulkan buffer-memory ownership explicit

**Goal:** make `VulkanMemoryManager` the sole Vulkan owner of
`VkDeviceMemory`, its mapping state, and suballocation lifetime. This fixes the
current invalid path where multiple `FrameContext` uniform buffers can be
allocated from one pooled `VkDeviceMemory` block and each calls `vkMapMemory`.
The full design is in [vulkan_memory_manager_plan.md](vulkan_memory_manager_plan.md).

- [x] Add a Vulkan-private `VulkanMemoryManager`, owned by `VulkanBackend`
  before `VulkanBufferManager`. It owns move-only RAII shared blocks and
  dedicated allocations; it is the only type allowed to call
  `vkAllocateMemory`, `vkMapMemory`, `vkUnmapMemory`, or `vkFreeMemory`.
- [x] Add one common `VulkanMemoryAllocation` record for buffer binding:
  native memory, allocation offset/range, stable mapped CPU address, and a
  Vulkan-private release route. The RHI and render layers must not observe
  pooled-versus-dedicated policy or `VkDeviceMemory`.
- [x] Implement the shared-block policy first. Persistently map every
  host-visible block exactly once and return `mapped_base + allocation.offset`
  for every suballocation. Convert `MapUniformBuffer` into an accessor for that
  stored pointer, not a wrapper around `vkMapMemory`.
- [x] Migrate `VulkanBufferManager`: create/destroy/bind `VkBuffer` only;
  request/release allocations through the manager; replace temporary
  map/unmap uploads with manager-owned writes. Remove its allocator maps,
  `FreeMemory`, and direct native-memory lifetime calls.
- [x] Add the dedicated policy for large buffers and Vulkan-required dedicated
  allocations. It maps its distinct memory object at most once, then frees it
  on allocation release; shared releases return a slot without unmapping the
  block.
- [x] Add non-coherent `Flush`/`Invalidate` range handling aligned to
  `nonCoherentAtomSize`, even when the preferred desktop memory type is
  host-coherent.
- [x] Add validation smoke coverage: at least two `FrameContext` arenas from
  one shared block, repeated frame-slot reuse, pooled and dedicated buffer
  destruction, and backend shutdown. No mapped-memory or free-while-mapped
  validation errors are acceptable.
- [x] Verify teardown order: wait for GPU idle → destroy frame/transient and
  cached buffer users → destroy `VkBuffer`s → destroy the memory manager →
  destroy the Vulkan device.

**Done when:** shared `VkDeviceMemory` blocks are mapped once for their entire
live lifetime, every live host-visible allocation has a stable CPU address, and
neither `FrameContext` nor `VulkanBufferManager` can independently map or free
native Vulkan memory.

**Landed 2026-08-22:** Vulkan buffer memory is now manager-owned. `GraphicsSmoke`
passes three frames on Vulkan and OpenGL, and the engine editor starts with the
Vulkan scene viewport registered through ImGui's descriptor bridge. Milestone
3.6 adds a >4 MiB mapped uniform-buffer smoke allocation to exercise the
dedicated path and its destruction lifecycle.

#### 3.6 — unify Vulkan shared and dedicated allocators

**Goal:** make the existing allocator abstraction the single implementation path
for Vulkan buffer and image memory, while keeping allocation policy private to the
memory manager.

- [x] Extend `IVulkanMemoryAllocator` allocation metadata with memory type,
  mapped-base, offset/range, and shared-versus-dedicated ownership information.
- [x] Replace fixed-slot pool allocation with aligned variable-range
  suballocation, free-range merging, and block selection by compatible memory
  type/properties.
- [x] Add dedicated-allocation support for Vulkan's required/preferred dedicated
  resource requirements, including correct `VkMemoryDedicatedAllocateInfo`
  chaining.
- [x] Make `VulkanMemoryManager` select memory type and shared/dedicated policy,
  then delegate block allocation to the unified allocator implementations.
- [x] Migrate `VulkanImageMemoryManager` and `VulkanBufferManager` to the same
  allocator family and remove the parallel buffer-only allocation implementation.
- [x] Exercise shared frame arenas, a >4 MiB dedicated mapped buffer, image
  allocation, and safe teardown through `GraphicsSmoke` on Vulkan and OpenGL.
- [ ] Add conditional hardware coverage that forces a non-coherent host-visible
  memory type where the adapter exposes one. Free-range merge/reuse is covered
  headlessly by `GraphicsContractTest` (2026-08-24).

**Done when:** buffers and images use one allocator abstraction; callers receive
only opaque allocation records; and no caller can observe or manually manage
the shared-versus-dedicated policy. The implementation landed 2026-08-22;
allocator-internal edge-case tests remain tracked above.

### Milestone 4 — remove legacy renderer behavior from OpenGL

**Goal:** Vulkan and OpenGL are two implementations of one contract, not two
different render architectures.

#### 4.1 — remove dead OpenGL demo ownership

- [x] Remove direct `AssetManager` calls and `config/path.h` from
  `OpenglBackend`.
- [x] Delete hard-coded sphere/wallpaper paths, camera animation, and the old
  demo UBO/descriptor setup helpers and members.
- [x] Remove backend-owned `glDrawElements` scene policy that is not part of
  `CommandRecorder` execution.

**Done when:** `OpenglBackend` contains no asset loading, scene data, or
demo-specific resource lifetime.

**Landed:** all legacy OpenGL demo helpers and their asset/path dependencies
are deleted. The remaining indexed-draw call exists only as the implementation
of `CommandRecorder::DrawIndexed` (2026-08-20).

#### 4.2 — complete the common OpenGL execution path

- [x] Make OpenGL consume caller-provided mesh/texture/sampler/buffer/pipeline
  handles and the common recording operations from milestones 3–4 only.
- [x] Match Vulkan ownership: render owns request/cache and frame-transient
  policy; the backend owns API object creation, translation, and destruction.
- [x] Keep API differences internal (SPIR-V vs GLSL, descriptor sets vs GL
  binding points, dynamic rendering vs default framebuffer).

**Done when:** the OpenGL backend can execute the same `RenderScene` and
`FrameContext` path with no scene-specific branch.

**Landed:** `GraphicsExample` now creates the same caller-owned
`RenderScene`, static resources, and `FrameContext` path for Vulkan and
OpenGL. OpenGL validates pipeline and binding inputs, uses `glBindBufferRange`
for frame UBO ranges, and only translates common RHI calls (2026-08-20).

#### 4.3 — verify Vulkan/OpenGL parity

- [x] Run the one-object smoke scene through the same `RenderSystem` /
  `RenderScene` path on Vulkan and OpenGL.
- [x] Verify drawing, resize behavior, frame-slot reuse, and shutdown order on
  both backends.
- [x] Record any remaining API-only differences in the module documentation.

**Done when:** switching `GraphicsAPIType` changes only backend implementation,
not the render scene, warmup policy, or frame-data ownership.

**Landed:** `GraphicsSmoke` runs the shared one-object scene for three frames
on Vulkan and OpenGL, dispatching a resize event on its second frame. Three
Vulkan frames exercise frame-slot reuse; both runs clean up frame contexts,
static resources, backend, and window before returning. The smoke exits zero
when both APIs complete; it is a no-crash contract test, not pixel comparison
(2026-08-20).

**Milestone done when:** no backend source includes `asset/`, `config/path.h`,
or embeds scene-specific paths/data, and switching `GraphicsAPIType` changes
only the backend implementation—not the render scene or warmup policy.

### Milestone 5 — harden the contract before adding a render graph

**Goal:** verify the seam and ownership model before introducing pass scheduling
or graph complexity.

- [x] Add headless tests for pipeline-cache key equality, stale resource handle
  rejection, and `PipelineDesc` validation (stages, bindings, artifacts) in
  `GraphicsContractTest` (2026-08-24).
- [x] Add backend contract coverage where possible: `GraphicsSmoke` creates and
  destroys multiple pipelines/resources and verifies normal render-first,
  backend-second teardown on Vulkan and OpenGL (2026-08-24).
- [x] Add one visual execution smoke for Vulkan and one for OpenGL using the
  same `RenderScene` code path. `GraphicsSmoke` draws three frames/API with a
  resize; pixel comparison remains future regression infrastructure (2026-08-24).
- [x] Document the stable common recording API in
  [graphics_module.md](graphics_module.md), and update render/status docs
  (2026-08-24).

**Done when:** both APIs render the same simple scene through `RenderSystem`,
all scene policy remains above `graphics/`, and the next abstraction pressure is
multiple passes/resource dependencies—not backend leakage. Only then evaluate a
render graph.

### Milestone 6 — split Vulkan backend implementation services

**Goal:** reduce `VulkanBackend` to the Vulkan composition root: device/swapchain
startup, frame coordination, service ownership, and ordered teardown. This is
an internal Vulkan refactor; it must not expand the common RHI surface.

#### 6.1 — extract `VulkanCommandRecorder`

- [x] Move `vkCmd*` translation, currently recorded pipeline/mesh state, and
  active command-buffer access out of `VulkanBackend` into a Vulkan-private
  `CommandRecorder` implementation created for each active frame (2026-08-24).
- [x] Let `VulkanBackend::BeginFrame` prepare the native command buffer and
  `GetCommandRecorder` return the active recorder; destroy it during `EndFrame`
  before submission, preserving the common BeginFrame → record → EndFrame
  lifetime rule (2026-08-24).
- [x] Delete `GetCurrentSceneCommandBuffer` once its remaining editor use is
  removed by 6.4 (2026-08-24).

#### 6.2 — extract `VulkanRenderTargetManager`

- [x] Move render-target handle storage, texture ownership, image-layout state,
  compatible preview views, and create/destroy operations out of
  `VulkanBackend` into `VulkanRenderTargetManager` (2026-08-24).
- [x] Keep attachment selection and layout transitions Vulkan-private; the
  common recorder continues to receive only `RenderTargetHandle` (2026-08-24).
- [x] Put swapchain-sized color/depth attachment recreation behind this service
  (2026-08-24).

#### 6.3 — extract `VulkanUploadContext`

- [x] Move staging-buffer creation, one-shot command recording, transfer
  submission, and synchronous wait/release from `VulkanBackend` into
  `VulkanUploadContext` (2026-08-24).
- [x] Keep the first version synchronous and scoped; do not introduce async
  uploads until a caller has a real batching/lifetime requirement (2026-08-24).
- [x] Route buffer and texture uploads through the context, preserving memory
  manager ownership of every staging allocation (2026-08-24).

#### 6.4 — move editor presentation into an editor Vulkan bridge

- [x] Move ImGui pass begin/end and swapchain UI transitions out of
  `VulkanBackend`; `VulkanRenderTargetManager` already owns the sRGB scene
  target's compatible UNORM preview view (2026-08-24).
- [x] Give `EditorImguiVulkanRenderer` a narrow Vulkan-private external-pass
  bridge rather than public command-buffer, queue, context, or manager getters
  (2026-08-24).
- [x] Delete `GetCurrentUICommandBuffer`, `BeginEditorUiRendering`, and
  `EndEditorUiRendering` from `VulkanBackend` (2026-08-24).

#### 6.5 — close native escape hatches

- [x] Remove public `GetVulkanContext`, `GetGraphicsQueue`, `GetPipelineResource`,
  resource-manager getters, and native command-buffer access after their last
  Vulkan-private consumers are injected directly (2026-08-24).
- [x] Verify no render/editor code names a `Vk*` type outside approved
  Vulkan-private bridges (2026-08-24). `EditorImguiVulkanRenderer` and
  `VulkanEditorBridge` are the approved editor Vulkan bridge.
- [x] Add a shutdown smoke after each extraction; teardown remains GPU idle →
  frame/transient users → resources → memory → device (2026-08-24).

**Done:** `VulkanBackend` coordinates frame lifecycle and owns services, but no
longer implements command encoding, render-target storage, upload work, or
editor composition directly. Its public interface exposes only the common RHI
contract; Vulkan-private resource adapters receive their specific services via
`VulkanContext`. A render graph may now be evaluated for multiple passes and
resource dependencies.

### Ownership guardrail

```text
RenderSystem / RenderScene: what to draw, material state, pipeline cache, scene lifetime
Resource pipeline:           source -> baked ShaderData
Graphics RHI:                descriptions/data -> API GPU objects, frame execution
Vulkan/OpenGL:               private implementation details
```

If a graphics-backend file needs an asset path, an `AssetID`, a scene camera, or
a hard-coded draw object, it is a render-layer responsibility that has leaked
downward. If render code needs `Vk*` or `gl*` types, the common recording API is
still incomplete.
