# Gameplay Module Design

Location: `engine/runtime/gameplay/`

## Status

**GP0 landed 2026-08-28.**
`Gameplay` is now a compileable Runtime target with gameplay-owned
`ActorHandle`/`ActorState` types. Render owns the public source-command
contract (`IRenderableSourceSink`), whose first primitive variant is static
mesh. The older `deprecated/game_framework/` and `deprecated/component/` code
is design history only. It establishes the desired Actor → Primitive → Mesh
vocabulary, but its ownership and rendering path must not be restored.

**GP1 landed 2026-08-28.** `GameplayWorld` now owns Actors behind generational
handles, and Actor owns components with `std::unique_ptr`. The module remains
headless.

**GP2 landed 2026-08-28.** `SceneComponent` now supports same-Actor,
acyclic attachment with lazy cached world-transform evaluation and subtree
dirty propagation. `PrimitiveComponent` contributes local bounds and
visibility/shadow state only; neither type owns a RenderWorld object, proxy,
or render registration.

**GP3 landed 2026-08-28.** `GameplayWorld` receives an optional, non-owning
`IRenderableSourceSink` observer and passes it to newly created Actors.
`MeshComponent` uses that observer only during activation, tick, and
deactivation to publish copied `StaticMeshRenderableSourceDesc` values. It
retains the returned generational registration token, coalesces dirty values
to one update per tick, and invalidates the token after one destroy request.

**GP4 bridge integration landed 2026-08-28.** `RenderSystem` owns a
mutex-protected `RenderableSourceRegistry`, which implements the source sink.
It drains copied commands at the start of `BeginFrame`, resolves ready mesh and
material inputs, and enqueues MeshProxy changes before
`RenderWorld::ApplyPendingCommands`. `RuntimeContext` owns GameplayWorld and
destroys it before RenderSystem, so active MeshComponents can enqueue source
destruction while the sink still exists.

**GP5 validation landed 2026-08-28.** The headless Gameplay and source-registry
tests cover command lifetime and proxy retirement. `GraphicsSmoke` passed on
both Vulkan and OpenGL after exercising a gameplay-created mesh's transform,
visibility, destruction, resize, and teardown path. Editor inspection is not
implemented: direct render-thread observation of the mutable game-thread World
would violate the boundary; a future read-only snapshot is required.

**GP6.1 landed 2026-08-30.** `CameraComponent` now owns transform/lens state
and publishes a copied, generational camera source. `CameraSourceRegistry`
selects one enabled source by priority on the render thread, and `RenderSystem`
applies it to its private `RenderCamera` while retaining viewport aspect and
matrix ownership. Invalid source values fall back to the bootstrap camera.
**GP6.2 landed 2026-08-30.** `GameplayWorld` owns one local
`PlayerController`; runtime startup creates an active root free camera,
installs the `Gameplay` input context, and possesses it. Keyboard/mouse
callbacks enqueue logical `camera.move`, `camera.look`, and `camera.zoom`
values into a mutex-protected frame snapshot. The controller consumes that
snapshot on the game thread and applies basis-relative movement, raw mouse
look, pitch/yaw limits, and FOV zoom. **GP6.3 landed 2026-08-30:** Window and
Input translate copied gamepad samples into logical stick/trigger/button input;
the controller consumes it on the game thread, and deterministic camera smoke
passes on Vulkan and OpenGL.

The executable implementation ledger is [TODO.md](TODO.md). The render-side
half of the boundary remains documented in
[world/component_module.md](../world/component_module.md) and
[world/mesh_proxy_TODO.md](../world/mesh_proxy_TODO.md).

## Purpose

Gameplay owns the authoritative, mutable game state: worlds, actor identity,
component ownership, transform attachment, activation, and gameplay ticking.
Render owns a copied, frame-safe representation of the subset that can be
drawn. Neither side owns the other.

```text
GameplayWorld (game thread)
  └─ Actor
      └─ SceneComponent
          └─ PrimitiveComponent
              └─ MeshComponent
                    │ logical state and dirty changes
                    ▼
             render-source command queue
                    ▼
RenderSystem (render thread)
  └─ source resolution → RenderWorld → MeshProxy snapshot → render passes

For the camera slice, the parallel control path is:

Window/GLFW events (render thread)
  → InputSystem logical action queue
  → PlayerController input snapshot (game thread)
  → CameraComponent transform/lens state
  → copied camera-source command
  → RenderSystem scene camera/culling
```

