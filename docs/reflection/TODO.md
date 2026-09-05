# Reflection Module TODO

**Status: RF1 landed (2026-09-05).** EnTT 3.16.0 is vendored, and the engine
Reflection target now owns the RF1 contracts, explicit EnTT context, frozen
catalog, and headless adapter tests. Gameplay metadata, the snapshot bridge,
and the Actor panel remain future stages.

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

- [ ] Add module-owned Gameplay registration units; do not centralize Gameplay
  type knowledge inside Reflection.
- [ ] Register the minimum reusable value types and selected properties of
  `SceneComponent`, mesh, directional/point/spot light, and camera components.
- [ ] Route stateful properties through public getters/setters so transform
  dirtiness, validation, activation, and render-source publication remain
  correct.
- [ ] Define neutral metadata for display name, category, read-only state,
  numeric range/step, tooltip, and widget semantic.
- [ ] Test registration coverage, supported value conversion, setter rejection,
  and observable component side effects on the game thread.

**Done when:** Runtime can inspect and safely mutate one representative
transform, mesh, light, and camera property without Editor or direct member
access, while existing Gameplay lifecycle and render-source tests still pass.

## RF3 — Gameplay editor bridge

- [ ] Introduce stable component-instance identity compatible with duplicate
  component types and Actor destruction/reclamation.
- [ ] Publish bounded immutable Actor snapshots at a game-thread boundary.
- [ ] Define value-only property-edit commands and a bounded queue from Editor
  to Runtime/Gameplay.
- [ ] Revalidate actor generation, component identity, property identity,
  reflected type, access flags, and value conversion on the game thread.
- [ ] Report accepted/rejected edit results without exposing object pointers.
- [ ] Test stale actors/components, destruction races, duplicate component
  types, queue bounds, invalid values, and shutdown cancellation.

**Done when:** a render-thread-style test can retain a copied snapshot, enqueue
an edit while Gameplay continues owning the object, and observe either the
validated new value or a deterministic rejection in the next snapshot/result.

## RF4 — World Outliner and Actor Inspector

- [ ] Add Editor panels that consume only the immutable catalog and gameplay
  editor bridge.
- [ ] Preserve selection by `ActorHandle`; clear it when the snapshot no longer
  contains the actor.
- [ ] Render supported scalar/vector/transform/enum/asset-reference widgets
  from property descriptors and show unsupported properties read-only.
- [ ] Submit edits as commands and display pending or rejected state without
  optimistic mutation of Gameplay memory.
- [ ] Keep ImGui widget policy in Editor and all EnTT types below the Reflection
  implementation boundary.
- [ ] Validate Editor lifecycle plus a Vulkan/OpenGL startup smoke with Actor
  selection and one visible transform/property edit.

**Done when:** the Actor panel enumerates a selected Actor's components and
edits a gameplay property safely across the thread boundary on both graphics
backends.

## Deferred

- [ ] Undo/redo transaction grouping and gizmo edit coalescing.
- [ ] Multi-object editing and mixed-value presentation.
- [ ] Level/prefab save-back and schema migration.
- [ ] Script bindings generated from reflection metadata.
- [ ] Dynamic module registration, hot reload, and generation-aware catalog
  handles.
- [ ] ECS migration; this requires a separate ownership, serialization, and
  render-source lifetime design.
