# Gameplay Module TODO

**Status: GP0–GP5 validation and bootstrap-actor migration landed 2026-08-28;
editor inspection deferred; GP6.1 camera data/source contract, GP6.2 local
camera traversal, GP6.3 controller/gamepad proof, and GP6.4 viewport camera
capture landed 2026-08-30. EnTT 3.16.0 dependency foundation landed
2026-09-05; GP8 light transform alignment landed 2026-09-05; Gameplay has
not migrated to ECS or reflection.**
Architecture map: [PLANS.md](PLANS.md). Detailed design:
[gameplay_module.md](gameplay_module.md).
The render-side proxy contract is already implemented; its integration ledger is
[world/mesh_proxy_TODO.md](../world/mesh_proxy_TODO.md).

**Scope rule:** build the smallest Actor/Primitive/Mesh path that can feed the
renderer. A component, source descriptor, or system enters this TODO only when
it has a current data source, a render consumer, a lifetime rule, and a test.

## Foundation — EnTT dependency

**Goal:** make a known ECS/reflection dependency available without coupling the
current Actor/component runtime to it prematurely.

- [x] Vendor the local EnTT 3.16.0 source, license, and upstream README under
  `third_party/entt/`; exclude the upstream repository metadata, build tree,
  tests, and tools.
- [x] Expose the upstream-style `EnTT::EnTT` header-only CMake target with a
  target-local C++20 requirement; keep the engine baseline at C++17.
- [x] Add a focused compile/test seam covering one registry operation and one
  reflected property read.
- [ ] Register Gameplay state through the engine-owned Reflection module after
  its catalog/access contracts and editor snapshot boundary are established.
  See the [Reflection roadmap](../reflection/TODO.md).
- [ ] Re-evaluate an ECS migration with ownership, serialization, and render
  source lifetime evidence; do not replace `GameplayWorld` as part of the
  dependency integration.

## GP0 — freeze the boundary

**Goal:** make the next implementation use one documented ownership and
threading model rather than revive the deprecated framework piecemeal.

- [x] Add the `Gameplay` CMake target at `engine/runtime/gameplay/`; it links
  Core and the narrow Render source-command interface only. It must not link
  Graphics or include a backend header. The targeted Debug build passed
  2026-08-28.
- [x] Add the module's public ownership/lifecycle contract to the Runtime and
  module documentation. Keep `deprecated/game_framework` and
  `deprecated/component` marked as history.
- [x] Define generational `ActorHandle` and an explicit Actor state enum:
  Constructed, Initialized, Active, Inactive, Destroyed.
- [x] Define a Render-owned `StaticMeshRenderableSourceDesc` and opaque registration
  token. Its payload may contain asset/material identity, transform, bounds,
  visibility, and shadow flags only; no pointers or resolved GPU handles.
- [x] Define the source command payload as a small concrete primitive variant.
  Its first and only alternative is static mesh; do not add point-cloud,
  skinned-mesh, terrain, or generic proxy types without their own consumer.
- [x] Decide and document the MP3 transitional material input: use an opaque
  material-instance identity until a serialized Material asset exists.

**Done when:** a dependency check shows Gameplay → Render's small source API,
Render → Graphics, and no dependency in the reverse direction. **Landed
2026-08-28:** `Gameplay` compiled through `Render`; Graphics stayed private to
Render and no Gameplay source/header includes a backend header.

## GP1 — World, Actor, and component ownership

**Goal:** establish authoritative gameplay lifetime before adding rendering.

- [x] Implement `GameplayWorld` as the sole owner of Actors via
  `std::unique_ptr` and generational Actor handles.
- [x] Implement actor creation, lookup, deferred destruction, and active-actor
  ticking. Reject stale handles deterministically.
- [x] Implement `Actor` as the sole owner of `std::unique_ptr<ActorComponent>`.
  Provide typed add/find APIs without external component ownership.
