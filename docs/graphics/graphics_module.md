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

This is deliberately **cross-API**: no Vulkan struct, no GL enum, no render pass. Nothing in it is API-specific, which is what lets a render module build it once and hand it to either backend. The shader members are `graphics::Shader*` — see the seam below.

### `Shader` and `ShaderModule` — the two shader seams — [`common/shader.h`](../../engine/runtime/graphics/backend/common/shader.h)

- **`graphics::Shader`** — the current seam. Contract is just `GetCode()/GetCodeSize()`. Today it is implemented as *path-backed* (`OpenglShader`/`VulkanShader` constructed from a file path). The intended consumer shape is a **`ShaderData`-backed** implementation: a thin `ResourceShader : Shader` whose `GetCode()` returns `shader_data->byte_code.data()`. The RHI interface does not change; only what backs it does.
- **`ShaderModule`** — [`common/shader_module.h`](../../engine/runtime/graphics/backend/common/shader_module.h) — the *other*, more API-shaped seam that already takes compiled bytes: `Initialize(GraphicsContext, std::shared_ptr<data::ShaderData>)`. Its implementations ([`vulkan_shader_module.cpp`](../../engine/runtime/graphics/backend/vulkan/vulkan_shader_module.cpp), [`opengl_shader_module.cpp`](../../engine/runtime/graphics/backend/opengl/opengl_shader_module.cpp)) reference `shader->glsl`/`shader->spirv` — fields that don't exist on `ShaderData` — and are **not in the build** (see the Graphics CMakeLists). Stale; the real consumers should read `data::ShaderData::api` + `byte_code`.

### Texture / Mesh / Sampler + managers — the good pattern

[`Texture`](../../engine/runtime/graphics/backend/common/texture.h), [`Mesh`](../../engine/runtime/graphics/backend/common/mesh.h), and `Sampler` are constructed **from data structs**, not from paths: `TextureManager::CreateTexture(GraphicsContext, const data::TextureData&, const TextureSettings&)`, `MeshManager::CreateMesh(GraphicsContext, const data::MeshData&)`. This is the correct RHI shape — take engine data, own the GPU object, hand back a handle.

Every manager follows the same pattern ([`texture_manager.h`](../../engine/runtime/graphics/backend/common/texture_manager.h), [`mesh_manager.h`](../../engine/runtime/graphics/backend/common/mesh_manager.h)):

| Field | Type | Job |
|---|---|---|
| `resources_` | `vector<unique_ptr<Slot>>` | slot storage — **owner of record** |
| `handle_system_` | `HandleSystem<T>` | generation counters for slot validity |

`ShaderManager` is the one manager that **breaks** this pattern: it is path-keyed (`unordered_map<string, ShaderHandle>`) and its `CreateShader<API>(type, path)` reads the file itself ([`shader_manager.cpp`](../../engine/runtime/graphics/backend/common/shader_manager.cpp)). That is the RHI reaching up into the resource layer, and it is the piece to retire.

### `RenderBackend` — the facade — [`common/render_backend.h`](../../engine/runtime/graphics/backend/common/render_backend.h)

Factory + frame loop + buffer creation:

```cpp
static std::unique_ptr<RenderBackend> CreateGraphicsBackEnd(GraphicsAPIType);
virtual void Initialize() / BeginFrame() / EndFrame() / Present() / Cleanup() = 0;
virtual BufferHandle CreateVertexBuffer(const void*, size_t) = 0;
virtual BufferHandle CreateIndexBuffer(const void*, size_t) = 0;
```

Two known leaks live here, both flagged in the code:
- `ShaderManager shader_manager_` is a **member** — the RHI owns a shader cache. Shader caching belongs to the render module (or the resource pipeline), not the RHI.
- `GLFWwindow *window_` is marked "just expose for test purpose, should be removed later."

