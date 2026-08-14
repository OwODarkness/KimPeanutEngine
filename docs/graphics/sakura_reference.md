# Sakura Engine — Graphics Module Reference

**Study snapshot: 2026-08-14, `SakuraEngine/SakuraEngine` branch `engine` (tip, not a tagged release).** A design reference for the RHI/render reconstruction — *learn from, don't copy*. Sakura's license and design constraints stay in Sakura; we abstract the idea and map it back to our modules.

## The core idea in one paragraph

Sakura writes **no per-API backend code of its own**. It vendors a cross-API GPU library (CGPU, as `SkrGraphics`) whose opaque handles (`CGPUDeviceId`, `CGPUQueueId`, `CGPUBufferId`, …) are the only GPU types the engine ever sees — all Vulkan/D3D12/Metal backends live *inside* that library. On top sit three thin layers: `RenderDevice` (device + queues, the single backend-switch point), a frame graph (`render_graph`) that owns the transient resource pools, and an ECS renderer (`renderer`) that culls → DrawCallList → graph, plus hash-keyed PSO/shader caches. The engine is backend-agnostic by *construction*: nothing in it can reach a raw API.

## Module map

```
engine/modules/render/
├── render_graph/   frame graph; "backend" = CGPU-backed executor + resource pools
├── renderer/       ECS renderer (camera → cull → DrawCallList) + hash caches + RenderDevice
└── live2d/         (out of scope for us)
```

- **RHI = vendored CGPU** (`SkrGraphics`): C-style handles + an `ECGPUBackend` enum. `render_device.h` includes `SkrGraphics/api.h`.
- **`RenderDevice`** (`renderer/include/SkrRenderer/render_device.h`) — the *only* backend knob is `Builder { ECGPUBackend backend; bool enable_debug_layer; ... }`. The class is just accessors: queues (gfx/copy/compute/dstorage), a linear sampler, a root-signature pool. No frame loop, no camera, no presentation — a pure resource/queue owner.
- **`RenderGraphBackend : RenderGraph`** (`render_graph/include/SkrRenderGraph/backend/graph_backend.hpp`) — one CGPU-backed executor holding the device, the queues, and the **transient resource pools** (`buffer_pool`, `texture_pool`, `bind_table_pool`, …) that do aliasing/recycling. The `backend/` dir here = resource pools, **not** API backends.
- **`renderer`** (`renderer/README.md`, Chinese) — "the ECS renderer walks the scene's cameras, culls, produces a DrawCallList, and submits it to the render pipeline." Its `graphics/` subdir is caching only: `pso_map`/`pso_key`, `shader_map`/`shader_hash`, `gpu_table`, `tlas_manager`.

## What transfers to KimPeanut

1. **Device is the seam; per-frame work is separate.** Our `RenderBackend` ([render_backend.h](../../engine/runtime/graphics/backend/common/render_backend.h)) fuses resource-owner, frame lifecycle, and buffer creation, and leaks `GLFWwindow*`/`CameraData` public. Sakura's cleanest split: a `RenderDevice` owning GPU state/queues, with frame execution above it. Maps onto the reconstruction — the render module fills a `PipelineDesc` and asks the *device*.
2. **`PipelineDesc` is a key, not an allocation** ([pipeline_types.h](../../engine/runtime/graphics/backend/common/pipeline_types.h)). Sakura's `pso_map`/`shader_map` are hash→resource caches. The RHI should `CreatePipeline(PipelineDesc)` (bake + cache) and the renderer dedupes by hashing the desc — which kills two known leaks at once: the path-keyed `ShaderManager` and `VulkanBackend` building the desc internally.
3. **Shaders arrive as bytes, not paths.** Sakura/CGPU never sees shader files; the cache layers key on hashes. Our target shape (`ShaderData::byte_code` → `Shader` impl) is exactly that.
4. **Keep per-API code minimal.** Sakura's real lesson: backend-agnosticism is bought by *not writing backends*. If hand-writing Vulkan + GL stays the plan, every future API is ours. Our `GraphicsAPIType` + `CreateGraphicsBackEnd` factory fits either path — it selects a library or a class.

## Traps / cautions

- **Naming collision.** Sakura's `backend/` = transient resource pools; our `graphics/backend/{vulkan,opengl}` = API backends. Don't let the shared word blur the docs.
- **Study, not import.** This is a reference for shape, not source to copy — their license and constraints don't transfer.
- **Reading tip, not a release.** The pattern may differ from any release you know.

## Sources

- `engine/modules/render/renderer/include/SkrRenderer/render_device.h`
- `engine/modules/render/render_graph/include/SkrRenderGraph/backend/graph_backend.hpp`
- `engine/modules/render/renderer/README.md` (Chinese)
- `engine/modules/render/renderer/include/SkrRenderer/graphics/` — `pso_map.hpp`, `shader_map.hpp`, `gpu_table.hpp`
- Repository: `SakuraEngine/SakuraEngine`, branch `engine`