- [x] Implement `ActorComponent` owner binding and the one-time
  initialize/activate/deactivate lifecycle. Constructors perform no registration.
- [x] Unit-test state transitions, duplicate/add-after-activation policy,
  stale Actor handles, and world/actor destruction ordering in
  `GameplayUnitTest` (3 tests passed 2026-08-28).

**Done when:** a headless test can create an Actor, add a test component,
activate/tick/deactivate/destroy it, and prove every lifecycle callback occurs
once in the documented order. **Landed 2026-08-28.** Components may be added
only while Constructed; duplicates are allowed; lifecycle enters in insertion
order and leaves in reverse order.

## GP2 — Scene transforms and primitive state

**Goal:** give gameplay a correct transform hierarchy and common renderable
state without any RenderWorld ownership.

- [x] Implement `SceneComponent` local/world transforms, subtree dirty
  propagation, and cached world-transform evaluation.
- [x] Implement one non-recursive attach/detach operation with null/self/cycle
  rejection. Limit attachments to components on the same Actor in this phase.
- [x] Verify composition uses `parent_world * local_transform` and add
  translation/rotation/scale regression tests.
- [x] Implement `PrimitiveComponent` visible/casts-shadow flags and local
  `spatial::AABB` data plus shared dirty handling. It owns no proxy class or
  render handle.
- [x] Unit-test parent changes, detach, dirty propagation, invalid attachment,
  and world-bounds updates.

**Done when:** primitive components provide correct world transform/bounds data
independently of a renderer. **Landed 2026-08-28:** same-Actor attachments
maintain one non-recursive link, lazy cached transform evaluation composes
`parent_world * local_transform`, and primitive bounds/flags remain headless.

## GP3 — MeshComponent source production

**Goal:** make a logical mesh component the only gameplay producer for the
existing renderable-proxy system.

- [x] Implement `MeshComponent` mesh asset reference, transitional material
  reference, local bounds, and primitive flags as the first **static-mesh**
  source variant.
- [x] Inject the source bridge through `GameplayWorld`/component activation;
  do not access `global_runtime_context` or `RenderSystem` from a component.
- [x] On activation, emit exactly one create command. Coalesce transform,
  mesh/material, bounds, and flag changes into value-based updates.
- [x] On deactivation/destruction, enqueue exactly one destroy command and
  invalidate the local registration token.
- [x] RenderSystem owns pending source records and resolves ready asset/material
  data into the existing `RenderWorld::MeshProxyDesc`; it alone touches
  `RenderWorld` and resource resolver APIs.
- [x] Define pending versus failed source diagnostics. Pending has no draw;
  failed has no proxy and keeps a reason for debugging.

**Gameplay-side done 2026-08-28:** `GameplayWorld` injects the render-owned
source-sink observer at Actor construction. An active `MeshComponent` emits a
single copied static-mesh create descriptor, coalesces changed transform/mesh/
material/bounds/flags into one update per tick, and destroys its registration
once during deactivation. It owns only the opaque registration token. GP4
provides render-side resolution; runtime visual proof remains GP5/MP3 work.

## Later primitive variants

These are intentionally not implementation tasks yet:

- Point cloud: add only with a point-cloud asset/data path and a render pass
  that consumes its source description.
- Skinned mesh: add only after animation supplies a stable pose/skeleton input
  and Render owns the corresponding draw data.
- Terrain: add only after terrain data, bounds/streaming, and its render policy
  exist.

Each future variant derives common state from `PrimitiveComponent`, publishes a
new value-only source description, and receives focused unit plus smoke
coverage. It does not reinstate a `PrimitiveSceneProxy` virtual draw hierarchy.

## GP4 — engine and thread integration

**Goal:** place gameplay and render work at safe sides of the current game ↔
render frame handshake.

- [x] Construct and initialize GameplayWorld after the RenderSystem source
  bridge is available and before the game loop begins producing frames.
