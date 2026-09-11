# MODE1 — 3DSceneHost and Live2DViewerHost

- Status: MODE1.5 landed 2026-09-11
- Owner: Engine composition / Runtime / Render / Live2D
- Parent plan: [Engine Host Mode Plans](../PLANS.md)
- Roadmap: [Engine Host Mode Roadmap](../TODO.md)

## Objective

Introduce an engine-level host boundary so the traditional 3D scene and the
Live2D viewer are separate application compositions. Preserve the existing
deferred renderer and scene control path while allowing the Live2D viewer to
run without scene-world or scene-editor construction.

## Current state

The application composition now selects a host by mode. The standalone
viewer owns `Live2DRenderer` directly; the traditional scene renderer owns
only deferred/PBR scene work. The temporary extension seam used for the first
cross-backend pixel validation has been removed.

Relevant current seams:

- `engine/module/live2d/live2d_viewer_host.cpp` owns the viewer backend,
  frame contexts, renderer, presentation, and capture.
- `engine/runtime/render/render_system.cpp` owns only the scene lifecycle and
  deferred/PBR recording.
- `render::RenderSubmission` and its executor remain the shared API-neutral
  submission seam for feature renderers.

## Scope

In scope:

- a process-level mode selection boundary;
- shared engine services and host lifecycle contracts;
- explicit `3DSceneHost` ownership of the scene stack;
- explicit `Live2DViewerHost` ownership of Live2D preview execution;
- viewer-mode startup, frame loop, presentation, capture, and shutdown;
- migration away from Live2D registration inside the scene `RenderSystem`.

Out of scope:

- a Live2D Gameplay component or level-authoring schema;
- a Live2D asset editor with motion timelines or parameter authoring;
- multiple hosts rendering concurrently in one process;
- a general render-graph rewrite;
- changing deferred PBR pass order, materials, or RenderWorld policy;
- animation, expression, physics, or emotion policy beyond the current viewer.

## Proposed contracts

The exact class names may be adjusted during implementation, but the ownership
must remain equivalent to these contracts:

```cpp
enum class ApplicationMode : uint8_t
{
    Scene3D,
    Live2DViewer,
};

class IApplicationHost
{
public:
    virtual ~IApplicationHost() = default;
    virtual bool Initialize(EngineServices &, std::string &) = 0;
    virtual bool Tick(float delta_time, std::string &) = 0;
    virtual bool RecordFrame(std::string &) = 0;
    virtual void Shutdown() noexcept = 0;
};
```

`EngineServices` is a Runtime-owned object containing shared facilities only.
It may expose API-neutral Graphics/RHI and frame scheduling, Asset/Resource,
input, window/presentation, logging, and capture services. It must not expose
`RenderWorld` or `DeferredRenderer` to `Live2DViewerHost`.

`3DSceneHost` owns the scene-specific `RenderSystem` and its collaborators.
`Live2DViewerHost` owns the Live2D system/renderer and a minimal preview state.

The neutral host contract belongs under Runtime. `3DSceneHost` is a built-in
Runtime implementation. `Live2DViewerHost` belongs to the optional Live2D
module and implements the neutral contract without being linked into
`RuntimeLib`. The application boundary only selects a registered provider; it
does not own either concrete host.

## Lifecycle

### Scene3D mode

```text
parse mode
  → create shared services
  → create 3DSceneHost
  → initialize Asset/Gameplay/RenderSystem/DeferredRenderer
  → load startup level
  → tick Gameplay and prepare scene sources
  → record deferred scene and editor presentation
  → shutdown editor, Gameplay, RenderSystem, and shared services
```

### Live2DViewer mode

```text
parse mode
  → create shared services
  → create Live2DViewerHost
  → initialize Live2D registration/system and viewer window
  → load configured .live2d product
  → create model instance and Live2D render resources
  → tick model and record Live2D passes/submission
  → present viewer output and process viewer controls
  → shutdown renderer, model instances, Live2D system, and shared services
```

No step in the viewer path loads a level, constructs `RenderWorld`, creates
`DeferredRenderer`, or initializes the normal scene editor UI.

## Ownership and dependency invariants

1. `3DSceneHost` is the only owner of traditional scene policy.
2. `Live2DViewerHost` is the only owner of standalone Live2D viewer policy.
3. `RenderSystem` does not include or register Live2D.
4. `Live2DRenderer` does not read `RenderWorld`, Gameplay components, or the
   scene camera; it receives viewer transform/viewport state from its host.
5. Both hosts may submit through shared API-neutral Graphics/RenderGraph
   contracts, but GPU resources remain owned by the creating host/feature.
6. The logger is shared and available in both modes; log ownership does not
   imply scene ownership.
7. The normal editor UI may attach only to the scene host's presentation
   capability. Viewer controls attach only to `Live2DViewerHost`; Runtime does
   not depend on Editor to provide either host.

## Migration stages

### MODE1.1 — composition seam (landed 2026-09-11)

