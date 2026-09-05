# Reflection Module TODO

**Status: RF3 implementation landed (2026-09-05); native MSVC validation is
environment-blocked.** EnTT 3.16.0 is vendored, the engine
Reflection target owns the RF1/RF2 contracts and frozen catalog, Gameplay
supplies a behavior-preserving registration satellite, and Runtime now owns a
bounded value-only Gameplay editor bridge. The World Outliner and Actor panel
remain future stages.

Architecture and decisions: [PLANS.md](PLANS.md). Cross-stage acceptance:
[Runtime Reflection Module spec](../../.spec/specs/runtime-reflection-module.md).

## RF1 — contracts and EnTT adapter

- [x] Create `engine/runtime/reflection/` and a dedicated Reflection CMake
  target; link EnTT privately or through the smallest implementation-facing
  target boundary.
- [x] Define engine-neutral IDs, values, descriptors, diagnostics,
  `IReflectionCatalog`, and game-thread-only `IReflectionAccess` without EnTT,
  ImGui, Gameplay, Render, Graphics, or Asset types in the core contracts.
- [x] Add `EnttReflectionRegistry` with an explicitly owned `entt::meta_ctx` and
  a thin `EnttReflectionRegistrar` for module registration.
- [x] Implement deterministic lifecycle states, duplicate/collision rejection,
  catalog freeze, immutable enumeration, idempotent shutdown, and rollback of
  failed initialization.
- [x] Add focused tests for registration, names/IDs, descriptors, read/write,
  read-only properties, conversion errors, duplicate/collision behavior,
  freeze rejection, isolated contexts, and shutdown.

Detailed design: [RF1 plan](.plan/RF1.md). Formal review:
[RF1 review](.review/RF1.md) — resolved on 2026-09-05.

**Done (2026-09-05):** a headless test registers a local test type through the EnTT
registrar, obtains only engine descriptors through `IReflectionCatalog`, reads
and writes it through `IReflectionAccess`, and proves no registration can
change the frozen catalog.

## RF2 — Gameplay registration

Detailed design: [RF2 plan](.plan/RF2.md). Formal review:
[RF2 review](.review/RF2.md) — changes requested on 2026-09-05.

- [x] Add module-owned Gameplay registration units; do not centralize Gameplay
  type knowledge inside Reflection.
- [x] Register the minimum reusable value types and selected properties of
  `SceneComponent`, mesh, directional/point/spot light, and camera components.
- [x] Route stateful properties through public getters/setters so transform
  dirtiness, validation, activation, and render-source publication remain
  correct.
- [x] Define neutral metadata for display name, category, read-only state,
  numeric range/step, tooltip, and widget semantic.
- [x] Test registration coverage, supported value conversion, setter rejection,
  and observable component side effects on the game thread.

**Implementation landed (2026-09-05):** RF2 adds owned descriptor metadata,
static/free-function adapters, the Gameplay-owned `GameplayReflection` target,
Runtime startup/teardown composition, and focused Reflection and Gameplay
tests. MinGW syntax checks and the 8-test Reflection executable pass. The
Visual Studio focused build is still blocked before compilation by the local
Windows SDK permission failure; the Gameplay test therefore remains pending
native execution evidence.

### Further RF2 work — extensible module contribution

- [ ] After direct Gameplay registration is proven, add a sealed
  `ReflectionRegistrationSet` that collects uniquely named module callbacks in
  deterministic bootstrap order and rejects late or duplicate contributions.
- [ ] When a real module-loader participant needs it, add
  `IReflectionContributor` so modules contribute callbacks without exposing a
  virtual template registrar or changing their existing
  `Register<Module>Reflection` functions.
- [ ] Prove two-module aggregation, collection sealing, duplicate rejection,
  and full initialization rollback when any contributed callback fails.

This follow-up does not permit post-freeze registration or module hot reload;
those require generation-aware catalog lifetime work in RF5.

**Done when:** Runtime can inspect and safely mutate one representative
transform, mesh, light, and camera property without Editor or direct member
access, while existing Gameplay lifecycle and render-source tests still pass.

## RF3 — Gameplay editor bridge

Detailed design: [RF3 plan](.plan/RF3.md). Formal review:
[RF3 review](.review/RF3.md) — resolved on re-review 2026-09-05.

- [x] Introduce stable component-instance identity compatible with duplicate
  component types and Actor destruction/reclamation.
- [x] Publish bounded immutable Actor snapshots at a game-thread boundary.
- [x] Define value-only property-edit commands and a bounded queue from Editor
  to Runtime/Gameplay.
- [x] Revalidate actor generation, component identity, property identity,
  reflected type, access flags, and value conversion on the game thread.
- [x] Report accepted/rejected edit results without exposing object pointers.
- [x] Test stale actors/components, destruction races, duplicate component
  types, queue bounds, invalid values, and shutdown cancellation.

**Done when:** a render-thread-style test can retain a copied snapshot, enqueue
an edit while Gameplay continues owning the object, and observe either the
validated new value or a deterministic rejection in the next snapshot/result.

**Implementation landed (2026-09-05):** RF3 adds per-Actor component instance
IDs, an exact bidirectionally validated GameplayReflection binding manifest,
bounded immutable snapshots with string/value byte accounting, game-thread edit
application with readback, request backpressure, expanded rejection coverage,
and Runtime startup/tick/shutdown integration. The final manually linked MinGW
bridge executable passes 12/12; native MSVC validation remains blocked by the
local Windows SDK access failure.
→ [RF3 journal](../../.spec/journal/2026-09-05-runtime-reflection-rf3.md)

## RF4 — World Outliner and Actor Inspector

Detailed design: [RF4 plan](.plan/RF4.md).

Implementation landed for the shared render-thread model, value-only panels,
RF2 widget policy, RF3 command/result feedback, and the non-overlapping default
workspace layout. Focused headless validation passes; native Editor lifecycle
linking and Vulkan/OpenGL interaction smoke remain environment-blocked or
pending.

- [x] Add Editor panels that consume only the immutable catalog and gameplay
  editor bridge.
- [x] Preserve selection by `ActorHandle`; clear it when the snapshot no longer
  contains the actor.
- [x] Render the RF2 scalar/bool/enum/string property set from descriptors,
  group transform leaves by metadata, and show unsupported properties
  read-only; compound values and asset references require later contracts.
- [x] Submit edits as commands and display pending or rejected state without
  optimistic mutation of Gameplay memory.
- [x] Keep ImGui widget policy in Editor and all EnTT types below the Reflection
  implementation boundary.
- [ ] Validate Editor lifecycle plus a Vulkan/OpenGL startup smoke with Actor
  selection and one visible transform/property edit.

**Done when:** the Actor panel enumerates a selected Actor's components and
edits a gameplay property safely across the thread boundary on both graphics
backends.

→ [RF4 journal](../../.spec/journal/2026-09-05-runtime-reflection-rf4.md)

## Deferred

- [ ] Undo/redo transaction grouping and gizmo edit coalescing.
- [ ] Multi-object editing and mixed-value presentation.
- [ ] Level/prefab save-back and schema migration.
- [ ] Script bindings generated from reflection metadata.
- [ ] Dynamic module registration, hot reload, and generation-aware catalog
  handles.
- [ ] ECS migration; this requires a separate ownership, serialization, and
  render-source lifetime design.
