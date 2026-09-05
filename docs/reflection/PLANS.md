# Reflection Module Plans

**Status: proposed.** This page defines the Runtime Reflection architecture and
links its implementation stages. Current work belongs in [TODO.md](TODO.md);
execution evidence belongs in `.spec/journal/`.

## Purpose

Reflection supplies stable descriptions of engine types and properties plus a
controlled way for an owning runtime thread to read or write a reflected
object. Its first consumer is the Gameplay Actor inspector, but its contract is
not Editor-specific and does not make Reflection an object owner.

The concrete design question is: how can KimPeanutEngine retain EnTT's useful
compile-time registration API while preventing EnTT types, ImGui, and mutable
Gameplay pointers from becoming cross-module contracts?

## Architecture

```text
module-owned registration .cpp
        |
        | EnttReflectionRegistrar (registration phase only)
        v
ReflectionSystem
  `- EnttReflectionRegistry
       |- owns entt::meta_ctx
       |- builds engine TypeDescriptor/PropertyDescriptor records
       `- implements controlled object access
        |
        +--> IReflectionCatalog -- immutable metadata --> Runtime / Editor tools
        `--> IReflectionAccess  -- owner-thread only ---> Gameplay bridge

GameplayWorld -- game thread --> copied ActorEditorSnapshot --> Editor
Editor -------- render thread -> queued PropertyEditCommand -> game thread
```

The consumer boundary is engine-owned and backend-neutral. Registration is
allowed to know that EnTT is the selected implementation because member
pointers and property types are compile-time information. Hiding that work
behind a virtual registration interface would require a second type-erasure
and reflection language before EnTT receives the data.

## Responsibilities

| Concern | Owner | Excluded dependencies or authority |
|---|---|---|
| Stable type/property IDs and descriptors | Reflection | ImGui, live Actor ownership, GPU types |
| Reflection backend context and factory translation | `reflection/entt` | Editor behavior, Asset loading |
| Registering `SceneComponent`, lights, cameras, and values | Gameplay registration unit | Reflection lifecycle ownership |
| Actor/component lifetime and property side effects | Gameplay | render-thread mutation |
| Snapshot publication and edit-command application | Runtime/Gameplay editor bridge | raw cross-thread object pointers |
| Widgets, selection, panel state | Editor | EnTT types and direct `GameplayWorld` access |
| Serialized schemas and migrations | Asset | inferred reflection save/load policy |

## Public consumer contracts

The first public vocabulary is deliberately small:

- `ReflectionTypeId` and `ReflectionPropertyId`: strong engine identifiers
  derived from canonical, fully qualified names. RF1 uses the same 32-bit hash
  width as the configured EnTT 3.16 `id_type`, without exposing that EnTT
  typedef. Registration also retains the names and rejects a hash collision
  rather than treating equal hashes as proof of identity.
- `ReflectionValue`: a bounded value variant for the first supported scalar
  and engine value types. Unsupported types fail explicitly; they do not become
  untyped pointers.
- `ReflectionTypeDescriptor` and `ReflectionPropertyDescriptor`: immutable
  names, categories, access flags, value type, and neutral editor hints such as
  range, step, tooltip, or widget semantic.
- `ReflectionObjectRef`: an ephemeral owner-thread-only pair of reflected type
  identity and object address. It is never published to Editor or retained
  past the owning operation.
- `IReflectionCatalog`: read-only lookup and enumeration after freeze.
- `IReflectionAccess`: typed read/write against an ephemeral object reference.
  Runtime owns this capability and does not inject it into the Editor.

The catalog and access interfaces must return explicit diagnostics for unknown
types/properties, const or read-only writes, conversion failure, invalid
values, and setter rejection.

## Registration boundary

Each owner module exports one explicit registration function, for example:

```cpp
void RegisterGameplayReflection(EnttReflectionRegistrar &registrar);
```

Registration uses getters and setters when mutation has invariants or side
effects. `SceneComponent` transform registration must call its public setter so
dirty propagation and copied render-source updates remain intact; registering
a private data member for direct mutation is not equivalent.

The registrar enforces canonical names, duplicate detection, supported value
types, descriptor construction, and registration-phase state. Static global
registration and constructor side effects are rejected because their order,
failure reporting, test isolation, and future module unloading are ambiguous.

## Lifecycle

```text
Constructed -> Registering -> Frozen -> ShuttingDown -> ShutDown
```

Runtime constructs `ReflectionSystem`, registers fundamental and module-owned
types on one thread, freezes the catalog, and only then publishes its read-only
interface. Freeze validates all descriptors and prevents later mutation.
Shutdown detaches consumers before clearing the EnTT context. The first version
does not support registration hot reload; that requires generation-aware
catalog handles and module-unload ordering.

## Actor-panel integration

Reflection does not solve world traversal, selection identity, or threading.
The Actor panel therefore arrives through a separate Runtime/Gameplay bridge:

1. Gameplay produces an immutable snapshot containing `ActorHandle`, display
   name, stable component-instance identity, type IDs, and copied values.
2. Editor retains handles and IDs, never `Actor*`, `ActorComponent*`, or
   `ReflectionObjectRef`.
3. A UI edit becomes a value-only command identifying actor, component
   instance, property, and proposed value.
4. The game thread revalidates every identity and applies the registered
   setter through `IReflectionAccess`.
5. The next snapshot reports the accepted value or a diagnostic.

Component-instance identity is required because the current Actor policy
allows duplicate component types. Type identity alone cannot address a
specific component.

## Reference findings

- EnTT 3.16.0 provides explicit `meta_ctx` ownership, context-aware factories,
  runtime get/set, custom metadata, and the experimental `davey` ImGui viewer.
  KimPeanutEngine adopts the explicit context and traversal concepts, not
  `davey`'s assumption that UI can inspect a live `entt::registry` directly.
  See the vendored `src/entt/meta/` and `src/entt/tools/davey.hpp`, plus the
  [EnTT 3.16 release](https://github.com/skypjack/entt/releases/tag/v3.16.0).
- Piccolo centralizes generated reflection registration and recursively builds
  component controls from field metadata. That validates registration before
  generic inspector UI, but its direct editor access to live world objects does
  not fit KimPeanutEngine's separate game/render threads. See Piccolo's
  [reflection registration](https://github.com/BoomingTech/Piccolo/tree/main/engine/source/runtime/core/meta/reflection)
  and [editor UI](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/editor/source/editor_ui.cpp).

No reference justifies migrating `GameplayWorld` to EnTT ECS merely to obtain
an inspector. The current Actor/component ownership remains unchanged.

## Stage map

- [RF1 — Reflection contracts and EnTT adapter](.plan/RF1.md): create the
  module, public value/descriptor interfaces, explicit context lifecycle,
  registrar, freeze validation, and focused unit tests.
- **RF2 — Gameplay registration and access:** register the minimum math,
  transform, scene, mesh, light, and camera properties through behavior-safe
  accessors; add owner-thread access tests without Editor.
- **RF3 — Gameplay editor bridge:** add component-instance identity, copied
  Actor snapshots, queued edit commands, stale-target rejection, and
  game-thread application.
- **RF4 — World Outliner and Actor Inspector:** consume snapshots, render
  metadata-selected widgets, preserve selection by handles, and display edit
  diagnostics without an EnTT or Gameplay pointer in Editor.
- **RF5 — authoring extensions:** design undo/redo transactions, multi-select,
  prefab/level save-back, scripting, and hot reload only after their concrete
  consumers and lifetime rules exist.

The multi-stage acceptance contract is the
[Runtime Reflection Module spec](../../.spec/specs/runtime-reflection-module.md).
