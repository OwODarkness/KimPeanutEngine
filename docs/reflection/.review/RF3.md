# RF3 Review — Gameplay Editor Bridge

- Task: RF3
- Plan: [RF3 — Gameplay Editor Bridge](../.plan/RF3.md)
- Review date: 2026-09-05
- Review status: resolved; native MSVC target validation remains
  environment-blocked

## Scope and baseline

Reviewed component identity, GameplayReflection binding ownership, snapshot and
edit contracts, bounded queue behavior, Runtime lifecycle integration, tests,
and documentation against the RF3 plan and Runtime/Gameplay ownership rules.
RF4 Editor panels, selection, and ImGui policy remain out of scope.

## Findings and resolutions

### RF3-R1 — Resolved: bridge traversal required complete component types

The bridge's private root-component traversal now includes the complete
`SceneComponent` definition. No public component container or raw pointer is
exposed to consumers.

### RF3-R2 — Resolved: edit tests used the wrong property-ID construction

The test now obtains the descriptor by stable catalog property name and uses
the descriptor's canonical type-qualified property ID.

### RF3-R3 — Resolved: binding and request validation were tightened

Bridge initialization validates valid catalog IDs, exact canonical names,
unique resolved types, and all object-maker functions. Duplicate request IDs
are checked before capacity, so malformed retries return `InvalidArgument`
even when the outstanding queue is full.

### RF3-R4 — Resolved: snapshot retention used reclaimed storage

The snapshot-retention test captures component IDs before Actor reclamation and
only inspects copied snapshot data after the Actor is destroyed. It also checks
that handle-slot reuse produces a new generation.

### RF3-R5 — Resolved: rejection, budget, and concurrency coverage was incomplete

The focused suite now covers stale components, stale Actor generation with
slot reuse, reflected-type mismatch, unknown properties, conversion failure,
setter rejection, read-only properties, duplicate request IDs, queue
backpressure, independent Actor/component/property/value-byte budgets,
wrong-thread pumping, concurrent snapshot loads, terminal results, and
idempotent shutdown.

### RF3-R6 — Resolved: duplicate requests were reported as queue-full

Submission checks `outstanding_request_ids_` before the capacity check. The
queue test now verifies duplicate IDs return `InvalidArgument`, while a new ID
at capacity returns `QueueFull`.

### RF3-R7 — Resolved: copied strings were outside the byte budget

Actor display names, component diagnostics, property diagnostics, and
`ReflectionValue` payloads now pass through the same bounded byte accounting.
Strings that do not fit are omitted, snapshot truncation is recorded, and
`omitted.value_bytes` is updated with saturating arithmetic.

### RF3-R8 — Resolved: binding validation was one-directional

Bridge initialization now checks both directions: every declared binding must
resolve uniquely, and every catalog type must have exactly one matching binding.
The focused suite also explicitly compares the complete binding manifest with
the frozen catalog.

## Architecture assessment

The bridge remains an adapter around existing ownership rather than a second
world model. Actor and component memory stay game-thread-owned; Runtime owns
bridge lifetime; Reflection owns descriptors and access capability. Consumer
interfaces contain only copied values, IDs, diagnostics, and commands. No
EnTT, `ReflectionObjectRef`, Gameplay container, or mutable object pointer
crosses the consumer boundary.

The live Engine startup constructs the bridge on the main/game thread after
presentation startup and before level loading. `TickGameplay` pumps edits before
Gameplay and publishes snapshots after ticking and reclamation. Shutdown closes
submission, cancels pending commands, clears the latest snapshot, and runs
before Gameplay and Reflection teardown.

## Validation

- CMake configure for Visual Studio 17 2022 and MinGW Makefiles — passed.
- MinGW C++17 `-Wall -Wextra -Wpedantic` syntax checks for the bridge
  implementation and focused tests — passed.
- Final manually compiled and linked `GameplayEditorBridgeUnitTest` — passed,
  12/12 tests.
- MSVC target build — blocked before source compilation by MSBuild `MSB4184`
  while probing `C:\Users\17519\AppData\Local\Microsoft SDKs`.
- Native Runtime/full-suite execution remains unverified because of that
  environment blocker; this is not reported as source-test success.

## Review conclusion

No unresolved RF3 source finding remains. RF3 is ready for RF4 consumer work;
native MSVC and Runtime lifecycle evidence should be rerun after the Windows
SDK/generated-build-directory permissions are repaired.
