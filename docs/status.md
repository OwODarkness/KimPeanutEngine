# Project Status

**Snapshot: 2026-08-26.** This is the agent's source of truth for *what state the world is in* — update it as work lands so a future session doesn't re-derive it. Per-module detail lives in the module docs ([asset](asset/asset_module.md), [graphics](graphics/graphics_module.md), [render](render/render_module.md), [resource](resource/resource_module.md)); this page is the one-line-per-item index.

## Done

- **Asset module** — two-tier ownership (unique_ptr wrappers, ref-counted payloads), thread-safe (load → state mutex order), content-addressed `path_index`. Refactor complete. → [asset_module.md](asset/asset_module.md)
- **Shader identity + artifact pipeline** — `ShaderProgramLoader` (`.shader` meta → per-stage `ShaderResource`), `ShaderProcessor` + `SPIRVCompiler` (GLSL → SPIR-V, content-addressed cache), `PreprocessOperation` (GLSL → preprocessed source, no cache). Per-API artifact via `ShaderProcessor::keep_source_` → `ShaderData` `byte_code` (Vulkan) or `source` (OpenGL). Wired end-to-end by the asset example; the render module is not — see below.
- **RHI** — Vulkan + OpenGL backends behind `RenderBackend::CreateGraphicsBackEnd`; cross-API handles, `PipelineDesc`, `TextureManager`/`MeshManager`/`SamplerManager`. → [graphics_module.md](graphics/graphics_module.md)
- **Editor UI build boundary (2026-08-26)** — `EditorLib` is now the backend-agnostic editor-tool layer, while `EditorUILib` owns ImGui, GLFW WSI, and the OpenGL/Vulkan ImGui renderers. VulkanSDK, glad, and ImGui are private `EditorUILib` dependencies; the Vulkan bridge remains confined to that module. → [editor module](editor/editor_module.md)
- **Graphics build encapsulation (2026-08-26)** — `Graphics` now keeps its
  backend include root plus `glad`, GLFW, and `VulkanSDK` private; consumers no
  longer inherit native SDK include/link requirements. `USE_OPENGL` and
  `USE_VULKAN` select source lists and factory availability; an OpenGL-only
  `Graphics` build succeeds. `EditorUILib`'s deliberate Vulkan ImGui bridge
  links Vulkan directly. → [graphics module](graphics/graphics_module.md)
- **Common graphics capabilities (2026-08-26)** — `RenderBackend` publishes
  immutable, common-RHI `GraphicsCapabilities` after initialization. Vulkan
  and OpenGL expose their sampled-texture-stage limit without leaking native
  properties; bindless textures deliberately report unavailable until one
  common resource-table contract enables them. → [graphics module](graphics/graphics_module.md)
- **Vulkan bindless sampled-texture table (B2, 2026-08-27)** — Vulkan now
  enables only the descriptor-indexing subset required by the common V1 table,
  when available. Graphics privately owns per-frame descriptor sets, deferred
  slot/resource retirement by completed submission serial, and global set-1
  binding; unsupported devices retain the ordinary bound-resource path. The
  smoke test exercised allocation, frame-boundary update, binding, retirement,
  and Vulkan/OpenGL fallback. → [graphics module](graphics/graphics_module.md)
- **OpenGL bindless sampled-texture table (B3, 2026-08-27)** — OpenGL now
  enables the V1 table only with `GL_ARB_bindless_texture` plus GPU 64-bit
  shader support. The backend owns resident handles in a private SSBO and uses
  a frame fence for deferred non-residency/reuse; unsupported drivers retain
  ordinary texture-unit bindings. → [graphics module](graphics/graphics_module.md)
- **Bindless material adoption (B4, 2026-08-27)** — Material templates can
  opt a compatible shader into the V1 texture-table convention. The resolver
  owns per-instance common slots and falls back atomically to ordinary bindings
  on unavailable capability or allocation failure; `FrameContext` supplies
  the table indices in its material UBO prefix. → [graphics module](graphics/graphics_module.md)
- **Bindless validation and rollout evidence (B5, 2026-08-27)** — The shader
  processor selects target-specific bindless declarations without exposing
  native APIs above Graphics; templates can select a bindless program while
  retaining their ordinary fallback program. `GraphicsSmoke` renders two
  textures through both bindless and bound material variants on Vulkan and
  OpenGL, asserting mode selection and slot coverage. → [graphics module](graphics/graphics_module.md)
- **Runtime bindless selection (2026-08-27)** — The bootstrap scene uses one
  shader-program asset containing ordinary and bindless compile variants.
  Render selects the bindless material path strictly from effective backend
  capability, retaining bound materials as the fallback. → [graphics module](graphics/graphics_module.md)
