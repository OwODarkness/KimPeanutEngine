# Editor Module

**Snapshot: 2026-08-24.** The editor is the engine's *core center*: a thin application shell that hosts the runtime engine and owns the tool UI on top of it. Its job is not to render or simulate — those belong to the runtime — but to be the hub that wires the runtime systems to the tooling UI, and to build/draw that UI.

## Core principles

1. **The editor is the core center.** One `Editor` object owns the whole tool surface, reaches every runtime system through a single hub (`EditorContext`), and is driven by the engine's boot/loop/teardown.
2. **`EditorUI` is the UI manager.** It owns the ImGui context, the component tree, and the platform backends; components never talk to the window or the GPU.
3. **WSI and render are wrapped behind interfaces** so the UI system never hardcodes GLFW or a graphics API. `IEditorImguiWSI` (window/event) and `IEditorImguiRenderer` (drawing) are chosen by the active API at startup; components see only ImGui.
4. **Component UI design.** The UI is a composable tree of `EditorUIComponent`s — windows, controls, and editor-specific panels are all components that render themselves into the current ImGui context.

## Directory layout

```
engine/editor/
  main.cpp                           application entry (owns nothing; boots the engine)
  CMakeLists.txt                     EditorLib (STATIC) — owns the target + shared config, add_subdirectory per subdir
  editor.h/.cpp                      Editor — the core center
  context/
    CMakeLists.txt                   explicit sources → EditorLib
    editor_context.h/.cpp            EditorContext + global_editor_context (the hub)
  ui/
    CMakeLists.txt                   explicit sources → EditorLib; add_subdirectory(component)
    editor_ui.h/.cpp                 EditorUI — the UI manager
    component/
      CMakeLists.txt                 the 8 widget .cpp files, explicit
      editor_ui_component.h          EditorUIComponent — the component base
      editor_window_component.*      window shell (also the panel base)
      editor_{container,text,button,listbox,plot,tooltip,menubar}_component.*
      editor_{slider,drag}_component.h   header-only templated controls
      editor_scene_component.h       scene viewport (declared; impl pending)
      editor_camera_component.h      camera panel (declared; impl pending)
  platform/
    CMakeLists.txt                   explicit sources → EditorLib
    editor_imgui_wsi.h               IEditorImguiWSI       (platform seam)
    editor_imgui_glfw_wsi.*          EditorImguiGLFWWSI    (GLFW implementation)
    editor_imgui_renderer.h          IEditorImguiRenderer  (render seam)
    editor_imgui_opengl_renderer.*   EditorImguiOpenglRenderer
    editor_imgui_vulkan_renderer.*   EditorImguiVulkanRenderer
  log/
    CMakeLists.txt                   explicit sources → EditorLib
    editor_log_component.*           the log window (a window component fed by LogSystem)
  settings/
    CMakeLists.txt                   explicit sources → EditorLib
    editor_settings.*                config/settings.json loader (log colors; EditorLib-only)
  profile/
    CMakeLists.txt                   explicit sources → EditorLib
    editor_metric.*                  EditorMetric base + EditorFuncMetric (the metric seam)
    editor_builtin_metrics.*         FPS / frame-time / memory metrics (sampler-injected)
    editor_profile_bar.*             bottom status bar component
```

Headers sit beside their sources, and everything is included via the **engine root** (`"editor/..."` paths), matching how runtime code is included. `EditorLib` PUBLIC-exposes `${CMAKE_SOURCE_DIR}/engine` itself, so the parent no longer supplies an editor include path.

**Build structure.** `EditorLib` is one STATIC target: [engine/editor/CMakeLists.txt](../../engine/editor/CMakeLists.txt) owns the target, the include root, and the link set (`RuntimeLib imgui glad nlohmann`, all PRIVATE), then `add_subdirectory`es each concern. Every subdirectory has its own small `CMakeLists.txt` that adds its sources explicitly via `target_sources(EditorLib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/...)` — no `file(GLOB)`, so a stray `.cpp` can't silently join the build. `main.cpp` is deliberately **not** part of `EditorLib`: the root [CMakeLists.txt](../../CMakeLists.txt) compiles it into the `KimPeanutEngine` executable itself (the glob the old build used pulled it into the lib too, a latent duplicate-`main`).

