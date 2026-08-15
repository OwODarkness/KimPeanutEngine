# Graphics (RHI) Work TODO

**Snapshot: 2026-08-15.** The working task list for the RHI leak-fixes and the render-module reconstruction. State-of-the-world lives in [status.md](../status.md) and [graphics_module.md](graphics_module.md); this page is the ledger that drives them — tick items here as they land, then fold the result into those two.

Items marked **← sakura** are ideas taken from [sakura_reference.md](sakura_reference.md) — *learned, not copied*.

## 1. Pipeline seams — shaders become `ShaderData`

- [x] **`PipelineDesc` shaders are `data::ShaderData*` directly** — no `graphics::Shader` wrapper. Landed 2026-08-15: `Shader`/`ResourceShader`/`ShaderLoader` retired (the single-impl seam's `api` dispatch was redundant — each backend reads the field its own API needs: Vulkan `byte_code`, OpenGL `source`).
- [ ] **Reconcile the `ShaderModule` seam or retire it** — `vulkan_shader_module`/`opengl_shader_module` read `shader->glsl`/`shader->spirv` (fields `ShaderData` doesn't have) and are **not in the build** ([dead_code.md](../dead_code.md)). Either make them consume `ShaderData::api` + `byte_code`, or delete them.
- [x] **`VulkanBackend::CreateGraphicsPipeline` stops building `PipelineDesc`** — landed 2026-08-15. It now **takes** the desc and bakes it (completing attachment formats from the swapchain format, an RHI-owned invariant); `OpenglBackend::CreatePipeline` likewise. Building the desc is render-module/example work.

## 2. Ownership — retire the path-keyed `ShaderManager`

- [x] **Remove the `ShaderManager` member from `RenderBackend`** — landed 2026-08-15, the whole manager is gone (the RHI owning a shader cache was the leak; caching belongs to the render module / resource pipeline).
- [x] **Retire `ShaderManager::CreateShader<API>(type, path)`**, which read the file itself — landed 2026-08-15. `shader_manager.*`, `shader_factory.h`, `vulkan_shader.*`, `opengl_shader.*` deleted; shaders arrive as `ShaderData` bytes.
- [ ] **Remove the build-time `glslc` → `.spv` step** in [Graphics/CMakeLists.txt](../../engine/runtime/graphics/CMakeLists.txt) — it feeds the prebuilt-`.spv` path and makes `glslc` a hard build requirement. Reconcile with the content-addressed `resource::ShaderCache`. *Deferred (2026-08-15): the `rhi_example` demo still reads `.spv`/`.vert` files as the caller; the RHI no longer reads shader files.*

## 3. `RenderBackend` facade cleanup

- [ ] **Drop the `GLFWwindow *window_` test seam** — flagged "should be removed later" in [render_backend.h](../../engine/runtime/graphics/backend/common/render_backend.h); the editor's Vulkan renderer already passes a null native handle.
- [ ] ← sakura **Split `RenderDevice` from frame lifecycle.** Our `RenderBackend` fuses device/resource-owner and `BeginFrame`/`EndFrame`/`Present`. Sakura separates a pure device (queues + resources, no per-frame state) from frame execution. Decide whether `RenderBackend` becomes the device seam or keeps the frame loop — the render module should talk to a stable device.

## 4. Pipeline cache — `PipelineDesc` as a *key* (← sakura)

- [ ] ← sakura **RHI exposes "bake this `PipelineDesc`", not "find me a cached shader"** — cache keyed by `(program AssetID, api)` on the render side, per the asset-module design note. Sakura's `pso_map`/`shader_map` are hash→resource tables; `PipelineDesc` is a *key* and the renderer dedupes by hashing it.

## 5. Wiring

- [ ] **`Render` links `Graphics`** — today `Render` never links `Graphics`; the legacy GL renderer and the RHI are two disconnected worlds ([status.md](../status.md)).
- [x] **Render-side queue drain + bootstrap preload → render cache** landed 2026-08-13 ([async_resource_queue.md](../async/async_resource_queue.md)) — this is the caller the reconstruction builds on.

## 6. Testing (headless, no GPU)

- [ ] Unit tests: `ShaderCache`, `GenerateShaderHash`, `HandleSystem`, asset manager — listed as next up in [status.md](../status.md).
