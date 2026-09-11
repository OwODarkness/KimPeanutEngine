# Engine Host Documentation Guide

Read the repository [agent contract](../../AGENTS.md), [host plans](PLANS.md),
and [host roadmap](TODO.md) before changing engine-level mode or application
composition design.

## Boundaries

- Engine composition selects one application host for a process.
- Shared services own logging, asset/resource access, graphics-device setup,
  frame scheduling, input, and window/presentation primitives.
- `3DSceneHost` owns the traditional Gameplay/Render scene stack, including
  `RenderWorld`, `DeferredRenderer`, scene capture, and a scene presentation
  capability. Editor UI attaches through a separate editor adapter.
- `Live2DViewerHost` owns the Live2D instance, Live2D render feature, viewer
  controls, and viewer presentation. It must not construct or access
  `RenderWorld`, `DeferredRenderer`, or the normal scene viewport/editor.
- Hosts may share API-neutral Graphics/RHI and future RenderGraph services;
  they do not share scene-policy objects or renderer-owned resources.
- A future Live2D asset editor is a separate host/session and is not part of
  this two-host stage.

## Documentation layout

- `PLANS.md` is the architecture map.
- `TODO.md` is the acceptance-oriented roadmap.
- `.plan/MODE1.md` is the concrete design for the first two hosts.
- `.spec/specs/` and `.spec/journal/` record implementation intent and factual
  execution evidence when implementation begins.

Do not put implementation history into `PLANS.md` or `TODO.md`.

## Validation

Host changes require validation of both startup paths. Scene mode must retain
the existing deferred/PBR and gameplay smoke evidence; Live2D viewer mode must
prove that no scene renderer is constructed and must pass OpenGL/Vulkan viewer
capture and shutdown checks.