- **RHI shader/pipeline seam (2026-08-15–20)** — shaders reach the backends as `data::ShaderData`: `PipelineDesc` holds `data::ShaderData*` directly (no `graphics::Shader` wrapper — `Shader`/`ResourceShader`/`ShaderLoader` retired); `CreatePipelineResource(PipelineDesc)` bakes caller-built descriptions into independently destroyable `PipelineHandle`s, while `RenderBackend::Initialize(WindowHandle)` only creates backend/frame state. The path-keyed `ShaderManager` (+ `shader_factory`, `vulkan_shader`, `opengl_shader`) is **retired and deleted**. The `rhi_example` creates two pipeline handles after runtime shader baking. The build-time `glslc` step and the `ShaderModule` seam landed retired 2026-08-16 ([TODO](graphics/TODO.md)). → [vulkanbackend.md](graphics/vulkanbackend.md)
- **Render-owned pipeline warmup (Phase 2 slice, 2026-08-20)** — `RenderSystem` now owns `RenderBackend`, initializes it with the runtime window/resize dispatcher, drives `BeginFrame`/`EndFrame`, and owns a fixed-default pipeline builder/cache. Bootstrap shader programs flow `LoadSync` → `ProcessShader` → `BuildDefaultPipelineDesc` → `CreatePipelineResource`; the cache keys packed program `AssetID`s and retains `PipelineHandle`s until render shutdown. Material-driven states and API-neutral recording remain next. → [render_module.md](render/render_module.md)
- **Static GPU-resource ownership (Phase 3.1, 2026-08-20)** — `RenderBackend` now exposes common mesh/texture/sampler create/destroy APIs; its private managers and Vulkan/OpenGL upload details remain hidden. `RenderSystem` caches handles for queued mesh, model, and texture assets; each `RenderCacheEntry` exposes one type-safe result variant rather than a loose collection of optional handles. `RenderScene` accepts borrowed static resource handles and no longer loads assets, uploads GPU data, or destroys those resources. Vulkan descriptor/command code remains in the scene until Phases 3.2–3.3. → [TODO.md](graphics/TODO.md)
- **Vulkan upload service extraction (Phase 6.3, 2026-08-24)** — `VulkanUploadContext` now owns synchronous staging-buffer creation, one-shot command recording, queue submission/wait, and staging release for buffer and texture uploads. `VulkanBufferManager`/`VulkanMemoryManager` remain the sole allocation owners; the common RHI surface is unchanged. → [TODO.md](graphics/TODO.md)
- **Vulkan editor presentation bridge (Phase 6.4, 2026-08-24)** — `VulkanEditorBridge` now owns only the frame-scoped external ImGui pass: swapchain UI transitions, dynamic-rendering begin/end, and present-layout fallback. `EditorImguiVulkanRenderer` receives initialization data and records via that bridge without calling `VulkanBackend` or retrieving general Vulkan resources; backend ownership of device/swapchain/frame submission is unchanged. → [TODO.md](graphics/TODO.md)
- **Vulkan native escape hatches closed (Phase 6.5, 2026-08-24)** — `VulkanBackend` no longer publishes contexts, queues, pipeline resources, managers, or native command buffers. Vulkan mesh/texture adapters receive direct private buffer-upload/image-memory services through `VulkanContext`; render code remains Vulkan-free and editor Vulkan is confined to the approved ImGui bridge. `GraphicsSmoke` passes Vulkan/OpenGL rendering, resize, and shutdown. → [TODO.md](graphics/TODO.md)
- **Render explicit pass scheduling (Render Phase 1, 2026-08-24)** — `RenderSystem` now owns a validated `ScenePass` → terminal `EditorCompositePass` schedule. The logical `SceneColor` write/read dependency is render-owned and RHI-free; the engine supplies the immediate ImGui callback after input polling, preserving the Render → Graphics dependency direction. `RenderPassScheduleTest` covers accepted ordering and invalid dependency/terminal cases; `GraphicsSmoke` remains green on Vulkan and OpenGL. → [render_module.md](render/render_module.md)
- **Common resource bindings (Phase 3.2, 2026-08-20)** — `RenderScene` now describes its two uniform buffers and sampled texture through handle-only `ResourceBindingSetDesc`s. `DescriptorSetHandle` hides the native implementation: Vulkan privately allocates/updates/binds descriptor pools/sets through `VulkanDescriptorSetManager`; OpenGL stores equivalent binding state. Raw pipeline/mesh draw commands remain Phase 3.3. → [TODO.md](graphics/TODO.md)
- **Minimal cross-API command recorder (Phase 3.3, 2026-08-20)** — `CommandRecorder` exposes pipeline/mesh/resource binding, viewport/scissor, and indexed draw intents during an active frame. Vulkan emits native `vkCmd*`; OpenGL emits equivalent state/draw calls. `RenderScene::Record` now depends only on common handles and the recorder; no Vulkan types or native commands remain in the scene. Phase 3.4 next introduces `FrameContext` for per-frame transient UBO and binding-set lifetime. → [TODO.md](graphics/TODO.md)
- **Frame contexts for transient render data (Phase 3.4, 2026-08-20)** — `RenderSystem` owns one `FrameContext` per backend frame slot and schedules registered scenes inside `BeginFrame`/`EndFrame`. A context owns a 64 KiB aligned uniform arena plus frame-local descriptor sets, recycling both only after the backend waits for that slot’s prior GPU work. `RenderScene` now owns only logical camera/renderable/material state and static RHI handles; UBO ranges and binding sets exist only during its supplied frame context. → [TODO.md](graphics/TODO.md)
- **OpenGL legacy demo ownership removed (Phase 4.1, 2026-08-20)** — `OpenglBackend` no longer loads assets, uses hard-coded demo paths, creates demo UBOs/descriptors, or animates camera/object state. It retains only RHI resource management and `CommandRecorder` translation. → [TODO.md](graphics/TODO.md)
- **OpenGL common execution path (Phase 4.2, 2026-08-20)** — the standalone graphics example creates the same caller-owned `RenderScene`, static resources, and `FrameContext` workflow for Vulkan and OpenGL. `OpenglBackend` validates caller-provided pipeline/resource bindings and translates them through its common command recorder path; API-only differences remain internal. → [TODO.md](graphics/TODO.md)
- **Vulkan/OpenGL parity smoke (Phase 4.3, 2026-08-20)** — the `GraphicsSmoke` executable runs the shared one-object scene for three frames on each API, dispatches a resize event on frame two, exercises Vulkan frame-slot reuse, and completes frame/static/backend/window shutdown with exit code zero. It verifies the no-crash contract, not pixel output. → [TODO.md](graphics/TODO.md)
- **Scene render target + editor viewport (2026-08-21)** — `RenderSystem` owns a render-level `RenderTarget`, backed by API-private offscreen color/depth textures through `RenderBackend`, and brackets every registered scene with `BeginRenderTarget`/`EndRenderTarget`. `RenderTargetView` provides a borrowed, non-owning presentation token for the color attachment; `EditorViewportComponent` consumes it through the `IEditorImguiRenderer` seam. OpenGL displays the texture through `ImGui::Image`; Vulkan presentation still needs its ImGui descriptor bridge. The final swapchain composite remains next. `GraphicsSmoke` exercises target recording on both APIs. → [render_module.md](render/render_module.md)
- **Bootstrap sphere scene (2026-08-21)** — `config/bootstrap.json` explicitly names `simple_triangle.shader`, `model/sphere/sphere.obj`, and `texture/wallpaper.jpg` as its startup scene. The engine passes this policy to `RenderSystem`, which owns and registers a `RenderScene` after those cached pipeline/mesh/material resources are ready. The editor viewport now has a real OpenGL scene to present. → [render_module.md](render/render_module.md)
- **Extensible render initialization (2026-08-20)** — `RenderSystemInitInfo` and `RenderSceneInitInfo` replace positional initialization arguments. Scene static resources are grouped as `RenderSceneResources`; the scene initialization path is API-neutral. → [render_module.md](render/render_module.md)
- **`VulkanDevice` extracted (Phase 1 of the Vulkan decoupling, 2026-08-15)** — landed as a **reconstruction**, not a move: the fused ~2,100-line backend was archived whole to [`backend/vulkan/deprecated/`](graphics/vulkanbackend.md) (git rename, history preserved) and a fresh backend was written that reuses the managers. New `vulkan_device.h/.cpp`: `VulkanDevice` owns instance/debug-messenger/surface/physical/logical device + the three queues + the extension/layer/suitability queries + `QueueFamilyIndices`/`SwapchainSupportDetail`/`RateDeviceSuitability`. The backend holds `std::unique_ptr<VulkanDevice>`, delegates `Initialize(window_)`/`Destroy()`, reads handles via accessors, and fills `context_` from it; `msaa_sampe_count_` is computed in the backend after device init. Dead weight dropped in the rewrite: `VK_CHECK`, the commented-out renderpass/framebuffer + multi-submit blocks. Build green, 86/86 tests. Demo window not re-run (headless) — verify the triangle still draws. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **`VulkanSwapchain` extracted (Phase 2 of the Vulkan decoupling, 2026-08-15)** — a direct move, not another reconstruction (the Phase-1 backend was already clean). New `vulkan_swapchain.h/.cpp`: `VulkanSwapchain` owns the swapchain, its image views, the chosen extent/format and the resize flag (`MarkResized`/`ClearResized`/`HasResized`), with `GetImage(i)`/`GetImageView(i)`/`GetImageCount()`/`GetExtent()`/`GetImageFormat()` accessors. Moved in: `CreateSwapchain`/`CreateSwapchainImageViews` (now `Initialize` + private helpers), the three `Choose*` helpers, and `GetMaxUsableSampleCount`. The backend holds `std::unique_ptr<VulkanSwapchain>` and delegates; `RecreateSwapchain` thins to `DestroyAttachmentResources()` (depth/color textures stay backend-owned) + `swapchain_->Recreate(width_, height_)` + recreate attachments; `CleanupSwapchain` thins to the same texture destroy + `swapchain_->Cleanup()`; `FramebufferResizeCallback` delegates to `swapchain_->MarkResized()`. Build green, 86/86 tests. Demo window not re-run (headless) — verify the triangle still draws and still resizes. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **`VulkanFrameContext` extracted (Phase 3; current shape 2026-08-24)** — `VulkanFrameContext` owns graphics/transfer command pools, per-frame scene command buffers, semaphores/fences, in-flight indexing, sync2 image transitions, and wait/acquire/reset/submit/present plumbing. Phase 6.3 moved one-shot allocation/submit/wait into `VulkanUploadContext`; Phase 6.4 deleted unused UI command buffers and delegates the external ImGui pass to `VulkanEditorBridge`. It still rebuilds frame × image-count render-finished semaphores after swapchain recreation. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **Scene recording extracted (Phase 4; current shape 2026-08-24)** — `RenderScene` records API-neutral intent through `CommandRecorder`; Vulkan-native command encoding is private to `VulkanCommandRecorder`. `VulkanBackend` no longer publishes a scene command buffer: it coordinates the frame, while the editor uses the constrained `VulkanEditorBridge` external-pass callback. Static resources remain render-owned and frame data remains in `FrameContext`. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **`RenderBackend` facade cleanup (Phase 5 of the Vulkan decoupling, 2026-08-16)** — the last documented phase. The `GLFWwindow *window_` test seam and the dead public `CameraData camera_data` member are gone from the cross-API facade; `RenderBackend::Initialize` now takes the native window handle (`WindowHandle` = `void*`) as an explicit parameter, and each backend casts it back to `GLFWwindow*` internally (the editor's `EditorImguiGLFWWSI` cast pattern). **Sakura split decided (TODO 3.2): keep the frame loop** — the device/frame separation already lives inside the backend (`VulkanDevice` = pure device + queues; `VulkanSwapchain`/`VulkanFrameContext` = frame lifecycle), so a frame executor above the facade would re-fuse what Phases 1–3 separated with no consumer that needs it. `Graphics`/`Render`/`RuntimeLib` build clean. *Full build + tests not re-run here: the working tree is missing the prebuilt `third_party/googletest` libs (all test targets fail on `gtest/gtest.h`), the vendored imgui backends are newer than the in-tree core (`ImTextureData` undeclared, `EditorLib` fails), and `tts_example.cpp` hits a codepage error — all pre-existing, unrelated to this change.* → [vulkanbackend.md](graphics/vulkanbackend.md)
- **Build-time `glslc` step removed (TODO 2.3 of the decoupling, 2026-08-16)** — `Graphics/CMakeLists.txt` no longer finds `glslc` or precompiles `.vert/.frag` → `.spv`; the `Shaders` custom target is gone, so `glslc` is no longer a configure-time build requirement. The `rhi_example` demo now bakes its shaders at runtime through the resource pipeline (`asset.LoadSync(simple_triangle.shader)` → `ProcessShader` → `ShaderData`), the flow the asset example proves — giving `ResourcePipeline::ProcessShader` its first **graphics-end** caller and unifying the demo across Vulkan + OpenGL (the pipeline fills `byte_code` or `source` per API). The dead `GetSPVShaderDirectory()`/`binary_root`/`PROJECT_BINARY_DIR` path helpers went with it. Build green, 37/37 tests. Demo window not re-run (headless) — verify the triangle still draws. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **Audio + TTS modules** — miniaudio system, buffer player, GPT-SoVITS client. Streaming playback (`StreamAudioPlayer`) fixed for startup stutter: first chunk decodes immediately, ring buffer refills via a low-water `FillBuffer`/`Refill`, temporary underruns output silence instead of stopping, and the FIFO dry-read no longer tears the player down. → [audio_module.md](audio/audio_module.md) (includes the stutter bug history), [tts_module.md](tts/tts_module.md)
- **Window icon** — `GLFW_WindowSystem::Initialize` sets the taskbar/window icon from `config/icon.png` (via `GetIconPath()` + `glfwSetWindowIcon`, decoded with stb_image). Non-fatal if the file is missing. `stb_image` became a compiled static lib (was header-only INTERFACE with `STB_IMAGE_IMPLEMENTATION` in a shared header — that caused duplicate-symbol link errors once a second TU included it); the implementation now lives once in `third_party/stb_image/stb_image_impl.cpp`.
- **Unit tests** — audio decode + bootstrap parser/request builder + Lua VM + profile.
- **Module design docs** — asset, graphics, render, resource, audio, tts, script, editor written. → [resource_module.md](resource/resource_module.md) (CPU-side processing layer: compiles/bakes, does not load or touch GPU), [editor_module.md](editor/editor_module.md) (editor as core center; WSI/renderer seams keep ImGui decoupled from GLFW/graphics API; composable `EditorUIComponent` tree).
- **Script module — Lua VM hosting layer** — relocated `engine/script/` → `engine/runtime/script/` and wired into `RuntimeLib` (was an orphaned top-level module). `LuaVM` (lib `ScriptLua`) is now an engine-agnostic sol2 wrapper: non-throwing API (bool + `LastError`, `std::optional` lookups), real `Initialize`/`Shutdown` state lifecycle, sandboxed library set (`os`/`io`/`debug`/`coroutine` unopened, `package.loadlib`/`cpath` stripped), per-execution instruction budget (runaway scripts abort instead of hanging the game thread), `SOL_ALL_SAFETIES_ON`. Unit-tested headless under `ScriptUnitTest`. **The engine owns a live instance:** `RuntimeContext::lua_vm_` is created + `Initialize`d at boot (`RuntimeContext::Initialize`) and released in `Clear()`. The `Script` lib is the seam for the future binding layer. → [script_module.md](script/script_module.md)
- **Editor restored (minimal)** — the engine owns the editor again (`Engine::editor_`, created in the ctor, ticked/cleared by the engine loop). `Editor` → `EditorUI` renders **two ImGui windows** — a "window" + "hello imgui" label, plus the **OutputLog** log window (wired 2026-08-13) — no scene manager / actor panel yet. Decoupling preserved: `IEditorImguiWSI` (GLFW) + `IEditorImguiRenderer` (GL/Vulkan, chosen by `GraphicsAPIType`) — ImGui never binds to a specific API. ImGui's context, WSI + renderer init and shutdown live on the **render thread** (`InitEditorUI`/`CloseUI`), where the GL/Vulkan context exists; the render tick is now input-poll → editor tick → swap so the UI renders the same frame. `EditorContext` (`global_editor_context`) was fixed up: `render::RenderSystem` type corrected, deleted `WorldSystem` dropped. The GL editor renderer loads `glad` itself (the legacy GL backend that used to own it isn't in the build) and owns the per-frame clear (`0.1` gray) as a stopgap until the reconstructed scene renderer returns. Rendering verified: the presented frame (`GL_FRONT`) matches the ImGui-rendered back buffer after `SwapBuffers`. → [editor_module.md](editor/editor_module.md)
- **Editor directory and target split (2026-08-13, updated 2026-08-26)** — headers sit beside their sources under `engine/editor/`, grouped by concern: `context/` (hub), `ui/` + `ui/component/` (UI manager + widget tree), `platform/` (WSI/renderer backends), `log/`, `settings/`. All editor includes use the engine root (`"editor/..."`), matching the runtime convention. `EditorLib` now contains only editor-tool orchestration and `EditorContext`; `EditorUILib` owns UI/platform sources and its private ImGui, glad, and VulkanSDK dependencies. Six all-comment dead files were deleted (`EditorSceneManager`, `EditorActorControlPanel`, scene/camera component impls), and the redundant `EditorLogManager` was removed when the log window joined the UI tree. Every subdirectory lists sources explicitly through `target_sources` (no `file(GLOB)`); `main.cpp` is compiled by the root exe target only. Builds clean.
- **Configurable editor settings (2026-08-13)** — `config/settings.json` (beside `bootstrap.json`) now drives the log window's per-`LogLevel` entry colors instead of the hard-coded `ExtractTipColorFromLogLevel` switch. `GetSettingsPath()` in `config/path.h`; `EditorSettings`/`ReadEditorSettings`/`DefaultLogColors` are in the `EditorUILib`-owned `engine/editor/settings/` module (the parser mirrors `ReadBootstrap`'s tolerance: missing file throws, malformed/missing entries warn + fall back to defaults). `EditorUI::Initialize` loads the colors with a try/catch fallback, and `EditorLogComponent` takes a `LogLevelColorTable` (indexed by `program::LogLevel`) instead of switching on level. Also fixed three stale `BootstrapTest` path assertions that expected relative paths from `BuildLoadRequests` (it returns absolute `GetAssetDirectory() + path`). → [editor_module.md](editor/editor_module.md)
- **Editor profile bar (2026-08-13)** — a bottom status bar showing FPS, frame ms, and memory. Two decoupling seams: `EditorMetric` (the extension point — implement `Name()`/`Sample()`, or wrap sampler lambdas in `EditorFuncMetric`) and `EditorProfileBarComponent` (samples injected metrics and draws them in one row; it only ever talks to `EditorMetric`). Built-ins: FPS (engine via an injected sampler), frame time (derived from fps, not a self-measured clock — that would see the render loop's pacing sleep), memory (process + system free via an injected stats sampler). **Measurement lives in the platform layer, not the editor and not the engine** — FPS stays in the engine (`Engine::GetFPS`, it's game-loop timing), but memory is an OS query and lives behind a platform seam: `MemoryStatsSampler` interface + `WindowsMemoryStatsSampler` under `runtime/platform/win/` (PSAPI/GlobalMemory, `psapi` linked into the `Platform` lib), owned by `RuntimeContext`, reached by the editor through `EditorContext`. The engine is platform-agnostic again. Plot-capable metrics draw a small sparkline via the base's history buffer. `EditorUI::Initialize` now takes an `EditorUIInitInfo` bundle (window, backend, log system, engine, memory sampler — defaulted, mirrors `EditorContextInitInfo`) so the signature doesn't grow with each injected dependency. Unit-tested under `ProfileTest` (5 cases, direct-compile; no Win32 in the test). → [editor_module.md](editor/editor_module.md)

