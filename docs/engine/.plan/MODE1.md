# MODE1 — 3DSceneHost and Live2DViewerHost

- Status: proposed
- Owner: Engine composition / Runtime / Render / Live2D
- Parent plan: [Engine Host Mode Plans](../PLANS.md)
- Roadmap: [Engine Host Mode Roadmap](../TODO.md)

## Objective

Introduce an engine-level host boundary so the traditional 3D scene and the
Live2D viewer are separate application compositions. Preserve the existing
deferred renderer and scene control path while allowing the Live2D viewer to
run without scene-world or scene-editor construction.

## Current state

The current application composition registers `Live2DModule` globally. The
module creates `Live2DRenderer`, registers it as an `IRenderExtension`, and
`RenderSystem` initializes and records it after `DeferredRenderer`. This is
useful for the first cross-backend pixel validation, but it means Live2D is
currently hosted by the traditional scene renderer.

Relevant current seams:

- `engine/module/live2d/live2d_module.cpp` registers the renderer with the
  global `RenderSystem`.
- `engine/runtime/render/render_system.cpp` owns `DeferredRenderer` and calls
  the optional extension during the scene frame.
- `engine/runtime/render/render_extension.h` is the temporary generic seam.

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

### MODE1.2 — scene host

Move current `RuntimeContext`/`RenderSystem` startup coordination behind
`3DSceneHost` without changing `DeferredRenderer` internals. Keep the editor
presentation adapter explicit and reject viewer-only services from this host.

### MODE1.3 — viewer host

Create `Live2DViewerHost` around the existing concrete Live2D renderer. Reuse
the API-neutral frame/Graphics services, but provide viewer-owned presentation
and capture. Initially the existing generic submission executor may remain the
recording implementation.

### MODE1.4 — mode validation

Add OpenGL/Vulkan startup and capture checks for both modes. Assert in logs or
test seams that viewer mode never initializes `DeferredRenderer` and scene mode
never registers a Live2D renderer.

### MODE1.5 — remove temporary coupling

Retire `IRenderExtension` registration from the scene path. Keep the generic
submission and future RenderGraph interfaces reusable by both hosts, but make
host ownership explicit.

## Validation plan

- Unit test mode parsing, duplicate/unknown mode rejection, and host factory
  selection.
- Scene-mode runtime smoke: existing `GraphicsSmoke`, Render tests, and editor
  startup path.
- Viewer-mode runtime smoke: Hiyori capture on OpenGL and Vulkan, resize,
  capture, and clean shutdown.
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
