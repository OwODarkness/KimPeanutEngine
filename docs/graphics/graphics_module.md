# Graphics (RHI) Module Design

Location: `engine/runtime/graphics/backend/`

The graphics module is the engine's RHI — the thin, cross-API layer that wraps Vulkan and OpenGL. It owns GPU state (swapchain, devices, buffers, textures, pipelines) and exposes it to the rest of the engine through **handles** and **cross-API descriptions**. The module above it (the render module) fills those descriptions; the backend bakes them into GPU objects. The RHI must never reach *up* — it knows nothing about `.shader` files, shader compilers, asset IDs, or the asset graph.

## Layout

```
engine/runtime/graphics/backend/
├── common/    cross-API interfaces, shared managers, handle types, PipelineDesc
├── opengl/    OpenGL backend (glad)
└── vulkan/    Vulkan backend
```

The `common/` types are the contract. The two backends are swappable implementations behind `RenderBackend::CreateGraphicsBackEnd(GraphicsAPIType)` ([`render_backend.cpp`](../../engine/runtime/graphics/backend/common/render_backend.cpp)).

## Key types

### Handles — [`common/api.h`](../../engine/runtime/graphics/backend/common/api.h)

Every GPU resource is referenced by a tagged handle built on `HandleSystem` (the same generation-counter machinery the asset cache uses, [`engine/runtime/core/base/handle.h`](../../engine/runtime/core/base/handle.h)):

```cpp
using TextureHandle = Handle<TextureTag>;
using SamplerHandle  = Handle<SamplerTag>;
using ShaderHandle   = Handle<ShaderTag>;
using PipelineHandle = Handle<PipelineTag>;
using BufferHandle   = Handle<BufferTag>;
using MeshHandle     = Handle<MeshTag>;
```

A handle is `(slot, generation)` — slots recycle, the generation guards stale handles.

### `PipelineDesc` — the pipeline contract — [`common/pipeline_types.h`](../../engine/runtime/graphics/backend/common/pipeline_types.h)

The one description a render module fills and a backend bakes:

```cpp
struct PipelineDesc {
    Shader *vert_shader, *frag_shader, *geom_shader;
    std::vector<VertexBindingDesc> binding_descs;
    std::vector<VertexAttributionDesc> attri_descs;
    PrimitiveTopologyType primitive_topology_type;
    RasterState raster_state;
    MultisampleState multisample_state;
    BlendAttachmentState blend_attachment_state;
    std::vector<std::vector<DescriptorBindingDesc>> descriptor_binding_descs; // descs[i] = set i
    std::vector<TextureFormat> color_attachment_formats;
    TextureFormat depth_attachment_format;
};
```

This is deliberately **cross-API**: no Vulkan struct, no GL enum, no render pass. Nothing in it is API-specific, which is what lets a render module build it once and hand it to either backend. The shader members are `data::ShaderData*` — see below.

### Shader input — `data::ShaderData` in `PipelineDesc` (Phase 0 landed 2026-08-15)

`PipelineDesc`'s shader members are `data::ShaderData*` directly — the resource pipeline's baked artifact *is* the RHI's input, no wrapper. Each backend reads the field its own API needs: Vulkan `byte_code` (SPIR-V), OpenGL `source` (preprocessed GLSL). The old `graphics::Shader` abstraction (`GetCode()`/`GetCodeSize()`, path-backed `OpenglShader`/`VulkanShader`, then the `ResourceShader` wrapper) is **retired** — its `api`-based dispatch was redundant because each backend *is* its own API.

