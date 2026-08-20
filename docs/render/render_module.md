# Render Module Design

Location: `engine/runtime/render/`

The render module is the engine's "what to draw and how" layer — the module that owns scenes, materials, cameras, render passes, and the requests for GPU pipelines. This is where the *call* originates: the render module asks the asset system for shader identity, asks the resource pipeline to bake it into artifacts, fills a `PipelineDesc`, and hands it to the RHI.

## Current state — the skeleton

The legacy OpenGL renderer (RenderShader, ShaderPool, RenderMaterial, RenderScene and the raw-GL pass code) has been **deprecated and removed** from the build. What remains is the reconstruction's starting point: `RenderSystem` (the facade) and — since the Vulkan decoupling's Phase 4 (2026-08-15) — **`RenderScene`**, the demo as the render module's first real scene. `Render` now links `Graphics` (PRIVATE) so the scene can record through the RHI; `RenderSystem` itself stays API-agnostic.

### `RenderSystem` — the facade — [`render_system.h`](../../engine/runtime/render/render_system.h)

The render-module facade. It owns the `ResourcePipeline` and the `RenderBackend` (both initialized with the chosen `GraphicsAPIType`) and **drains the async load queue** (`RuntimeContext::async_load_queue_`, passed in by pointer) in two modes: `PostInitialize` full-drains the bootstrap batch, and `Tick` budget-drains a bounded number of requests per frame. Shader-program requests load via `asset.LoadSync`, process through `resource.ProcessShader`, build the fixed `BuildDefaultPipelineDesc`, and enter a render-side `PipelineHandle` cache keyed by program `AssetID`. `RenderCacheEntry` is a request-result record: it pins the asset payload and holds exactly one typed `RenderResource` result (`PipelineHandle`, `MeshHandle`, or `TextureBinding { TextureHandle, SamplerHandle }`), keyed by `request_id` and polled via `IsReady` / `GetCached`. The separate type-specific caches own the deduplicated handles. `Tick` owns the RHI frame bracket; scenes own their camera/view state.

Initialization is bundle-based so dependencies can grow without an unstable
positional signature: `RenderSystemInitInfo` carries the API, native window,
resize dispatcher, and load queue. `RenderSceneInitInfo` groups its backend and
`RenderSceneResources` (pipeline, mesh, texture/sampler material binding). The
scene bundle now names the common `RenderBackend`; its dynamic UBO path uses
common buffer/extent/frame APIs, and its draw path uses `CommandRecorder`.

### `RenderCamera` — camera data — [`render_camera.h`](../../engine/runtime/render/render_camera.h)

A **pure data holder**, not a system — no lifecycle, no input, no movement, no rendering. It stores the camera parameters (position, rotation, fov/near/far/aspect) and derives the view/proj matrices the render pass needs:

```cpp
struct CameraData {
    alignas(16) Matrix4f view;
    alignas(16) Matrix4f proj;
};

CameraData GetCameraData() const;   // {view.Transpose(), proj.Transpose()}
```

Modeled on the deprecated `CameraComponent` (git history: `engine/runtime/component/camera_component.*`), stripped of the scene hierarchy and input. Its role is to be **owned by `RenderScene`**: the game sets position/rotation through `RenderScene::GetCamera()`; the scene writes the resulting `CameraData` into its per-frame UBO.

### `RenderScene` — the demo as the first real scene — [`render_scene.h`](../../engine/runtime/render/render_scene.h)

The triangle demo, moved out of the Vulkan backend (Phase 4 of the [Vulkan decoupling](../graphics/vulkanbackend.md)) and into the render module. It borrows static mesh/texture/pipeline handles from the render-resource cache, owns per-frame uniform buffers + descriptor sets and its `RenderCamera`, and records the frame's draws against the RHI's scene command buffer:

- `Initialize(VulkanBackend*)` — loads texture + mesh through the asset manager, creates the GPU objects via the backend managers, allocates the UBOs (persistently mapped) and descriptor sets.
- `Tick(delta)` — writes the per-pass (camera, from the swapchain extent) and per-object (spinning) UBOs for the current frame in flight.
- `Record()` — binds the pipeline, vertex/index buffers, descriptor set and issues `vkCmdDrawIndexed` into `GetCurrentSceneCommandBuffer()`.
- `Cleanup()` — destroys descriptor pool, UBO buffers, mesh/sampler/texture through the backend managers.

**Cross-API recording** — `Record(CommandRecorder&)` describes the draw with common handles. The backend owns native command-buffer access and translates the recorder operations for its graphics API.

## Target architecture

The reconstruction makes the render module the **caller** in a one-way, high→low dependency chain:

```
render module
   ├──→ asset.AssetManager.LoadSync(.shader)     identity
   ├──→ resource.ResourcePipeline.ProcessShader  bake → ShaderData
   └──→ graphics.RenderBackend.CreatePipelineResource(PipelineDesc)   consume
                │
                └──→ nothing. RHI only reads byte_code + state.
```

### The ownership split

| Concern | Owner |
|---|---|
| Shader identity (`.shader` meta, stage descs) | `asset/` |
| Compile source → artifact (`ShaderData`, disk cache) | `resource/` → [resource_module.md](../resource/resource_module.md) |
| "I want this pipeline" — load, compile, fill desc, request | **render module** |
| Bake desc → GPU objects | `graphics/` RHI |

### The `PipelineDesc` seam

`graphics::PipelineDesc` ([`pipeline_types.h`](../graphics/graphics_module.md)) is the contract. The render module:

