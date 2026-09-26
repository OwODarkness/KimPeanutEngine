# GP9 — Runtime Gameplay Control Requests

Status: complete (2026-09-26). Gameplay, Runtime provider, editor consistency,
focused tests, and live command smoke are implemented and verified.

## Design question

How can a local agent request a live Actor position or rotation change while the
Runtime command system stays generic and Gameplay retains authority over Actor
identity, lifetime, and mutation?

## Current baseline

- `CommandRegistry` validates command schemas and policy, queues Game-lane work,
  and dispatches handlers from `Engine::GameTick()`.
- `GameplayWorld` owns Actors in a generational-handle map. It is the authority
  for lookup and Actor lifetime; the project has not migrated this ownership to
  an ECS registry.
- `SceneComponent` exposes transform setters that propagate dirtiness and let
  derived components publish copied render and camera source values.
- The editor already changes Actors through value-only property edits. Its
  snapshots carry `ActorHandle`; each edit identifies the component instance,
  reflected type, property, and proposed value. Gameplay revalidates and applies
  the edit on the game thread through Reflection.
- Level objects already carry authored names. LevelInstance now copies those
  names onto the owned Gameplay Actors; Actors created without an authored
  name may remain unnamed. The editor label includes both the name and handle.
- `PlayerController` retains control rotation separately from the possessed
  `CameraComponent` transform. External camera rotation must synchronize those
  values so a later look input does not restore an older orientation.

## Decision

Treat Runtime commands as a request-distribution boundary. The generic command
registry owns command names, argument validation, permissions, dispatch, and
Game-lane scheduling. A Runtime integration provider translates a validated
request into an explicit Gameplay operation. Gameplay resolves handles, checks
current Actor/component state, invokes the owning transform setters, and
reports the applied result.

Provide bounded, read-only `actor.list` and `actor.query` commands alongside
mutation. `actor.list` can filter by a case-insensitive `name_contains`
substring and returns name and handle as separate fields. An agent needs to
discover current handles before it can address an Actor, and it needs to read
current state after changes. The editor's private
snapshot feed is not a Runtime command interface. Gameplay returns value-only
summaries and transform snapshots; list entries are ordered deterministically
by handle. All three commands use the Game lane because `GameplayWorld` owns
the mutable Actor set on that thread.

Use `ActorHandle` (`id` plus `generation`) as the target identity. Names aid
discovery and may be duplicated or changed; filtering never makes a name a
unique control selector.

`actor.control` sets a root SceneComponent's local position and rotation by
handle and preserves its scale. `actor.query` returns both local and world root
transforms when the Actor has a root SceneComponent. Commands must not write
component members directly. If the controlled root is the currently possessed
camera, the Gameplay operation synchronizes PlayerController's control
rotation. The editor keeps its reflected value-edit path and notifies
GameplayWorld after a successful camera rotation edit, so both paths use the
same controller synchronization behavior without making the editor a command
router.

This work does not migrate Gameplay to ECS, expose Actors to RuntimeCommand,
make the editor bridge a command router, or offer arbitrary reflective writes
to Agent callers. Generic reflected property commands remain a separate design
until a concrete consumer requires them.

## Ownership and data flow

```text
Agent JSON line
  -> loopback transport queue
  -> CommandRegistry schema/policy and Game lane
  -> Runtime Gameplay command provider
       |-> actor.list -> value-only name/handle/state summaries
       |-> actor.query -> value-only state and transform snapshot
       `-> actor.control
  -> GameplayWorld control operation
  -> ActorHandle lookup and SceneComponent setter
  -> PlayerController sync when the target is its possessed camera
  -> copied camera/render source values
  -> terminal command result with applied transform