- [x] Tick GameplayWorld on the game thread before the frame-ready notification.
- [x] Drain/resolve source commands on the render thread in `RenderSystem`
  before `RenderWorld::ApplyPendingCommands` and snapshot creation.
- [x] During shutdown, deactivate the world, drain render destruction, then
  release render resources before window/backend teardown.
- [ ] Add assertions/logging for wrong-thread world mutation or render-source
  consumption where the existing thread IDs make that practical.

**Bridge integration landed 2026-08-28:** `RuntimeContext` owns GameplayWorld
and gives it the RenderSystem-owned source sink. GameTick updates it before the
frame-ready handoff. RenderSystem drains the mutex-protected source inbox,
keeps pending/failed records proxy-free, resolves ready sources, then applies
the resulting RenderWorld commands at BeginFrame. Runtime teardown destroys
GameplayWorld before RenderSystem drains and releases render resources.
Wrong-thread assertion/logging remains follow-up instrumentation.

## GP5 — validation and editor follow-up

**Goal:** prove the gameplay-to-render boundary under both backends before
extending gameplay scope.

- [x] Add a dedicated `GameplayUnitTest` target for GP1–GP3 headless tests.
- [x] Keep and extend `RenderWorldTest` for bridge-produced command ordering,
  stale registration rejection, and snapshot isolation.
- [x] Extend `GraphicsSmoke` to create a World/Actor/MeshComponent, move it,
  toggle visibility, destroy it, resize, and shut down on Vulkan and OpenGL.
- [ ] Add editor inspection only after the runtime lifecycle is stable; editor
  tools observe/manage gameplay state but do not become its owner.
- [x] Update status/module docs with the runtime evidence and remaining gaps.

**Validation landed 2026-08-28:** `GameplayUnitTest` covers lifecycle and
MeshComponent source commands; `RenderableSourceRegistryTest` covers pending,
ready, update, stale-token, and proxy retirement behavior. `GraphicsSmoke`
passed three frames each on Vulkan and OpenGL after creating an Actor/
MeshComponent, moving it, toggling visibility, resizing, destroying it, and
tearing down. Editor inspection remains deferred: it needs a read-only,
game-thread-safe snapshot rather than direct editor access to GameplayWorld.

## Bootstrap Actor migration

**Goal:** make the configured startup mesh use the same World/Actor/
MeshComponent path as ordinary gameplay content.

- [x] Replace RenderSystem's bootstrap-only `MeshProxy` creation with one
  logical `StaticMeshRenderableSourceDesc` containing the loaded mesh identity
  and render-owned material identity.
- [x] Transfer that value through the existing render-start synchronization,
  then create the Actor on the game thread with `CreateStaticMeshActor`.
- [x] Keep Render free of Gameplay ownership and keep Gameplay free of
  `MeshProxy`/Graphics access.

**Landed 2026-08-28:** Render startup prepares the descriptor after bootstrap
loading, `RuntimeContext` retains it until startup synchronization completes,
and `RuntimeContext::FinalizeGameStartup` creates the ordinary World-owned
Actor on the game thread. Activation publishes the normal source command;
Render resolves it at its next frame boundary.

## GP6 — camera component and local scene traversal

**Goal:** add one local free-fly camera so a player can traverse the scene while
preserving Gameplay ownership, the game/render thread boundary, and Render's
ownership of camera matrices and GPU-facing state.

**Chosen shape:** `CameraComponent` is a `SceneComponent` containing transform
and lens/view definition only. A small local `PlayerController` owns input
bindings, possession of a camera Actor, and control rotation. A Render-owned
camera source registry consumes copied values at the frame boundary. This is a
UE-like ownership seam without adding reflection, Pawn/Character inheritance,
network replication, or a second camera matrix implementation.

### GP6.1 — camera data and source contract

- [x] Add `CameraComponent` under `engine/runtime/gameplay/component/` as a
  `SceneComponent`; store FOV, near/far, projection mode, enable/priority, and
  cached world-space basis values with validation and safe defaults.