1. `asset.LoadSync(path)` → `ShaderProgramResource` (all stages, `Uncompiled`).
2. `resource.ProcessShader(stages)` → each stage's `ShaderData` is `Ready` (`byte_code`, `api`, `entry`).
3. Hand each stage's `ShaderData` straight to the `PipelineDesc` — `PipelineDesc` takes `data::ShaderData*` directly (landed 2026-08-15; no `graphics::Shader` wrapper).
4. Fill the rest of `PipelineDesc` (vertex layout, blend/raster/multisample state, descriptor bindings, attachment formats) from render-module-owned material/pipeline definitions.
5. `backend.CreatePipelineResource(desc)`.

### Warmup at init

A real game compiles its shaders at startup so first-frame pipeline requests are cache hits. The render module owns this: at init it reads a manifest of `.shader` paths, calls `asset.LoadSync` + `resource.ProcessShader` for each, populating `resource::ShaderCache` (disk) + `shader->data` (memory). Later backend requests hit the cache. Same `ProcessShader` API — just called eagerly at boot. (Later: material traversal instead of a manifest.) The async half of this is the **async resource queue** — [async_resource_queue.md](../async/async_resource_queue.md): a loading thread runs `LoadSync` + `ProcessShader` off-frame, and the render thread drains finished artifacts under a frame budget, so runtime loads never block a frame.

## Reconstruction plan (ordered)

1. **Wire the resource pipeline in.** Give the render module (or `RuntimeContext`) an owned `ResourcePipeline`, initialized with the chosen `GraphicsAPIType`. It currently has no owner and no callers.
2. **Make the RHI a pure receiver.** ✅ Landed 2026-08-15/16: `ShaderManager`/`Shader`/`ResourceShader`/`ShaderLoader` retired; `PipelineDesc` carries `data::ShaderData*`; the commented-out asset-loading block in `VulkanPipelineManager` was deleted. The build-time `glslc` step is gone too (2026-08-16, TODO 2.3) — the `rhi_example` demo bakes shaders at runtime via `ProcessShader`, the first graphics-end caller of step 1's pipeline.
3. **Move `PipelineDesc` construction out of the backend** into render-module pipeline/material code. `RenderBackend::CreatePipelineResource` only *bakes* what it is handed.
4. **Add the render-module request path:** `asset.LoadSync(.shader)` → `resource.ProcessShader` → fill `PipelineDesc` → `backend.CreatePipelineResource`. Add a warmup pass in render init.
5. **Close the resource-pipeline gaps it will hit:** `ProcessShader` should take the whole `ShaderProgramResource` (all stages) as one unit, not a flat stage list; and add a `CompileFailed` status carrying the compiler error, so the render module can distinguish failure and not bake empty bytes.
6. **Give the skeleton its content.** The legacy GL code is already retired, so there is no coexisting path to replace pass-by-pass. `RenderSystem` grows: it owns the RHI backend (via `RenderBackend`), the async queue drain + ready cache, and the scene graph; `RenderCamera` data feeds the frame's `PerPassData`.
7. **Finish:** `Render` links `Graphics` — ✅ landed 2026-08-15 (Phase 4 of the Vulkan decoupling, TODO 5.1; PRIVATE link, the demo `RenderScene` records through the RHI). The second half remains: `RenderSystem` becomes the render-module facade that owns the backend, the warmup, and the scene graph (only `RenderScene` does that today).

## Invariants

- **One-way dependency, high → low:** render → asset, render → resource, render → graphics. `graphics` never reaches up (no `.shader` paths, no compilers, no asset IDs).
- **The render module is the only caller** of "load + compile + request pipeline." Neither the RHI nor the asset loader initiates a pipeline build.
- **Pipeline requests always carry baked bytes.** `PipelineDesc` shaders are backed by `ShaderData::byte_code`; the RHI never compiles or reads source.
- **Derived data lives in the resource pipeline.** The render module caches *pipeline* objects (keyed by `(program AssetID, api)`), never shader sources.

## Refactor status

**In progress — legacy deprecated, queue consumer and pipeline/static-resource ownership in place, first scene in.** `RenderSystem` owns the `ResourcePipeline`, the `RenderBackend`, the RHI frame bracket, and fixed-default pipeline/mesh/texture/sampler caches. It drains the async load queue in two modes (bootstrap full-drain + per-frame budgeted drain), building one `PipelineHandle` per loaded shader program/default state and render-ready handles for queued mesh/texture/model assets. `Render` links `Asset` and `Graphics` (PRIVATE). **The demo scene landed as `RenderScene` (2026-08-15)** — it receives non-owning RHI handles, owns logical camera state, and describes UBO/texture bindings through `ResourceBindingSetDesc`; Vulkan descriptor pools, sets, and writes are hidden behind `DescriptorSetHandle`. **Phase 3.3 gives the scene an API-neutral `CommandRecorder`; Phase 3.4 introduces `FrameContext` so transient UBO and binding-set allocation belongs to the current frame rather than the scene.** The target design (render → asset → resource → graphics, `PipelineDesc` seam, async warmup) is unchanged. The dedicated loading thread is deferred — the render thread loads in-place, budgeted. See [the graphics module doc](../graphics/graphics_module.md) for the RHI side of the same change.

## Future work

- **Frame-local transient data (Phase 3.4, in progress).** `RenderSystem` owns
  a `FrameContext` for every backend frame slot. The context owns the transient
  uniform-buffer arena and returns aligned buffer/range allocations; scenes will
  next consume these allocations instead of retaining UBOs and descriptor sets.

- **Render graph (very future, not started).** Long-term the render module moves from direct pass code to a render graph: passes recorded as nodes with explicit resource dependencies (textures, buffers, pipeline state), then culled/ordered and baked into RHI draw calls. `RenderSystem` becomes the graph executor — it records the frame's passes, drains the async queue, and issues the RHI calls that complete the render task. Listed now so the design doesn't lock in a pass order prematurely.