`EditorLib` is a STATIC lib; the exe is `engine/editor/main.cpp` + `EngineLib` (which INTERFACE-links `RuntimeLib` + `EditorLib`). `Engine` (in `RuntimeLib`) owns the `editor::Editor` instance, which is why `RuntimeLib` PRIVATE-links `EditorLib` while `EditorLib` PRIVATE-links `RuntimeLib` — a two-way static-lib dependency. It resolves at link time, but it deviates from the clean `Editor → Engine → …` layering and is a known wrinkle (see [Follow-up seams](#follow-up-seams)).

## Editor — the core center

`Editor` ([editor.h](../../engine/editor/editor.h), [editor.cpp](../../engine/editor/editor.cpp)) is a thin shell with four responsibilities, each on a specific thread:

| Method | Thread | What it does |
|---|---|---|
| `Initialize(engine)` | main | Stores the `runtime::Engine*`, fills `EditorContext` from the runtime's global context, sets `global_editor_context.editor = this`. No GPU work. |
| `InitEditorUI()` | render | `editor_ui_->Initialize(EditorUIInitInfo{...})` — ImGui + backend init must run where the GL/Vulkan context lives; the init fields are filled from `EditorContext`. |
| `Tick()` | render | `BeginDraw()` → `Render()` → `EndDraw()` on `EditorUI`. |
| `CloseUI()` | render | Shuts ImGui down before window teardown. |
| `Clear()` | main | Resets editor state after the render thread has joined (ImGui was already torn down there). |

The engine drives the whole lifecycle ([engine.cpp](../../engine/runtime/engine.cpp)): `Editor` is created in `Engine`'s ctor, `Initialize`d at boot, `InitEditorUI`d when the render thread comes up, `Tick`ed each frame right after input polling and before the swap, `CloseUI`d at render-thread teardown, and `Clear`ed at shutdown. The editor is therefore an *in-process tool layer over the runtime*, not a separate executable.

## EditorContext — the hub

`EditorContext` ([editor_context.h](../../engine/editor/context/editor_context.h)) is how the editor reaches the runtime. A single `global_editor_context` singleton mirrors the runtime's own global-context pattern and carries **non-owning** pointers to the systems the tooling needs:

- `window_system_`, `render_system_`, `log_system_`, `input_system_`, `runtime_engine_`, `graphics_api_type_` — copied from `EditorContextInitInfo`, which `Editor::Initialize` fills from `runtime::global_runtime_context`.
- `editor` — back-pointer to the `Editor` shell.

Components and panels pull what they need from `global_editor_context` rather than being handed system pointers through constructors.

## EditorUI — the UI manager

`EditorUI` ([editor_ui.h](../../engine/editor/ui/editor_ui.h), [editor_ui.cpp](../../engine/editor/ui/editor_ui.cpp)) owns three things:

- `wsi_` (`unique_ptr<IEditorImguiWSI>`)
- `renderer_` (`unique_ptr<IEditorImguiRenderer>`)
- `components_` (`vector<unique_ptr<EditorUIComponent>>`) — the tool UI tree

`Initialize(EditorUIInitInfo)` runs on the render thread: `ImGui::CreateContext` + `StyleColorsDark`, then `CreateImguiBackends` picks the concrete WSI and renderer by `init_info.backend_type`, and finally the tool tree is assembled by one private builder per panel — `BuildMenuBar`, `BuildPlaceholderWindow`, `BuildLogWindow` (reads `LogSystem` each frame), `BuildProfileBar` (reads `engine` FPS and the platform `memory_sampler` via injected metric samplers). The init fields are a defaulted bundle (`EditorUIInitInfo` in `editor_ui.h`), mirroring `EditorContextInitInfo`; the builders keep `Initialize` an orchestration list so adding a panel is one more builder call, not more inline code. `BeginDraw`/`EndDraw` bracket the frame (`renderer_->NewFrame()` → `wsi_->NewFrame()` → `ImGui::NewFrame()`; the end side is currently a no-op), and `Render` iterates the components, then calls `ImGui::Render()` + `renderer_->Render()`.

Component code touches **only ImGui** — it never sees `GLFWwindow*` or a `Vk*` handle. That is the payoff of the next two seams.

## WSI + renderer seams (no GLFW / graphics-API hardcoding)

Both seams live behind one virtual interface each and are selected once, by `GraphicsAPIType`, in `EditorUI::Initialize`. The generic plumbing types are `WindowHandle = void*` and `GraphicsContext { GraphicsAPIType type; void* native }` ([base/type.h](../../engine/runtime/core/base/type.h)).

### `IEditorImguiWSI` — window + events
`Initialize(WindowHandle, GraphicsAPIType)` / `Shutdown()` / `NewFrame()`. The only implementation today is `EditorImguiGLFWWSI` ([editor_imgui_glfw_wsi.cpp](../../engine/editor/platform/editor_imgui_glfw_wsi.cpp)), which wraps `imgui_impl_glfw` and delegates to `ImGui_ImplGlfw_InitForOpenGL` or `InitForVulkan` by API type. A future SDL/Win32 backend is a new subclass — `WindowAPIType` already enumerates them, and nothing in the editor would change.

### `IEditorImguiRenderer` — drawing
`Initialize(GraphicsContext)` / `Shutdown()` / `NewFrame()` / `Render()`. Two implementations:

- **`EditorImguiOpenglRenderer`** ([.cpp](../../engine/editor/platform/editor_imgui_opengl_renderer.cpp)) — `imgui_impl_opengl3`, `#version 450`. Loads `glad` itself (the legacy GL backend that used to own the proc table isn't in the build) and owns the per-frame clear (`0.1` gray) as a stopgap until the reconstructed scene renderer returns. This is the working path today.
- **`EditorImguiVulkanRenderer`** ([.cpp](../../engine/editor/platform/editor_imgui_vulkan_renderer.cpp)) — `imgui_impl_vulkan`, initialized from `VulkanEditorBridgeInfo` and records draw data through the frame-scoped `graphics::VulkanEditorBridge`. It owns ImGui's descriptor pool, sampler, and viewport texture descriptor, but only borrows backend device/swapchain/frame resources. It cannot retrieve a general command buffer, queue, context, or manager from `VulkanBackend`.

The seam's value: swapping GL ↔ Vulkan, or GLFW ↔ SDL, is a one-line change in the factory. No component, and no `EditorUI` frame logic, is affected.

## Project settings (`config/settings.json`)

Editor-wide preferences live in `config/settings.json` (beside `bootstrap.json`) so UI behavior isn't hard-coded in components. `EditorSettings` ([editor_settings.h](../../engine/editor/settings/editor_settings.h)) carries the OpenGL default-framebuffer `background_color` (`[r, g, b, a]`, default `[0.10, 0.10, 0.10, 1.00]`) and a per-`LogLevel` color table. `EditorUI` applies a compact deep-dark ImGui palette for its chrome; the configurable background fills any uncovered framebuffer pixels. `ReadEditorSettings(path)` ([editor_settings.cpp](../../engine/editor/settings/editor_settings.cpp)) parses it with the same tolerant posture as `ReadBootstrap` — a missing file throws, malformed colors retain defaults, and unknown level names are ignored. `GetSettingsPath()` ([config/path.h](../../engine/runtime/core/config/path.h)) resolves the path, and `EditorUI::Initialize` loads the settings once (catching failures into the defaults), then injects the background color into the selected ImGui renderer before building the panels. The loader lives in `settings/` under EditorLib because `Bootstrap` is deliberately engine-scoped — the editor can't use it.

## Component UI design

### Base + composition
`EditorUIComponent` ([editor_ui_component.h](../../engine/editor/ui/component/editor_ui_component.h)) is a single virtual `Render()`. Components hold `shared_ptr<EditorUIComponent>` children (`AddComponent`), so the UI is a **tree** drawn depth-first each frame — the ImGui immediate-mode idiom. Windows and containers add children; leaves draw one widget.

### Windows and containers
- **`EditorWindowComponent`** — the top-level shell: an ImGui window (`Begin(title_, &is_open_)` → `RenderContent()` → `End()`) whose geometry is an `EditorWindowConfig` of viewport fractions (`pos_x/pos_y/width/height` ratios + `locked`). Unlocked (`locked=false`) applies the geometry once (`ImGuiCond_FirstUseEver`), so the window moves/resizes freely; locked (`locked=true`, default) pins it to the viewport every frame (`ImGuiCond_Always` + `NoMove`/`NoResize`), so it follows the OS window but can't be moved/resized. Each window draws a lock/unlock toggle in its title bar (next to the close button); `SetLocked`/`IsLocked` are the programmatic seam. Tracks `pos_x/pos_y/width_/height_` from ImGui each frame; subclasses override `RenderContent()` to draw panel-specific content and children. Most editor panels are this subclass.
- **`EditorContainerComponent`** — a bare child container.

### Primitive controls
- `EditorTextComponent` — static text; `EditorDynamicTextComponent<T>` — templated, binds a `const T*` and re-formats it every frame (`label_ + std::to_string(*text_ref_)`, null-guarded).
- `EditorButtonComponent` — label + `ButtonStyle` (text/bg/hover/active colors) + `FOnButtonClickNotify` **delegate** for the click.
- `EditorListboxComponent` — `vector<const char*>` items, current/last index tracking, `OnItemSelected(old, new)`.
- `EditorSliderComponent<T>` / `EditorDragComponent<T>` — templated with explicit `float`/`int` specializations mapping to `ImGui::SliderFloat/Int` and `ImGui::DragFloat/Int`; each binds a `T* data_` (writes straight into engine state, null-guarded).
- `EditorPlotComponent` — samples a `std::function<float(float)>` over `[begin, end]` at a step and plots it.
- `EditorTooltipComponent` — wraps text or an inner component.
- `EditorMenuBarComponent` / `EditorMainMenuBarComponent` — `Menu`/`MenuItem` (title, shortcut, enabled, selected) model.

### Editor-specific panels
- **`EditorSceneComponent`** — a window presenting a `FrameBuffer` (the scene view) that tracks mouse state (`is_left_mouse_down/drag/release/click`, position) and exposes `FOnMouseClickCallback` + `is_scene_window_focus`. It is the input seam for scene picking.
- **`EditorCameraControlComponent`** — camera config window (fov, move/rotate speed, near/far, reset) over a `RenderCamera*`.
- **`EditorLogComponent`** — the log window (`log/`). Created by `EditorUI` with the context's `LogSystem*` plus the `LogLevelColorTable` and rendered through the component tree; each entry is `TextColored` with its level's color, which comes from `config/settings.json` ([Project settings](#project-settings-configsettingsjson)).
- **`EditorProfileBarComponent`** — the bottom status bar (`profile/`). Samples a list of injected `EditorMetric`s and draws them in one row, anchored to the bottom of the main viewport via public ImGui API only. It never sees the engine or Win32 — the metrics are the decoupling seam.

### Profile metrics (status bar)
- **`EditorMetric`** ([editor_metric.h](../../engine/editor/profile/editor_metric.h)) — the extension seam. `Name()` + `Sample()` (the latter returns the formatted string for the frame); the base owns an optional plot history (`history_capacity`, `0` opts out) that drives the sparkline. `EditorFuncMetric` wraps a value/plot sampler-lambda pair for ad-hoc readouts without a subclass.
- **Built-ins** ([editor_builtin_metrics.h](../../engine/editor/profile/editor_builtin_metrics.h)): `EditorFPSMetric` (engine FPS via an injected sampler), `EditorFrameTimeMetric` (ms, derived from the fps sampler by the caller — a self-measured clock would see the render loop's pacing sleep, not frame cost), `EditorMemoryMetric` (process + system free via an injected stats sampler).
- **Measurement lives in the platform layer, not the editor — and not the engine.** FPS is game-loop timing, so it stays in the engine (`Engine::GetFPS`). Memory is an OS query, so it lives behind a **platform seam**: `MemoryStatsSampler` (interface + `CreateMemoryStatsSampler(PlatformType)` factory, mirroring `WindowSystem`) with `WindowsMemoryStatsSampler` under `engine/runtime/platform/win/` (PSAPI/GlobalMemory, `psapi` linked into the `Platform` lib). `RuntimeContext` owns the instance; the editor reaches it through `EditorContext::memory_sampler_` and only consumes it through the injected sampler. The engine never touches the OS — it is platform-agnostic.

### Data binding style
Components hold **raw pointers** to engine data (`T*`, `const T*`, `RenderCamera*`, `LogSystem*`, `FrameBuffer`) and re-read them each frame; writes go straight back into the engine object. This is deliberate — it matches ImGui's immediate mode and keeps the editor dependency-light. The null guards on every bind are what keep an unset panel from crashing a frame.

## Threading

ImGui is not thread-safe and its backends require a live GL/Vulkan context, so **all ImGui work — context creation, WSI/renderer init, per-frame draw, teardown — runs on the render thread**. `Editor::Initialize`/`Clear` are the exceptions (main thread, no GPU). `Clear` must run *after* the render thread has joined because `CloseUI` already destroyed the ImGui state there; `Clear` only resets editor state. The render loop is: input poll → `editor_->Tick()` → swap.

## Current state

- **Minimal UI.** Today the tree is a **top menu bar** (`File`/`Edit`/`Tool`/`Help`, no items or event bindings yet), a **Viewport** window, the **log window** (`OutputLog`, fed by `LogSystem`), and the **profile bar** (bottom status bar: FPS, frame ms, memory, each with optional sparklines). `EditorViewportComponent` borrows `RenderSystem`'s `RenderTargetView` every frame and asks the selected ImGui renderer for its `ImTextureID`; it owns neither the render target nor the registration. The OpenGL renderer presents the color-texture token now; Vulkan presentation waits for its ImGui descriptor bridge. Log entry colors are configured in `config/settings.json` (per level, with in-code defaults as fallback).
- **Deleted seeds (2026-08-13 restructure).** `EditorSceneManager`, `EditorActorControlPanel`, and the scene/camera component implementations were all-comment files and were **removed** during the directory restructure. They are the resurrection seeds for scene-picking + gizmos + the actor transform panel — recover them from git history, don't expect them in the tree.
- **Vulkan editor presentation** — `EditorImguiVulkanRenderer` records through `VulkanEditorBridge`, which brackets the swapchain UI pass and returns the image to present layout. The bridge is valid only during the active backend frame; Vulkan device/swapchain ownership and submit/present remain in graphics.
- **GL renderer owns the frame clear** — a stopgap until the render-module reconstruction restores the scene renderer.

## Follow-up seams

- **Rebuild the scene manager** — resurrect `EditorSceneManager`/`EditorActorControlPanel` against the reconstructed render module (scene viewport, click-to-pick, gizmos, actor transform panel).
- **Log window polish** — the `OutputLog` window is virtualized (`ImGuiListClipper` — only visible rows are laid out + formatted) and reads a thread-safe snapshot (`LogSystem::GetLogSnapshot()` copies the buffer under the logger mutex), so the render loop never walks the live vector. Remaining: auto-scroll/follow, a level filter, and handling very long lines (they currently overflow horizontally, single-line).
- **Swap WSI abstraction** — SDL/Win32 `IEditorImguiWSI` implementations when the runtime supports those `WindowAPIType`s.
- **Break the RuntimeLib ↔ EditorLib cycle** — prefer an editor-owned engine-interface (`Engine` declares what the editor may call, editor implements the rest) or move `Engine::editor_` ownership out of `RuntimeLib`.
- **Simplify `EditorContext`** — let the hub own fewer raw system pointers as the runtime formalizes its own context API.
