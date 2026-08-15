# Vulkan Backend — Analysis & Decoupling Plan

**Snapshot: 2026-08-15.** A structural review of `VulkanBackend` — the original 2,149-line version is archived at [`backend/vulkan/deprecated/`](../../engine/runtime/graphics/backend/vulkan/deprecated/) — and a phased plan to peel the facade. Phases 0–3 are **landed** (see below); the current backend is the Phase-1/2/3 reconstruction ([vulkan_backend.h](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.h), [vulkan_backend.cpp](../../engine/runtime/graphics/backend/vulkan/vulkan_backend.cpp), [vulkan_device.h](../../engine/runtime/graphics/backend/vulkan/vulkan_device.h), [vulkan_device.cpp](../../engine/runtime/graphics/backend/vulkan/vulkan_device.cpp), [vulkan_swapchain.h](../../engine/runtime/graphics/backend/vulkan/vulkan_swapchain.h), [vulkan_swapchain.cpp](../../engine/runtime/graphics/backend/vulkan/vulkan_swapchain.cpp), [vulkan_frame_context.h](../../engine/runtime/graphics/backend/vulkan/vulkan_frame_context.h), [vulkan_frame_context.cpp](../../engine/runtime/graphics/backend/vulkan/vulkan_frame_context.cpp)). Companion to [graphics_module.md](graphics_module.md) (the RHI contract), [TODO.md](TODO.md) (the working ledger), and [sakura_reference.md](sakura_reference.md) / [rhi_design_material.md](rhi_design_material.md) (the design material). This page is the *how* for the Vulkan backend specifically; the ledger items stay authoritative for what.

## Verdict in one paragraph

`VulkanBackend` is a **correct resource layer under a god-object facade**. The resource ownership is genuinely split out — `VulkanBufferManager` (handle + allocator-strategy memory), `VulkanPipelineManager` (bakes a `PipelineDesc`), `VulkanImageMemoryManager`, and the cross-API `TextureManager`/`SamplerManager`/`MeshManager` — and that is the model the rest should follow. But *everything above resource creation* collapsed into one class: device lifecycle, swapchain, command pools, sync objects, image transitions, queue-ownership transfers, the per-frame UBO/descriptor wiring, **and the actual scene content** (one hardcoded mesh, one pipeline, one camera demo) all fused into ~2,100 lines (the original, archived at [`backend/vulkan/deprecated/vulkan_backend.cpp`](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp)) that read asset paths from disk. The fix is not a rewrite — it is peeling the facade, phase by phase, with the current demo as the regression test until the render module can take over. **Phases 0–3 (shaders → `ShaderData`; `VulkanDevice`, `VulkanSwapchain`, `VulkanFrameContext` extraction) are landed**; the line references below point at the archived original, where the numbers are still exact.

## What's already right — the decoupled foundation

These are the classes the plan builds on; their shape is correct and stays:

| Manager | Ownership | Pattern |
|---|---|---|
| [`VulkanBufferManager`](../../engine/runtime/graphics/backend/vulkan/vulkan_buffer_manager.h) | `VkBuffer` + memory, **strategy-pluggable** (`IVulkanMemoryAllocator` pool vs dedicated, per `VulkanMemoryUsageType`) | handle in, `VulkanBufferResource*` out |
| [`VulkanPipelineManager`](../../engine/runtime/graphics/backend/vulkan/vulkan_pipeline_manager.h) | `VkPipeline` + layout + descriptor layouts | `PipelineHandle` ↔ `PipelineDesc` |
| [`VulkanImageMemoryManager`](../../engine/runtime/graphics/backend/vulkan/vulkan_image_memory_manager.h) | image memory | handle-based |
| `TextureManager` / `SamplerManager` / `MeshManager` (common/) | cross-API GPU objects | slot + `HandleSystem`, create **from data structs**, never from paths |

Two details that make the decoupling cheap:

