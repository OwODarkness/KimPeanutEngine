# RF2 — Gameplay Registration and Access

- Status: implementation landed; MSVC validation blocked by environment
- Parent roadmap: [Reflection Module TODO](../TODO.md#rf2--gameplay-registration)
- Cross-stage spec: [Runtime Reflection Module](../../../.spec/specs/runtime-reflection-module.md)
- Depends on: [RF1 — Reflection Contracts and EnTT Adapter](RF1.md)

## Objective

Register the first editor-relevant Gameplay component properties through the
landed RF1 contracts, compose the frozen catalog into Runtime startup, and
prove that reflection writes preserve Gameplay validation, transform
propagation, and copied render-source publication. RF2 remains headless: Actor
enumeration, component-instance identity, cross-thread edit commands, and
ImGui controls belong to RF3 and RF4.

## Concrete design question

How can a future Actor inspector edit transform, mesh, light, and camera state
without exposing EnTT or mutating component members directly, while keeping
Reflection independent of Gameplay math, Asset, Render, and Editor types?

RF2 answers this with Gameplay-owned scalar adapter functions. A compound
property such as local location is presented as stable scalar leaves such as
`transform.location.x`. A leaf setter copies the current `Vector3f` or
`Rotatorf`, changes one field, validates the complete result, and calls the
component's public setter. Reflection therefore continues to carry only the
RF1 scalar/string value variant.

## Entry conditions

- RF1 provides stable IDs, scalar/string `ReflectionValue`, immutable
  descriptors, `IReflectionCatalog`, owner-thread `IReflectionAccess`, a
  transactional `ReflectionSystem`, and an explicit EnTT context.
- RF1's registrar accepts data members and member getter/setter pairs, but its
  traits do not yet accept free-function adapters and descriptors do not yet
  carry editor-neutral presentation hints.
- Gameplay components own their mutable state. Their public setters propagate
  transform dirtiness or mark a copied render source dirty for the next tick.
- [GP8 light transform alignment](../../gameplay/.plan/GP8.md) has landed:
  point/spot positions and directional/spot directions now derive from world
  Scene transform, `+X` is the shared forward axis, and factories translate
  the existing level position/direction schema into local transforms.
- Camera setters reject invalid values by leaving state unchanged; remaining
  light-specific setters still rely on Render's stricter source validation.
- Runtime does not yet own or publish a `ReflectionSystem`.

## Architecture decision

```text
Reflection                         Gameplay
  descriptor metadata                component public API
  callable registrar support         scalar adapter functions
            ^                                  |
            |                                  v
            +------- GameplayReflection target
                              |
                              v
                  Runtime ReflectionSystem
                  register -> validate -> freeze
```

`GameplayReflection` is a small satellite target under the Gameplay module. It
depends on `Gameplay` and `Reflection`; the core `Gameplay` target does not
depend on Reflection. Runtime invokes its one explicit registration function.
Reflection never includes a Gameplay, Asset, Render, or Editor header.

The RF2 catalog is deliberately flat. Every concrete component type owns its
complete editor-facing property list. Shared templated helpers add only the
transform channels that affect downstream behavior: all channels for Mesh,
location/rotation for Camera, rotation for DirectionalLight, location for
PointLight, and location/rotation for SpotLight. Primitive leaves are added to
Mesh. This avoids reflected base-class traversal; adding inheritance now would
also require base-aware descriptors, pointer adjustment, lookup, and collision
policy.

## Proposed source changes

```text
engine/runtime/reflection/
  reflection_types.h                         # neutral metadata vocabulary
  entt/entt_reflection_registrar.h            # free-function getter/setter forms
  entt/entt_reflection_registry.h/.cpp        # metadata storage/validation

engine/runtime/gameplay/reflection/
  CMakeLists.txt
  gameplay_reflection.h                      # one registration declaration
  gameplay_reflection_internal.h             # private shared transform helpers
  actor_reflection.cpp                       # Scene/Mesh definitions
  light_reflection.cpp                       # Directional/Point/Spot definitions
  camera_reflection.cpp                      # Camera definition
  gameplay_reflection.cpp                    # thin module-level aggregator

engine/runtime/
  CMakeLists.txt                              # RuntimeLib -> GameplayReflection
  runtime_global_context.h/.cpp               # lifecycle owner and catalog getter

engine/test/unit/reflection/
  reflection_registry_test.cpp               # metadata contract validation

engine/test/unit/gameplay_reflection/
  CMakeLists.txt
  gameplay_reflection_test.cpp                # component behavior/access proof
```

File names may follow local naming conventions during implementation, but the
satellite dependency direction and headless test split are constraints.

## RF2.1 — Neutral property metadata

Extend `ReflectionPropertyDescriptor` with an owned
`ReflectionPropertyMetadata` value containing only presentation-neutral data:

- `display_name` and `category` strings;
- an optional tooltip;
- `ReflectionWidgetSemantic`, initially `Default`, `Position`,
  `RotationDegrees`, `Scale`, `Color`, `Distance`, `AngleRadians`, and `Enum`;
- optional finite numeric minimum, maximum, and positive step;
- integer enum options containing a stable numeric value and display label.

Readable, writable, and editor-visible state remains in
`ReflectionPropertyFlags`; it must not be duplicated in metadata. Widget
semantics are hints, not an instruction to use a particular ImGui widget and
not mutation authority.

Registrar `Property` and `ReadOnly` calls accept metadata by value and move it
into frozen descriptor storage. Existing RF1 calls remain source-compatible by
using an empty default.

Freeze rejects:

- non-finite numeric hints, `minimum > maximum`, or `step <= 0`;
- numeric hints on non-numeric values;
- enum semantics on non-integer values;
- empty or duplicate enum labels/values;
- metadata on a property whose access flags or value type are inconsistent.

Focused Reflection tests prove metadata ownership after caller temporaries are
destroyed, deterministic enumeration, valid hints, each invalid-descriptor
case, and unchanged RF1 access behavior.

## RF2.2 — Callable registration adapters

Extend the thin EnTT registrar traits to support non-capturing free functions
in these forms, in addition to the existing member functions:

```cpp
Value Getter(const Component &);
bool Setter(Component &, Value);
```

The registrar still derives the reflected value type at compile time, records
the engine descriptor/access callbacks, and registers the same getter/setter
pair in the owned EnTT context. A boolean `false` maps to
`ReflectionResultStatus::SetterRejected`; incompatible `ReflectionValue`
conversion remains `TypeMismatch`.

Captured lambdas and general `std::function` callbacks are excluded. They
would add lifetime-bearing callable storage and make EnTT registration differ
from the engine access record. The accepted free functions are static and
module-owned.

## RF2.3 — Gameplay registration set

Expose one declaration from the satellite target:

```cpp
reflection::ReflectionResult RegisterGameplayReflection(
    reflection::EnttReflectionRegistrar &registrar);
```

The public declaration may mention the concrete registrar as a forward
declaration because this is a registration-phase API, not a consumer API. The
implementation `.cpp` includes EnTT-facing registration headers and Gameplay
component headers.

Use these canonical type names:

- `kpengine.gameplay.SceneComponent`
- `kpengine.gameplay.MeshComponent`
- `kpengine.gameplay.DirectionalLightComponent`
- `kpengine.gameplay.PointLightComponent`
- `kpengine.gameplay.SpotLightComponent`
- `kpengine.gameplay.CameraComponent`

Use lower-case dotted property names as stable machine names. Human labels and
grouping belong in metadata. Register the following minimum set:

| Property family | Stable properties | Setter rule |
|---|---|---|
| Scene transform | `transform.location.x/y/z`, `transform.rotation.pitch/yaw/roll`, `transform.scale.x/y/z` | Require a finite aggregate; reconstruct and call `SetLocalLocation`, `SetLocalRotation`, or `SetLocalScale`. |
| Mesh primitive | `render.visible`, `render.casts_shadow`, `mesh.lod_bias` | Call public primitive/mesh setters; integer conversion must remain checked. |
| Directional light | Scene rotation plus `light.color.r/g/b`, `light.intensity`, `light.enabled`, `light.casts_shadow` | Rotation uses the shared `+X`-forward transform path; color must be finite/non-negative and intensity finite/non-negative. |
| Point light | Scene location plus color/intensity/enabled/shadow above and `light.range` | Location must form a finite transform; range must be finite and greater than zero. |
| Spot light | Scene location/rotation plus color/intensity/range/enabled/shadow and `light.inner_cone`, `light.outer_cone` | Transform derives source position/direction; require `0 <= inner <= outer < pi/2` after either cone edit. |
| Camera | Scene location/rotation plus `camera.projection`, `camera.field_of_view`, `camera.near_plane`, `camera.far_plane`, `camera.orthographic_height`, `camera.enabled`, `camera.priority` | Transform drives the copied camera pose; validate lens values before calling the existing void setters and preserve the current camera domain and near/far relationship. |

Transform leaves are registered on `SceneComponent` and repeated through a
shared family of location, rotation, and scale helpers. Registration composes
those subfamilies per concrete component rather than blindly exposing the full
base-class transform: Mesh gets location/rotation/scale; Camera gets
location/rotation; DirectionalLight gets rotation; PointLight gets location;
and SpotLight gets location/rotation. Primitive fields are included in Mesh.
A flat lookup by concrete type ID therefore returns the complete set of
controls that currently affect that component's behavior.

RF2 does not recreate the removed `light.position.*` or `light.direction.*`
properties. GP8 made Scene transform the single authoring source, so the
stable names are `transform.location.*` and `transform.rotation.*` on the
applicable light descriptors. Source direction remains derived and is not
independently editable.

`camera.projection` uses a signed integer adapter plus `Enum` metadata with
`Perspective` and `Orthographic` options. The adapter rejects every other
integer before converting to `render::CameraProjectionMode`.

Light-specific adapter validation mirrors the currently authoritative
`render::IsLightDescValid` color, intensity, range, and cone domains. Transform
leaf adapters independently require a finite resulting aggregate. Direction
cannot become zero through rotation because GP8 derives a normalized forward
vector; zero/non-finite legacy direction rejection remains at the factory
conversion boundary. Rejected writes leave the component unchanged and do not
mark its source dirty. RF2 should centralize shared validation predicates in a
Gameplay-owned helper if factories or component setters also need them; it
must not make Gameplay call a private Render implementation function.

### Metadata examples

- transform position: category `Transform`, semantic `Position`, step `0.1`;
- rotation: category `Transform`, semantic `RotationDegrees`, step `0.1`;
- color: category `Light`, semantic `Color`, minimum `0`, step `0.01`;
- light intensity: category `Light`, minimum `0`, step `0.1`;
- spot cone: category `Light`, semantic `AngleRadians`, minimum `0`, maximum
  just below pi/2, step `0.01`;
- camera FOV: category `Camera`, minimum `1`, maximum `179`, step `0.1`.

Metadata limits must match setter acceptance. A UI hint must not claim a
narrower hard domain merely because it produces a convenient slider.

## RF2.4 — Runtime composition and lifetime

Add a Runtime-owned `std::unique_ptr<reflection::ReflectionSystem>` declared
before `gameplay_world_`, so explicit teardown can destroy Gameplay objects
before shutting down reflection.

At the start of `RuntimeContext::Initialize`, before window or render startup:

1. construct a fresh `ReflectionSystem` if needed;
2. initialize it with `RegisterGameplayReflection`;
3. require a successful frozen state;
4. throw the existing startup-style diagnostic on failure without publishing
   a partial catalog.

Runtime may expose `const IReflectionCatalog *GetReflectionCatalog() const` for
read-only metadata discovery. RF2 does **not** expose `IReflectionAccess` to
Editor or return a mutable component reference. The future RF3 game-thread
bridge receives access internally when it is implemented.

`RuntimeContext::Clear` destroys the level and `GameplayWorld`, then shuts
down/resets Reflection. The operation stays idempotent. Runtime tests verify
that successful startup publishes the frozen Gameplay types and failed or
repeated teardown does not leave a catalog visible.

## Further RF2 work — contributor collection seam

After direct Runtime composition with `RegisterGameplayReflection` is proven,
generalize how loaded engine modules contribute registrations without changing
the per-type or per-module registration functions.

Add a build-phase `ReflectionRegistrationSet` that accepts a unique canonical
module name plus its existing `ReflectionRegistrationFunction`. The set is
open only during bootstrap contribution, preserves the module loader's
explicit deterministic order, rejects duplicate module names and empty
callbacks, and seals before `ReflectionSystem::Initialize` executes the
transaction.

When KimPeanutEngine has a concrete module-loader participant contract, add a
small contributor interface shaped like:

```cpp
class IReflectionContributor
{
public:
    virtual ~IReflectionContributor() = default;

    virtual reflection::ReflectionResult ContributeReflection(
        reflection::ReflectionRegistrationSet &registrations) = 0;
};
```

The interface contributes one or more named callbacks; it does not register
C++ types itself. In particular, do not introduce a virtual
`IReflectionRegistrar::RegisterType<T>()`: C++ cannot provide virtual template
methods, and type-erasing that operation would duplicate the EnTT adapter.
`EnttReflectionRegistrar` remains the concrete implementation-phase API used
inside callbacks, while `IReflectionCatalog` remains the clean consumer API.

The resulting flow is:

```text
loaded module / IReflectionContributor
        | contributes named callback
        v
ReflectionRegistrationSet -- seal --> ReflectionSystem::Initialize
                                             |
                                             v
                                  execute all / validate / freeze
```

This is a later RF2 extension slice, not a prerequisite for the first Gameplay
catalog. It becomes justified when a second independently loaded engine module
or plugin contributes reflection. All contributors must be loaded before
freeze; dynamic addition/removal and module hot reload remain RF5 work because
they require catalog generations and consumer invalidation.

Focused tests for this slice cover deterministic order, duplicate module-name
rejection, empty callbacks, contribution after seal, rollback when any module
registration fails, and successful aggregation of at least two synthetic
modules.

## Explicit exclusions

- `AssetID` mesh/material editing. An integer-packed ID is not an authoring
  value; it needs an asset-reference contract and picker semantics in RF4.
- local/world bounds, world transform, camera basis vectors, attachment
  pointers, owner pointers, and render source handles. These are derived,
  identity-bearing, or internal state rather than editable authoring values.
- transform channels with no current behavior: DirectionalLight location and
  scale; PointLight rotation and scale; SpotLight scale; Camera scale. The
  reusable helpers exist, but the concrete descriptor must not advertise a
  control whose downstream source ignores it.
- structured `Vector3f`, `Rotatorf`, or `Transform3f` alternatives in
  `ReflectionValue`; scalar leaves are sufficient for the first consumer.
- reflected inheritance, construction, arrays, serialization, undo/redo,
  scripting, Actor traversal, component-instance identity, and cross-thread
  access.
- direct Editor use of `ReflectionObjectRef` or `IReflectionAccess`.

## Implementation order

1. Add metadata types, registrar overloads, freeze validation, and focused RF1
   regression tests.
2. Add free-function getter/setter traits and prove one local adapter through
   both the engine access interface and the explicit EnTT context.
3. Add the `GameplayReflection` target and register Scene/Mesh first; prove a
   transform leaf and LOD-bias write.
4. Add light adapters with Render-compatible validation and source-update
   tests.
5. Add camera fields, enum metadata, relational rejection, and source-update
   tests.
6. Compose the registration function into Runtime startup and teardown.
7. As later RF2 work, introduce the sealed contributor collection only when a
   real second module/plugin contribution path exists.
8. Run the focused and dependency-impact validation matrix, review the diff,
   then update TODO/status and create an RF2 journal only with landed facts.

## Validation

### Reflection contract tests

- valid metadata survives freeze and is returned only through clean engine
  descriptors;
- invalid ranges, steps, semantics, and enum tables reject initialization and
  publish no catalog;
- a free getter/setter pair reads, writes, and reports `SetterRejected`;
- existing RF1 member registration and context isolation tests remain green.

### Gameplay reflection tests

- all six canonical type names and the expected property IDs are present;
- a transform-axis write changes only that axis, dirties child world
  transforms, and reaches an active mesh/camera source on the next Gameplay
  tick;
- mesh LOD bias, visibility, and shadow edits use public setters and coalesce
  into the existing one-update-per-tick source behavior;
- each light family accepts a valid transform edit through its applicable
  location/rotation leaves, rejects NaN/negative light values or invalid cone
  relationships, and publishes no update for a rejection;
- directional/spot rotation writes publish the GP8-derived `+X`-forward
  direction, and attached point/spot edits publish the expected world-space
  position after the next Gameplay tick;
- camera accepts valid FOV/plane/projection edits, rejects invalid and
  relationally inconsistent values with `SetterRejected`, and publishes the
  accepted source on tick;
- wrong-thread access is still rejected before any Gameplay adapter executes;
- a const object permits reads and rejects writes.

Use recording source sinks already established by Gameplay tests. Check both
component state immediately after the reflection write and the copied source
description after `GameplayWorld::Tick`; compilation alone is not sufficient
proof of the side effects.

### Commands

```powershell
.\tools\kp.ps1 build ReflectionUnitTest
.\tools\kp.ps1 test ReflectionUnitTest
.\tools\kp.ps1 build GameplayReflectionUnitTest
.\tools\kp.ps1 test GameplayReflectionUnitTest
.\tools\kp.ps1 build GameplayUnitTest
.\tools\kp.ps1 test GameplayUnitTest
.\tools\kp.ps1 build RuntimeStartupTest
.\tools\kp.ps1 test RuntimeStartupTest
```

If public Runtime headers or target linkage affect broader consumers, run the
full Debug build and CTest suite. If MSVC remains blocked by the recorded
Windows SDK permission failure, report that environment blocker separately
and retain successful MinGW evidence; do not describe the source as validated
by MSVC.

## Acceptance criteria

- [x] Reflection descriptors carry validated, owned, UI-neutral metadata with
  no ImGui or Gameplay type.
- [x] The registrar supports static Gameplay adapter functions without adding
  lifetime-bearing callable storage.
- [x] `GameplayReflection` owns all Gameplay type knowledge and the core
  `Gameplay` target does not depend on Reflection.
- [x] Frozen descriptors for Scene, Mesh, all three lights, and Camera contain
  the specified complete flat property sets.
- [x] Representative transform, mesh, light, and camera writes go through
  public component setters and preserve observable side effects.
- [x] Invalid camera/light writes return `SetterRejected`, preserve state, and
  produce no render-source update.
- [x] Runtime initializes the catalog transactionally before presentation,
  exposes only read-only metadata publicly, and tears it down after Gameplay.
- [ ] Focused Reflection, GameplayReflection, Gameplay, and Runtime startup
  tests pass, with broader validation when dependency impact requires it.
- [x] Landed behavior and validation are recorded in an RF2 journal before the
  roadmap or status page marks RF2 complete.

## Reference gate

- EnTT 3.16's `meta_factory::data<Setter, Getter>` accepts static/free
  getter-setter candidates as well as member candidates. RF2 uses that native
  form and extends only the engine-side compile-time traits; it does not add a
  second dynamic registration language. See the vendored
  `third_party/entt/src/entt/meta/factory.hpp` and the
  [upstream v3.16 source](https://github.com/skypjack/entt/blob/v3.16.0/src/entt/meta/factory.hpp).
- Piccolo demonstrates that stable field metadata can drive generic component
  controls, but its editor walks live reflected objects. RF2 adopts the
  metadata-first registration idea only; RF3 retains KimPeanutEngine's copied
  snapshot and queued-edit boundary. See Piccolo's
  [editor UI source](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/editor/source/editor_ui.cpp).
- KimPeanutEngine's own Render light validation and Gameplay source-update
  tests are the authoritative domain and side-effect references for RF2. No
  reference justifies moving Actor/component ownership into EnTT ECS.
