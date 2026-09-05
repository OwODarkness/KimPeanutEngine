# Runtime Reflection RF1 Journal

Date: 2026-09-05

## Objective

Land the first Runtime Reflection stage without coupling Reflection to
Gameplay, Editor, Asset, Render, Graphics, ImGui, or ECS ownership. The stage
must provide stable consumer contracts, an explicit EnTT context, transactional
registration, deterministic freeze, and controlled owner-thread access.

## Design and reference gate

- Read `docs/reflection/AGENTS.md`, `PLANS.md`, `TODO.md`, and `.plan/RF1.md`.
- Kept the public consumer vocabulary in `reflection_types.h`,
  `i_reflection_catalog.h`, and `i_reflection_access.h`; EnTT appears only in
  the adapter headers and implementation.
- Followed the existing reference synthesis in `docs/engine-reference/README.md`
  and `docs/reflection/PLANS.md`: explicit EnTT context ownership is adopted,
  while direct editor access and ECS migration are not. The vendored EnTT
  `meta_ctx`/factory implementation was inspected to verify context-aware
  registration and access behavior.

## Landed changes

- Added the standalone `Reflection` CMake target under
  `engine/runtime/reflection/`.
- Added strong 32-bit type/property IDs, bounded scalar/string values,
  descriptors, flags, structured statuses, and ephemeral owner-thread object
  references.
- Added `IReflectionCatalog` and `IReflectionAccess` with no EnTT or ImGui
  types in their headers.
- Added `EnttReflectionRegistry` and `EnttReflectionRegistrar` with explicit
  `entt::meta_ctx` ownership, stable registration names, duplicate/collision
  checks, deterministic sorting, freeze rejection, and idempotent shutdown.
- Added `ReflectionSystem` to build a candidate registry, publish only after
  successful freeze, and discard failed initialization candidates.
- Added `ReflectionUnitTest` coverage for context isolation, deterministic
  descriptors, scalar/string reads and writes, read-only and setter-rejected
  properties, conversion errors, freeze rejection, rollback, collisions,
  shutdown, and owner-thread enforcement.

## Validation evidence

- `cmake -S . -B build -G "Visual Studio 17 2022"` — passed.
- `cmake --build build --config Debug --target ReflectionUnitTest -- /m:1` —
  blocked before compilation by MSVC's denied Windows SDK probe at
  `Microsoft.Cpp.WindowsSDK.props`.
- MinGW C++17 syntax check of the Reflection sources and test — passed.
- MinGW C++17 build and execution of `ReflectionUnitTest` — passed, 5/5.

## Remaining risk

The MSVC focused target and CTest discovery remain unverified because the
machine denies the Windows SDK path probe. RF2 must add module-owned Gameplay
registration and behavior-preserving component access; this stage intentionally
does not change Gameplay, Runtime startup, or Editor behavior.

## Review correction — 2026-09-05

The RF1 review identified four contract gaps. The implementation now binds
typed `type_info` to object references and rejects wrong-object access before
any type-erased cast; uses exclusive power-of-two bounds for floating-to-integer
conversion and rejects non-finite/overflowed floating targets; normalizes and
validates access flags at registration/freeze and enforces readability; and
extends the focused tests for duplicate names, property-ID collisions, unknown
and wrong object types, numeric boundaries, and late registration.

Validation after the correction: MinGW C++17 build with
`-Wall -Wextra -Wpedantic` and execution of `ReflectionUnitTest` passed 6/6.
The MSVC target remains blocked before compilation by the denied Windows SDK
probe.