## In progress / built but not wired

- **Mesh proxy foundation (MP1 + basic MP2 + CPU frustum visibility, 2026-08-26)** — `RenderWorld`,
  owned by `RenderSystem`, accepts value-only create/update/destroy commands,
  applies them at the frame boundary, and returns immutable `MeshProxy`
  snapshots behind generational `RenderableHandle`s. The ScenePass now draws
  every visible ready proxy through Material V1 and `FrameContext`; the old
  bootstrap `RenderScene` path is retired from the engine. `SceneVisibility`
  conservatively builds the ScenePass list by rejecting proxies whose shared
  `spatial::AABB`s are outside the camera frustum; malformed bounds stay
  visible. `CoreSpatial` owns this bounds value for future World, Physics,
  Render, and editor consumers. World
  partition, LOD, occlusion culling, shadow classification, and transparent
  depth sorting are deliberately deferred. Opaque work is sorted by resolved
  pipeline, material instance, then mesh before ScenePass recording.
  → [mesh proxy TODO](world/mesh_proxy_TODO.md)

- **Material System V1 (M1–M4, 2026-08-26)** — Render owns real generational
  template and instance handles, immutable surface-template descriptors, and
  typed sparse instance overrides through compact parameter IDs; `MeshProxy`
  carries that real material handle. `MaterialSystem` now reports pending,
  ready, or failed resolution while the private resolver owns common pipeline,
  texture, and sampler handles. `FrameContext` now turns a ready instance into
  transient constant/texture bindings, and the bootstrap scene uses that real
  handle rather than a raw texture binding. `GraphicsSmoke` passes on Vulkan
  and OpenGL, including resize and teardown. → [material design](render/material_system.md),
  [material TODO](render/material_system_TODO.md)

