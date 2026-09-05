# Render Overview

KimPeanutEngine’s Render module owns the policy of what to draw and how: render
world state, materials, pass scheduling, render-target policy, and requests for
RHI resources. Graphics/RHI owns backend translation, GPU objects, submission,
and synchronization.

**Current status:** Render has an API-neutral facade, one typed fixed pass
sequence, material system, prepared-asset catalog, render-source bridge,
SceneColor target, runtime capture service, and an address-stable scene
coordinator. Editor presentation uses a typed Graphics bridge and borrowed
target view; the editor keeps Scene Color as its 80%-width main image and
previews the Render-owned G-buffer and shadow conversion views in a separate
20%-width Debug Viewer window through that same seam. Later limits include
stronger
deterministic visual regression and any independently justified render-graph/
generalized-scene work.

```text
Gameplay source values → Render source sink → RenderWorld / MeshProxy
                                         → passes / materials / PipelineDesc
                                         → Graphics RHI → API backend
```

## Document map

- [Design](design.md) — render ownership, frame policy, pass scheduling, and
  the `PipelineDesc` seam.
- [Lifecycle](lifecycle.md) — initialization, frame work, source updates,
  capture completion, and teardown.
- [Dependencies](dependencies.md) — allowed module directions and forbidden
  backend/resource edges.
- [Risks](risks.md) — known limits, validation gaps, and follow-up conditions.
- [Usage](usage.md) — runtime capture/debug workflow and validation evidence.
- [Module plans](PLANS.md) — Render architecture and submodule plan map.
- [Module TODO](TODO.md) — Render-wide roadmap and submodule index.
- [Agent guide](AGENTS.md) — documentation and implementation boundaries.
- [Render scene](render_scene/overview.md) — current scene recording boundary.
- [Material system](material_system/overview.md) — templates, instances, and
  frame-local material bindings. Its plans and roadmap are [here](material_system/PLANS.md)
  and [here](material_system/TODO.md).
- [Render capture](render_capture/overview.md) — SceneColor capture and PNG
  export path. Its plans and roadmap are [here](render_capture/PLANS.md) and
  [here](render_capture/TODO.md).
- [Deferred PBR](deferred_pbr/PLANS.md) — PBR architecture, stage plans, and
  roadmap.
- [Implementation history](render_module.md#render-roadmap) — preserved legacy
  narrative; current plans and TODOs live in the module/submodule indexes.

## Compatibility detail

[render_module.md](render_module.md) remains the pre-structure historical
narrative. Read the focused pages above first; preserve the narrative while
migrating any still-useful detail into those pages.
