# Render Overview

KimPeanutEngine’s Render module owns the policy of what to draw and how: render
world state, materials, pass scheduling, render-target policy, and requests for
RHI resources. Graphics/RHI owns backend translation, GPU objects, submission,
and synchronization.

**Current status:** Render has an API-neutral facade, pass schedule, material
system, render-source bridge, SceneColor target, and runtime capture service.
The immediate limitations are deterministic visual-regression inputs, additional
debug views, and the remaining render-graph/generalized scene work.

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
- [Render scene](render_scene/overview.md) — current scene recording boundary.
- [Material system](material_system/overview.md) — templates, instances, and
  frame-local material bindings.
- [Render capture](render_capture/overview.md) — SceneColor capture and PNG
  export path.
- [Implementation history and roadmap](render_module.md#render-roadmap) —
  preserved phase ledger while a dedicated current roadmap is established.

## Compatibility detail

[render_module.md](render_module.md) remains the pre-structure historical
narrative. Read the focused pages above first; preserve the narrative while
migrating any still-useful detail into those pages.