`VulkanBackend` additionally owns its own `pipeline_manager_`, `texture_manager_`, `sampler_manager_`, `mesh_manager_`, `buffer_manager_`, `image_memory_manager_` ([`vulkan_backend.h`](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.h)). Ownership of these is fine — they are per-backend GPU state. What is **not** fine is that `VulkanBackend::CreateGraphicsPipeline()` *builds* the `PipelineDesc` itself ([`vulkan_backend.cpp:712`](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.cpp#L712-L752)): it decides shader stage, vertex layout, and descriptor bindings. Building a pipeline description is render-module work; the backend should only *bake* the desc it is handed.

## Current state — leaks to fix

The RHI does four things it should not:

1. **Builds `PipelineDesc` internally.** `VulkanBackend::CreateGraphicsPipeline` hardcodes the `simple_triangle` pipeline — stage, layout, bindings ([`vulkan_backend.cpp:712-751`](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.cpp#L712-L751)).
2. **Reads shader files by path.** `ShaderManager::CreateShader<API>(type, path)` loads the file itself ([`shader_manager.cpp:42-66`](../../engine/runtime/graphics/backend/common/shader_manager.cpp#L42-L66)).
3. **Loads prebuilt `.spv` / `.vert` at init** instead of consuming `ShaderData` from the resource pipeline — Vulkan reads build-time `.spv` ([`vulkan_backend.cpp:717-720`](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.cpp#L717-L720)), OpenGL reads `.vert/.frag` source ([`opengl_backend.cpp:119-122`](../../engine/runtime/graphics/backend/opengl/opengl_backend.cpp#L119-L122)).
4. **The build-time glslc step.** `Graphics/CMakeLists.txt` runs `glslc` at configure/build time to precompile `asset/shader/simple_triangle.vert/.frag` → `.spv` into `CMAKE_BINARY_DIR/shaders/`, feeding leak #3. This is the "vulkan_backend loads prebuilt .spv" legacy path; it should be reconciled with the content-addressed `resource::ShaderCache`.

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
- The `Shader` interface — implemented by the render module over `ShaderData` (or the `ShaderModule` seam, once its implementations read `ShaderData::api` + `byte_code`).

## Build wiring

- `Graphics` (STATIC) links `Core glad VulkanSDK`, PRIVATE `Data Asset glfw` ([`graphics/CMakeLists.txt`](../../engine/runtime/graphics/CMakeLists.txt)).
- `RuntimeLib` links `Graphics` **PUBLIC** ([`runtime/CMakeLists.txt`](../../engine/runtime/CMakeLists.txt)) — the render module and editor can use it.
- The glslc build step (above) is part of this CMake.

## Design notes

- **Two shader seams, one target.** `graphics::Shader` (byte source) and `ShaderModule` (device module) both point at the same fix: consume `data::ShaderData { stage, api, byte_code, entry }` instead of paths. Prefer the smaller change first — a `ShaderData`-backed `Shader` implementation keeps `PipelineDesc` and `CreatePipelineResource` untouched.
- **Managers are the model.** `TextureManager`/`MeshManager`/`SamplerManager` (slot + HandleSystem + create-from-data) are the right RHI pattern. `ShaderManager` (path-keyed, reads files) is the anti-pattern to delete.
- **Pipeline cache ownership.** Per the asset module's design note, a render-side pipeline cache keyed by `(program AssetID, api)` is cleaner than stuffing artifacts into the asset graph. The RHI should expose "bake this `PipelineDesc`", not "find me a cached shader".
- **Derived data stays out of the RHI.** Compiling source → bytes is `resource/`. The RHI only ever sees bytes.

## Refactor status

**Not started — leaks present.** The RHI still builds pipeline descriptions and reads shader files by path. The seam (`ShaderModule` taking `ShaderData`) is defined but its implementations are stale and unbuilt. Target state: `PipelineDesc` built by the render module, shaders backed by `ShaderData`, `ShaderManager` retired. See [the render module doc](../render/render_module.md) for the reconstruction that drives this.
