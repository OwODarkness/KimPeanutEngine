# Graphics (RHI) Work TODO

**Snapshot: 2026-08-16.** The working task list for the RHI leak-fixes and the render-module reconstruction. State-of-the-world lives in [status.md](../status.md) and [graphics_module.md](graphics_module.md); this page is the ledger that drives them — tick items here as they land, then fold the result into those two.

Items marked **← sakura** are ideas taken from [sakura_reference.md](sakura_reference.md) — *learned, not copied*.

## 1. Pipeline seams — shaders become `ShaderData`

- [x] **`PipelineDesc` shaders are `data::ShaderData*` directly** — no `graphics::Shader` wrapper. Landed 2026-08-15: `Shader`/`ResourceShader`/`ShaderLoader` retired (the single-impl seam's `api` dispatch was redundant — each backend reads the field its own API needs: Vulkan `byte_code`, OpenGL `source`).
- [x] **Retire the `ShaderModule` seam** — landed 2026-08-16. `common/shader_module.h` + `vulkan_shader_module.*`/`opengl_shader_module.*` deleted: the seam's two impls read `shader->glsl`/`shader->spirv` (fields `ShaderData` doesn't have), `GetHandle() → const void*` was a leaky abstraction (Vulkan's module and GL's compiled-shader object have different lifetimes), and the work already lives inline in the pipeline bakes — `VulkanPipelineManager::CreateShaderModule(device, bytes, size, &module)` for Vulkan, `glCreateShader/glCompileShader/glAttachShader/glLinkProgram` in `OpenglPipeline::Initialize` for OpenGL. Each backend wraps the raw-data → API-object step where the shader is needed.
- [x] **`VulkanBackend::CreateGraphicsPipeline` stops building `PipelineDesc`** — landed 2026-08-15. It now **takes** the desc and bakes it (completing attachment formats from the swapchain format, an RHI-owned invariant); `OpenglBackend::CreatePipeline` likewise. Building the desc is render-module/example work.

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

- [x] **`Render` links `Graphics`** — landed 2026-08-15 (Phase 4 of [vulkanbackend.md](vulkanbackend.md)): `Render` links `Graphics` PRIVATE. The demo scene (`render::RenderScene`) records through the RHI's frame API; `RenderSystem` itself stays API-agnostic. The legacy GL renderer and the RHI are no longer fully disconnected, but `RenderSystem` is still un-reconstructed.
- [x] **Render-side queue drain + bootstrap preload → render cache** landed 2026-08-13 ([async_resource_queue.md](../async/async_resource_queue.md)) — this is the caller the reconstruction builds on.

## 6. Testing (headless, no GPU)

- [ ] Unit tests: `ShaderCache`, `GenerateShaderHash`, `HandleSystem`, asset manager — listed as next up in [status.md](../status.md).
