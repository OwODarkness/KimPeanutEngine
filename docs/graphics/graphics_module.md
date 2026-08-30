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

## Build boundary

`Graphics` compiles common sources plus the backend source lists selected by
`USE_OPENGL` and `USE_VULKAN`. The native libraries (`glad`, `VulkanSDK`, and
GLFW) and the `backend/` source include root are **PRIVATE** implementation
dependencies. Consumers receive only the common RHI target contract through
their own module headers; they do not inherit native SDK include paths or link
requirements. The `EditorUILib` Vulkan ImGui bridge is the explicit exception:
it links `VulkanSDK` itself because it deliberately records Vulkan-only UI
work. `EditorLib` remains the backend-agnostic editor-tool layer and does not
link `VulkanSDK`, glad, or ImGui.

An OpenGL-only configuration (`-DUSE_OPENGL=ON -DUSE_VULKAN=OFF`) compiles only
the common and OpenGL sources; requesting a disabled API from the factory
returns no backend. The current repository-wide `engine/runtime` include root
still permits a determined internal target to spell a private file path. A
future physical public/private header-directory split will make that include
rule mechanically enforceable too.

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
Destroying a handle advances its generation immediately, so a released handle is
rejected even before its slot is recycled. Callers must treat every successful
`Destroy*` call as the end of that handle's lifetime.

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

`CreatePipelineResource` validates this contract before allocating API objects:
vertex and fragment artifacts must be present, match the selected API and
stages, and be non-empty; vertex binding/location and per-set descriptor
binding indices must be unique; descriptor counts and attachment formats must
be valid. Invalid descriptions return an invalid `PipelineHandle`.

**Attachment semantics (D2 landed 2026-08-29):** empty `color_attachment_formats`
is a legal **depth-only** pipeline and `depth_attachment_format ==
`TEXTURE_FORMAT_UNKNOW`` means **no depth**; a description is rejected only when
both are absent. Each backend derives its depth test/write from this: a
no-depth pipeline disables them (a stale depth test against a missing
attachment discards fragments). Vulkan and OpenGL both store the caller's
formats on the pipeline resource so the recorder can reject a target mismatch
at bind time (see below).

### Render targets — the target contract (D2 landed 2026-08-29)

Offscreen targets are also caller-described, via [`common/render_target.h`](../../engine/runtime/graphics/backend/common/render_target.h):

```cpp
struct RenderTargetColorAttachment {         // per-attachment load/store/clear
    TextureFormat format = TEXTURE_FORMAT_RGBA8_SRGB;
    RenderTargetLoadOp load_op = Clear;
    RenderTargetStoreOp store_op = Store;
    std::array<float,4> clear_color{0,0,0,1};
};
struct RenderTargetDepthAttachment {
    TextureFormat format = TEXTURE_FORMAT_D32;
    RenderTargetLoadOp load_op = Clear;
    RenderTargetStoreOp store_op = Store;
    float clear_depth = 1.0f; uint32_t clear_stencil = 0;
    bool shader_readable = false;            // opt-in sampled depth (D4)
};
struct RenderTargetDesc {
    uint32_t width = 0, height = 0; uint32_t sample_count = 1;
    std::vector<RenderTargetColorAttachment> color_attachments;
    std::optional<RenderTargetDepthAttachment> depth;
};
```

`ValidateRenderTargetDesc` and
`ValidateRenderTargetPipelineCompatibility` ([`common/render_target_validation.{h,cpp}`](../../engine/runtime/graphics/backend/common/render_target_validation.h)) are the shared gate: a target needs a non-zero extent and ≥1 attachment overall; D2 accepts `sample_count == 1` only because multisample attachment/resolve and sampled-MSAA bindings are not implemented yet. A pipeline matches a target only when color count and per-color format, depth presence/format (UNKNOW = compatible with any), and sample count all agree. Both backends translate N color attachments (Vulkan `vkCmdBeginRendering` with N `VkRenderingAttachmentInfo`; OpenGL `glNamedFramebufferTexture(GL_COLOR_ATTACHMENT0+i)` + `glDrawBuffers`, or `glDrawBuffer(GL_NONE)` for depth-only), read back color[0] for RGBA8_SRGB targets, and expose per-attachment `GetRenderTargetColorAttachment(index)` / `GetRenderTargetDepthAttachment` plus an opt-in sampled-depth accessor on the facade — no native image/view leaks above Graphics.