- **Render resource resolver extraction (2026-08-25)** — `RenderSystem` now
  coordinates resource requests but no longer implements static RHI resource
  creation/caching itself. Its private `RenderResourceResolver` owns the
  default pipeline, mesh, texture, and sampler caches and releases their
  handles before backend teardown. This is the M3 integration seam for
  `MaterialSystem`; it exposes neither `RenderBackend` nor native API objects.
  → [render module](render/render_module.md)

- **Graphics contract hardening (2026-08-24)** — `GraphicsContractTest` now
  covers stale/forged handle rejection, `PipelineDesc` validation, and Vulkan
  shared-block range merge/reuse without a GPU. `GraphicsSmoke` passes the same
  `RenderScene` through Vulkan and OpenGL, including resize, dedicated mapped
  buffer allocation, and teardown. The remaining graphics test debt is a
  pipeline-cache equality test plus conditional non-coherent-memory hardware
  coverage. → [graphics TODO](graphics/TODO.md)

- **`ResourcePipeline::ProcessShader` has callers on both ends now** — the asset example bakes `simple_triangle` GLSL → SPIR-V end-to-end, and the `rhi_example` demo (2026-08-16, TODO 2.3) bakes its shaders through the pipeline at startup, replacing the build-time `glslc` step. Nothing reads prebuilt `.spv`/`.vert` files anymore. The graphics end **consumes `ShaderData`** as `data::ShaderData*` in `PipelineDesc` (2026-08-15); the render module itself still isn't wired.
- **Render module reconstruction** — `RenderSystem` owns the API-neutral `RenderBackend`, default `PipelineDesc` warmup/cache, and frame lifecycle. It still lacks material-defined state, a scene graph, and API-neutral recording; `RenderScene` remains the Vulkan-specific demo seam.