- [x] Keep `CameraComponent` free of `InputSystem`, `RenderCamera`,
  `CameraData`, `RenderWorld`, Graphics, GLFW, and native API types. Update its
  basis from `SceneComponent::OnTransformChanged()` and preserve the existing
  X-forward/Y-up/Z-back math convention.
- [x] Add a narrow Render-owned camera-source descriptor/sink/registry with
  generational identity. Register/update/unregister camera values using the
  same copied-command and lifetime rules as renderable/light sources.
- [x] Inject the camera sink through `GameplayWorld`; make `RenderSystem`
  consume one explicit active source before culling, shadow fitting, and
  deferred lighting. Keep viewport aspect and `RenderCamera` private to Render,
  with a safe default-camera fallback.

**Landed 2026-08-30:** `CameraComponent` publishes validated camera state
through `CameraSourceRegistry`; Render selects one enabled source by priority,
applies perspective or orthographic projection through its private
`RenderCamera`, and restores the bootstrap camera when no source is active.
Gameplay retains only the opaque source token. Gameplay and render contract
tests pass.

### GP6.2 — local PlayerController and free-fly composition

- [x] Add a minimal local `PlayerController` owned by the Gameplay world. It
  stores a possessed `ActorHandle`, binds/unbinds logical camera actions, and
  never owns the possessed Actor or a render object.
- [x] Add `CreateCameraActor` (or an equivalently focused factory) that
  creates an Actor with a root `CameraComponent`, initializes it, and activates
  its camera source. Keep the first controller-driven camera at the Actor root;
  do not add an untested world-transform mutation path for attached cameras.
- [x] Define `camera.move` (`Vector3f`), `camera.look` (`Vector2f`), and optional
  `camera.zoom` (`float`) actions through `InputContext`; bind keyboard/mouse
  mappings without putting GLFW constants in Gameplay.
- [x] Accumulate callbacks into a thread-safe or double-buffered InputSystem
  frame snapshot because GLFW events are currently delivered on the render
  thread while Gameplay ticks on the game thread. Consume the snapshot before
  controller/world ticking; callbacks must not mutate `CameraComponent`.
- [x] Apply movement/look on the game thread: basis-relative translation with
  `delta_time`, raw mouse delta without `delta_time`, yaw wrapping, pitch clamp
  near ±89°, and world-up vertical movement with no roll. Gamepad analog values
  use a radial dead-zone and a per-second policy.
- [x] Make context priority and lifecycle explicit so gameplay camera input
  does not interfere with the editor console/UI. Bindings are removed on
  unpossess and controller/world teardown; context switching remains an
  explicit InputSystem integration point for editor ownership.

**Landed 2026-08-30:** `GameplayWorld` owns one local `PlayerController` and
ticks it before Actors. `CreateCameraActor` creates an active root camera;
runtime startup creates the `Gameplay` context, binds keyboard/mouse logical
actions, and possesses that camera. Render-thread action callbacks enqueue
copied values into a mutex-protected `InputSystem` snapshot; game-thread
controller ticks apply movement, look, and zoom. GP6.3 adds the
platform-neutral gamepad path and deterministic smoke proof.

### GP6.3 — controller/gamepad expansion and proof

- [x] Extend Input/Window with a platform-neutral gamepad sample/event path;
  poll or translate controller state once per frame and map it to logical
  action values. Add radial dead-zone, inversion, and sensitivity tests;
  gameplay must not call GLFW gamepad APIs directly.
- [x] Unit-test CameraComponent basis/projection/attachment behavior,
  PlayerController possession and stale-handle rejection, input snapshot
  handoff, release/unbind behavior, pitch/yaw limits, and deterministic
  movement scaling.
- [x] Add camera-source registry tests for copied updates, explicit active
  selection, stale/invalid fallback, and teardown ordering.
