# Known Dead Code & Stale Paths

**Last audited: 2026-08-15.** One place listing code that is *not in the build*, *not wired*, or *slated for retirement*. Rules of thumb for agents working in this repo:

- **"Not in the build"** → don't fix it as if it were live. Either delete it or (if it's the seed of the intended design) resurrect it deliberately.
- **"Slated for retirement"** → don't extend it; replace it per the reconstruction docs.
- When you find new dead/stale code, add it here.

**Removed 2026-08-15 (Phase 0 of the [Vulkan decoupling](graphics/vulkanbackend.md)):** `shader_manager.h/.cpp`, `shader_factory.h`, `vulkan_shader.h/.cpp`, `opengl_shader.h/.cpp`, the `graphics::Shader` base (`common/shader.h`), `ResourceShader`, `ShaderLoader` (`common/shader_loader.h/.cpp`), and the orphaned `backend/vulkan/CMakeLists.txt` / `backend/opengl/CMakeLists.txt` (never `add_subdirectory`'d; listed deleted sources) are gone from the tree. Shaders now arrive as `data::ShaderData*` in `PipelineDesc`.

**Archived 2026-08-15 (Phase 1 of the [Vulkan decoupling](graphics/vulkanbackend.md)):** the original fused `vulkan_backend.h/.cpp` (~2,100 lines) was git-renamed into `backend/vulkan/deprecated/` (below) and the backend was reconstructed around `VulkanDevice`.

**Deleted 2026-08-15 (Phase 3 of the [Vulkan decoupling](graphics/vulkanbackend.md)):** the backend's shared one-shot command buffers (`dst_command_buffer_`/`shader_command_buffer_`/`copy_command_buffer_`) and their `transition_dst_semaphore_`/`copy_semaphore_`, plus the dead transfer/mipmap helpers (`TransferBufferOwnership`, `CopyBuffer` — inlined into `CreateBuffer`, both `ReleaseImageOwnerShip`/`AcquireImageOwnerShip` overloads, `GenerateMipmaps`) were deleted. The one-shot primitives + the sync2 barrier now live on `VulkanFrameContext`, which allocates a fresh command buffer per one-shot op instead of stomping a shared field.

**Moved out 2026-08-15 (Phase 4 of the [Vulkan decoupling](graphics/vulkanbackend.md)):** all scene content was deleted from the live `VulkanBackend` — `SetupResource`, `CreateVertexBuffers`, `CreateUniformBuffers`/`CreateUniformBuffer`, `CreateDescriptorPool`/`CreateDescriptorSets`, `UpdateUniformBuffer`, `CopyBufferToImage`, `RecordCommandBuffer`, and the `per_pass_ubo_`/`per_object_ubo_`/`descriptor_sets_` members. The demo lives on as **`render::RenderScene`** ([render_scene.cpp](../../engine/runtime/render/render_scene.cpp)), the render module's first real scene, recording through the backend's frame API. Don't reintroduce scene content into the backend — the scene belongs above the RHI.

## Not in the build (stale)

| File | Problem |
|---|---|
| [`deprecated/vulkan_backend.h/.cpp`](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.h) | The pre-Phase-1 fused backend, archived 2026-08-15. **Not in the build** — `Graphics/CMakeLists.txt` compiles the `VulkanDevice`-based reconstruction. Kept as the source reference for the rewrite; the line numbers in [vulkanbackend.md](graphics/vulkanbackend.md) point here. |
| [`opengl_shader_module.h/.cpp`](../../engine/runtime/graphics/backend/opengl/opengl_shader_module.h) | Implements `ShaderModule`; references `shader->glsl`, which does **not** exist on `data::ShaderData`. Not in `Graphics/CMakeLists.txt`. |
| [`vulkan_shader_module.h/.cpp`](../../engine/runtime/graphics/backend/vulkan/vulkan_shader_module.h) | Same — references `shader->spirv` ([line 24](../../engine/runtime/graphics/backend/vulkan/vulkan_shader_module.cpp#L24)). Not in the build. |

The `ShaderModule` interface ([`common/shader_module.h`](../../engine/runtime/graphics/backend/common/shader_module.h)) itself has **no live consumers** — only the two unbuilt implementations include it. Its signature (`Initialize(context, shared_ptr<ShaderData>)`) is the *right* seam; if you resurrect it, fix the implementations to read `ShaderData::api` + `byte_code`.

## Commented-out cruft (live files)

- [`vulkan_pipeline_manager.cpp:31-45`](../../engine/runtime/graphics/backend/vulkan/vulkan_pipeline_manager.cpp#L31-L45) — the correct asset-loading path is sitting there as a comment. Do it for real; delete the comment.

## Live but slated for retirement (don't delete, don't extend)

| Code | Why it's going | Replaced by |
|---|---|---|
| `glslc` build step in [`graphics/CMakeLists.txt`](../../engine/runtime/graphics/CMakeLists.txt) | Precompiles hardcoded `.vert/.frag` → `.spv` at build time, feeding the prebuilt-shader path (which only the `rhi_example` reads). | `resource::ShaderCache` (content-addressed, per-API). |
| The legacy [`engine/runtime/render/`](../../engine/runtime/render/) tree (`ShaderPool`, `RenderShader`, `RenderMaterial`, raw `GLuint` render passes; `RenderScene` is taken by the new [`render_scene.cpp`](../../engine/runtime/render/render_scene.cpp)) | OpenGL-hardcoded, predates the RHI. `Render` links `Graphics` PRIVATE now (2026-08-15), but `RenderSystem` itself is still un-reconstructed. | Reconstructed render module per [render_module.md](render/render_module.md). |
| `RenderBackend::window_` member ([`render_backend.h`](../../engine/runtime/graphics/backend/common/render_backend.h)) | Marked in code: "for test, remove later". | Engine-owned backend. (`ShaderManager` member already deleted 2026-08-15.) |
