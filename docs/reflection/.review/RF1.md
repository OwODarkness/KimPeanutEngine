# RF1 Review — Reflection Contracts and EnTT Adapter

- Task: RF1
- Plan: [RF1 — Reflection Contracts and EnTT Adapter](../.plan/RF1.md)
- Review date: 2026-09-05
- Review status: resolved (2026-09-05)

## Scope and baseline

Reviewed the RF1 public value, descriptor, catalog, access, lifecycle, EnTT
adapter, CMake, and focused-test changes against the RF1 plan and the Reflection
module ownership rules. Gameplay registration, the editor bridge, and Actor UI
remain outside this review.

The baseline is the uncommitted RF1 implementation present in the worktree on
2026-09-05. Unrelated worktree changes were not reviewed.

## Evidence inspected

- `docs/reflection/AGENTS.md`, `PLANS.md`, `TODO.md`, and `.plan/RF1.md`
- `.spec/specs/runtime-reflection-module.md`
- `.spec/journal/2026-09-05-runtime-reflection-rf1.md`
- `engine/runtime/reflection/` and `engine/test/unit/reflection/`
- Direct CMake wiring in `engine/runtime/CMakeLists.txt` and
  `engine/test/unit/CMakeLists.txt`
- Vendored EnTT 3.16.0 meta factory behavior used by the adapter

## Findings

### RF1-R1 — Resolved (P1): object/type mismatches can cause undefined behavior

`ReflectionObjectRef::ForMutable` and `ForConst` accept an arbitrary
`ReflectionTypeId` beside a type-erased address. `Read` and `Write` validate
only that the supplied ID is registered, then their callbacks cast the address
to the C++ type associated with that ID. They cannot establish that the address
actually points to that type. A mismatched component/type mapping therefore
reaches an invalid `static_cast` and dereference instead of returning the
declared `WrongObjectType` status.

Evidence:

- `engine/runtime/reflection/reflection_types.h:233`
- `engine/runtime/reflection/entt/entt_reflection_registry.cpp:318`
- `engine/runtime/reflection/entt/entt_reflection_registry.h:98`
- `engine/runtime/reflection/entt/entt_reflection_registry.h:148`

RF1 should bind verifiable C++ type identity when an object reference is
created, or restrict construction to a registry/typed factory that can reject a
mismatch before any cast.

### RF1-R2 — Resolved (P1): floating-to-integer range checks admit out-of-range values

The conversion path compares a `double` with integer bounds after converting
those bounds to `double`. `INT64_MAX` rounds to `2^63` and `UINT64_MAX` rounds
to `2^64`, so the values `2^63` and `2^64` respectively pass the checks before
being cast to an integer type that cannot represent them. This defeats the
structured conversion-failure contract and can invoke undefined behavior for a
property edit at the boundary. Floating-point target conversion also accepts
non-finite or out-of-range results without validation.

Evidence:

- `engine/runtime/reflection/reflection_types.h:348`
- `engine/runtime/reflection/reflection_types.h:374`
- `engine/runtime/reflection/reflection_types.h:382`

Use a range check whose boundary is representable in the source domain (or a
checked numeric conversion), and add boundary tests for every supported numeric
category.

### RF1-R3 — Resolved (P2): freeze does not validate descriptor access flags

Registration accepts caller-supplied flags without checking their relationship
to the registered callbacks, and `Read` invokes a property's reader without
checking `ReflectionPropertyFlags::Readable`. Conversely, `ReadOnly` accepts an
explicit `Writable` flag even though it stores no writer, so the frozen
descriptor can advertise a capability that access rejects. `Freeze` only sorts
and rebuilds lookup tables; it performs no semantic descriptor validation.

Evidence:

- `engine/runtime/reflection/entt/entt_reflection_registry.cpp:133`
- `engine/runtime/reflection/entt/entt_reflection_registry.cpp:199`
- `engine/runtime/reflection/entt/entt_reflection_registry.cpp:326`
- `engine/runtime/reflection/entt/entt_reflection_registrar.h:151`

Normalize or reject inconsistent flags during registration/freeze and enforce
the readable capability in `Read`.

### RF1-R4 — Resolved (P2): the recorded test claim exceeds the exercised cases

The roadmap and project status state that duplicate names and the planned error
surface are covered, but the five tests exercise a forced type-ID collision and
an unknown property only. They do not exercise duplicate type names, duplicate
property names, a property-ID collision, unknown object type, wrong object
type, invalid numeric boundaries, or late property registration through a
previously returned type registrar. The focused suite therefore does not yet
prove the RF1 validation list or the claimed structured-failure behavior.

Evidence:

- `engine/test/unit/reflection/reflection_registry_test.cpp:83`
- `engine/test/unit/reflection/reflection_registry_test.cpp:173`
- `docs/reflection/TODO.md:22`
- `docs/status.md:27`

Add the missing contract cases and correct landed claims until the resulting
tests have actually run.

## Validation performed

- `.\tools\kp.ps1 build ReflectionUnitTest` — blocked before compilation by
  MSBuild `MSB4184`: access denied while probing
  `C:\Users\17519\AppData\Local\Microsoft SDKs`.
- `ctest --test-dir build -C Debug -R ReflectionRegistry --output-on-failure`
  — initial baseline passed, 5/5, before the resolution round.
- `g++ -std=c++17 -Wall -Wextra -Wpedantic ... -fsyntax-only` for the two
  Reflection `.cpp` files — passed with no diagnostics.

The MSVC failure is an environment blocker, not evidence of a source failure.
The initial review used focused validation because RF1 is not yet a shared
Runtime dependency; the resolution round added and reran the missing contract
cases below.

## Resolution round

- **RF1-R1:** `ReflectionObjectRef` now binds the C++ `type_info` at its typed
  factory boundary. Registry access compares that identity with the registered
  type before any callback can cast the address, returning `WrongObjectType` on
  mismatch. The test covers both wrong registered type and unknown type IDs.
- **RF1-R2:** floating-to-integral conversion now uses exclusive power-of-two
  bounds in the source `double` domain, avoiding rounded `INT64_MAX` and
  `UINT64_MAX` comparisons. Floating targets reject non-finite and overflowed
  results. Boundary tests cover signed 64-bit, unsigned 64-bit, float overflow,
  and infinity.
- **RF1-R3:** registration normalizes flags to the available callbacks,
  `Freeze` validates the invariant, and `Read` enforces `Readable`. A
  read-only property that requests `Writable` is published without that flag.
- **RF1-R4:** the focused suite now covers duplicate type/property names,
  forced property-ID collision, unknown and wrong object types, numeric
  boundaries, and late property registration through a returned registrar.

The corrected MinGW C++17 focused build passes 6/6 with `-Wall -Wextra
-Wpedantic`. The MSVC SDK probe remains an environment-only blocker.

## Acceptance assessment

The dedicated target, dependency direction, public consumer vocabulary,
explicit EnTT context, transactional publication, deterministic enumeration,
idempotent shutdown, object/type validation, checked conversion boundaries,
descriptor access invariants, and focused evidence are present. RF1 is accepted
with the MSVC build verification remaining environment-blocked.