- [x] Extend `GraphicsSmoke` with a deterministic keyboard/mouse input sequence
  that moves and looks through the bootstrap scene, changes visibility/culling,
  and captures the result on Vulkan and OpenGL. A physical gamepad is not part
  of the smoke prerequisite.
- [x] Update gameplay status/module docs with ownership, thread, input-device,
  and validation evidence after the implementation lands.

**Landed 2026-08-30:** `WindowSystem` emits a copied six-axis/fifteen-button
gamepad sample once per poll; `InputSystem` selects one active connected pad,
translates sticks/triggers/buttons to platform-neutral keys, and applies radial
dead-zone, inversion, and sensitivity policy. `PlayerController` consumes left
stick/right stick/trigger values on the game thread, while disconnect releases
buttons and zeros analog state. `InputContext` binding lookup/unbind is safe
against concurrent callback and teardown access. `GameplayUnitTest` and
`InputSystemTest` cover gamepad translation, dead-zone processing, disconnect,
stale possession, pitch clamp, snapshot handoff, and unbind behavior.
`GraphicsSmoke` drives deterministic keyboard/mouse movement and look through
the copied camera-source path and passes on Vulkan and OpenGL.

### GP6.4 — scene viewport camera capture

- [x] Add a platform-neutral `WindowSystem` mouse-capture seam. The GLFW
  implementation disables the OS cursor and enables raw mouse motion while
  capture is active, then restores normal cursor behavior on release/teardown.
- [x] Toggle capture only from the rendered scene image: the first left click
  over the viewport captures and hides the mouse; the next left click releases
  capture and stops camera control.
- [x] Keep editor/runtime/gameplay decoupled with an
  `ISceneCameraControlSink` notification. The editor records the request;
  `RuntimeContext` applies it on the game thread before `GameplayWorld::Tick`,
  where `PlayerController` consumes or discards the input snapshot.
- [x] Reset relative cursor tracking whenever the cursor mode changes so the
  first captured event cannot create a large look jump. Release capture when
  the viewport is destroyed.
- [x] Gate active input-context processing immediately on the capture
  transition, while leaving editor key listeners alive. This prevents a
  cursor/key/gamepad event from the released mode from changing the camera on
  the next game tick.
- [x] Test that disabling the local controller prevents movement, look, and
  zoom, while re-enabling it restores normal camera input.

**Landed 2026-08-30:** `EditorViewportComponent` owns only the transient UI
capture state and injected seams. It never reaches the camera Actor or calls
Gameplay directly. `RuntimeContext` owns the atomic request and applies it at
the existing game-thread boundary; the same behavior works for keyboard,
mouse, and gamepad actions.

**Done when:** a local player can possess the bootstrap free camera and use
keyboard/mouse actions to traverse the rendered scene on both backends; no
camera or controller callback touches Gameplay from the render thread; the
active camera source is copied into RenderSystem without a Gameplay pointer,
matrix object, GPU handle, or backend type crossing the boundary. Gamepad input
is an additional logical-action path, not a second camera-control API.

## GP7 — level asset and startup-scene migration

**Status: GP7 complete 2026-09-02.** The
authoritative scope, ownership, stages, reference findings, and validation plan
are in the [GP7 level-asset spec](../../.spec/specs/gameplay-level-asset.md).

