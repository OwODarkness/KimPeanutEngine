# Graphics (RHI) Work TODO

**Snapshot: 2026-08-14.** The working task list for the RHI leak-fixes and the render-module reconstruction. State-of-the-world lives in [status.md](../status.md) and [graphics_module.md](graphics_module.md); this page is the ledger that drives them — tick items here as they land, then fold the result into those two.

Items marked **← sakura** are ideas taken from [sakura_reference.md](sakura_reference.md) — *learned, not copied*.

## 1. Pipeline seams — shaders become `ShaderData`

- [ ] **`ResourceShader : Shader` backed by `ShaderData`** — a thin impl whose `GetCode()` returns `shader_data->byte_code.data()` ([shader.h](../../engine/runtime/graphics/backend/common/shader.h)). Smallest change: `PipelineDesc` and `CreatePipelineResource` stay untouched. This is the "two shader seams, one target" note in graphics_module.md.
- [ ] **Reconcile the `ShaderModule` seam or retire it** — `vulkan_shader_module`/`opengl_shader_module` read `shader->glsl`/`shader->spirv` (fields `ShaderData` doesn't have) and are **not in the build** ([dead_code.md](../dead_code.md)). Either make them consume `ShaderData::api` + `byte_code`, or delete them and keep only `graphics::Shader`.
- [ ] **`VulkanBackend::CreateGraphicsPipeline` stops building `PipelineDesc`** ([vulkan_backend.cpp:712](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.cpp#L712)). It hardcodes the `simple_triangle` stage/layout/bindings; building a pipeline description is render-module work, the backend only *bakes* the desc it is handed.

## 2. Ownership — retire the path-keyed `ShaderManager`

- [ ] **Remove the `ShaderManager` member from `RenderBackend`** ([render_backend.h](../../engine/runtime/graphics/backend/common/render_backend.h)) — the RHI owning a shader cache is the leak. Caching belongs to the render module / resource pipeline.
- [ ] **Retire `ShaderManager::CreateShader<API>(type, path)`**, which reads the file itself ([shader_manager.cpp:42-66](../../engine/runtime/graphics/backend/common/shader_manager.cpp#L42)). Once shaders arrive as `ShaderData` bytes it has nothing left to do.
- [ ] **Remove the build-time `glslc` → `.spv` step** in [Graphics/CMakeLists.txt](../../engine/runtime/graphics/CMakeLists.txt) — it feeds the prebuilt-`.spv` path and makes `glslc` a hard build requirement. Reconcile with the content-addressed `resource::ShaderCache`.

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