## Planned (next up)

- **Gameplay editor inspection (deferred)** — Gameplay is game-thread-owned,
  while the current editor runs on the render thread. Add a read-only snapshot
  before exposing Actor/component state to editor tools; do not give Editor
  mutable GameplayWorld ownership. → [gameplay design](gameplay/gameplay_module.md)
- **Gameplay boundary contract (GP0, 2026-08-28)** — the `Gameplay` Runtime
  target now owns `ActorHandle`/`ActorState`; Render owns the header-level
  `IRenderableSourceSink`, generational source token, and static-mesh source
  descriptor variant. Gameplay links only Core and Render, while Graphics stays
  Render-private. Runtime smoke and editor follow-up remain GP5. →
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay World/Actor/component ownership (GP1, 2026-08-28)** —
  `GameplayWorld` owns generational Actor records with deferred storage
  reclamation; Actor uniquely owns its components and drives their one-time
  initialize, ordered activation/tick, and reverse-order deactivation. Actor
  destruction invalidates the handle immediately. `GameplayUnitTest` covers
  lifecycle order, duplicate/late-add policy, stale handles, and teardown.
  → [gameplay design](gameplay/gameplay_module.md),
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay scene transforms and primitive state (GP2, 2026-08-28)** —
  same-Actor SceneComponent attachments reject self/cycles and keep cached
  transforms correct through `parent_world * local_transform`. Primitive state
  consists only of visible/casts-shadow flags and local/world AABBs; it remains
  headless and has no RenderWorld ownership. `GameplayUnitTest` covers parent
  changes, detach, invalid attachment, transform composition, and bounds. →
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay MeshComponent source production (GP3, 2026-08-28)** —
  GameplayWorld injects a non-owning Render source sink; MeshComponent emits
  value-only static-mesh create/update/destroy requests and retains only the
  generational source token. It coalesces dirty state to one update per tick;
  no Gameplay type can reach RenderWorld, MeshProxy, or Graphics. The eight
  `GameplayUnitTest` cases include this command lifecycle. →
  [gameplay design](gameplay/gameplay_module.md),
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay/render bridge integration (GP4, 2026-08-28)** — RenderSystem owns
  the mutex-protected source sink and render-thread source records. BeginFrame
  resolves ready logical mesh/material values into queued MeshProxy changes
  before RenderWorld applies them; pending/failed records have no proxy.
  RuntimeContext owns and ticks GameplayWorld before the game-to-render
  handoff, then destroys it before RenderSystem shutdown. Focused source and
  gameplay tests pass; runtime graphics smoke remains GP5. →
  [gameplay design](gameplay/gameplay_module.md),
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay validation (GP5, 2026-08-28)** — `GameplayUnitTest` and the
  render-source registry contract test cover source lifecycle. `GraphicsSmoke`
  passed the gameplay mesh create/move/visibility/destroy/resize/teardown path
  on Vulkan and OpenGL (three frames each). Editor inspection is deliberately
  deferred pending a read-only gameplay snapshot. →
  [gameplay TODO](gameplay/TODO.md)
