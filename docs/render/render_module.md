# Render Module Design

Location: `engine/runtime/render/`

The render module is the engine's "what to draw and how" layer — the module that owns scenes, materials, cameras, render passes, and the requests for GPU pipelines. This is where the *call* originates: the render module asks the asset system for shader identity, asks the resource pipeline to bake it into artifacts, fills a `PipelineDesc`, and hands it to the RHI.

As of 2026-08-24, a pipeline request is rejected before backend allocation when
its baked shader artifacts, stage slots, vertex layout, descriptor bindings, or
attachment formats are invalid. Render owns the valid description and its
cache policy; graphics owns only the resulting handle and API object lifetime.

## Current state — the skeleton

The legacy OpenGL renderer (RenderShader, ShaderPool, RenderMaterial, RenderScene and the raw-GL pass code) has been **deprecated and removed** from the build. What remains is the reconstruction's starting point: `RenderSystem` (the facade) and — since the Vulkan decoupling's Phase 4 (2026-08-15) — **`RenderScene`**, the demo as the render module's first real scene. `Render` now links `Graphics` (PRIVATE) so the scene can record through the RHI; `RenderSystem` itself stays API-agnostic.

### `RenderSystem` — the facade — [`render_system.h`](../../engine/runtime/render/render_system.h)

The render-module facade. It owns the `ResourcePipeline`, `RenderBackend`,
`RenderResourceResolver`, and `MaterialSystem` (all initialized for the chosen
`GraphicsAPIType`) and **drains the async load queue**
(`RuntimeContext::async_load_queue_`, passed in by pointer) in two modes:
`PostInitialize` full-drains the bootstrap batch, and `Tick` budget-drains a
bounded number of requests per frame. Shader-program requests load via
`asset.LoadSync`, process through `resource.ProcessShader`, then ask the
resolver to build/cache the current default pipeline. `RenderCacheEntry` is a
request-result record: it pins the asset payload and holds exactly one typed
`RenderResource` result (`PipelineHandle`, `MeshHandle`, or
`TextureBinding { TextureHandle, SamplerHandle }`), keyed by `request_id` and
polled via `IsReady` / `GetCached`. The resolver owns the deduplicated static
RHI handles and destroys them before backend teardown. `Tick` owns the RHI
frame bracket; scenes own their camera/view state.

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

- `Initialize(RenderSceneInitInfo)` — receives borrowed static pipeline/mesh/material handles.
- `Record(FrameContext&, CommandRecorder&)` — derives camera/object data, allocates
  uniform ranges and a binding set from the active frame context, then describes
  the indexed draw with common handles.
- `Cleanup()` — releases only the scene's logical references; the frame context
  owns transient GPU objects and `RenderSystem` owns cached static resources.

**Cross-API recording and transient data** — `Record(FrameContext&, CommandRecorder&)` describes the draw with common handles. The backend owns native command-buffer access; `FrameContext` owns the per-slot UBO arena and binding sets; the scene owns no transient GPU handle.

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

**In progress — legacy deprecated, queue consumer and pipeline/static-resource ownership in place, first scene in.** `RenderSystem` owns the `ResourcePipeline`, `RenderBackend`, RHI frame bracket, and a render-private `RenderResourceResolver`. The resolver owns fixed-default pipeline/mesh/texture/sampler creation, deduplication, and destruction; `RenderSystem` only drains requests and coordinates the result. It drains the async load queue in two modes (bootstrap full-drain + per-frame budgeted drain), building one `PipelineHandle` per loaded shader program/default state and render-ready handles for queued mesh/texture/model assets. `Render` links `Asset` and `Graphics` (PRIVATE). **The demo scene landed as `RenderScene` (2026-08-15)** — it receives non-owning RHI handles, owns logical camera state, and describes UBO/texture bindings through `ResourceBindingSetDesc`; Vulkan descriptor pools, sets, and writes are hidden behind `DescriptorSetHandle`. **Phase 3.3 gives the scene an API-neutral `CommandRecorder`; Phase 3.4 introduces `FrameContext` so transient UBO and binding-set allocation belongs to the current frame rather than the scene.** The target design (render → asset → resource → graphics, `PipelineDesc` seam, async warmup) is unchanged. The dedicated loading thread is deferred — the render thread loads in-place, budgeted. See [the graphics module doc](../graphics/graphics_module.md) for the RHI side of the same change.

## Render roadmap

Graphics is now a stable RHI executor: it owns native resources, synchronization,
and API translation, while Vulkan-specific editor presentation is constrained to
`VulkanEditorBridge`. The next work is therefore a **render-module phase**, not
another graphics/Vulkan phase.

### Render Phase 1 — explicit pass scheduling

**Goal:** make `RenderSystem` own the frame's pass order and resource
dependencies explicitly, without prematurely implementing a general render
graph.

- [x] Introduce a render-private pass interface or equivalent pass descriptor.
  A pass declares its name, the render resources it reads and writes, and a
  recording operation. `RenderPassSchedule` currently validates the ordered
  declarations; its logical resources are render-owned names rather than RHI
  handles. It names no `Vk*` objects, layouts, queues, or command buffers
  (2026-08-24).
