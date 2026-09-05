# RF2 Review — Gameplay Registration and Access

- Task: RF2
- Plan: [RF2 — Gameplay Registration and Access](../.plan/RF2.md)
- Review date: 2026-09-05
- Review status: resolved (2026-09-05); native execution remains environment-blocked

## Scope and baseline

Reviewed the RF2 metadata vocabulary, static/free-function registrar support,
Gameplay-owned registration satellite, Runtime composition, and focused tests
against the RF2 plan, Reflection boundaries, and Gameplay ownership/lifecycle
rules. RF3 snapshots/edit queues and RF4 Editor widgets remain outside scope.

The baseline is the uncommitted RF1/RF2 implementation present in the
worktree on 2026-09-05. Unrelated worktree changes were not reviewed.

## Evidence inspected

- `docs/reflection/AGENTS.md`, `PLANS.md`, `TODO.md`, and `.plan/RF2.md`
- `docs/gameplay/AGENTS.md`, `PLANS.md`, and `gameplay_module.md`
- `.spec/specs/runtime-reflection-module.md`
- `.spec/journal/2026-09-05-runtime-reflection-rf2.md`
- Reflection metadata, registrar, registry, lifecycle, and focused tests
- Gameplay Reflection registration units and focused tests
- Scene, mesh, light, and camera component setter/source-update paths
- Runtime Reflection construction, initialization, publication, and teardown

## Findings

### RF2-R1 — Resolved (P1): the current GameplayReflection suite failed its catalog test

`RegistersCanonicalFlatComponentCatalog` expects
`kpengine.gameplay.SceneComponent` not to contain `transform.scale.x`. The RF2
plan explicitly requires all location, rotation, and scale leaves on
`SceneComponent`, and `RegisterActorReflection` correctly registers all three
families. The assertion therefore contradicts both the governing plan and the
implementation and currently fails the required focused suite.

Evidence:

- `engine/test/unit/gameplay_reflection/gameplay_reflection_test.cpp:177`
- `engine/runtime/gameplay/reflection/actor_reflection.cpp:23`
- `docs/reflection/.plan/RF2.md:191`

The assertion now requires the SceneComponent scale leaves. The catalog test
also checks the complete flat property set and uniqueness of all property IDs.

### RF2-R2 — Resolved (P2): the focused tests did not prove the planned Gameplay behavior matrix

The three GameplayReflection tests cover catalog presence, one Mesh location
and LOD update, one PointLight range update, and one Camera FOV update. They do
not exercise exact property sets/IDs, child transform propagation,
visibility/shadow writes, DirectionalLight or SpotLight writes, attached light
world transforms, `+X`-forward direction publication, cone relationships,
camera plane/projection relationships, wrong-thread Gameplay access, or const
Gameplay reads. Moreover, each invalid-write check follows a valid write before
the same tick; coalescing therefore prevents the test from proving that the
rejected write independently produced no source update.

Evidence:

- `engine/test/unit/gameplay_reflection/gameplay_reflection_test.cpp:145`
- `engine/test/unit/gameplay_reflection/gameplay_reflection_test.cpp:182`
- `engine/test/unit/gameplay_reflection/gameplay_reflection_test.cpp:217`
- `docs/reflection/.plan/RF2.md:368`

The focused suite now covers exact property sets, attached transform
propagation, mesh visibility/shadow edits, directional and spot light source
publication, attached point/spot world transforms, cone rejection, camera
projection and near/far rejection, const reads, and wrong-thread access. Each
rejection case uses a clean tick/update baseline so it proves that the rejected
write did not publish a source update.

### RF2-R3 — Resolved (P2): getter/setter pairs could publish a false value contract

The two-accessor `Property` overload derives the descriptor value type from the
getter, while `WriteSetter` converts according to the setter argument. It does
not verify that the getter's clean return type, setter argument type, and
registered component type agree. A probe pairing a `float` getter with an
`int` setter compiles successfully, producing a floating-point descriptor that
rejects fractional values and invokes an integer setter. Metadata and access
semantics can therefore disagree even though registration and freeze succeed.

Evidence:

- `engine/runtime/reflection/entt/entt_reflection_registrar.h:153`
- `engine/runtime/reflection/entt/entt_reflection_registrar.h:164`
- `engine/runtime/reflection/entt/entt_reflection_registrar.h:285`

The two-accessor registrar overload now requires, at compile time, that the
getter and setter are invocable for the reflected component and that their
clean value types are identical. Numeric cross-type accessor pairs are not
implicitly accepted; conversion remains an edit-time `ReflectionValue`
conversion concern after registration has established one value contract.

### RF2-R4 — Resolved (P2): light metadata did not match the setter domain

The `light.range` setter requires a finite value strictly greater than zero,
but its descriptor has no minimum. Spot cone setters accept every finite
`float` below `pi/2` that satisfies the inner/outer relationship, while the
metadata maximum is `pi/2 - 0.000001`, excluding additional representable
values that the setter accepts. This conflicts with RF2's rule that metadata
limits match setter acceptance and can make the future inspector advertise too
wide a range or prevent valid edits.

Evidence:

- `engine/runtime/gameplay/reflection/light_reflection.cpp:11`
- `engine/runtime/gameplay/reflection/light_reflection.cpp:74`
- `engine/runtime/gameplay/reflection/light_reflection.cpp:89`
- `engine/runtime/gameplay/reflection/light_reflection.cpp:131`
- `engine/runtime/gameplay/reflection/light_reflection.cpp:137`
- `docs/reflection/.plan/RF2.md:231`

Range metadata now uses the smallest positive finite `float` accepted by the
strictly-positive setter. Spot-cone metadata uses `std::nextafter(pi/2, 0)`;
this is the largest representable `float` accepted by the exclusive setter
bound. The catalog test checks both metadata boundaries.

## Validation performed

- `ctest --test-dir build -C Debug -R
  "^(ReflectionRegistry|GameplayReflection|GameplayWorldTest|RuntimeStartupTest)\."
  --output-on-failure` — failed, 38/39 passed. The only failure is RF2-R1.
- `.\tools\kp.ps1 build ReflectionUnitTest GameplayReflectionUnitTest
  GameplayUnitTest RuntimeStartupTest` — the wrapper attempted the first target
  and was blocked before compilation by MSBuild `MSB4184`: access denied while
  probing `C:\Users\17519\AppData\Local\Microsoft SDKs`.
- MinGW C++17 `-Wall -Wextra -Wpedantic` syntax checks for Reflection,
  GameplayReflection, and both focused test translation units — passed; only
  the pre-existing `AssetID` initialization-order warning remains.
- A C++17 compile probe for a mismatched `float` getter/`int` setter pair —
  passed, confirming RF2-R3.

The MSVC failure is an environment blocker rather than a source failure. The
CTest failure was a baseline RF2 source/test-contract failure; the resolution
round below corrects the assertion and expands the focused coverage.

## Resolution validation

- MSVC `/Zs` syntax compilation of the Reflection implementation, all split
  GameplayReflection translation units, the GameplayReflection test, and
  RuntimeContext — passed after the RF2-R3 template constraints and RF2-R4
  metadata correction.
- MinGW C++17 `-Wall -Wextra -Wpedantic` syntax compilation of the same sources
  — passed; only the pre-existing `AssetID` initialization-order warning
  remains.
- The existing standalone Reflection executable — passed, 8/8.
- CMake configure — passed, including propagation of the public EnTT include
  path to GameplayReflection.
- Rebuilding and executing the GameplayReflection and Runtime focused targets
  remains blocked before compilation by MSBuild `MSB4184` while probing the
  local Windows SDK path. The source fixes are therefore syntax-validated but
  not claimed as native runtime execution evidence.

## Acceptance assessment

The ownership shape is sound: core Reflection does not depend on Gameplay;
`GameplayReflection` owns Gameplay type knowledge; Runtime publishes only the
catalog and destroys Gameplay before Reflection. The intended six flat
component registrations and behavior-preserving setter paths are present.

The four review findings are resolved in source and covered by the expanded
focused test. RF2's remaining evidence gap is environmental: native execution
of the GameplayReflection and Runtime focused binaries is pending repair of
the Windows SDK permission failure.