In the editor, camera control is an explicit viewport mode rather than a
permanent global input state. A left click over the rendered scene image enters
mouse capture: `WindowSystem` hides the cursor and enables relative/raw motion.
The next left click exits capture and restores the cursor. The editor only
notifies the injected `ISceneCameraControlSink`; `RuntimeContext` stores that
request atomically and applies it before the game-thread `GameplayWorld::Tick`.
`PlayerController` consumes the copied input snapshot only while enabled, so
the editor never dereferences the possessed Actor or mutates Gameplay from the
render thread. Runtime also gates active-context processing immediately, so
cursor/key/gamepad events from the released editor mode cannot become stale
camera actions; changing that gate also clears pending logical events at the
same mutex-protected boundary. Cursor tracking is reset at each mode transition
to avoid a first-frame delta spike.

This module follows Unreal-style names because they express the intended game
authoring model. It does not adopt Unreal's reflection, UObject, garbage
collection, networking, editor, or broad gameplay-framework scope.

## Tiny-engine scope

Gameplay is a small runtime layer, not a reimplementation of UE. The first
release exists to put one real mesh in a game World and move/destroy it safely
across the game-to-render boundary. It adds no reflection, property system,
prefab system, service registry, generic event bus, or ECS migration.

The component hierarchy is similarly small. `PrimitiveComponent` owns only the
state shared by drawable primitives (transform-derived bounds, visibility, and
shadow flags). Each concrete primitive publishes its own value source:

```text
PrimitiveComponent
  ├─ MeshComponent        → StaticMeshRenderableSourceDesc  (first slice)
  ├─ PointCloudComponent  → PointCloudRenderableSourceDesc  (only when needed)
  ├─ SkinnedMeshComponent → SkinnedMeshRenderableSourceDesc (after animation)
  └─ TerrainComponent     → TerrainRenderableSourceDesc     (after terrain data)
```

These are source-data variants, not a restored `PrimitiveSceneProxy` class
hierarchy. Add a variant only when its CPU data, render pass, lifetime, and
validation consumer exist. In particular, `MeshComponent` means a static mesh
in the first implementation; it must not silently become a skinned-mesh or
terrain abstraction.

## Authoring helpers

`gameplay::CreateStaticMeshActor(world, StaticMeshActorDesc)` is the first
convenience composition helper. It lives under `gameplay/factory/`, validates
the supplied mesh/material/bounds values, builds an Actor with one root
`MeshComponent`, then initializes and activates it. The factory contains that
composition policy; `GameplayWorld` still only owns, finds, ticks, and destroys
Actors. Do not introduce a factory registry or ActorFactory inheritance tree
unless a second concrete construction policy needs shared dispatch.

## Design question and local constraints

**Question:** How can an Actor/Primitive/Mesh hierarchy produce visible meshes
without recreating the deprecated direct component-to-OpenGL path?

Existing constraints:

- `RenderSystem` owns `RenderWorld`; its `MeshProxy` records hold resolved
  `graphics::MeshHandle` and `render::MaterialInstanceHandle` values.
- `RenderWorld` already safely accepts queued create/update/destroy commands,
  applies them at the render-frame boundary, and exposes immutable snapshots.
- Game and render work run on different threads. A gameplay component cannot
  share a mutable proxy or an RHI resource with render work.
- Asset owns identity and CPU data; Render resolves ready asset data into
  render resources; Graphics owns GPU lifetime and API execution.

The deprecated implementation violated these constraints: `PrimitiveComponent`
owned a `PrimitiveSceneProxy`, `MeshComponent` owned render objects and
performed LOD work, and `MeshSceneProxy` bound VAOs and issued `gl*` calls.

## Ownership and dependency rules

| Concern | Owner | Must not contain |
|---|---|---|
| Actor identity, components, transforms, gameplay visibility | Gameplay | `Vk*`, `gl*`, command recording, GPU handles |
| Source create/update/destroy values | Gameplay → Render bridge | component/proxy pointers |
| Source readiness and asset-to-render conversion | Render | Actor ownership or gameplay ticking |
| `MeshProxy`, draw-list generation, culling, material instances | Render | mutable gameplay objects |
| GPU objects, synchronization, API translation | Graphics/RHI | gameplay policy |

