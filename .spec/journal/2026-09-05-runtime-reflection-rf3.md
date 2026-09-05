# Runtime Reflection RF3 Journal

Date: 2026-09-05

## Objective

Provide a value-only bridge between game-thread-owned Gameplay Actors and a
future render-thread Editor panel. RF3 preserves Actor ownership, rejects
stale identities, publishes bounded immutable snapshots, and returns one
terminal result for every accepted property-edit command.

## Design and reference gate

- Read `docs/reflection/AGENTS.md`, `PLANS.md`, `TODO.md`, and `.plan/RF3.md`.
- Compared Godot's remote inspector path: it resolves an object identity again
  at mutation time. RF3 adopts late identity resolution but returns an explicit
  terminal stale-target result instead of silently ignoring a missing object.
- Compared Bevy Remote Protocol: it correlates requests and schedules mutation
  through the owner side. RF3 adopts request correlation and game-thread
  application but remains in-process and uses the existing bounded
  `ReflectionValue` instead of JSON/network serialization.
- Kept the bridge in a Gameplay-owned satellite target. Runtime owns the
  concrete bridge and exposes only snapshot/edit interfaces; Gameplay and
  Reflection do not depend on Editor.

## Landed changes

- Added `ComponentInstanceId`, monotonic per-Actor assignment, read-only
  component access, and private bridge traversal seams. Duplicate component
  types are independently addressable and vector growth does not change IDs.
- Added a collocated GameplayReflection binding manifest. Bindings use exact
  dynamic-type matching and typed ephemeral object-reference makers; bridge
  initialization validates one-to-one agreement with the frozen catalog.
- Added value-only snapshot, command, submission, and result contracts with
  explicit actor/component/property/value and outstanding-edit budgets.
- Added deterministic snapshot construction: Actors sort by handle, components
  retain insertion order, properties retain catalog order, budgets truncate by
  prefix, and unreflected components remain visible with invalid type identity
  and a diagnostic.
- Added game-thread edit pumping with actor-generation/component/type/property
  revalidation, RF2 write/readback, stale/rejected/cancelled terminal statuses,
  and result backpressure until consumers drain results.
- Added reverse catalog-to-binding validation, bounded accounting for display
  names and diagnostics, and focused coverage for read-only properties,
  independent snapshot budgets, request-ID precedence, concurrent loads, and
  idempotent shutdown.
- Integrated bridge construction after frozen reflection, edit pumping before
  Gameplay tick, snapshot publication after tick, and shutdown before Gameplay
  and Reflection destruction. The live `Engine::Initialize()` path now invokes
  this startup step on the main/game thread after presentation startup and
  before level loading.
- Added the focused `GameplayEditorBridgeUnitTest` target and Runtime interface
  lifecycle assertions. Runtime returns mutable narrow bridge interfaces because
  edit submission and result consumption are stateful; no implementation type
  crosses that boundary.
- Added direct standard includes required by MinGW compilation in
  `quaternion.tpp` and `vulkan_device.h`; these compile-hygiene corrections
  were discovered while building the RF3 target and are unrelated to bridge
  policy.

## Validation evidence

- `cmake -S . -B build -G "Visual Studio 17 2022"` — passed.
- `cmake -S . -B build-mingw -G "MinGW Makefiles"` — passed.
- MinGW C++17 `-Wall -Wextra -Wpedantic` syntax checks for the changed bridge,
  Actor, GameplayReflection, focused tests, and RuntimeContext — passed; only
  pre-existing `AssetID`, Runtime member-order, and variadic-macro warnings
  remain.
- MinGW built the complete dependency chain through
  `GameplayEditorBridgeUnitTest`; after an incremental CMake dependency-file
  permission failure, the final test translation unit was compiled and linked
  against the built libraries directly.
- Final bridge executable — passed, 12/12 tests.
- `cmake --build build --config Debug --target GameplayEditorBridgeUnitTest` —
  blocked before source compilation by MSBuild `MSB4184`: access denied while
  probing `C:\Users\17519\AppData\Local\Microsoft SDKs`.
- The incremental MinGW CMake rebuild after the test-helper correction was also
  blocked by a permission-denied write to its generated
  `compiler_depend.make.tmp` file; direct compilation/linking supplied the
  final executable evidence.

## Acceptance assessment

RF3 behavior is landed and covered by the focused bridge executable. The
remaining evidence gap is environmental: native MSVC execution of the bridge,
Runtime startup/lifecycle tests, and the full Debug suite require repair of the
Windows SDK/generated-build-directory permissions. RF4 UI work remains outside
this stage.