`RasterState::front_face` is expressed in the engine's y-up clip-space
convention. OpenGL translates it directly. Vulkan retains a positive-height
viewport, whose upper-left framebuffer coordinates reverse winding, so its
pipeline translation swaps clockwise/counter-clockwise. This keeps common
back-face culling semantic across APIs without shader branches or a viewport
orientation change.

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

`RenderBackend::GetCapabilities()` returns an immutable
`GraphicsCapabilities` value populated once during backend initialization. It
reports the effective common-RHI path, rather than the complete native driver
feature set: `max_sampled_textures_per_shader_stage` is queried from the active
device, while `bindless_textures` becomes true only after a backend enables the
native feature *and* the common RHI exposes a bindless resource-table contract.
It is intentionally false today on Vulkan and OpenGL; neither backend has that
common path yet. This lets render policy choose portable behavior without
including Vulkan/OpenGL feature structures or probing the native device.

### Planned bindless texture path

Bindless is planned as an **optional sampled-texture binding mode**, not as a
general escape hatch for native descriptor heaps or OpenGL texture handles. In
the existing bound path, a material's texture is written into a transient
`ResourceBindingSetDesc` and is selected before a draw. In the bindless path,
the material carries a small common table index; shaders use that index to
sample an RHI-owned global texture table. The render module still owns material
policy and decides whether to use the path; Graphics owns the table's GPU
representation, native descriptor/handle state, and deferred slot reuse.

```text
MaterialInstance -> common bindless texture slot -> shader table lookup
                       |                         |
                       +-- Graphics owns native --+
                           Vulkan descriptor table / GL resident handle table
```

For an opt-in material template, `FrameContext` omits ordinary sampled-texture
bindings and writes the table slot IDs into the beginning of its set-0,
binding-3 material block. V1 is a `std140 uvec4 texture_indices[]` prefix with
one 16-byte element per material parameter; the parameter ID is the array
index and `.x` is the common generational slot ID. A compatible shader must
use this table lookup; if the capability is absent or a slot cannot be acquired,
the resolver clears the partial cache and retains ordinary material bindings.
One shader-program asset carries both bound and bindless variants. Graphics
capability selection chooses the bindless variant only when the table is
available; otherwise it selects the ordinary variant and per-draw bindings.

The normal engine bootstrap config names one `shader_program`. Changing the
backend's effective `GraphicsCapabilities::bindless_textures` result changes
the active variant at template resolution; it does not require a scene-code
switch or a native API branch.

`ShaderProcessor` supplies exactly one target macro to compatible shader
sources: `KP_GRAPHICS_API_VULKAN` or `KP_GRAPHICS_API_OPENGL`. The V1 sample
uses one fragment source with `KP_USE_BINDLESS` to declare Vulkan's set-1
runtime sampler array or OpenGL's resident-handle SSBO without leaking either
declaration into Render. Pipeline cache keys include the binding model, so a
bound and bindless variant cannot reuse an incompatible descriptor layout.

The first scope is sampled textures only. Buffers, storage images, acceleration
structures, and arbitrary descriptor arrays are separate decisions: they must
not be folded into a vague `bindless` flag simply because an API supports them.
The common handle will be generational, so a released texture-table slot cannot
silently refer to a newly allocated texture. A slot remains unavailable for
reuse until every submitted frame that could read it has completed. The table
must also have a fixed shader-visible layout/version, an explicit capacity, and
a defined fallback when the capability is unavailable or exhausted.

The B0 common ABI is version `1`: it reserves descriptor set `1`, binding `0`
for the sampled-texture table and caps V1 at 4096 slots. Shader preprocessing
publishes `KP_BINDLESS_TEXTURE_TABLE_ABI_VERSION`,
`KP_BINDLESS_TEXTURE_TABLE_SET`, and `KP_BINDLESS_TEXTURE_TABLE_BINDING`; a
compatible shader declares the backend-appropriate sampled-array form with
those values. A material passes only the `BindlessTextureHandle` slot index,
never a native resource handle. `GraphicsCapabilities` exposes only
whether this complete common path is usable and its clamped table capacity. An
invalid acquired slot, an unavailable capability, or an exhausted table means
the renderer must use the ordinary `ResourceBindingSetDesc` material binding.
The B0 API defines allocation/release requests, but the default backend
implementation deliberately returns an invalid slot until B2/B3 establish
native creation, visibility, and retirement semantics.

### Bindless slot lifetime protocol (B1)