`Gameplay` may depend on a small, Render-owned public command interface. It
must not include backend headers or call `RenderWorld` directly. `Render` must
not depend on `Gameplay`; `RenderSystem` receives source values only.

GP0 establishes this in CMake: `Gameplay` links `Core` and `Render`; `Graphics`
remains a private dependency of `Render`. `ActorHandle` and `ActorState` live
under `gameplay/`, while `IRenderableSourceSink`, `RenderableSourceHandle`, and
the source descriptor variant live under `render/`.

GP3 injects the interface at `GameplayWorld` construction. The World and
Actors observe it but do not own it; its lifetime must therefore outlive the
GameplayWorld. This is deliberately a small testable seam until GP4 connects
the engine's game and render loops.

GP4 makes that seam concrete. The source registry allocates and invalidates
`RenderableSourceHandle`s under its command mutex, but only the render thread
owns source records and optional `RenderableHandle`s. Pending and failed
records own no proxy; a pending/failed update retires an earlier proxy, while a
ready value creates or updates one at the render-frame boundary.

## Core types and lifecycle

The first implementation is deliberately small:

```text
GameplayWorld
  └─ Actor
      ├─ ActorComponent
      └─ SceneComponent
          └─ PrimitiveComponent
              └─ MeshComponent
```

- `GameplayWorld` owns `std::unique_ptr<Actor>` objects behind generational
  `ActorHandle`s. It performs spawn/destruction at a defined world boundary and
  ticks active actors on the game thread.
- `Actor` owns all components with `std::unique_ptr`. It has one optional root
  `SceneComponent`; its actor transform is the root's world transform.
- `ActorComponent` has a non-owning Actor pointer and explicit lifecycle hooks.
  Constructors establish local values only; no system registration occurs there.
- `SceneComponent` holds a local transform, cached world transform, dirty flag,
  non-owning parent pointer, and non-owning child links. The Actor remains the
  memory owner of every component in the first slice.
- `PrimitiveComponent` adds visible/casts-shadow flags and local bounds. It is a
  semantic gameplay base, not a proxy base class, and provides common dirty
  handling for concrete source producers.
- `MeshComponent` adds logical mesh and material references. It computes
  world bounds from the shared `spatial::AABB` value and emits the first,
  static-mesh source update.

Use this lifecycle:

```text
Constructed → Initialized → Active ↔ Inactive → Destroyed
```

Initialization happens once. Activation/deactivation may repeat. Components
must deactivate before their actor or world releases them. The first module has
one regular `Tick`; it does not introduce UE-style tick groups, replication,
reflection, or a task scheduler.

GP1 policy is explicit: components may be added only while an Actor is
Constructed; duplicate component types are allowed; initialization and
activation run in insertion order; deactivation runs in reverse insertion
order. `DestroyActor` immediately invalidates its handle and deactivates an
active Actor, while `GameplayWorld::Tick` reclaims its owned storage at the
world boundary.

## Transform attachment rules

`SceneComponent::AttachTo` is the single operation that changes an attachment.
It rejects null/self parents and parent-child cycles, removes the old parent
link, updates both new links once, then marks the subtree dirty. `Detach` is
its inverse. No method may call the other recursively to add the same link.

World transforms are derived as `parent_world * local_transform`; root
components use their local transform. A local transform change dirties the
component and descendants. The update pass recomputes only dirty transforms,
then `PrimitiveComponent` subclasses derive world bounds and render-source
updates from the result.

Cross-Actor attachments are deferred. In the first slice, a SceneComponent may
attach only to another component owned by the same Actor. This avoids a second
ownership/lifetime graph before level streaming and actor destruction policies
exist.

## Gameplay-to-render source bridge

The existing `RenderWorld::EnqueueCreate` takes a resolved `MeshProxyDesc`, so
it is intentionally unsuitable for gameplay. Add a separate Render-owned
source interface whose descriptors contain only logical values. The interface
accepts a small `std::variant` of concrete primitive source descriptions; its
only initial alternative is static mesh:

```cpp
struct MeshRenderableSourceDesc
{
    asset::AssetID mesh_asset;
    asset::AssetID material_asset;
    Transform3f world_transform;
    spatial::AABB world_bounds;
    RenderableFlags flags;
};
```

The variant is extended for point clouds, skinned meshes, terrain, or another
primitive only with a corresponding render resolver and pass consumer. Do not
introduce a generic virtual proxy or empty component subclasses merely to make
the list look complete.

