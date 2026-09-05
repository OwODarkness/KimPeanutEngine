# Reflection Module Documentation Guide

Read the repository [agent contract](../../AGENTS.md), [module plans](PLANS.md),
[roadmap](TODO.md), and the affected owner module documentation before changing
Reflection design or implementation.

## Boundaries

- Reflection owns type/property metadata, engine-neutral reflected values,
  controlled runtime property access, and the lifetime of the selected
  reflection backend.
- Runtime owns Reflection startup, freeze, publication, and shutdown order.
- Each module owns registration of its own C++ types. Reflection must not gain a
  dependency on Gameplay, Asset, Render, Editor, or another registering module.
- Gameplay remains the owner of mutable Actors and components. Reflection does
  not change object ownership, component lifetime, or tick policy.
- Editor may consume immutable catalog data and copied editor snapshots. It
  must not receive a live Gameplay pointer or game-thread mutation interface.
- Asset serialization, prefab construction, scripting exposure, undo/redo, and
  ECS migration are separate consumers or later designs; they are not implicit
  responsibilities of Reflection.

## EnTT containment

- EnTT types may appear in `engine/runtime/reflection/entt/` and in the
  module-owned registration `.cpp` files that use the EnTT registrar.
- Public consumer contracts must not expose `entt::meta_ctx`,
  `entt::meta_type`, `entt::meta_any`, or another EnTT type.
- Normal Gameplay, Editor, Asset, Render, and Graphics headers must not include
  EnTT solely to inspect reflected data.
- Do not create a second generic template reflection language merely to hide
  EnTT from registration code. The clean boundary is the consumer API.

## Threading and lifecycle

- Registration is single-threaded and completes before the catalog is
  published. A successful freeze makes descriptors immutable.
- Live object reads and writes occur only on the owning thread. The first
  Gameplay consumer is game-thread-only.
- The render-thread Editor communicates through copied snapshots and queued
  edit commands. It never wraps a live object in an opaque reflection value.
- Reflection shutdown begins only after all catalog readers and live-object
  accessors are detached. If dynamic modules are introduced later, unregister
  their callbacks before unloading their code.

## Documentation and validation

- `PLANS.md` owns module architecture and design decisions.
- `TODO.md` owns the acceptance-oriented roadmap.
- `.plan/<stage>.md` owns a concrete stage design.
- `.spec/specs/` owns cross-stage intent; `.spec/journal/` records factual
  implementation and validation evidence.

Follow the [validation matrix](../validation_matrix.md). Reflection contracts,
EnTT adapter behavior, registration failure, freeze, access conversion, and
shutdown require focused headless unit tests. CMake or shared public-header
changes require reconfiguration and affected-consumer builds. Actor-panel work
also requires Editor lifecycle and runtime smoke evidence.