The Runtime host contract and provider registry now live in
`engine/runtime/host/application_host.*`. Launch parsing accepts `--mode
scene3d` and `--mode live2d-viewer`, with `scene3d` remaining the default. The
application composition root passes the parsed mode to `Engine`; a non-default
mode fails early until a feature module registers its provider. The current
scene startup path is otherwise unchanged. A concrete shared `EngineServices`
view is intentionally deferred because the existing RuntimeContext still owns
the scene-specific lifecycle; extracting it belongs with MODE1.2/1.3.

### MODE1.2 — scene host (landed 2026-09-11)

Added the Runtime-owned `Scene3DHost` lifecycle shell and made the application
module composition mode-aware. `Engine` creates and drives the built-in scene
host for `scene3d`; the existing `RuntimeContext`/`RenderSystem` startup,
Gameplay tick, deferred/PBR recording, and editor presentation remain the
compatibility implementation behind that shell.

`ModuleBootstrap` now registers the `Live2DViewerHost` provider only for
`live2d-viewer`. Therefore the normal scene host cannot register a Live2D
renderer or draw the Live2D viewer. The full mechanical extraction of
`RuntimeContext` ownership is intentionally deferred until the shared
services needed by a second host are concrete.

### MODE1.3 — viewer host

`Live2DViewerHost` now owns the standalone viewer window, backend, frame
contexts, Cubism system, configured model, and concrete Live2D renderer. Viewer
startup is performed on the render thread and never initializes the lazy scene
services, `RenderWorld`, `DeferredRenderer`, level, or editor UI.

The generic submission executor now supports an explicit presentation pass.
OpenGL records that pass to the default framebuffer; Vulkan records it through
the backend-owned swapchain presentation bridge. Live2D keeps its offscreen
mask target but sends its final drawable pass directly to the viewer window.
The viewer does not use the scene `RenderSystem`; it calls the reusable Live2D
renderer and generic submission executor directly.

### MODE1.4 — mode validation (landed 2026-09-11)

Launch parsing now rejects `--startup-level` and `--agent-port` in
`live2d-viewer` mode, and rejects the viewer-only `--capture` option in
`scene3d` mode. `--capture save/screenshots/validation/*.png` requests one
standalone viewer capture without creating the scene command registry.

`Live2DViewerHost` owns that capture request and completes it at the
presentation boundary: before OpenGL buffer swap and after Vulkan present. The
existing API-neutral readback/export service remains reusable for future
offscreen viewer targets. The renderer recreates only its feature-owned output
target when the backend extent changes; swapchain recreation remains a backend
responsibility.

`RuntimeContext::AreSceneServicesInitialized()` and the viewer startup guard
provide the negative construction seam. Scene host initialization asserts that
its scene services exist; viewer startup fails if they were constructed.

Validated with `RuntimeLaunchOptionsTest` (7/7), a RelWithDebInfo engine build,
and standalone Hiyori captures on both OpenGL and Vulkan. OpenGL keeps the
existing sRGB default-framebuffer path; Vulkan applies the equivalent encoding
in the Live2D shader through `KP_GRAPHICS_API_VULKAN`. The viewer framing is
1.25x wider horizontally, while the deferred/PBR scene path remains unchanged
and was not made dependent on the viewer capture service.

### MODE1.5 — remove temporary coupling

Removed the temporary `IRenderExtension` contract and all corresponding
registration, initialization, frame-recording, output-view, and cleanup code
from `RenderSystem`. Removed the obsolete `Live2DModule` scene lifecycle and
the unused scene-editor Live2D preview registration. `Live2DRenderer` remains
under `Live2DRender`, but is now a concrete renderer owned directly by
`Live2DViewerHost`.

The change preserves the generic `RenderSubmission`/executor path and does not
change `DeferredRenderer` pass policy, PBR materials, scene capture, or the
OpenGL/Vulkan backend contracts.

## Validation plan

- Unit test mode parsing, duplicate/unknown mode rejection, and host factory
  selection.
- Scene-mode runtime smoke: existing `GraphicsSmoke`, Render tests, and editor
  startup path.
- Viewer-mode runtime smoke: Hiyori startup and first-frame presentation on
  OpenGL; Vulkan capture/resize coverage remains MODE1.4 validation work.
- Negative viewer assertion: no `DeferredRenderer` or `RenderWorld`
  construction is observed.
- Negative scene assertion: no Live2D renderer registration occurs.
- Review public headers for forbidden host dependencies and run
  `git diff --check`.

## Risks and open questions

- The current `RenderSystem` owns more frame/backend lifecycle than a clean
  shared `EngineServices` seam. Extraction must avoid duplicating Vulkan/OpenGL
  synchronization logic.
- The current capture service is tied to `RenderSystem`; viewer capture may
  need a host-neutral capture service before extension retirement.
- A future in-editor Live2D asset editor may need multiple preview sessions;
  this stage intentionally supports one standalone viewer host only.
