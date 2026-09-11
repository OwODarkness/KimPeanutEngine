# Engine Host Mode Plans

**Status: proposed.** This document defines the engine-level composition
boundary for a traditional 3D scene host and a standalone Live2D viewer host.
It does not redesign the deferred renderer or implement a general editor
framework.

## Design decision

The engine selects one application host at startup. Each host owns its own
scene or preview policy, while low-level services remain shared.

```text
                         EngineHost
                             |
             selects exactly one application host
                    /                         \
                   /                           \
        3DSceneHost                    Live2DViewerHost
             |                               |
      GameplayWorld                      Live2DSystem
      RenderWorld                         Live2DRenderer
      DeferredRenderer                    viewer presentation
      optional scene UI                   optional viewer UI
             \                               /
              \                             /
               shared EngineServices
        Asset / Resource / Graphics / Frame / Log
```

The shared layer is infrastructure, not scene policy. A host may use the
common Graphics/RHI device, command submission, frame lifetime, RenderGraph,
window, input, and logging services. It must not import another host's scene
objects merely to reuse the frame loop.

## Module placement

The hosts are runtime execution contexts, not application-owned feature
objects. The application layer only selects a mode and registers optional host
providers:

```text
engine/application/          thin startup/composition boundary
  application_mode.*          mode parsing/selection
  host_registration.*         host/factory registration only

engine/runtime/               RuntimeLib
  neutral host lifecycle and shared runtime services
  3d_scene_host.*             built-in traditional scene host
  no Live2D dependency

engine/runtime/render/        Render
  traditional scene renderer and RenderWorld policy
  no Live2D dependency

engine/module/live2d/         Live2DModule / Live2DRender
  Cubism runtime, asset integration, reusable Live2D rendering,
  live2d_viewer_host.*        optional Live2D viewer host

engine/module/                ModuleBootstrap
  optional-module registration adapter used by the application boundary
```

The application boundary may depend on `RuntimeLib`, `EditorLib`, and optional
feature modules, but it does not own host implementations. The dependency
direction is therefore:

```text
Application startup → host registry / mode selection
RuntimeLib      → 3DSceneHost / Render / Gameplay / Graphics
Live2DModule    → Live2DViewerHost / Live2DRender → Render
Render          -/→ Live2D
```

The neutral host contract lives under Runtime. The shared service view is
still represented by the existing Runtime-owned services during MODE1.1 and
will be extracted only when a second host needs the same lifecycle. The
optional `Live2DViewerHost` is registered by the Live2D module through that
contract; Runtime does not include or link Live2D. A separate `Runtime/Service`
target may be extracted later for genuinely shared frame/device services, but
it is not the owner of either host.

## Host responsibilities

### `3DSceneHost`

`3DSceneHost` is the current game/editor scene path. It owns or coordinates:

- Gameplay world and authored level startup;
- Render source handoff and `RenderWorld`;
- `RenderSystem` and `DeferredRenderer`;
- PBR passes, scene capture, and normal scene presentation;
- a scene presentation capability that an optional editor adapter can consume.

The host itself does not depend on Editor. The level viewport, outliner,
inspector, gizmos, and GPU debug viewer attach from the editor layer.

The host must not contain Live2D-specific registration, draw scheduling, or
preview-target policy.

### `Live2DViewerHost`

`Live2DViewerHost` is a standalone preview path. It owns or coordinates:

- Live2D asset loading and one or more `Live2DModelInstance` objects;
- Live2D update, frame extraction, mask planning, and render submission;
- a Live2D-specific output target or direct viewer backbuffer presentation;
- viewer controls, diagnostics, capture, and logging.

It must not construct or depend on:

- `GameplayWorld` for scene composition;
- `RenderWorld` or mesh-proxy source registries;
- `DeferredRenderer`, PBR materials, scene cameras, or level loading;
- the normal editor viewport, outliner, inspector, or scene gizmos.

The viewer may use a tiny private preview state containing a model, camera-like
fit transform, and input state. That state is not a general engine scene.

## Dependency direction

```text
EngineHost
  -> EngineServices
  -> selected IApplicationHost

3DSceneHost
  -> Gameplay / RenderSystem / Editor scene adapter

Live2DViewerHost
  -> Live2D runtime / Live2D render feature / Viewer UI adapter

Both hosts
  -> API-neutral Graphics/RHI, frame scheduler, RenderGraph, Asset/Resource,
     input, window, and logging services
```

`RenderSystem` becomes a scene-host collaborator, not the universal home for
every renderer. `Live2DRenderer` remains reusable GPU/render code, but its
final host is `Live2DViewerHost`, not a `RenderSystem` extension.

## Runtime modes versus editor asset sessions

The startup mode and an editor asset session are different concepts:

- `3DSceneHost` and `Live2DViewerHost` are process-level startup hosts. They
  are mutually exclusive in the first implementation.
- A future Live2D asset editor may run inside the editor process as a separate
  preview session. It may reuse `Live2DRenderer` and shared Graphics services,
  but it still must not use the level `RenderWorld`.
- A future animation editor can follow the same preview-session pattern. It is
  deliberately outside this stage.

This keeps the tiny engine simple now without preventing Unreal-like asset
editors later.

## Migration rule for the current implementation

The current `IRenderExtension` registration is a temporary validation seam.
During migration:

1. Keep it working for the existing Live2D capture while the new host is
   introduced.
2. Add explicit host selection at the application composition root.
3. Move Live2D initialization, ticking, recording, presentation, and cleanup
   behind `Live2DViewerHost`.
4. Make `RenderSystem` owned by `3DSceneHost` and remove its Live2D extension
   dependency.
5. Retain only shared API-neutral frame and Graphics contracts between the two
   hosts.

The migration must not modify DeferredRenderer pass policy to accommodate
Live2D.

## Reference mapping

Sakura provides the closest small-engine precedent: its `SkrLive2D` module has
its own renderer, while the standalone `live2d-viewer` application creates its
own window, RenderGraph, ImGui viewer, model state, and render loop. Unreal
provides the larger-editor precedent: level viewports and asset-editor
viewports use separate editor/toolkit and preview-scene contexts while sharing
engine services. KimPeanutEngine should use Sakura's process-level separation
first and leave Unreal-style asset-editor sessions for a later stage.

## Navigation

- [Concrete MODE1 design](.plan/MODE1.md)
- [Roadmap](TODO.md)
- [Render module plans](../render/PLANS.md)
- [Live2D module plans](../live2d/PLANS.md)