- **Raw `ShaderData` → API object is a per-backend pipeline-bake detail, not a seam.** The old `ShaderModule` abstraction is **retired and deleted 2026-08-16** ([TODO 1.2](TODO.md)): its impls read `shader->glsl`/`shader->spirv` (fields `ShaderData` doesn't have), `GetHandle() → const void*` was a leaky abstraction (Vulkan's `VkShaderModule` and GL's compiled-shader object have different lifetimes — GL deletes the shader right after link), and the work was already duplicated in the pipeline bakes. Each backend wraps the conversion where the shader is needed: `VulkanPipelineManager::CreateShaderModule(device, byte_code.data(), size, &module)` ([vulkan_pipeline_manager.cpp:323](../../engine/runtime/graphics/backend/vulkan/vulkan_pipeline_manager.cpp#L323)), and `OpenglPipeline::Initialize` does `glCreateShader` → `glShaderSource` → `glCompileShader` → `glAttachShader` → `glLinkProgram` → `glDeleteShader` from `source` ([opengl_pipeline.cpp:15](../../engine/runtime/graphics/backend/opengl/opengl_pipeline.cpp#L15)).

### Texture / Mesh / Sampler + managers — the good pattern

[`Texture`](../../engine/runtime/graphics/backend/common/texture.h), [`Mesh`](../../engine/runtime/graphics/backend/common/mesh.h), and `Sampler` are constructed **from data structs**, not from paths: `TextureManager::CreateTexture(GraphicsContext, const data::TextureData&, const TextureSettings&)`, `MeshManager::CreateMesh(GraphicsContext, const data::MeshData&)`. This is the correct RHI shape — take engine data, own the GPU object, hand back a handle.

Every manager follows the same pattern ([`texture_manager.h`](../../engine/runtime/graphics/backend/common/texture_manager.h), [`mesh_manager.h`](../../engine/runtime/graphics/backend/common/mesh_manager.h)):

| Field | Type | Job |
|---|---|---|
| `resources_` | `vector<unique_ptr<Slot>>` | slot storage — **owner of record** |
| `handle_system_` | `HandleSystem<T>` | generation counters for slot validity |

`ShaderManager` was the one manager that **broke** this pattern: it was path-keyed (`unordered_map<string, ShaderHandle>`) and its `CreateShader<API>(type, path)` read the file itself — the RHI reaching up into the resource layer. **Retired and deleted 2026-08-15** (`shader_manager.*`, `shader_factory.h`, `vulkan_shader.*`, `opengl_shader.*`).

### `RenderBackend` — the facade — [`common/render_backend.h`](../../engine/runtime/graphics/backend/common/render_backend.h)

Factory + frame loop + buffer creation:

```cpp
static std::unique_ptr<RenderBackend> CreateGraphicsBackEnd(GraphicsAPIType);
virtual void Initialize(WindowHandle) / BeginFrame() / EndFrame() / Cleanup() = 0;
virtual PipelineHandle CreatePipelineResource(const PipelineDesc&) = 0;
virtual bool DestroyPipelineResource(PipelineHandle) = 0;
virtual MeshHandle CreateMesh(const data::MeshData&) = 0;
virtual TextureHandle CreateTexture(const data::TextureData&, const TextureSettings&) = 0;
virtual SamplerHandle CreateSampler(const SamplerSettings&) = 0;
virtual DescriptorSetHandle CreateResourceBindingSet(PipelineHandle,
                                                      const ResourceBindingSetDesc&) = 0;
virtual BufferHandle CreateVertexBuffer(const void*, size_t) = 0;
virtual BufferHandle CreateIndexBuffer(const void*, size_t) = 0;
```

The `window_` test seam is gone (Phase 5, 2026-08-16): `Initialize` takes the native window handle (`WindowHandle` = `void*`) as an explicit parameter — the backends cast it back to `GLFWwindow*` internally, so the common interface never sees GLFW. The dead public `CameraData camera_data` member was removed with it. The `ShaderManager shader_manager_` member was **deleted 2026-08-15** — shader caching belongs to the render module / resource pipeline.

Static-resource creation is also façade-owned (2026-08-20): render code passes
CPU `data::MeshData` / `data::TextureData` plus common settings to
`RenderBackend`; the backend creates its private `GraphicsContext`, delegates
handle storage to its mesh/texture/sampler managers, and performs API-specific
uploads internally. Callers never access a manager or Vulkan/OpenGL context.

Resource binding follows the same rule (Phase 3.2, 2026-08-20).
`ResourceBindingSetDesc` is a small variant of `UniformBufferBinding` and
`SampledTextureBinding`, expressed only with RHI handles. The backend returns a
`DescriptorSetHandle`; Vulkan's pool, set allocation, update writes, image
layouts, and native binding stay inside `VulkanDescriptorSetManager`. OpenGL
stores the equivalent binding state behind the same handle. The current
`BindResourceBindingSet` call is transitional; Phase 3.3 moves it onto a common
frame command recorder.

`VulkanBackend` additionally owns its own `pipeline_manager_`, `texture_manager_`, `sampler_manager_`, `mesh_manager_`, `buffer_manager_`, `image_memory_manager_` ([`vulkan_backend.h`](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.h)). Ownership of these is fine — they are per-backend GPU state. Since 2026-08-20 the common facade initializes independently of pipelines, then `CreatePipelineResource(PipelineDesc)` bakes any caller-owned description into a `PipelineHandle`. Vulkan completes omitted attachment formats from the swapchain format, an RHI-owned invariant.

#### The frame-recording API (Phase 4 landed 2026-08-15)

The Vulkan backend now splits the frame lifecycle so **a caller records the draws** ([vulkanbackend.md](vulkanbackend.md) Phase 4):

- `BeginFrame()` — waits on the in-flight fence, acquires the next swapchain image, resets the fence, and prepares the frame's **scene command buffer** (`GetCurrentSceneCommandBuffer()`): begin, color/depth transitions to attachment-optimal, `vkCmdBeginRendering`, viewport/scissor. Callers issue draws against that buffer between `BeginFrame`/`EndFrame`.
- `EndFrame()` — ends rendering, transitions the swapchain image to present, submits, presents, handles resize.
- Scene-side facilities — `CreateUniformBuffer`/`MapUniformBuffer` (persistent per-buffer mapping), `UploadTexturePixels` (stage → one-shot copy → sample), and the manager getters (`GetTextureManager`/`GetMeshManager`/`GetSamplerManager`, `GetBufferResource`).

The demo that used to live inside the backend is now the render module's first real scene, [`render::RenderScene`](../../engine/runtime/render/render_scene.h), recording through this API (raw `vkCmd*` for now — Vulkan-specific stopgap). The RHI still never initiates; it exposes where the frame is and the caller decides what's in it.

## Current state — leaks fixed vs remaining

Leaks 1–4 were **fixed 2026-08-15/16** (Phase 0 of the [Vulkan decoupling](vulkanbackend.md) + the glslc removal 2026-08-16).

1. ~~**Builds `PipelineDesc` internally.**~~ Fixed — callers build the desc; `CreatePipelineResource(PipelineDesc)` bakes it into an independent handle.
2. ~~**Reads shader files by path.**~~ Fixed — `ShaderManager` retired; shaders arrive as `data::ShaderData*` in `PipelineDesc`.
3. ~~**Prebuilt `.spv` / `.vert` still read at init.**~~ Fixed 2026-08-16 — the `rhi_example` demo now bakes its shaders at runtime through the resource pipeline (`LoadSync(.shader)` → `ProcessShader` → `ShaderData`), so nothing reads prebuilt shader files anymore.
4. ~~**The build-time glslc step.**~~ Fixed 2026-08-16 — the `glslc` build step is deleted from `Graphics/CMakeLists.txt` ([TODO 2.3](TODO.md)); `ProcessShader` compiles into the content-addressed `resource::ShaderCache` at runtime instead.

## The intended contract — RHI as pure receiver

**The RHI responds, it never initiates.** It takes descriptions and engine data, and produces GPU objects:

```
render module ──(PipelineDesc, data::TextureData, data::MeshData)──▶ RHI backend
                    │
                    └──▶ GPU objects (VkPipeline, VkBuffer, GL programs, …)
```

The RHI must not know:
- `.shader` file formats, shader source paths, or shaderc/SPIR-V — that is `resource/` + `asset/`.
- Asset IDs or the asset graph — that is `asset/`.
- "which pipeline a game needs" — that is the render module.

It should know:
- `PipelineDesc` (pipeline contract), `GraphicsContext`, `data::*` payloads.
- `data::ShaderData*` — the baked artifact, carried in `PipelineDesc`. The backend converts it to its API-native object inside the pipeline bake (`VkShaderModule` / compiled GL shader).

## Build wiring

- `Graphics` (STATIC) links `Core glad VulkanSDK`, PRIVATE `Data Asset glfw` ([`graphics/CMakeLists.txt`](../../engine/runtime/graphics/CMakeLists.txt)).
- `RuntimeLib` links `Graphics` **PUBLIC** ([`runtime/CMakeLists.txt`](../../engine/runtime/CMakeLists.txt)) — the render module and editor can use it.
- No build-time shader step remains — shaders compile at runtime through `ResourcePipeline::ProcessShader` (see [resource_module.md](../resource/resource_module.md)).

## Design notes

- **One shader seam — and it's flat.** `PipelineDesc` carries `data::ShaderData { stage, api, byte_code, source, entry }` directly — the `graphics::Shader` wrapper is retired (2026-08-15) and the `ShaderModule` device-module seam followed (2026-08-16, [TODO 1.2](TODO.md)). Each backend reads the field its own API needs and wraps the raw → API-object conversion inside its pipeline bake.
- **Managers are the model.** `TextureManager`/`MeshManager`/`SamplerManager` (slot + HandleSystem + create-from-data) are the right RHI pattern. `ShaderManager` (path-keyed, reads files) is the anti-pattern to delete.
- **Pipeline cache ownership.** Per the asset module's design note, a render-side pipeline cache keyed by `(program AssetID, api)` is cleaner than stuffing artifacts into the asset graph. The RHI should expose "bake this `PipelineDesc`", not "find me a cached shader".
- **Derived data stays out of the RHI.** Compiling source → bytes is the CPU-side processing layer — `resource/` (see [resource_module.md](../resource/resource_module.md)). The RHI only ever sees bytes.

## Refactor status

**Phases 0–5 landed (2026-08-15/16), plus TODO 2.3 and 1.2 (2026-08-16).** Shaders arrive as `data::ShaderData*` in `PipelineDesc`; `ShaderManager`/`Shader`/`ResourceShader`/`ShaderLoader` retired; `VulkanDevice`/`VulkanSwapchain`/`VulkanFrameContext` extracted; scene recording extracted — the backend exposes the frame's command buffer and the demo lives as `render::RenderScene`. Phase 5 (2026-08-16): the `window_`/`camera_data` public seams are gone — `Initialize` takes the native window handle explicitly — and the sakura split is decided (the facade keeps the frame loop; the device/frame split lives inside the backend). Milestone 1 (2026-08-20): backend initialization no longer takes a pipeline; `CreatePipelineResource`/`DestroyPipelineResource` own independent `PipelineHandle` lifetimes. TODO 2.3 (2026-08-16): the build-time `glslc` step is gone — the `rhi_example` bakes shaders at runtime via `ProcessShader`, giving the resource pipeline its first graphics-end caller. TODO 1.2 (2026-08-16): the `ShaderModule` seam is retired. See [the render module doc](../render/render_module.md) for the reconstruction that drives this.

Task ledger: [TODO.md](TODO.md) (the working list, tick as items land) · design references: [sakura_reference.md](sakura_reference.md) (how Sakura Engine shapes its render backends — learn from, not copy) and [rhi_design_material.md](rhi_design_material.md) (RHI design material from a Zhihu thread on wrapping a modern-game-engine RHI layer — critical synthesis).
