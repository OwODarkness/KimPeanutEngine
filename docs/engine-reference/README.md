# Engine Reference Index

This directory contains durable design knowledge extracted from open-source engine studies. The documents describe transferable patterns and trade-offs for KimPeanutEngine; they are not source-import instructions.

## Reference matrix

| Design question | Reference engine | Repository / branch | KimPeanutEngine mapping |
|---|---|---|---|
| RHI backend abstraction and render graph | Sakura Engine | `SakuraEngine/SakuraEngine`, `engine` | `engine/runtime/graphics`, `engine/runtime/render` |
| ECS, reflection, and core containers | Sakura Engine | `SakuraEngine/SakuraEngine`, `engine` | runtime core and future world model |
| Editor and tool UI | Sakura Engine | `SakuraEngine/SakuraEngine`, `engine` | `engine/editor` |
| Asset → resource → RHI pipeline | Piccolo | `BoomingTech/Piccolo`, `main` | `engine/runtime/asset`, `core/resource`, `render` |
| Cross-API backend contracts | bgfx | `bkaradzic/bgfx`, `master` | `engine/runtime/graphics/backend/common` |
| Production rendering and shader/resource systems | Godot | `godotengine/godot`, `master` | graphics, resource, and editor boundaries |
| Render-system and shader-manager abstractions | Ogre | `Ogre3D/ogre`, `master` | render/resource ownership |
| Compact graphics and resource cache | Urho3D | `urho3d/urho3d`, `master` | asset and graphics scope control |
| ECS and scene as an alternative paradigm | Bevy | `bevyengine/bevy`, `main` | future gameplay/world architecture |
| Vulkan-first runtime rendering and validation | gkNextEngine | `gameknife/gkNextEngine`, `main` | modern renderer direction and validation workflow |

## Reference conclusions

- [Sakura graphics reference](../graphics/sakura_reference.md) — backend-independent device, render-graph, and cache boundaries.
- [gkNextEngine rendering reference](../graphics/gknext_reference.md) — Vulkan-first rendering, GPU-driven techniques, runtime/editor integration, and validation.
- [RHI design material](../graphics/rhi_design_material.md) — critical synthesis of RHI abstraction trade-offs.

## Study rules

1. Pin the question to one subsystem before studying another engine.
2. Read the reference repository's actual source, preferably headers first, and record the branch or revision studied.
3. Extract the design pattern and its trade-offs; do not copy implementation or assume the reference's constraints fit KimPeanutEngine.
4. Map the result to a concrete KimPeanutEngine module, owner, and data flow.
5. Record the conclusion in `docs/`, then update the affected design ledger or TODO.

Sakura and Piccolo remain the closest architectural references. gkNextEngine is the closest reference for Vulkan-first runtime rendering, modern GPU submission, complete render workflows, and validation. bgfx remains useful for cross-API contract questions.
