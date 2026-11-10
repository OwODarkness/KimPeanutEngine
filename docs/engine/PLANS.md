# Engine Host Mode Plans

**Status: core Scene3D host in Runtime.** This document defines the engine-level composition
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
        Runtime Scene3DHost             Live2DViewerHost
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

Runtime owns the neutral host lifecycle, descriptor, and global registry.
The built-in 3D scene host belongs to Runtime's core application composition.
Optional application modes are registered by the module that provides them:

```text
engine/runtime/               RuntimeLib
  host/application_host.*     neutral lifecycle and global registry
  application/scene_3d_host.* built-in 3D scene application host
  shared runtime services
  no optional feature-host implementations

engine/runtime/render/        Render
  traditional scene renderer and RenderWorld policy
  no Live2D dependency

engine/module/live2d/         Live2D feature target / Live2DRender
  Cubism runtime, asset integration, reusable Live2D rendering,
  live2d_viewer_host.*        optional Live2D viewer host

engine/module/                ModuleBootstrap
  optional-module registration adapter used by the application boundary
```

The core registry installs the built-in `scene3d` host. Optional modules add
their registrations to the same registry. Runtime does not depend on optional
feature modules. The dependency direction is:

```text
Application startup → Runtime registry + linked optional-module registrations
Runtime core → Gameplay / Render / Graphics / Scene3DHost
Live2D feature → Runtime / Live2DViewerHost / Live2DRender → Render
Runtime        -/→ Live2D optional module
Render         -/→ Live2D
```

The shared service view is still represented by Runtime-owned services. The
`Scene3DHost` is a built-in Runtime application; `Live2DViewerHost` is
registered by the Live2D module through Runtime's neutral contract. Runtime
does not include or link optional feature modules. A separate `Runtime/Service` target may be
extracted later for genuinely shared frame/device services, but it is not the
owner of either host.

## Host responsibilities

### `Scene3DHost`

`Scene3DHost` is the current built-in game/editor scene path and belongs to
`engine/runtime/application`. It owns or coordinates:

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

Scene3DHost
  -> Gameplay / RenderSystem / Editor scene adapter

Live2DViewerHost
  -> Live2D runtime / Live2D render feature / Viewer UI adapter

Both hosts
  -> API-neutral Graphics/RHI, frame scheduler, RenderGraph, Asset/Resource,
     input, window, and logging services
```

## Host configuration and composition

Command-line parsing produces `RuntimeLaunchOptions`; it does not write
application-specific values into `Engine`. The application entry point passes
those options to the host registry, whose per-host factory builds a typed
configuration and constructs the selected host before `Engine::Initialize()`.
Engine receives and owns the completed `IApplicationHost`, then drives its
lifecycle.

`Live2DViewerHost` owns its model selection, bubble content, and standalone
capture/resize settings. Panel content is a Panel-owned value shared with the
Live2D bubble consumer; `PanelViewerHost` adds its dot colour and ramp settings.
The common standalone capture/resize value lives at the Runtime host boundary,
while each host owns the value and applies its own semantics. The startup-level
override remains on Engine for now because its asset loading and promotion
sequence has a separate ownership boundary.

Adding an application-specific startup option therefore extends its launch
options and its composition factory, not Engine's public setter/getter surface.

`RenderSystem` becomes a scene-host collaborator, not the universal home for
every renderer. `Live2DRenderer` remains reusable GPU/render code, but its
final host is `Live2DViewerHost`, not a `RenderSystem` extension.

## Runtime modes versus editor asset sessions

The startup mode and an editor asset session are different concepts:

- `Scene3DHost` and `Live2DViewerHost` are process-level startup hosts. They
  are mutually exclusive in the first implementation.
- A future Live2D asset editor may run inside the editor process as a separate
  preview session. It may reuse `Live2DRenderer` and shared Graphics services,
  but it still must not use the level `RenderWorld`.
- A future animation editor can follow the same preview-session pattern. It is
  deliberately outside this stage.

This keeps the tiny engine simple now without preventing Unreal-like asset
editors later.

## Migration result

The former `IRenderExtension` registration was a temporary validation seam.
MODE1.5 removed it after the standalone host gained initialization, ticking,
recording, presentation, capture, and cleanup ownership. `RenderSystem` now
contains no Live2D registration or feature branch. Only the API-neutral frame,
Graphics, and RenderSubmission contracts are shared between the hosts.

The migration must not modify DeferredRenderer pass policy to accommodate
Live2D.

## Frame loop and lane pacing

`Engine::Run()` executes on the main OS thread as the game lane and owns the
authoritative tick. A render lane runs on its own thread. Each frame the render
lane blocks until the game lane publishes a tick, so the two lanes are
lock-stepped rather than pipelined: the render lane cannot begin frame *N*
before the game lane finishes tick *N*, and it waits out any difference between
the two periods.

That makes the game lane's tick period a hard floor for the frame rate. Both
lanes pace with a sub-frame `sleep_for` against `target_fps`, and on Windows
`Sleep()` is rounded up to the scheduler's timer granularity — about 15.6 ms by
default. A pacing sleep of a few milliseconds therefore overshot to a tick
period near 15 ms, and the render lane inherited it, spending roughly a third of
every frame waiting on the handoff. Render work was invisible in the frame rate
because the game lane, not the renderer, was the constraint.

`Engine::Run()` holds the process at 1 ms resolution for its duration so the
pacing sleep does what it asks; the handoff wait then drops to nothing and the
render lane becomes the binding constraint.

Two consequences for future changes:

- Any sub-frame sleep on either lane's pacing path reintroduces the same trap.
  The pacing period must stay short relative to the timer granularity in force.
- Before attributing a frame-rate change to render work, check `game_wait_ms`.
  If it is non-zero, the frame rate is following the game lane and the render
  measurement is not the constraint.

## Reference mapping

Sakura provides the closest small-engine precedent: its `SkrLive2D` module has
its own renderer, while the standalone `live2d-viewer` application creates its
own window, RenderGraph, ImGui viewer, model state, and render loop. Unreal
provides the larger-editor precedent: level viewports and asset-editor
viewports use separate editor/toolkit and preview-scene contexts while sharing
engine services. KimPeanutEngine should use Sakura's process-level separation
first and leave Unreal-style asset-editor sessions for a later stage.
The Sakura repository's public Live2D Viewer description confirms a separate
standalone application, but its source tree could not be inspected through the
available source browser during the host-configuration study. The typed
factory-to-host flow above follows KimPeanutEngine's existing registry and
keeps that process-level separation without importing an unverified pattern.

## Navigation

- [Central settings architecture](../settings/PLANS.md): proposed shared
  configuration/discovery service with domain-owned settings and safe apply;
  Engine coordinates its lifetime without adding preference-specific fields.
- [Concrete MODE1 design](.plan/MODE1.md)
- [Roadmap](TODO.md)
- [Render module plans](../render/PLANS.md)
- [Live2D module plans](../live2d/PLANS.md)
