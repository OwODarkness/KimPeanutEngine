# RF1 — Reflection Contracts and EnTT Adapter

- Status: proposed
- Parent roadmap: [Reflection Module TODO](../TODO.md#rf1--contracts-and-entt-adapter)
- Cross-stage spec: [Runtime Reflection Module](../../../.spec/specs/runtime-reflection-module.md)

## Objective

Create the smallest coherent Runtime Reflection module: clean engine consumer
contracts, an explicit EnTT 3.16.0 context implementation, deterministic
registration/freeze/shutdown, and headless proof. RF1 must be useful without
Gameplay, Editor, serialization, or ECS migration.

## Entry state

- `third_party/entt/` contains EnTT 3.16.0 and exposes `EnTT::EnTT`.
- `EnTTUnitTest` proves a raw registry operation and one raw meta property read.
- No engine target owns `entt::meta_ctx`; current test registration uses the
  default context and resets it during teardown.
- Gameplay remains a hand-written Actor/component runtime, and Editor runs on
  the render thread.

## Proposed source layout

```text
engine/runtime/reflection/
  CMakeLists.txt
  reflection_system.h/.cpp
  reflection_types.h
  i_reflection_catalog.h
  i_reflection_access.h
  entt/
    entt_reflection_registry.h/.cpp
    entt_reflection_registrar.h

engine/test/unit/reflection/
  CMakeLists.txt
  reflection_registry_test.cpp
```

Names may be adjusted to local conventions during implementation, but the
public/EnTT split and dependency direction are acceptance constraints.

## Public types

RF1 defines:

- strongly typed 32-bit `ReflectionTypeId` and `ReflectionPropertyId` values,
  matching the configured EnTT identifier width without exposing its typedef;
- canonical names retained beside IDs for diagnostics and collision checks;
- `ReflectionValue` with only the types proven by tests in RF1, initially
  `bool`, signed/unsigned integer, floating point, and `std::string`;
- `ReflectionPropertyFlags`, including readable, writable, and editor-visible;
- property/type descriptors whose strings and arrays remain valid for the
  frozen catalog lifetime;
- structured read/write and initialization results with stable status enums
  plus diagnostic text;
- ephemeral `ReflectionObjectRef`, documented and enforced as owner-thread-only.

Math vectors, transforms, enums, AssetIDs, arrays, optionals, and nested objects
enter RF2 only with a registered consumer and conversion tests.

## Interfaces

`IReflectionCatalog` exposes const lookup and enumeration. Returned descriptors
are immutable and remain valid until consumer detachment and system shutdown.

`IReflectionAccess` exposes read and write against `ReflectionObjectRef`. It
must not retain the object address. It rejects unknown IDs, type mismatch,
read-only properties, unsupported values, failed conversion, and a setter that
does not accept the value.

The interfaces expose no registration method. Registration is deliberately a
concrete build/startup concern because C++ member-pointer registration cannot
be meaningfully virtualized without rebuilding a parallel reflection system.

## EnTT implementation

`EnttReflectionRegistry` owns one `entt::meta_ctx`; it never relies on the
default locator. The registrar writes into this context and simultaneously
builds or validates engine descriptors. EnTT objects remain private to the
implementation and registrar-facing headers.

The registrar supports type declaration and property declaration through
member pointers or getter/setter pairs. RF1 needs only a local test type, but
the API must preserve setter return/failure semantics needed by Gameplay in
RF2.

Registration is explicit and ordered. Duplicate canonical names, duplicate
property names within a type, mismatched IDs, and unsupported property value
types fail initialization with diagnostics. A hash collision is detected by
comparing canonical names.

## Freeze and lifetime

`ReflectionSystem::Initialize` executes supplied registration functions into a
fresh implementation. If any function fails, initialization discards the
candidate context and publishes nothing. `Freeze` validates descriptor links,
sorts enumeration deterministically, and transitions the system to immutable
read mode.

After freeze:

- catalog lookups and enumeration are const;
- the registrar rejects new types/properties;
- catalog storage does not move;
- live-object access is allowed only on the documented owner thread;
- shutdown is idempotent and occurs only after consumers detach.

RF1 does not support unfreeze, incremental registration, hot reload, or dynamic
library unloading.

## Dependency boundary

```text
Reflection public contracts -> C++ standard library / smallest required Core values
Reflection EnTT adapter      -> Reflection contracts + EnTT::EnTT
Reflection tests             -> Reflection adapter + GoogleTest
```

Reflection must not depend on Gameplay, Editor, Asset, Render, Graphics, ImGui,
Lua, or a graphics backend. EnTT must not become a public transitive dependency
of the consumer interface target.

## Validation

Add a focused `ReflectionUnitTest` target covering:

1. explicit context isolation from EnTT's default context;
2. type/property registration and deterministic enumeration;
3. engine descriptor lookup without an EnTT type in the test-facing consumer
   path;
4. scalar/string reads and writes;
5. read-only and setter-rejected writes;
6. unknown type/property and incompatible value diagnostics;
7. duplicate and forced-collision initialization failure;
8. registration rejection after freeze;
9. failed-initialization rollback and idempotent shutdown.

Because RF1 creates a CMake target and public headers, reconfigure and build the
focused target, then run its CTest entry. Escalate to the full build/test suite
if the target or public contract becomes a shared Runtime dependency during
RF1.

## Acceptance criteria

- [ ] A dedicated Reflection target exists and has no upward module dependency.
- [ ] Public consumer headers contain no EnTT or ImGui type.
- [ ] EnTT registration uses one explicitly owned context.
- [ ] Registration and catalog publication are transactional.
- [ ] The frozen catalog is immutable and deterministic.
- [ ] Reads/writes return structured failure instead of asserting or exposing
  untyped pointers.
- [ ] Focused tests pass and prove context isolation, lifecycle, and errors.
- [ ] Documentation/status are updated only with factual landed evidence.

## Explicit non-goals

- Gameplay type registration or component traversal.
- Actor snapshots, edit queues, selection, or ImGui widgets.
- Serialization, construction/factories, script binding, undo/redo, or prefabs.
- Replacing `GameplayWorld` or Actor/component ownership with `entt::registry`.
- A backend-agnostic template registration language.