- [x] Move the current implicit sequence into two registered passes:
  `ScenePass` writes the scene `RenderTarget`; `EditorCompositePass` consumes
  its borrowed presentation view and performs the terminal editor/UI
  composition through the existing editor bridge (2026-08-24).
- [x] Make `RenderSystem` create the active `FrameContext`, validate the
  declared order, bracket `ScenePass` with common RHI recording, and retain
  ownership of pass registration and lifetime. `RenderScene` becomes scene-pass
  content, not the scheduler. The engine supplies an immediate external
  composite callback only after input polling, so Render never links to Editor
  (2026-08-24).
- [x] Validate the two-pass sequence on Vulkan and OpenGL with the existing
  `GraphicsSmoke` path, including resize and shutdown. Add a headless ordering
  test: a read must follow its writer, and an unsatisfied dependency must be
  rejected before recording. `RenderPassScheduleTest` covers accepted
  Scene→Editor order, read-before-write rejection, and terminal-pass ordering
  (2026-08-24).
- [x] Document the pass/resource ownership rule: render owns logical pass
  policy; graphics owns resource allocation and API-private synchronization;
  the editor bridge is a constrained terminal external pass rather than a
  general backend escape hatch (2026-08-24).

**Done when:** the scene target and editor composite are represented as render
passes with explicit dependencies, `RenderSystem` is their sole scheduler, and
switching Vulkan/OpenGL changes only RHI execution. Do not add graph culling,
automatic transient aliasing, or generalized barrier planning until this
two-pass model has more than one real consumer. **Landed 2026-08-24.**

### Render Phase 2 — Material System V1 (M1–M4 landed 2026-08-26)

M1 replaced `MeshProxy`'s temporary material alias with real render-owned
template and instance handles, immutable surface policy, and template-instance
lifetime protection. M2 adds compact parameter IDs, typed sparse overrides,
and default fallback. M3 resolves material shader/pipeline state and
texture+sampler references through the render-private
[`RenderResourceResolver`](../../engine/runtime/render/render_resource_resolver.h)
which owns static-resource resolution and cache lifetime; MaterialSystem holds
only readiness/diagnostic state. M4 makes `FrameContext` pack material constants
and allocate the transient binding set; the bootstrap scene and graphics smoke
now consume a real `MaterialInstanceHandle`, rather than a raw texture binding.
`FrameContext` remains the owner of frame-local bindings. See
[material_system.md](material_system.md) and
[material_system_TODO.md](material_system_TODO.md).

### Render Phase 3 — renderable proxy input (first slice landed 2026-08-26)

Before general graph work, reconstruct the gameplay-to-render input boundary:
`RenderWorld` now owns queued create/update/destroy commands, the generational
`MeshProxy` registry, and immutable frame snapshots. The ScenePass submits every
visible ready proxy through `FrameContext` and the common command recorder;
there is deliberately no culling or sorting yet. This gives later shadow,
G-buffer, and graph passes real scene inputs instead of a bootstrap-only
`RenderScene`. The design and task ledger are in
[world/component_module.md](../world/component_module.md) and
[world/mesh_proxy_TODO.md](../world/mesh_proxy_TODO.md).

### Later render phases

After Render Phase 3 supplies multiple real pass consumers, evolve the same
declarations into a render graph: dependency sorting, dead-pass culling,
transient-resource lifetime analysis, then backend-private barrier and aliasing
plans. These remain render decisions expressed through the common RHI; Vulkan
and OpenGL continue to own their native state transitions.

## Future work

- **Scene render target + editor presentation (first slice landed).** `RenderSystem` owns a
  render-level `RenderTarget`, backed by private RHI color/depth attachments.
  `RenderTarget::GetView()` returns a borrowed `graphics::RenderTargetView`
  for presentation only: extent plus opaque native color/image-view tokens. It
  neither exposes an owning RHI handle nor transfers destruction responsibility;
  the view expires when its target is resized or destroyed. Vulkan/OpenGL create
  and destroy the attachments privately. The editor consumes the view through
  its API-specific bridge; Render Phase 1 represents the composition sequence
  as an explicit render pass dependency.

- **Bootstrap scene.** `config/bootstrap.json` may define a `scene` block with
  `shader_program`, `model`, and `texture` paths. The engine passes this startup
  policy into `RenderSystem`; after the bootstrap request batch has baked, the
  system resolves the three cached render resources, owns a `RenderScene`, and
  registers it. The configuration names the scene; neither the editor nor the
  RHI contains a demo asset path.

- **Frame-local transient data (Phase 3.4, landed).** `RenderSystem` owns a
  `FrameContext` for every backend frame slot and schedules registered scenes
  with the active context plus common recorder. The context owns transient UBO
  ranges and binding sets; scenes retain only logical and static resource state.

- **Render graph (after Render Phase 1).** The long-term graph uses the pass
  declarations above as nodes with explicit resource dependencies, then adds
  culling, ordering, and lifetime analysis before issuing RHI recording calls.
  `RenderSystem` remains the graph executor; graphics remains an executor rather
  than a scheduler.