```

The transport thread must never resolve an Actor or touch Gameplay state. The
Runtime provider may hold a registration token for its lifetime, but it should
resolve `GameplayWorld` at dispatch or be removed before that world is
destroyed. No Actor, component, or `ReflectionObjectRef` pointer crosses the
transport or Editor boundary.

## Command contract

Commands:

- `actor.list` is read-only and Agent-allowed. It accepts a bounded
  `offset` and `limit`, and returns live actor handles and states in ascending
  handle order. The optional `name_contains` substring filters matches before
  paging. The initial result uses the existing flat `CommandData` map:
  `count`, `total_count`, `has_more`, and indexed fields such as
  `actors.0.name`, `actors.0.id`, `actors.0.generation`, and
  `actors.0.state`. Keep the maximum page size small and fixed. A
  result-format expansion for nested arrays is not part of GP9.
- `actor.query` is read-only and Agent-allowed. It accepts `id` and
  `generation` and returns state, root presence, and local/world root
  transforms as flat keys when available.
- `actor.control` is mutating and Agent-allowed. It sets the addressed Actor
  root's local position and rotation, preserving scale.

All descriptors use the Gameplay category and Game execution lane. The list and
query commands have no MutatesState flag and require no Mutating capability.
The control command requires Agent allow-list and MutatesState policy; its
schema identifies the Actor by `id` and `generation` and accepts finite numeric
local position and rotation channels. It returns the resolved handle and
actual applied local transform so callers can distinguish accepted state from
requested state.

The Runtime policy layer must continue to require the Mutating capability for
`actor.control`. Handle generation is checked on every request;
display labels and cached pointers are not authoritative. Listing omits
destroyed Actors and never returns pointers. `actor.query` rejects stale
handles. `actor.control` rejects missing/destroyed Actors, missing root
SceneComponents, and invalid numeric values with structured statuses and a
useful diagnostic. A handle may become stale between list and mutation; the
generation check makes that race a deterministic rejection.

## Implementation stages

1. **Gameplay query and operation:** expose bounded value-only summaries for
   live Actor handles, query state and local/world root transforms, and define
   the Actor transform control API. Keep scale and non-transform gameplay
   state unchanged. Synchronize a possessed camera's control rotation.
2. **Runtime adapter:** register the read-only list/query and mutating control
   commands from Runtime after scene services exist, retain their registration
   tokens, resolve Gameplay safely, parse typed arguments, call Gameplay APIs,
   and return bounded result data. Keep `RuntimeCommand` independent of
   Gameplay.
3. **Editor consistency (complete):** the existing reflected edit path notifies
   Gameplay after a successful camera rotation write; snapshot and edit
   feedback semantics remain intact.
4. **Validation (complete):** focused Gameplay, editor bridge, Runtime command,
   and level tests cover handle validation, transform results, Game-lane
   execution, controller synchronization, authorization, and render-source
   propagation. A checked-in level was launched through the local agent port;
   query/control/query and a post-control frame capture succeeded.

## Acceptance criteria

- `commands.list` and `help` expose `actor.list`, `actor.query`, and
  `actor.control` with their schemas in Scene3D mode.
- An authorized Agent can list live Actor names and handles with deterministic
  ordering and bounded results; names assist discovery but are not treated as
  identity.
- `actor.query` returns the current Actor state and value-only root transform
  data; stale handles receive a deterministic not-found result.
- An authorized Agent `actor.control` request changes the addressed Actor's
  local position and rotation through Gameplay's public setters, preserves
  scale, and returns the applied transform.
- A stale generation, invalid numeric value, or absent root component is
  rejected without partial mutation.
- All Actor lookup and mutation happens on `Engine::GameTick()`'s game thread.
- A camera pose update is not overwritten by the next PlayerController look
  update; movement uses the accepted orientation.
- Camera/render source publication reflects the change at the normal Gameplay
  update boundary.
- Existing editor snapshots, property edits, and transform gizmo behavior keep
  their value-only thread boundary.
- The generic command registry and transport contain no Gameplay dependency or
  Actor-specific lookup logic.

## Related design

- [Gameplay architecture](../gameplay_module.md)
- [Gameplay roadmap](../TODO.md)
- [Command architecture](../../command/architecture.md)
- [Reflection architecture](../../reflection/PLANS.md)
