# Runtime Reflection RF2 Journal

Date: 2026-09-05

## Objective

Register editor-relevant Gameplay properties through the RF1 contracts without
coupling the core Reflection target to Gameplay, and compose the frozen
catalog into Runtime startup while preserving component validation and copied
render-source publication.

## Design and reference gate

- Read `docs/reflection/AGENTS.md`, `PLANS.md`, `TODO.md`, and `.plan/RF2.md`.
- Inspected EnTT 3.16's `meta_factory` registration form and kept the native
  static/free getter-setter path; the engine-side registrar only adds compile-
  time traits and descriptor callbacks.
- Compared the metadata-driven editor pattern in Piccolo with the local
  boundary. RF2 adopts metadata as neutral catalog data, while live object
  ownership and future snapshot/edit transport remain deferred to RF3.
- Kept Gameplay knowledge in the `GameplayReflection` satellite. The core
  `Gameplay` target does not depend on Reflection.

## Landed changes

- Added owned `ReflectionPropertyMetadata`, widget semantics, numeric hints,
  enum options, and freeze-time validation for inconsistent metadata.
- Extended `EnttReflectionRegistrar` with free getter/setter adapters while
  preserving the existing member-function API.
- Added `GameplayReflection` with canonical registrations for Scene, Mesh,
  DirectionalLight, PointLight, SpotLight, and Camera components. Adapters use
  public component APIs and expose flat scalar leaves only.
- Added Runtime reflection ownership, startup initialization before
  presentation, read-only catalog access, and teardown after Gameplay.
- Added focused Reflection metadata/adapter tests, GameplayReflection source
  behavior tests, and Runtime startup/teardown coverage.

## Architecture refinement

The Gameplay satellite was decomposed after the initial RF2 implementation:

- `actor_reflection.cpp` owns Scene and Mesh registrations.
- `light_reflection.cpp` owns Directional, Point, and Spot registrations.
- `camera_reflection.cpp` owns Camera registration.
- `gameplay_reflection.cpp` is only the deterministic module aggregator.
- `gameplay_reflection_internal.h` contains private shared transform
  registration helpers and does not become a consumer-facing API.

This keeps per-type knowledge local while preserving one Runtime registration
entry point and the existing Reflection-to-Gameplay dependency direction.

MSVC syntax checking then exposed a portability gap in the free-function
registrar traits: MSVC forms `decltype(Getter)` and `decltype(Setter)` as
function references for these non-type template arguments. Reference-form
traits were added alongside the pointer forms, preserving the same static
adapter API on both compilers.

## Validation evidence

- `cmake -S . -B build -G "Visual Studio 17 2022"` — passed.
- MinGW C++17 `-Wall -Wextra -Wpedantic` syntax checks for Reflection,
  ReflectionSystem, all split GameplayReflection translation units, the
  GameplayReflection test, and RuntimeContext —
  passed; only pre-existing `AssetID` member-order warnings remain.
- `build/RF2-ReflectionUnitTest-mingw.exe --gtest_color=no` — passed, 8/8.
- `cmake -S . -B build -G "Visual Studio 17 2022"` after the split — passed.
- `cmake --build build --config Debug --target ReflectionUnitTest
  GameplayReflectionUnitTest RuntimeStartupTest -- /m:1` — blocked before
  compilation by `MSB4184`: Visual Studio cannot access
  `C:\Users\17519\AppData\Local\Microsoft SDKs` while evaluating
  `Microsoft.Cpp.WindowsSDK.props`.

## Review correction — 2026-09-05

- Corrected the SceneComponent catalog expectation and expanded the
  GameplayReflection test matrix for exact property sets, unique property IDs,
  attached transforms, all light families, cone/projection/plane rejection,
  const reads, wrong-thread access, and clean rejected-write baselines.
- Added compile-time accessor constraints requiring component invocability and
  identical getter/setter value types.
- Aligned range metadata with the smallest positive accepted `float` and cone
  metadata with `std::nextafter(pi/2, 0)`.
- MSVC `/Zs` syntax compilation of the complete RF2 source/test set passed;
  the rebuilt standalone Reflection test passed 8/8. Native GameplayReflection
  execution remains blocked by the Windows SDK permission failure.

## Remaining risk

Native execution of the GameplayReflection and Runtime startup tests remains
unverified until the Windows SDK permission issue is fixed. The contributor
collection seam, Actor/component snapshots, queued edits, and Editor bridge
remain intentionally deferred to later reflection stages.