- **`VulkanPipelineManager::CreateShaderModule` already takes raw bytes** — `CreateShaderModule(VkDevice, const void *data, size_t, ...)` ([vulkan_pipeline_manager.h:29](../../engine/runtime/graphics/backend/vulkan/vulkan_pipeline_manager.h#L29)). The pipeline manager is already `ShaderData`-ready; the path-keyed `ShaderManager` that used to sit between them was **retired in Phase 0**.
- **Dynamic rendering** — the render-pass/framebuffer path is commented out in favor of `vkCmdBeginRendering`. No render pass object to keep alive; the swapchain is just images + views.

## What's wrong — the grouped logic

### 1. The backend IS the scene

The worst offender: [RecordCommandBuffer:1864](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L1864) binds *one specific pipeline*, *one specific mesh*, *one descriptor set*, issues *one* `vkCmdDrawIndexed`. A backend should expose "record this command list" and let a caller decide what to draw. Similarly [SetupResource:868](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L868) `LoadSync`s `wallpaper.jpg` and `sphere.obj` straight from the asset manager *inside the backend*, stages the texture, transitions it, and submits. The RHI knows asset paths today ([graphics_module.md](graphics_module.md) calls this the "responds, never initiates" violation).

### 2. `PipelineDesc` built internally

[CreateGraphicsPipeline:712](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L712) hardcodes the `simple_triangle.spv` file paths, the vertex layout, and the descriptor bindings, then calls `pipeline_manager_->CreatePipelineResource`. Building a pipeline description is render-module work; the backend should only bake the desc it is handed. This is the only place the "backend responds" rule is violated — every other resource path already takes a handle or a desc.

### 3. Device, swapchain, command, frame — all one class

One class owns instance, debug messenger, surface, physical/logical device, three queues, swapchain + its views, two command pools, five command buffers, semaphores/fences, two UBO sets, and the descriptor pool. [Initialize:177](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L177) runs eleven unrelated setup steps in sequence; [BeginFrame:199](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L199) fuses wait → acquire → reset fence → **update the demo camera UBO** → record → submit → present → recreate-on-resize. Recreating the swapchain drags in the whole object. There is no `VulkanDevice`, no `VulkanSwapchain`, no frame context — so nothing can be reused or tested independently, and the editor's Vulkan renderer (which passes a null native handle) has nowhere to hang a second context.

### 4. Two barrier implementations coexisting

[TransitionImageLayout2:1538](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L1538) (sync2) is used by `RecordCommandBuffer`; the legacy [TransitionImageLayout:1509](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L1509) is used by `SetupResource`. The device already enables `synchronization2` ([CreateLogicalDevice:508](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L508)). This is dead weight, not just duplication — sync2 is the one to keep.

### 5. Shared one-shot command buffers

`dst_command_buffer_`, `shader_command_buffer_`, `copy_command_buffer_` are single, reused fields ([vulkan_backend.h:175-180](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.h#L175)). `SetupResource` begins + submits `dst_command_buffer_` while `CopyBufferToImage` records into it from a helper — two callers stomp a pending submit. The correct primitive already exists: `BeginSingleTimeCommands`/`EndSingleTimeCommands` ([vulkan_backend.cpp:1376](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L1376)) allocate and free a fresh buffer per one-shot op.

### 6. Frame-bound demo data baked in

`per_pass_ubo_`/`per_object_ubo_`/`descriptor_sets_` are frame resources created once with a hardcoded layout ([CreateUniformBuffers:1184](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L1184)), and [UpdateUniformBuffer:1344](../../engine/runtime/graphics/backend/vulkan/deprecated/vulkan_backend.cpp#L1344) animates a hardcoded camera + spinning object every frame. That is scene content inside the frame loop — it is the render module's job, and it is what makes `BeginFrame` unfusable from the rest.

## The decoupling plan (ordered)

**Sequencing principle: mechanical extraction before behavioral change.** Every phase must compile and still render the current demo — the demo is the regression test while the structure peels. Behavior only changes in Phase 4, and only after the structure is stable. Phases 0–3 are pure moves (a new class takes the code, the backend delegates); Phase 4 is the one real rewrite, and it lands last on purpose.

### Phase 0 — Shaders become bytes ✅ landed 2026-08-15

Everything downstream is easier to verify once the backend stops *choosing* shaders.

- [x] `PipelineDesc` shaders are now `data::ShaderData*` directly ([TODO 1.1](TODO.md)) — no `graphics::Shader` wrapper. `Shader`, `ResourceShader`, and `ShaderLoader` were retired: the wrapper's `api`-based dispatch was redundant because each backend *is* its own API (Vulkan reads `byte_code`, OpenGL reads `source`).
- [x] `VulkanBackend::CreateGraphicsPipeline(PipelineDesc)` **takes** the desc instead of building it ([TODO 1.3](TODO.md)). The backend bakes only; a swapchain-bound desc gets its attachment formats completed from the swapchain format (an RHI-owned invariant). The caller (today: the `rhi_example`, later: the render module) owns stage/layout/bindings.
- [x] Retire the path-keyed `ShaderManager` ([TODO 2](TODO.md)) — `shader_manager.*`, `shader_factory.h`, `vulkan_shader.*`, `opengl_shader.*` deleted and dropped from the build.
- [ ] Build-time `glslc` → `.spv` step ([TODO 2.3](TODO.md)) — **deferred**: the `rhi_example` still reads prebuilt `.spv`/`.vert` files as the *caller* (the RHI no longer reads them). Remove once the render module owns shader sourcing.
- [ ] `ShaderModule` reconcile/retire ([TODO 1.2](TODO.md)) — the stale seam is untouched here.

**Landed:** `PipelineDesc` holds `data::ShaderData*` (`byte_code` for Vulkan, `source` for OpenGL — the resource pipeline's artifact *is* the RHI's input, no wrapper). Both backends read the field their own API needs and touch no shader files. `RenderBackend::Initialize(PipelineDesc)` is the RHI entry; the demo `rhi_example.cpp` builds the full desc with `data::ShaderData`s (reading `.spv`/`.vert` from disk as a stopgap until the render module owns sourcing) and hands it over.

### Phase 1 — Extract `VulkanDevice` ✅ landed 2026-08-15 (reconstruction, not a mechanical move)

Landed as a **reconstruction** rather than an in-place refactor: the fused ~2,100-line backend had no logic worth carefully preserving, so the original was archived whole to [`backend/vulkan/deprecated/`](../../engine/runtime/graphics/backend/vulkan/deprecated/) (git rename — history intact, source reference for the rewrite) and a fresh backend was written that reuses the well-patterned managers. New [`vulkan_device.h`](../../engine/runtime/graphics/backend/vulkan/vulkan_device.h)/[`.cpp`](../../engine/runtime/graphics/backend/vulkan/vulkan_device.cpp): `VulkanDevice` owns instance/debug-messenger/surface/physical/logical device + the three queues + the extension/layer/suitability queries + `RateDeviceSuitability`, `QueueFamilyIndices`, `SwapchainSupportDetail`.

The backend holds `std::unique_ptr<VulkanDevice>`, runs `device_->Initialize(window_)` in `Initialize` and `device_->Destroy()` in `Cleanup`, and reads every device handle back through accessors (`GetInstance`/`GetPhysicalDevice`/`GetLogicalDevice`/`GetSurface`/`GetGraphicsQueue`/`GetPresentQueue`/`GetTransferQueue`); `context_` fills from it (`InitVulkanContext`). `msaa_sampe_count_` is computed by the backend after device init (`GetMaxUsableSampleCount` on `device_->GetPhysicalDevice()`), staying out of the device for Phase 2. The rewrite also dropped dead weight the original carried: the unused `VK_CHECK` macro, the commented-out renderpass/framebuffer + multi-submit blocks, and the abandoned ownership-transfer stubs. Build green, 86/86 unit tests pass; swapchain/frame/scene code carried over verbatim (Phase 2–4 work). *Not re-run here: the demo window — verify visually that the triangle still draws.*

Why first: it is the largest, most mechanical, most testable move, and every later phase reads device state.

### Phase 2 — Extract `VulkanSwapchain` ✅ landed 2026-08-15 (lifecycle, zero behavior change)

New [`vulkan_swapchain.h`](../../engine/runtime/graphics/backend/vulkan/vulkan_swapchain.h)/[`.cpp`](../../engine/runtime/graphics/backend/vulkan/vulkan_swapchain.cpp): `VulkanSwapchain` owns the swapchain, its image views, the chosen extent/format and the resize flag, with `GetImage(i)`/`GetImageView(i)`/`GetImageCount()`/`GetExtent()`/`GetImageFormat()`/`GetSwapchain()` accessors. Moved in from the backend: `CreateSwapchain`/`CreateSwapchainImageViews` (now `Initialize(VulkanDevice*, GLFWwindow*)` + private helpers), the three `Choose*` helpers, `GetMaxUsableSampleCount`, and the resize flag (`MarkResized`/`ClearResized`/`HasResized`). Dynamic rendering means nothing else to keep — the render-pass/framebuffer blocks were already dropped in the Phase-1 reconstruction.

The backend holds `std::unique_ptr<VulkanSwapchain>` and delegates: `swapchain_->Initialize(device_.get(), window_)` runs right after device init (with `msaa_sampe_count_ = swapchain_->GetMaxUsableSampleCount()` following it); `BeginFrame`/`RecordCommandBuffer`/`CreateGraphicsPipeline`/`CreateDepthResource`/`CreateColorResource`/`CreateSyncObjects`/`UpdateUniformBuffer` read handles through the accessors. `RecreateSwapchain` thins to `DestroyAttachmentResources()` (the depth/color textures, which stay backend-owned) + `swapchain_->Recreate(width_, height_)` + recreate the attachments; `CleanupSwapchain` thins to the same texture destroy + `swapchain_->Cleanup()`; `FramebufferResizeCallback` delegates to `swapchain_->MarkResized()`. A direct extraction, not another archive-reconstruct — the Phase-1 backend was already clean. Build green, 86/86 tests. *Not re-run here: the demo window — verify visually that the triangle still draws and still resizes.*

### Phase 3 — Extract command + sync context ✅ landed 2026-08-15 (lifecycle, zero behavior change)

New [`vulkan_frame_context.h`](../../engine/runtime/graphics/backend/vulkan/vulkan_frame_context.h)/[`.cpp`](../../engine/runtime/graphics/backend/vulkan/vulkan_frame_context.cpp): `VulkanFrameContext` owns the command pools (graphics + transfer), the per-frame scene/UI command buffers, the semaphores/fences and the in-flight index, plus the `BeginSingleTimeCommands`/`EndSingleTimeCommands` one-shot primitives and the sync2 `TransitionImageLayout` (raw barrier + `TextureUsage` overloads) and `CopyBufferToImage`. Moved in from the backend: `CreateCommandPools`/`CreateCommandBuffers`/`CreateSyncObjects`, `GetCurrentUICommandBuffer`, and `BeginFrame`'s wait/acquire/reset/submit/present plumbing (`WaitForInFlightFence`, `AcquireNextImage`, `ResetInFlightFence`, `ResetCurrentSceneCommandBuffer`, `Submit`, `Present`, `AdvanceFrame`). `MAX_FRAMES_IN_FLIGHT` lives here as the single source of truth; the backend references it.

While moving the transition/copy helpers in, the plan's two cleanups landed: **standardized on the sync2 barrier** (the legacy `TransitionImageLayout` + `TransitionImageLayout2` pair collapsed to the one sync2 barrier) and **deleted the shared one-shot command buffers** (`dst_command_buffer_`/`shader_command_buffer_`/`copy_command_buffer_` and their `transition_dst_semaphore_`/`copy_semaphore_`) — each one-shot op now allocates its own buffer via the begin/end helper. Dead helpers with zero live callers were deleted too (`TransferBufferOwnership`, both `ReleaseImageOwnerShip`/`AcquireImageOwnerShip`, `GenerateMipmaps`); `CopyBuffer` was inlined into `CreateBuffer`'s stage→device copy. `RecreateSwapchain` notifies the frame context (`OnSwapchainRecreated`) so the `frame × image_count`-indexed render-finished semaphore vector is rebuilt when a resize changes the image count — fixing a latent out-of-bounds. The backend keeps thin resolvers: `CopyBufferToImage(cmd, BufferHandle, ...)` resolves the `VkBuffer` then delegates, `GetCurrentUICommandBuffer()` delegates. Build green, 86/86 tests. *Not re-run here: the demo window — verify visually that the triangle still draws and still resizes.*

### Phase 4 — Extract scene recording (the behavior change — last on purpose)

Replace `RecordCommandBuffer` with a public recording API: the backend exposes "here is the current frame's command buffer + attachments," and a caller (the render module) issues draws against it. Then delete the demo from the backend:

- `SetupResource` (`wallpaper.jpg` + `sphere.obj` load, staging, transitions), `CreateVertexBuffers`, `CreateUniformBuffers`/`CreateUniformBuffer`, `CreateDescriptorPool`/`CreateDescriptorSets`, `UpdateUniformBuffer`, `CopyBufferToImage` — all scene content moves out. `per_pass_ubo_`/`per_object_ubo_`/`descriptor_sets_` become the render module's per-frame resources.
- The demo reappears as the render module's first real scene. This is what makes `Render` ↔ `Graphics` wiring ([TODO 5.1](../../docs/graphics/TODO.md)) meaningful.

### Phase 5 — Facade cleanup

- Drop the `GLFWwindow *window_` test seam from `RenderBackend` ([TODO 3.1](TODO.md)).
- Decide the sakura split ([TODO 3.2](TODO.md)): `RenderBackend` becomes the *device seam* (stable, no per-frame state) and a frame executor sits above — which is what Phases 1–4 have been carving out.

## Target shape

```
VulkanBackend (facade: BeginFrame/Present, delegates, takes PipelineDesc)
 ├── VulkanDevice         instance / physical / logical / queues / surface
 ├── VulkanSwapchain      swapchain + image views + extent + resize
 ├── VulkanFrameContext   command pools + buffers + sync + in-flight index
 ├── VulkanBufferManager      (exists)
 ├── VulkanPipelineManager    (exists, bakes PipelineDesc → VkPipeline)
 ├── VulkanImageMemoryManager (exists)
 └── common Texture / Sampler / Mesh managers  (exist)
```

The backend keeps owning GPU state (the managers) and the frame submission skeleton. It stops owning: shader *choice*, pipeline desc, swapchain/device lifecycle internals, and — most importantly — **what the scene draws**.

## What stays out of scope here

- The OpenGL backend is untouched by this plan; it is the port, Vulkan is the reference ([rhi_design_material.md](rhi_design_material.md) §6.5).
- Descriptor bindless, compute/transfer queues as separate paths, render-graph reordering ([rhi_design_material.md](rhi_design_material.md) §2, §6.6) are future work, not this decoupling.
- The `ShaderModule` seam is handled by [TODO 1.2](TODO.md), not by this plan.