`material_asset` is a serialized authoring identity. Render resolves it to a
private shared template plus default instance; Gameplay never receives either
render handle. Per-Actor parameter overrides remain deferred until they have a
separate copied override-value contract.

The RenderSystem implementation owns source registration tokens, tracks
pending source records, resolves ready mesh/material resources, and only then
enqueues a resolved `MeshProxyDesc` into `RenderWorld`. Gameplay stores at most
an opaque registration token for update/destroy; it never stores a
`MeshProxy*`, `graphics::MeshHandle`, or backend object.

```text
MeshComponent Activate/dirty/deactivate
  → Create/Update/Destroy MeshRenderableSourceDesc
  → RenderSystem source registry
  → resolve asset + material readiness
  → RenderWorld queued MeshProxy command
  → apply at render-frame boundary
```

An unresolved mesh or material remains pending and produces no draw. This is a
normal state, not a component failure. Resolution failure exposes a diagnostic
to the owning source record; it must not create a partially valid proxy.

## Threading and teardown

`GameplayWorld::Tick` runs on the game thread before its frame-ready signal.
It publishes only copyable source commands. On the render thread,
`RenderSystem::BeginFrame` drains and resolves source commands before it calls
`RenderWorld::ApplyPendingCommands`; scene passes then consume snapshots only.

World teardown is ordered:

```text
stop game ticking
→ deactivate actors/components and enqueue source destroys
→ render thread drains source and RenderWorld destroys
→ destroy material/resource caches and backend
→ destroy window/context
```

The source queue and generational tokens make a component's game-thread
destruction safe even when the matching render proxy retires a frame later.

## Camera and local scene traversal (GP6.1–GP6.3 landed)

### Problem and boundary

The deprecated implementation combined two responsibilities: its
`CameraComponent` derived view/projection data from a scene transform, while a
`Camera` Actor also bound WASD/QE, mouse delta, and scroll input. That code is a
useful behavior reference, but restoring it would reintroduce the old shared
scene hierarchy and direct input/render coupling. The current `SceneComponent`
already supplies same-Actor transforms and dirty propagation; the current
`RenderSystem` owns `scene_camera_`, performs viewport-aspect updates, and uses
the camera for culling and deferred-light reconstruction.

GP6 therefore separates the responsibilities:

```text
GameplayWorld (game thread)
  ├─ local PlayerController   — input, possession, control rotation
  └─ camera Actor
       └─ CameraComponent      — SceneComponent + lens/view definition

RenderSystem (render thread)
  └─ CameraSourceRegistry → selected source → existing RenderCamera
```

`PlayerController` is intentionally a small KimPeanut gameplay object, not a
UE-sized replicated `APlayerController`. It owns a non-owning input-system
seam, one possessed `ActorHandle`, and local control state. The camera Actor
remains owned by `GameplayWorld`; the controller only selects and drives it.
Pawn/Character, server authority, replication, and network prediction are
separate later designs.

### `CameraComponent` responsibilities

`CameraComponent` derives from `SceneComponent` and owns only camera-authored
state:

- perspective projection parameters: FOV, near plane, and far plane;
- perspective or orthographic projection mode;
- camera basis queries derived from the world transform, with a stable world-up
  convention and no roll in the first free-fly mode;
- enabled/priority or an equivalent local selection flag used by the active
  camera policy.

It must not bind input, know about `InputSystem`, build GPU-facing `CameraData`,
hold a `RenderCamera*`, or expose a native graphics object. Transform changes
invalidate its cached basis through `SceneComponent::OnTransformChanged()`.
The render side derives view/projection matrices from the copied transform and
lens values, preserving the existing matrix and depth conventions in
`RenderCamera`.

The first factory creates a root camera Actor so local transform equals world
transform. Attached cameras remain supported by the existing same-Actor
attachment rules, but controller-driven world-space movement should not gain a
second transform ownership path until a tested world-transform setter exists.

### Camera source handoff and active selection

The Render-owned `ICameraSourceSink`/`CameraSourceRegistry` contract is now
parallel to the existing renderable and light source bridges. Its value-only
descriptor contains world transform, projection/lens values, enabled state,
and explicit priority; the registry supplies the generational source identity.
Create/update values are validated before entering the queue. It contains no
`CameraData`, `RenderCamera`, RenderWorld pointer, or GPU handle.