- [x] GP7.1: complete the level schema and dependency-graph plan; review risks
  are resolved in [GP7.1](.plan/GP7.1.md#review-risks-resolved-2026-09-01):
  add a versioned, Asset-owned `LevelResource` and derive its model, material,
  and environment dependency edges during load. See the [GP7 journal](../../.spec/journal/2026-09-01-gameplay-level-asset.md)
  for implementation and validation evidence.
- [x] GP7.2: follow the [static-mesh instantiation and rollback plan](.plan/GP7.2.md)
  to add a Runtime-owned level instance that preflights dependencies,
  transactionally creates static-mesh Actors in `GameplayWorld`, retains
  stable authored-ID mappings, and deterministically rolls back/unloads them.
  See the [GP7 journal](../../.spec/journal/2026-09-01-gameplay-level-asset.md)
  for implementation and validation evidence.
- [x] GP7.3: follow the [lights, cameras, and environment-source plan](.plan/GP7.3.md)
  to extend the landed level transaction across every V1 Actor kind and add a
  singular value-only environment source with transactional Render resolution.
  See the [GP7 journal](../../.spec/journal/2026-09-01-gameplay-level-asset.md)
  for implementation and validation evidence.
- [x] GP7.4: follow the [startup-level migration and fixture plan](.plan/GP7.4.md)
  to close transitive material dependencies, reduce bootstrap scene policy to
  `startup_level`, migrate the PBR showcase, and create separate point- and
  spot-shadow validation levels.
- [x] GP7.5: follow the [runtime evidence and handoff plan](.plan/GP7.5.md) to
  prove failure rollback, source retirement, dependency lifetime,
  Vulkan/OpenGL smoke, and visual captures; record execution evidence in the
  GP7 journal.

**Done when:** bootstrap selects one authored level rather than containing the
scene; Asset owns level identity/dependencies, Runtime owns the level instance,
GameplayWorld owns instantiated Actors, and Render receives copied source
values only. A separate `WorldResource` remains deferred until multiple-level
composition or streaming has a concrete consumer.

## Post-GP7 — selectable startup-level override

- [x] Follow the [startup-level override plan](.plan/STARTUP_LEVEL_OVERRIDE.md)
  to add `--startup-level level/<fixture>.level`, with strict Asset path
  validation and CLI-over-Bootstrap precedence.
- [x] Extract the current ad-hoc launch parsing into a small tested options
  parser that rejects unknown, duplicate, missing, and malformed arguments.
- [x] Prove PBR, point, and spot fixture launch/capture on Vulkan and OpenGL
  without modifying `config/bootstrap.json`.

**Done when:** an agent or developer can select a startup fixture for one
process launch while Bootstrap remains the validated persistent default and
Runtime still receives only a ready Level AssetID. Live level switching remains
a separate transition/streaming design. **Landed 2026-09-02.**

## GP8 — light transform alignment before Reflection RF2

**Goal:** make light components consume the same SceneComponent transform family
that Reflection will expose for Mesh and Camera, without changing the V1 level
file schema.

- [x] Use world-transform position for point and spot sources.
- [x] Derive directional and spot source directions from world rotation with
  the Camera convention: zero rotation points along +X.
- [x] Remove duplicated light position/direction state and setters; mark light
  sources dirty from `OnTransformChanged()`.
- [x] Convert the existing level position/direction fields into Scene
  transform setters at the Gameplay factory boundary, rejecting invalid
  directions before Actor creation.
- [x] Cover attachment, world-transform changes, update coalescing, and
  invalid direction conversion in focused Gameplay tests.

**Landed 2026-09-05:** Gameplay light authoring now has one transform source;
the level loader/schema remains unchanged. This stage intentionally precedes
Reflection RF2 so light reflection can expose only color, intensity, range,
cone, enabled, and shadow properties.

## After GP6

1. Complete the proposed [GP7 level-asset migration](#gp7--level-asset-and-startup-scene-migration)
   after deferred-renderer verification so future render fixtures do not
   accumulate in bootstrap.
2. Add another focused `gameplay/factory/` helper only when it represents a
   distinct real Actor composition; keep factories outside GameplayWorld.
3. Implement Material Asset V1 from the
   [material-system M6 plan](../render/material_system/.plan/M6.md), then replace
   the transitional material reference with its asset identity.
4. Add shadow classification and transparent depth sorting to the proxy-derived
   draw lists.
5. Add shadow and G-buffer/lighting consumers.
6. Evolve the existing render pass schedule into a render graph only after those
   consumers produce real resource dependencies.
7. Evaluate physics, scripting, serialization, editor inspection, and
   cross-Actor attachment as separate designs.
