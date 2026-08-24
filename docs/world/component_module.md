# World Components and Renderable Proxies

**Status: future reconstruction plan only (2026-08-24).** There is no active
`world` or component runtime module yet. The archived implementation under
[`deprecated/component/`](../../deprecated/component/) and
[`deprecated/render/`](../../deprecated/render/) is design history, not code to
restore unchanged.

## Purpose

The world/component side builds scene state. The render side consumes a
render-thread-safe representation of the subset that can be drawn. A component
must never become a GPU object or record API commands itself.

```text
world / Actor
  └─ PrimitiveComponent / MeshComponent       mutable gameplay state
       └─ queued create, update, destroy request
            └─ RenderSystem / RenderWorld     owns renderable registry
                 └─ MeshProxy frame snapshot  render-ready data
                      └─ cull + build draw lists
                           └─ render passes / common RHI recording
```

This is the useful part of Unreal's primitive-proxy pattern: game objects and
their transforms stay on the world side; rendering reads a copied, stable
proxy. It is not a request to copy Unreal's class hierarchy or its renderer.

## Ownership and boundary

| Concern | Owner | Must not contain |
|---|---|---|
| Actor/component hierarchy, transform mutation, world visibility | future world module | `Vk*`, `gl*`, command recording, GPU objects |
| Renderable registration commands | boundary from world to render | raw proxy pointers shared across threads |
| `MeshProxy`, `RenderableHandle`, registry, frame snapshot | render module | Actor/component ownership or direct asset loading |
| Mesh/material GPU handles, draw-list sorting, culling | render module | native Vulkan/OpenGL objects exposed upward |
| Layout transitions, native resource allocation, submissions | graphics module | gameplay/component policy |

`MeshComponent` owns logical references such as mesh/material asset identity,
local/world transform, and world-state flags. `RenderSystem` resolves those into
render-ready common handles after resource preparation. A world component
does not retain `graphics::MeshHandle`, `VulkanContext`, or a `MeshProxy*`.

## Target `MeshProxy` shape

The first proxy should be a concrete data record, not a `PrimitiveProxy` base
class. Extract a base/variant only when static mesh, skinned mesh, terrain,
particles, decals, or another concrete category actually need shared behavior.

```cpp
struct MeshProxy
{
    RenderableHandle handle;       // render-owned generational identity
    graphics::MeshHandle mesh;     // render-private resolved resource
    MaterialInstanceHandle material; // temporary alias; future render material identity

    Transform3f world_transform;
    AABB world_bounds;
    RenderableFlags flags;         // visible, opaque, casts_shadow, etc.
};
```

`AABB` and `MaterialInstanceHandle` are temporary aliases in
[`mesh_proxy.h`](../../engine/runtime/render/render_world/mesh_proxy.h) until
the World bounds type and render material-instance system exist. The invariants
are not:

- no `Draw()` virtual function on the proxy;
- no shader, VAO, descriptor, `Vk*`, or `gl*` field;
- no direct game-thread mutation while rendering reads it;
- proxy lifetime belongs to the render registry, addressed through a
  generational `RenderableHandle`.

The deprecated `MeshSceneProxy` is useful evidence for the desired transform,
bounds, visibility, and registration behavior, but its `Draw()` /
`DrawGeometryPass()` OpenGL ownership must not return. See
[`mesh_scene_proxy.cpp`](../../deprecated/render/mesh_scene_proxy.cpp) and
[`mesh_component.cpp`](../../deprecated/component/mesh_component.cpp).

## Thread and frame model

The engine has game and render work, so the boundary is a queue plus a render
snapshot, not shared mutable state:

```text
world thread: CreateRenderable / UpdateTransform / UpdateMaterial / Destroy
                          ↓ queued commands
render frame boundary: apply commands → update registry → build snapshot
                          ↓
ScenePass / ShadowPass / later graph passes read the snapshot only
```

Destroy must be deferred until the render thread has consumed all work that
references the proxy. `RenderableHandle` generation validation rejects stale
updates and stale destroys.

## Relationship to the render graph

Mesh proxies solve **what can be drawn**. A render graph solves **which passes
run and which resources they consume/produce**. The graph should be built after
the proxy registry can feed at least a scene pass and a shadow/deferred pass:

```text
MeshProxy registry → culling / DrawCallList
                   ├─ ShadowPass
                   └─ Scene/GBufferPass
                         → later: lighting, post-process, editor composite
```

This ordering prevents a graph from being designed around a one-off bootstrap
scene rather than real renderable inputs.

## Future work

The executable task ledger is [mesh_proxy_TODO.md](mesh_proxy_TODO.md). It
starts with the render-owned proxy contract, then reconstructs world
components as producers only after that contract is tested.