`BindlessTextureSlotAllocator` is common, CPU-only lifetime policy that each
backend-private table will own. It neither stores textures nor creates native
descriptors/handles. Allocation returns a generational slot; release makes that
slot immediately invalid to callers, increments its generation, and quarantines
the physical table index behind the caller-supplied monotonic submission serial.
Only `CollectCompleted(completed_serial)` may return a quarantined index to the
allocator. This keeps a stale material slot from aliasing either a replacement
or a later texture while earlier GPU work can still read the old entry.

The required backend protocol is:

1. Queue creates and replacements as pending table writes. Apply them before
   command recording begins for their target frame; writes requested after
   recording starts become visible no earlier than the following frame.
2. When releasing a slot, include the last submission that could reference it:
   the most recently submitted frame plus the current frame's pending serial if
   it is recording. Keep the old native descriptor/resident handle alive until
   that serial is complete.
3. At a frame-slot fence completion point, report the completed submission to
   `CollectCompleted`, then recycle matching indices. No normal update or
   release may call `WaitIdle`.

V1 capacity is fixed for the initialized table. The allocator publishes its
configured capacity, allocated count, quarantined count, and allocation-failure
count so B2/B3 can provide deterministic exhaustion telemetry. If a 16-bit
generation would wrap to zero, the index stays permanently quarantined rather
than making an old handle valid again.

Vulkan will require a deliberately enabled descriptor-indexing feature set,
descriptor-set layout/pool flags, and update/reuse synchronization. OpenGL will
require the chosen bindless texture extension, a resident-handle lifetime, and
the equivalent table upload. Those details stay private to their backends.
`GraphicsCapabilities::bindless_textures` turns true only after the complete
common contract exists and the initialized backend can honor it; a driver
advertising an extension alone is insufficient.

Vulkan completes this path when descriptor indexing supports runtime sampled
arrays, non-uniform sampled-image indexing, partially bound descriptors, and
the update-after-bind subset. It allocates one private global descriptor set per
frame slot, applies requested table entries only after that slot fence completes,
and binds the current set at the reserved layout position whenever a pipeline is
bound. Pipeline layouts reserve set `1` while the table is enabled; ordinary
set-`0` material bindings remain unchanged. A release invalidates the common
slot immediately but retains both its descriptor reference and physical index
until the recorded submission serial is complete. Unsupported devices expose
zero table capacity and continue through ordinary resource bindings.

OpenGL completes the same V1 contract only when both
`GL_ARB_bindless_texture` and `GL_ARB_gpu_shader_int64` are present and the
three resident-handle entry points load successfully. Its private table is a
shader-storage buffer of resident `GLuint64` texture/sampler handles bound at
the logical V1 binding. The current OpenGL backend has one frame slot: it fences
the submitted frame, waits that fence before its next table update, and only
makes released handles non-resident after it completes. A driver without the
extension path reports zero capacity; ordinary texture-unit bindings remain the
fallback.

Resource binding follows the same rule (Phase 3.2, 2026-08-20).
`ResourceBindingSetDesc` is a small variant of `UniformBufferBinding` and
`SampledTextureBinding`, expressed only with RHI handles. The backend returns a
`DescriptorSetHandle`; Vulkan's pool, set allocation, update writes, image
layouts, and native binding stay inside `VulkanDescriptorSetManager`. OpenGL
stores the equivalent binding state behind the same handle. The current
`CommandRecorder::BindResourceBindings` is the scene-facing call. Vulkan
translates it to descriptor-set binding; OpenGL stores equivalent binding state
and uses `glBindBufferRange` for uniform ranges plus texture/sampler binding
points. These are backend-only differences.

`VulkanBackend` additionally owns its own `pipeline_manager_`, `texture_manager_`, `sampler_manager_`, `mesh_manager_`, `buffer_manager_`, `image_memory_manager_`, synchronous `VulkanUploadContext`, and `VulkanEditorBridge` ([`vulkan_backend.h`](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.h)). Ownership of these is fine — they are per-backend GPU state. The upload context owns staging-buffer creation and one-shot transfer submission, but delegates allocation and release to the buffer/memory managers. The editor bridge borrows the active frame/swapchain resources and brackets one external ImGui pass; it never owns or exposes general backend resources. `VulkanBackend` publishes none of these managers or native Vulkan objects: Vulkan mesh/texture adapters receive only the private buffer-upload and image-memory services they require through `VulkanContext`. Since 2026-08-20 the common facade initializes independently of pipelines, then `CreatePipelineResource(PipelineDesc)` bakes any caller-owned description into a `PipelineHandle`. Attachment formats come from the caller's `PipelineDesc` exactly as written (D2 removed the swapchain auto-fill); the pipeline resource stores them for target-compatibility checks.

