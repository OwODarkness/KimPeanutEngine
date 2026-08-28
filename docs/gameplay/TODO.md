# Gameplay Module TODO

**Status: GP0–GP5 validation and bootstrap-actor migration landed 2026-08-28;
editor inspection deferred.** Design:
[gameplay_module.md](gameplay_module.md).
The render-side proxy contract is already implemented; its integration ledger is
[world/mesh_proxy_TODO.md](../world/mesh_proxy_TODO.md).

**Scope rule:** build the smallest Actor/Primitive/Mesh path that can feed the
renderer. A component, source descriptor, or system enters this TODO only when
it has a current data source, a render consumer, a lifetime rule, and a test.

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

## After GP5

1. Add another focused `gameplay/factory/` helper only when it represents a
   distinct real Actor composition; keep factories outside GameplayWorld.
2. Implement Material Asset V1 from the
   [material-system M6 ledger](../render/material_system_TODO.md), then replace
   the transitional material reference with its asset identity.
3. Add shadow classification and transparent depth sorting to the proxy-derived
   draw lists.
4. Add shadow and G-buffer/lighting consumers.
5. Evolve the existing render pass schedule into a render graph only after those
   consumers produce real resource dependencies.
6. Evaluate physics, scripting, serialization, editor inspection, and
   cross-Actor attachment as separate designs.