- **Bootstrap Gameplay Actor migration (2026-08-28)** — Render startup now
  prepares a logical static-mesh source after loading bootstrap assets and
  creating the render-owned material identity. The startup handshake transfers
  that value to the game thread, where `CreateStaticMeshActor` creates the
  normal World-owned Actor; `RenderSystem` no longer owns a special bootstrap
  `MeshProxy`. → [gameplay design](gameplay/gameplay_module.md)
- **Material Asset V1 — asset loading slice (2026-08-28)** — Asset now loads
  strict version-1 `*.material` JSON into CPU-only `MaterialResource` values:
  material-relative shader/texture paths, surface policy, and scalar/vector/
  texture parameter sources. The format is documented beside the asset module;
  Render-side resolution is recorded in the following M6 completion entry.
  → [asset file structure](../engine/runtime/asset/README.md),
  [material TODO](render/material_system_TODO.md)
- **Material Asset V1 — render resolution (2026-08-28)** — Gameplay now
  publishes material AssetIDs, while RenderSystem resolves each loaded material
  into one cached private template/default-instance pair and supplies only that
  instance to MeshProxy. Bootstrap now references `bootstrap.material`; the
  initial unlit texture convention is `base_color_texture` at binding 2.
  → [material TODO](render/material_system_TODO.md)