#### The frame-recording API (Phase 4 landed 2026-08-15)

The Vulkan backend now splits the frame lifecycle so **a caller records the draws** ([vulkanbackend.md](vulkanbackend.md) Phase 4):

- `BeginFrame()` — waits on the in-flight fence, acquires the next swapchain image, resets the fence, and begins the frame command buffer. It does not select a render attachment.
- `CommandRecorder::BeginRenderTarget` / `EndRenderTarget` — select and clear an offscreen target, set its viewport/scissor, and bracket the caller's draws. Vulkan performs dynamic rendering and transitions the stored color result to shader-read layout; OpenGL binds/unbinds its framebuffer.
- `EndFrame()` — closes any open target, asks the editor bridge to make the acquired swapchain image presentable if no external pass recorded it, then submits, presents, and handles resize. The editor bridge composites the ImGui scene viewport into that swapchain image when the editor is active.
- Scene-side facilities — `CreateUniformBuffer`/`MapUniformBuffer` (persistent per-buffer mapping), and `UploadTexturePixels` (the backend routes stage → one-shot copy → sample through `VulkanUploadContext`). Backend-private managers remain implementation details.

The demo that used to live inside the backend is now the render module's first real scene, [`render::RenderScene`](../../engine/runtime/render/render_scene.h). It records API-neutral commands through `CommandRecorder`, and the active render `FrameContext` owns transient UBO ranges and binding sets. Vulkan encodes the commands with `vkCmd*`; OpenGL issues the corresponding `gl*` calls. The RHI still never initiates; it exposes where the frame is and the caller decides what's in it.

The stable recording interval is deliberately small:

```text
BeginFrame → FrameContext allocation/bindings → BeginRenderTarget
           → BindPipeline / BindMesh / BindResourceBindings
           → SetViewport / SetScissor / DrawIndexed → EndRenderTarget → EndFrame
```

`CommandRecorder*` is available only between `BeginFrame` and `EndFrame`.
Static resources and pipelines are render-owned; frame UBO ranges and binding
sets are transient and must not be retained after their frame slot ends.

### Contract verification (2026-08-24)

`GraphicsContractTest` covers immediate stale/forged-handle rejection,
`PipelineDesc` validation, and aligned shared-block free-range merging without
requiring a GPU. `GraphicsSmoke` renders the same `RenderScene` through Vulkan
and OpenGL for three frames, including resize, shared frame-slot reuse, a
dedicated >4 MiB mapped uniform allocation, and normal teardown. It is a visual
execution smoke, not yet a pixel-comparison test.

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

**Phases 0–5 and Milestone 6.1–6.5 landed (2026-08-24), plus TODO 2.3 and 1.2 (2026-08-16).** Shaders arrive as `data::ShaderData*` in `PipelineDesc`; `ShaderManager`/`Shader`/`ResourceShader`/`ShaderLoader` retired; `VulkanDevice`/`VulkanSwapchain`/`VulkanFrameContext` extracted; scene recording extracted — the backend exposes only common recording through `CommandRecorder`. Milestone 6.1 extracted Vulkan command encoding, 6.2 extracted render-target ownership, 6.3 extracted synchronous staging/transfer uploads into `VulkanUploadContext`, 6.4 moved editor composition behind `VulkanEditorBridge`, and 6.5 removed public native Vulkan/manager escape hatches; the common RHI surface did not expand. Phase 5 (2026-08-16): the `window_`/`camera_data` public seams are gone — `Initialize` takes the native window handle explicitly — and the sakura split is decided (the facade keeps the frame loop; the device/frame split lives inside the backend). Milestone 1 (2026-08-20): backend initialization no longer takes a pipeline; `CreatePipelineResource`/`DestroyPipelineResource` own independent `PipelineHandle` lifetimes. TODO 2.3 (2026-08-16): the build-time `glslc` step is gone — the `rhi_example` bakes shaders at runtime via `ProcessShader`, giving the resource pipeline its first graphics-end caller. TODO 1.2 (2026-08-16): the `ShaderModule` seam is retired. See [the render overview](../render/overview.md) for the reconstruction that drives this.

Task ledger: [TODO.md](TODO.md) (the working list, tick as items land) · design references: [sakura_reference.md](sakura_reference.md) (how Sakura Engine shapes its render backends — learn from, not copy) and [rhi_design_material.md](rhi_design_material.md) (RHI design material from a Zhihu thread on wrapping a modern-game-engine RHI layer — critical synthesis).
