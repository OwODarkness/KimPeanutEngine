# Graphics (RHI) Work TODO

**Snapshot: 2026-08-20.** The working task list and ordered roadmap for the RHI leak-fixes and the render-module reconstruction. State-of-the-world lives in [status.md](../status.md) and [graphics_module.md](graphics_module.md); this page is the ledger that drives them — tick items here as they land, then fold the result into those two.

Items marked **← sakura** are ideas taken from [sakura_reference.md](sakura_reference.md) — *learned, not copied*.

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

**First seam:** `render::FrameContext` is declared in `render/frame_context.h`.
It currently carries only frame identity/global CPU data; allocation and
backend lifecycle wiring remain the work below.

- [x] Add `FrameContext`, owned and rotated by `RenderSystem`: one context per
  frame in flight, carrying frame index, frame-global CPU data, and transient
  RHI allocation services.
- [x] Add a common dynamic-uniform allocator. `FrameContext::AllocateUniform<T>`
  returns a buffer/range valid only for its frame slot; the allocator resets or
  recycles only after that slot's GPU work is complete.
- [ ] Add transient resource-binding-set allocation through `FrameContext`.
  Binding sets that reference frame UBO ranges are released with the frame;
  scenes must not retain their handles across frames.
- [ ] Move `RenderScene`'s persistent `UniformBuffer` storage, mapped pointers,
  descriptor-set vector, and manual destruction out of the scene. The scene
  retains logical camera/renderable/material state and writes frame data through
  its supplied context.
- [ ] Put truly global dynamic data (frame number, elapsed time, global
  lighting/environment) in `FrameContext`; keep camera data in a scene-created
  render view and per-object transforms in scene draw data.
- [ ] Preserve static ownership: `RenderSystem` keeps cached mesh, texture,
  sampler, and pipeline handles; a future material system may cache immutable
  texture/sampler binding sets separately.
- [ ] Enforce the frame sequence: `BeginFrame` → acquire/begin `FrameContext`
  → scenes build draw data and record through the common recorder → end context
  → `EndFrame`.
- [ ] Verify destruction order: scene logical state → frame contexts/transient
  allocations → render cached static resources/pipelines → backend/device.

**Done when:** scenes own no transient `BufferHandle` or per-frame
`DescriptorSetHandle`; all such handles are valid only inside the supplied
`FrameContext`, while the same scene-facing code remains Vulkan/OpenGL-neutral.

**Landed so far:** `RenderSystem` creates one `FrameContext` for each backend
frame slot. Each owns a 64 KiB persistently mapped uniform-buffer arena;
allocations return an aligned buffer/offset/range/mapped-pointer record and the
cursor resets only after `RenderBackend::BeginFrame` has made that slot reusable.
Vulkan uses `minUniformBufferOffsetAlignment`; OpenGL uses
`GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT` (2026-08-20).

### Milestone 4 — remove legacy renderer behavior from OpenGL

**Goal:** Vulkan and OpenGL are two implementations of one contract, not two
different render architectures.

- [ ] Remove direct `AssetManager` calls, hard-coded sphere/wallpaper paths,
  camera animation, demo UBO ownership, and `glDrawElements` scene policy from
  `OpenglBackend`.
- [ ] Make OpenGL consume caller-provided mesh/texture/sampler/buffer/pipeline
  resources and the common recording operations from milestone 3.
- [ ] Match Vulkan resource ownership and destruction semantics: render owns
  request/cache policy; the backend owns API object creation and destruction.
- [ ] Keep API differences internal (SPIR-V vs GLSL, descriptor sets vs GL
  binding points, dynamic rendering vs default framebuffer).

**Done when:** no backend source includes `asset/`, `config/path.h`, or embeds
scene-specific paths/data, and switching `GraphicsAPIType` changes only the
backend implementation—not the render scene or warmup policy.

### Milestone 5 — harden the contract before adding a render graph

**Goal:** verify the seam and ownership model before introducing pass scheduling
or graph complexity.

- [ ] Add headless tests for pipeline-cache key equality, stale resource handle
  rejection, and `PipelineDesc` validation (required stages, valid bindings,
  non-empty baked shader artifacts).
- [ ] Add backend contract tests where possible: create/destroy multiple
  resources and pipelines; verify cleanup order is render resources first,
  backend/device second.
- [ ] Add one visual smoke test for Vulkan and one for OpenGL using the same
  `RenderScene` code path.
- [ ] Document the stable common recording API in
  [graphics_module.md](graphics_module.md), then update
  [render_module.md](../render/render_module.md) and [status.md](../status.md).

**Done when:** both APIs render the same simple scene through `RenderSystem`,
all scene policy remains above `graphics/`, and the next abstraction pressure is
multiple passes/resource dependencies—not backend leakage. Only then evaluate a
render graph.

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
