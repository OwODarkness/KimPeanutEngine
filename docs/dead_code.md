# Known Dead Code & Stale Paths

**Last audited: 2026-08-09.** One place listing code that is *not in the build*, *not wired*, or *slated for retirement*. Rules of thumb for agents working in this repo:

- **"Not in the build"** → don't fix it as if it were live. Either delete it or (if it's the seed of the intended design) resurrect it deliberately.
- **"Slated for retirement"** → don't extend it; replace it per the reconstruction docs.
- When you find new dead/stale code, add it here.

## Not in the build (stale)

| File | Problem |
|---|---|
| [`opengl_shader_module.h/.cpp`](../../engine/runtime/graphics/backend/opengl/opengl_shader_module.h) | Implements `ShaderModule`; references `shader->glsl`, which does **not** exist on `data::ShaderData`. Not in `Graphics/CMakeLists.txt`. |
| [`vulkan_shader_module.h/.cpp`](../../engine/runtime/graphics/backend/vulkan/vulkan_shader_module.h) | Same — references `shader->spirv` ([line 24](../../engine/runtime/graphics/backend/vulkan/vulkan_shader_module.cpp#L24)). Not in the build. |
| [`common/shader_factory.h`](../../engine/runtime/graphics/backend/common/shader_factory.h) | `ShaderTraits` template — no file includes it; the only `CreateShader` is commented out. Vestigial; safe to delete. |

The `ShaderModule` interface ([`common/shader_module.h`](../../engine/runtime/graphics/backend/common/shader_module.h)) itself has **no live consumers** — only the two unbuilt implementations include it. Its signature (`Initialize(context, shared_ptr<ShaderData>)`) is the *right* seam; if you resurrect it, fix the implementations to read `ShaderData::api` + `byte_code`.

## Commented-out cruft (live files)

- [`vulkan_pipeline_manager.cpp:31-45`](../../engine/runtime/graphics/backend/vulkan/vulkan_pipeline_manager.cpp#L31-L45) — the correct asset-loading path is sitting there as a comment. Do it for real; delete the comment.
- [`vulkan_shader.cpp:40-60`](../../engine/runtime/graphics/backend/vulkan/vulkan_shader.cpp#L40-L60) — commented-out legacy `CreateShaderModule`.
- [`vulkan_backend.cpp`](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.cpp) — large commented-out renderpass/framebuffer blocks (≈ lines 660–710, 754+).

## Live but slated for retirement (don't delete, don't extend)

| Code | Why it's going | Replaced by |
|---|---|---|
| [`common/shader_manager.h/.cpp`](../../engine/runtime/graphics/backend/common/shader_manager.h) + path-backed `OpenglShader`/`VulkanShader` | Path-keyed; the RHI reads shader files itself — reaches up into resource/asset. | A `ShaderData`-backed `graphics::Shader` (render module wraps `ShaderData::byte_code`). |
| [`common/shader_loader.h/.cpp`](../../engine/runtime/graphics/backend/common/shader_loader.h) (graphics) | `ReadTextFile`/`ReadBinaryFile` — only used by the path-backed shaders above. | Nothing — file reading is `resource/`'s job. |
| `glslc` build step in [`graphics/CMakeLists.txt`](../../engine/runtime/graphics/CMakeLists.txt) | Precompiles hardcoded `.vert/.frag` → `.spv` at build time, feeding the prebuilt-shader path. | `resource::ShaderCache` (content-addressed, per-API). |
| The whole legacy [`engine/runtime/render/`](../../engine/runtime/render/) tree (`ShaderPool`, `RenderShader`, `RenderMaterial`, `RenderScene`, raw `GLuint` render passes) | OpenGL-hardcoded, predates the RHI, doesn't link `Graphics`. | Reconstructed render module per [render_module.md](render/render_module.md). |
| `RenderBackend::window_` + `ShaderManager` member ([`render_backend.h`](../../engine/runtime/graphics/backend/common/render_backend.h)) | Marked in code: "for test, remove later". | Engine-owned backend; shader caching belongs to the render module. |