- **Material Asset V1 — M6.1 validation (2026-08-28)** — the Render-owned
  material-asset cache is independently testable. Focused tests cover
  deduplication, invalid/unloaded/broken references, schema-version rejection,
  and failed-source proxy retirement; Vulkan/OpenGL graphics smoke passed.
  → [material TODO](render/material_system_TODO.md)
1. **Render module reconstruction** — the 7-step plan in [render_module.md](render/render_module.md): wire the resource pipeline in, move `PipelineDesc` construction out of the backend, add the warmup pass, retire the legacy GL path.
2. **RHI leak fixes** — `ShaderManager` retired + `PipelineDesc` shaders backed by `ShaderData` (**landed 2026-08-15**, Phase 0 of [vulkanbackend.md](graphics/vulkanbackend.md)). **`VulkanDevice` extracted (landed 2026-08-15**, Phase 1 — reconstruction; original archived at `backend/vulkan/deprecated/`). **`VulkanSwapchain` extracted (landed 2026-08-15**, Phase 2). **`VulkanFrameContext` extracted (landed 2026-08-15**, Phase 3 — command pools, scene/UI buffers, sync objects, in-flight index, one-shot primitives, sync2-only barriers; shared one-shot buffers + dead transfer helpers deleted). **Scene recording extracted (landed 2026-08-15**, Phase 4 — the backend exposes "the current frame's command buffer + attachments"; the demo moved out to `render::RenderScene`, the render module's first real scene; TODO 5.1 `Render` links `Graphics` landed). **Facade cleanup (landed 2026-08-16**, Phase 5 — `window_`/`camera_data` public seams dropped, `Initialize` takes the native window handle; sakura split decided: keep the frame loop). **Build-time `glslc` step removed (landed 2026-08-16**, TODO 2.3 — the demo bakes shaders at runtime via `ProcessShader`). **`ShaderModule` seam retired (landed 2026-08-16**, TODO 1.2 — raw `ShaderData` → API object stays inline in the pipeline bakes).
3. **Resource pipeline gaps** — add `CompileFailed` status (carry error text); make `ProcessShader` take the whole `ShaderProgramResource` as one compile unit.
4. **Headless unit tests** — asset manager, `GenerateShaderHash`, `ShaderCache`, `HandleSystem` are all testable without a GPU.
5. **Async resource queue** — the request-based producer/consumer seam (loading thread → render thread) that keeps shader compile / texture decode / GPU bakes off the render frame. Design written; `AssetLoadRequest` + `RequestState` defined in `asset/asset_load_request.h`, generic `AsyncQueue<T>` in `core/async` (header-only lib, wired into Core). **Landed (2026-08-13): the render-side consumer** — `RenderSystem` drains the queue in two modes (bootstrap full-drain + per-frame budgeted drain), loads via `asset.LoadSync`, processes shaders via `ResourcePipeline`, and caches results in a render cache. The dedicated **loading thread is deferred**: the render thread loads in-place, budgeted (`kMaxRuntimeLoadsPerFrame`). Its first consumer is the bootstrap preload flow — item 6. → [async_resource_queue.md](async/async_resource_queue.md)
6. **Bootstrap preload pipeline** — the end-to-end flow the async queue exists for. Engine boot reads the need-list and runs each entry through the whole chain:
   - **read** — `GetBootstrapPath()` + `bootstrap::ReadBootstrap` parse `config/bootstrap.json` into asset paths
   - **load** — each entry becomes an `AssetLoadRequest`; the loading thread runs `asset.LoadAsync` + `resource.ProcessShader`, then flips it `Ready`
   - **queue** — `Ready` requests land in the ready pipe
   - **render** — per-frame `DrainFrameBudget`: fill `PipelineDesc`, `graphics.CreatePipelineResource`, insert into the ready-cache
   - **gate** — the bootstrap waits for the batch before entering the main loop
   Landed so far: `config/bootstrap.json` + `GetBootstrapPath()` + `bootstrap::ReadBootstrap` (nlohmann parser, unit-tested under `BootstrapTest`) + `bootstrap::BuildLoadRequests` (need-list → Queued `AssetLoadRequest`s; type sniffed from extension, unknown + duplicate entries skipped, unit-tested). The preload lives in an **engine-scoped module** `engine/runtime/bootstrap/` (lib `Bootstrap`, only `RuntimeLib` links it) — deliberately not core/resource public API, so render/editor/graphics can't invoke it. `Engine::Initialize` reads the config once (guarded by `PreloadBootstrap`) and pushes the requests onto the async queue's **incoming leg** (`RuntimeContext::async_load_queue_`); a missing bootstrap.json is a hard boot error surfaced by `main`'s try/catch. **Landed (2026-08-13): the render side consumes it** — `RenderSystem::PostInitialize` full-drains the bootstrap batch (load + `ProcessShader` → render cache), then `Tick` budget-drains later runtime requests. The loading thread is deferred; the render thread loads in-place under a per-frame budget.
7. **Script binding layer** — the `Script` lib seam under `engine/runtime/script/` is empty; the engine-facing layer (bind kpengine classes to Lua, root `package.path` at `GetScriptDirectory()` → `asset/script/`, load scripts through the asset pipeline, a `ScriptSystem` owned by `RuntimeContext`) is next. → [script_module.md](script/script_module.md)

## Known broken / known issues

- **Concrete scene recording remains Vulkan-only** — `RenderSystem` now owns the common backend/pipeline lifecycle, but `RenderScene` still uses raw Vulkan commands. Phase 3 replaces that seam with common recording commands.
- **`main.cpp` selects examples by uncommenting** — most examples block (windows, `while(1)`); running the binary from an agent shell will hang.

## Dead code & stale paths

→ [docs/dead_code.md](dead_code.md). Everything that is not in the build, not wired, or slated for retirement lives there so it doesn't get "fixed" as if live.
