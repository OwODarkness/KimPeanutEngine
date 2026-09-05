# RF3 — Gameplay Editor Bridge

- Status: implemented (2026-09-05); native MSVC target validation is
  environment-blocked
- Parent roadmap: [Reflection Module TODO](../TODO.md#rf3--gameplay-editor-bridge)
- Cross-stage spec: [Runtime Reflection Module](../../../.spec/specs/runtime-reflection-module.md)
- Depends on: [RF2 — Gameplay Registration and Access](RF2.md)

## Objective

Add the value-only bridge between game-thread-owned Actors and a future
render-thread Editor panel. RF3 assigns stable identities to component
instances, publishes bounded immutable snapshots, accepts bounded property-edit
commands, applies them through RF2 reflection on the game thread, and returns a
terminal result for every accepted command. RF3 remains headless: RF4 owns the
World Outliner, selection model, ImGui widgets, and presentation of results.

## Concrete design question

How can Editor identify one component and request a later property edit without
retaining a Gameplay or Reflection object pointer, when Actor handles can be
recycled, an Actor may contain duplicate component types, Runtime and Editor
run on different threads, and shutdown may race queued work?

RF3 answers with two-part component addressing, immutable snapshot publication,
and game-thread revalidation:

```text
(ActorHandle{id, generation}, ComponentInstanceId)
                  + ReflectionTypeId + ReflectionPropertyId
                                  |
render thread: value-only command  |
                                  v
                         bounded bridge queue
                                  |
game thread: resolve -> revalidate -> reflect write -> read back accepted value
                                  |
                                  v
render thread: immutable snapshot + terminal edit result
```

## Entry conditions

- RF2 has frozen the Gameplay reflection catalog before presentation and keeps
  `IReflectionAccess` private to Runtime.
- `GameplayWorld` owns Actors in an `unordered_map`; `ActorHandle` already has
  an ID and generation, and destroyed Actors become unavailable before their
  storage is reclaimed.
- `Actor` owns components in insertion order as `unique_ptr` values. Components
  may share a concrete type, but they can only be added while the Actor is
  constructed and there is no component removal or replacement API.
- Gameplay mutation and reflection access are game-thread-only. Editor/ImGui
  executes on the render thread.
- `RuntimeContext::TickGameplay` is the owner-thread integration point, while
  Runtime teardown already destroys Gameplay before Reflection.

## Architecture decision

Add a Gameplay-owned satellite target named `GameplayEditorBridge` beside
`GameplayReflection`. It may depend on `Gameplay`, `GameplayReflection`, and
`Reflection`; the core `Gameplay` target remains independent of Reflection and
Editor. Runtime owns the concrete bridge and supplies only its clean consumer
interfaces to later Editor code.

```text
Gameplay ----------------------------+
  Actor/component ownership           |
  component instance IDs              |
                                      v
GameplayReflection ------------ GameplayEditorBridge <---- Runtime owner
  type bindings + object-ref maker     |       |
Reflection ---------------------------+       +--> immutable snapshots
  frozen catalog + owner-thread access         +<-- value-only edit commands
                                                      |
                                                      v
                                                 Editor in RF4
```

The bridge is an adapter around existing ownership, not a second world model.
It does not expose `GameplayWorld`, `Actor`, `ActorComponent`,
`ReflectionObjectRef`, `IReflectionAccess`, or EnTT through its consumer API.

## RF3.1 — Component-instance identity

Add the following strong value type to Gameplay:

```cpp
struct ComponentInstanceId
{
    uint32_t value = 0;
    bool IsValid() const noexcept;
};
```

`Actor::AddComponentInternal` assigns IDs monotonically in component insertion
order. Zero is invalid, assigned IDs are never reused within an Actor, and
overflow rejects the addition before ownership changes. Change the internal
insertion operation to report failure so `AddComponent` can return `nullptr`
without publishing the newly allocated component. `ActorComponent` exposes its
ID as read-only; only `Actor` assigns it.

The stable component address is the pair of `ActorHandle` and
`ComponentInstanceId`:

- Actor generation rejects a command retained across Actor destruction,
  reclamation, and handle-slot reuse.
- Component ID distinguishes duplicate concrete component types on one Actor.
- A component generation is unnecessary in RF3 because components cannot be
  removed, replaced, or independently reclaimed. If Gameplay later adds such
  operations, that feature must add generation-aware component handles before
  it permits ID reuse; RF3 must not guess that lifetime policy now.

The bridge gets narrow friend/private traversal access to Actor storage. Do not
add a general public API that returns a component container or raw pointers to
consumers merely for inspection.

## RF3.2 — Reflected component binding

RF2 knows how to register each concrete component type but a polymorphic
`ActorComponent *` cannot safely be passed to a descriptor using only its base
address and a runtime type ID. Extend the GameplayReflection satellite with a
private binding manifest. Each per-type registration unit contributes:

- the same canonical type name used for registration;
- an exact dynamic-type match operation; and
- functions that create correctly typed const or mutable
  `ReflectionObjectRef` values for the duration of one game-thread operation.

The module-level aggregator returns a frozen, deterministic manifest to the
bridge. Bridge initialization resolves every canonical name through the RF2
catalog and fails if a binding is missing, duplicated, or resolves to the wrong
type. Registration and binding remain collocated so a central `dynamic_cast`
switch cannot silently drift from the catalog.

Do not store `ReflectionTypeId` in core `ActorComponent`; that would invert the
satellite dependency. Do not use compiler RTTI names as persistent identities.
An unreflected component remains structurally visible in a snapshot with its
component ID, an invalid reflection type ID, no reflected properties, and a
generic diagnostic; its compiler-specific RTTI name is not published.

## RF3.3 — Immutable snapshot contract

The public bridge contract contains values only. Exact spelling may follow
local conventions, but it must represent:

```cpp
struct PropertyValueSnapshot
{
    ReflectionPropertyId property;
    ReflectionResultStatus status;
    ReflectionValue value;
};

struct ComponentEditorSnapshot
{
    ComponentInstanceId component;
    ReflectionTypeId type;
    bool is_root;
    std::vector<PropertyValueSnapshot> properties;
};

struct ActorEditorSnapshot
{
    ActorHandle actor;
    ActorState state;
    std::string display_name;
    std::optional<ComponentInstanceId> root_component;
    std::vector<ComponentEditorSnapshot> components;
};

struct GameplayEditorSnapshot
{
    uint64_t revision;
    bool truncated;
    SnapshotOmissionCounts omitted;
    std::vector<ActorEditorSnapshot> actors;
};
```

The snapshot references descriptors by stable IDs rather than copying catalog
metadata. RF4 joins those IDs with the immutable `IReflectionCatalog`.
Readable properties carry copied values; a read failure carries its structured
status and diagnostic. Non-readable and non-editor-visible properties are not
included.

Actor storage is unordered, so snapshot Actors are sorted by handle ID then
generation. Components retain insertion order and properties retain frozen
catalog order. Gameplay has no authored Actor name today; RF3 uses the
deterministic fallback `Actor <id>:<generation>` and does not introduce a level
serialization field under the guise of Editor display.

`GameplayEditorBridgeConfig` supplies finite maxima for Actors, components,
properties, total copied string/value bytes, and outstanding edits. Production
defaults are explicit constants; tests use small overrides. Snapshot building
uses checked arithmetic and deterministic prefix truncation, records omitted
counts, and never exceeds its configured budgets.

The game thread builds a fresh
`std::shared_ptr<const GameplayEditorSnapshot>` and publishes it with the C++17
atomic shared-pointer free functions. The render thread atomically loads the
latest complete snapshot. No vector or string is mutated after publication.
Consumers may retain an old snapshot safely, but a revision is observational
and never substitutes for revalidating target identities.

## RF3.4 — Bounded command and result flow

Split the consumer authority into two narrow interfaces, or an equivalent
facade with the same capability boundary:

```cpp
class IGameplayEditorSnapshotSource
{
public:
    virtual std::shared_ptr<const GameplayEditorSnapshot>
        GetLatestSnapshot() const = 0;
    virtual std::vector<PropertyEditResult> ConsumeEditResults() = 0;
};

class IGameplayEditorEditSink
{
public:
    virtual PropertyEditSubmission SubmitPropertyEdit(
        PropertyEditCommand command) = 0;
};
```

An edit command contains a caller-generated request ID, `ActorHandle`,
`ComponentInstanceId`, expected `ReflectionTypeId`,
`ReflectionPropertyId`, and proposed `ReflectionValue`. It contains no pointer,
descriptor reference, callback, or arbitrary function object.

Submission performs only thread-safe envelope and budget checks. Its immediate
status is `Queued`, `QueueFull`, `Stopped`, or `InvalidArgument`; semantic
success is reported later. Request IDs must be nonzero and unique among
outstanding requests.

Use one bounded outstanding-edit capacity across queued commands and terminal
results:

1. A successful submission reserves a slot and queues the command.
2. The game thread drains commands and replaces each with exactly one terminal
   result in its reserved slot.
3. The render thread consumes terminal results and releases those slots.
4. If the consumer does not drain results, later submissions return
   `QueueFull`; the game thread never blocks and no accepted result is dropped.

The implementation may use mutex-protected bounded containers. A lock-free
queue is not an RF3 requirement; deterministic capacity and ownership are more
important at this scale.

For every accepted command, the game thread revalidates in this order:

1. the bridge is running and the request is well formed;
2. the exact Actor handle, including generation, resolves and is not destroyed;
3. the component ID resolves within that Actor;
4. the live component binding matches the command's expected reflected type;
5. the property exists and is writable;
6. RF2 accepts the value conversion and behavior-preserving setter.

Terminal statuses distinguish at least `Applied`, `StaleActor`,
`StaleComponent`, `ReflectedTypeMismatch`, `UnknownProperty`, `NotWritable`,
`ValueRejected`, and `CancelledByShutdown`, while preserving the underlying
`ReflectionResultStatus` and diagnostic where useful. After a successful write,
the bridge immediately reads the property back and reports the actual accepted
value; setters may normalize or otherwise preserve a value different from the
proposal.

## RF3.5 — Frame and lifecycle integration

Runtime constructs the bridge after RF2 has frozen the catalog and after the
Gameplay world exists. It passes borrowed catalog/access capabilities and
Gameplay ownership references to the concrete bridge; only Runtime owns their
lifetimes.

At each game tick:

1. drain and apply pending property edits at the beginning of
   `RuntimeContext::TickGameplay`;
2. tick Gameplay so setter dirtiness reaches copied render sources in the same
   tick; and
3. build and publish the next Actor snapshot after tick and destroyed-Actor
   reclamation.

All object resolution, reflection object-ref construction, access calls, and
snapshot reads occur on the game thread. Snapshot load, submission, and result
consumption are the only render-thread operations.

The concrete bridge lifecycle is
`Constructed -> Running -> Stopping -> Stopped`. Shutdown order is:

1. Editor panels detach and stop submitting commands;
2. bridge submission closes;
3. queued commands become `CancelledByShutdown`, the latest snapshot is
   cleared, and no new Gameplay/reflection access begins;
4. Runtime destroys Gameplay objects; and
5. Runtime shuts down Reflection.

Current Runtime teardown occurs after game-side access has ended and may run on
the render thread. Consequently bridge shutdown may cancel queues and release
copied values there, but it must not traverse Gameplay or call
`IReflectionAccess`. Shutdown is idempotent.

## Proposed source changes

```text
engine/runtime/gameplay/
  actor/actor_types.h                         # ComponentInstanceId
  actor/actor.h/.cpp                          # assignment and private traversal seam
  component/actor_component.h                 # read-only instance ID
  reflection/
    gameplay_reflection.h/.cpp                # private binding manifest entry point
    *_reflection.cpp                          # collocated concrete bindings
  editor_bridge/
    CMakeLists.txt
    gameplay_editor_bridge_types.h            # value-only public contracts
    i_gameplay_editor_bridge.h                # snapshot/edit capabilities
    gameplay_editor_bridge.h/.cpp              # concrete bounded bridge

engine/runtime/
  CMakeLists.txt                               # RuntimeLib -> GameplayEditorBridge
  runtime_global_context.h/.cpp                # ownership/startup/teardown

engine/test/unit/
  gameplay/                                    # component identity invariants
  gameplay_reflection/                         # binding/catalog agreement
  gameplay_editor_bridge/                      # snapshot/queue/thread/lifetime tests
  runtime/                                     # composition and shutdown ordering
```

Public/private header placement may follow the existing source-tree convention,
but Editor-facing headers must not expose implementation includes. Do not add a
new Runtime-to-Editor dependency: RF4 receives borrowed bridge interfaces in
the existing initialization dependency bundle.

## Implementation order

1. Add `ComponentInstanceId`, assignment rules, lookup/traversal seam, and
   Gameplay tests for duplicate types, vector growth, overflow, and Actor reuse.
2. Add the GameplayReflection binding manifest and prove one-to-one agreement
   with the frozen RF2 catalog for every registered concrete component.
3. Define value-only snapshot, command, submission, and result contracts plus
   explicit budgets.
4. Implement deterministic owner-thread snapshot construction and atomic
   immutable publication.
5. Implement bounded outstanding edit tracking, game-thread resolution,
   reflection write/read-back, and terminal results.
6. Compose bridge startup, tick, and shutdown into Runtime without exposing
   `IReflectionAccess` or expanding the Runtime/Editor cycle.
7. Run focused and dependency-impact validation, review the diff, then update
   TODO/status and create an RF3 journal containing only landed evidence.

## Validation

### Identity and binding tests

- duplicate components of the same type receive distinct, stable IDs;
- component IDs remain stable as the Actor's component vector grows;
- Actor destruction/reclamation and handle reuse invalidate the old pair;
- every RF2 concrete reflected component has exactly one binding whose
  canonical type resolves to the expected frozen descriptor;
- an unreflected component is visible but cannot produce an editable object
  reference.

### Snapshot tests

- snapshots contain no raw pointer or `ReflectionObjectRef` and remain readable
  after the represented Actor is destroyed;
- Actors are sorted deterministically, component/property order is stable, and
  root identity is correct;
- reflected values and read failures are copied without duplicating catalog
  descriptors;
- each configured budget has a deterministic truncation test with accurate
  omission counts and no partial mutable publication;
- concurrent latest-snapshot loads observe only complete revisions.

### Edit tests

- a render-thread-style producer queues a valid edit; the game thread applies
  the public setter, reads back the accepted value, and the next snapshot shows
  it;
- duplicate-type components prove that only the addressed instance changes;
- stale Actor generation, absent component ID, expected-type mismatch, unknown
  property, read-only property, conversion failure, and setter rejection each
  return their deterministic terminal status without mutation;
- submitting, then destroying/reclaiming the Actor and reusing its handle slot
  before application returns `StaleActor`;
- queue saturation rejects before ownership transfer, every accepted request
  produces exactly one result, and undrained results preserve backpressure;
- wrong-thread pumping performs no reflection access;
- shutdown cancels queued commands, rejects later submissions, clears the
  snapshot, is idempotent, and performs no access after Gameplay/Reflection
  teardown.

### Commands

```powershell
.\tools\kp.ps1 build GameplayUnitTest
.\tools\kp.ps1 test GameplayUnitTest
.\tools\kp.ps1 build GameplayReflectionUnitTest
.\tools\kp.ps1 test GameplayReflectionUnitTest
.\tools\kp.ps1 build GameplayEditorBridgeUnitTest
.\tools\kp.ps1 test GameplayEditorBridgeUnitTest
.\tools\kp.ps1 build RuntimeStartupTest
.\tools\kp.ps1 test RuntimeStartupTest
```

Run `ReflectionUnitTest` as a regression suite because RF3 uses its result
mapping and object-ref construction. Run the full Debug build and CTest suite
if public Runtime headers or CMake target propagation broaden the impact. The
RF3 test target is proposed and is created by this stage.

## Acceptance criteria

- [x] Every Actor component has a nonzero per-Actor instance ID that remains
  stable for its lifetime and distinguishes duplicate types.
- [x] GameplayReflection supplies a complete, validated mapping from live
  supported components to correctly typed ephemeral reflection object refs.
- [x] The bridge publishes deterministic, bounded, immutable Actor snapshots
  with no live object pointer or copied descriptor graph.
- [x] Edit commands and results are value-only, bounded, request-correlated,
  and provide backpressure without blocking the game thread.
- [x] The game thread revalidates Actor generation, component identity,
  reflected type, property authority, conversion, and setter outcome before
  reporting success.
- [x] Every accepted command receives exactly one terminal result, including
  cancellation during shutdown.
- [x] A successful edit is read back, appears in the next snapshot, and
  preserves existing Gameplay setter side effects in the same tick.
- [x] Runtime owns bridge startup/tick/shutdown without exposing
  `IReflectionAccess` or adding an Editor dependency to Gameplay/Reflection.
- [ ] Focused Gameplay, GameplayReflection, bridge, Reflection regression, and
  Runtime lifecycle tests pass, with environment blockers recorded separately;
  bridge execution passes, but native MSVC/Runtime execution is blocked.
- [x] Landed behavior and validation are recorded in an RF3 journal before the
  roadmap or status page marks RF3 complete.

## Explicit exclusions

- World Outliner/Inspector widgets, selection state, and ImGui policy (RF4).
- Actor creation/deletion commands and component add/remove/reorder commands.
- authored Actor naming or level-schema changes.
- asset-reference values and pickers.
- optimistic Editor-side mutation, undo/redo, edit coalescing, or transactions.
- pagination, filtering, or remote/network serialization; bounded complete
  snapshots are sufficient for the first local consumer.
- dynamic reflection registration, module unload, hot reload, or catalog
  generation handles (RF5).
- migration of Actor/component ownership to EnTT ECS.

## Reference gate

- Godot's remote inspector transports an object ID, property, and copied value,
  then resolves the object again through `ObjectDB` before mutation. RF3 adopts
  identity-based late resolution and rejects its silent missing-object path in
  favor of a terminal result for every accepted request. See Godot's
  [Editor debugger sender](https://github.com/godotengine/godot/blob/master/editor/debugger/script_editor_debugger.cpp)
  and [runtime scene debugger](https://github.com/godotengine/godot/blob/master/scene/debugger/scene_debugger.cpp).
- Bevy Remote Protocol uses request IDs, entity/component identities, scheduled
  application-side handlers, and structured results/errors. RF3 adopts request
  correlation and owner-schedule application, but not JSON, networking, or
  Serde because this bridge is in-process and RF2 already defines a bounded
  scalar `ReflectionValue`. See Bevy's
  [remote protocol](https://github.com/bevyengine/bevy/blob/main/crates/bevy_remote/src/lib.rs)
  and [built-in methods](https://github.com/bevyengine/bevy/blob/main/crates/bevy_remote/src/builtin_methods.rs).
- EnTT's generational entity identifiers support the existing `ActorHandle`
  principle, but KimPeanutEngine components do not have an independent removal
  lifecycle. A generational component handle would therefore be unused policy
  in RF3 rather than evidence-based design.