`GameplayWorld` injects the non-owning sink at construction. A camera component
registers on activation, emits a copied create/update value, and unregisters
exactly once on deactivation/destruction. `CameraSourceRegistry` consumes the
source queue at the frame boundary and selects the highest-priority enabled
source, with source ID as the deterministic tie-break. `RenderSystem` applies
that value before visibility, shadow fitting, and deferred lighting. RenderSystem
owns aspect from the active frame extent, so resize remains a render concern.
If no source is selected, RenderSystem restores the bootstrap/default camera
without reading Gameplay objects on the render thread.

The first implementation supports one local active camera. Multiple cameras,
camera cuts, viewports, render-to-texture, and editor camera ownership require
an explicit selection/viewport contract and are not hidden inside registration
order.

### Input and controller flow

The current InputSystem already translates GLFW key, cursor, mouse-button, and
scroll events into `InputContext` actions, but those callbacks execute during
`RenderThreadFunc`'s `PollEvents()`. `GameplayWorld::Tick()` runs on the game
thread before the render thread consumes the frame. A camera action callback
must therefore append to a mutex-protected frame input queue or double-buffered
snapshot; it must never call `CameraComponent` setters directly. `GameTick()`
consumes the completed snapshot before ticking the controller/world.

The logical first mapping is:

| Action | Value | Keyboard/mouse | Gamepad |
|---|---|---|---|
| `camera.move` | `Vector3f` | W/S forward, A/D strafe, E/Q vertical | left stick XY, left/right triggers for vertical |
| `camera.look` | `Vector2f` | cursor delta | right stick XY |
| `camera.zoom` | `float` | mouse wheel, optional | omitted in the first gamepad slice |

Gameplay binds action identity, not GLFW constants. `PlayerController` retains
binding handles and removes them before deactivation; a dedicated gameplay
context must not steal editor-console/UI input. The controller accumulates
actions and applies them in its game-thread tick: movement is basis-relative and
scaled by `delta_time`, raw mouse delta is not multiplied by `delta_time`, and
gamepad stick input is dead-zoned and treated as a per-second rate. Yaw wraps,
pitch clamps to a safe range such as `[-89°, 89°]`, and the first mode keeps
world-up vertical movement to avoid roll drift.

“Controller” has two meanings that must stay separate in the design. The local
`PlayerController` is the UE-like gameplay ownership seam; a gamepad is merely
an `InputDevice`. Gamepad polling/events belong in Input/Window platform code,
then arrive as logical action values. The GLFW backend emits a copied sample
rather than exposing `GLFWwindow` or a native controller type to Gameplay.
`InputSystem` uses one active connected pad, converts the six raw axes and
button states into stable `InputKey` values, and emits zero/release transitions
on disconnect. Stick processing is radial: values inside the configured dead
zone become zero, the remaining range is remapped to `[0, 1]`, then sensitivity
and optional Y inversion are applied. Raw input triggers are local and are not
replicated; a future networked controller would replicate validated movement or
view state according to a separate networking design.

### Lifecycle, failure handling, and validation

- Camera source registration occurs only after component initialization and is
  retired before the Actor/World releases the component.
- Controller bindings are installed on activation/possession and removed on
  unpossess/deactivation, including shutdown and input-context changes.
- A missing camera, invalid lens range, stale source handle, or absent input
  snapshot is a safe no-op/fallback, never a render-thread dereference of a
  Gameplay object.
- GP6.1 headless tests cover camera basis/lens validation, copied source
  lifecycle, priority selection, stale handles, and registry clearing.
- GP6.2/GP6.3 headless tests cover copied input snapshots, active free-camera
  factory composition, controller possession and stale handles, deterministic
  movement, mouse look, FOV zoom, pitch clamping, gamepad sampling,
  disconnect-zeroing, dead-zone policy, and binding teardown.
- GP6.4 headless coverage verifies the controller gate; the live editor path
  verifies viewport click-to-capture, hidden cursor/raw motion, click-to-release,
  and camera inactivity after release.
- `GraphicsSmoke` feeds a deterministic W/release/mouse sequence through the
  same input and camera-source path used by runtime startup, then renders the
  scene on Vulkan and OpenGL. The smoke does not require a physical gamepad.

### Reference findings

The repository reference index had no camera/input study, so the discovery gate
searched and read current source from Godot, O3DE, and Bevy at the revisions
linked below:

- [O3DE `CameraComponentController.h`](https://github.com/o3de/o3de/blob/d2faa51802b1c0dccb2e02b4b67e3fe3306e9cf3/Gems/Camera/Code/Source/CameraComponentController.h)
  keeps lens/view requests and transform notifications in a camera controller,
  with activation/deactivation as explicit lifecycle boundaries. KimPeanut
  adopts the separation and lifecycle intent, not O3DE buses, reflection, or
  render-pipeline ownership.
- [O3DE `FlyCameraInputComponent`](https://github.com/o3de/o3de/blob/d2faa51802b1c0dccb2e02b4b67e3fe3306e9cf3/Gems/AtomLyIntegration/AtomBridge/Code/Source/FlyCameraInputComponent.cpp)
  is a separate input component: it accumulates mouse, keyboard, and gamepad
  values, then applies movement/rotation during tick. That supports the
  controller/input split and accumulation rule here; its event buses and
  platform-specific device code do not transfer.
- [Godot `Camera3D`](https://github.com/godotengine/godot/blob/5ec4857b340b6284a18b49b2eda462bd250f219a/scene/3d/camera_3d.h)
  is a transform-bearing camera with explicit projection parameters and an
  active-camera/viewport selection policy. KimPeanut adopts the explicit
  active-view concept while retaining RenderSystem ownership of the backend
  camera and frame extent.
- [Bevy `Camera3d`](https://github.com/bevyengine/bevy/blob/227d3a6c661b3bdf3020d3e8290b5a6ffd0226f7/crates/bevy_camera/src/components.rs)
  and its [first-person example](https://github.com/bevyengine/bevy/blob/227d3a6c661b3bdf3020d3e8290b5a6ffd0226f7/examples/camera/first_person_view_model.rs)
  keep camera/projection data composable and distinguish raw mouse delta from
  time-scaled analog input, with a safe pitch limit. KimPeanut adopts those
  narrow math/input rules, not Bevy's ECS or schedule model.

The bootstrap scene follows the same ownership rule. Render startup loads the
configured mesh and creates its render-owned material identity, then transfers
one `StaticMeshRenderableSourceDesc` through the existing startup handshake.
After that handshake, `RuntimeContext::FinalizeGameStartup` creates the actor
on the game thread with `CreateStaticMeshActor`; there is no bootstrap-only
`MeshProxy` owned by `RenderSystem`.

## Reference findings

- [O3DE Entity](https://github.com/o3de/o3de/blob/development/Code/Framework/AzCore/AzCore/Component/Entity.h)
  establishes a useful narrow rule: an entity initializes once, may activate
  and deactivate repeatedly, and destroys components only after deactivation.
  KimPeanut adopts the lifecycle ordering, not O3DE's reflection/services.
- [O3DE Component](https://github.com/o3de/o3de/blob/development/Code/Framework/AzCore/AzCore/Component/Component.h)
  distinguishes construction from activation. KimPeanut components likewise do
  no registration or cross-system lookup from constructors.
- [Piccolo Level](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/runtime/function/framework/level/level.h)
  keeps stable game-object ownership in the level. Its value is the World owns
  actor identity and tick order, not its reflection machinery.
- [Piccolo MeshComponent](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/runtime/function/framework/component/mesh/mesh_component.cpp)
  publishes dirty mesh values through a render swap context. KimPeanut adopts
  the copied handoff, but rejects the component's direct global RenderSystem
  and asset-manager access because they would cross the Asset/Render boundary.
- [Piccolo RenderSwapContext](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/runtime/function/render/render_swap_context.h)
  separates logic-produced render descriptions from render-side consumption.
  KimPeanut adopts that copied-command boundary but uses a mutex-protected,
  RenderSystem-owned registry instead of Piccolo's global swap context.

## Non-goals for the first module

- ECS replacement or conversion.
- A UE-sized gameplay framework, reflection/property system, prefab system, or
  generalized component-service architecture.
- Reflection, serialization, prefabs, level-file loading, or editor inspectors.
- Broad input/remapping infrastructure, scripting, networking, replication,
  physics, and gameplay abilities. GP6's local camera action snapshot is a
  deliberately narrow InputSystem consumer, not a general input framework.
- Cross-Actor attachment, world partition, streaming, LOD, or occlusion.
- A general render graph or proxy class hierarchy.

Those features should follow only once the Actor/Primitive/Mesh lifecycle and
render-source boundary have runtime evidence.
