# C++20 Migration Checklist

This checklist tracks the engine's incremental migration to C++20. A module is
not considered modernized merely because it compiles with `/std:c++20`; each
adopted feature must improve a real interface, preserve ownership and lifetime
rules, and have focused validation.

**Baseline:** C++20 is enabled by the root CMake project and the standalone
Live2D probe.

**Started:** 2026-09-14
**Last reviewed:** 2026-09-14
**Owner:** KimPeanutEngine maintainers

## Status legend

- `[ ]` planned or not started
- `[-]` investigated or partially complete
- `[x]` complete with recorded validation

## Global contract

- [x] Root CMake config requests C++20.
- [x] Standalone probe config requests C++20.
- [x] Active README, status, and dependency documentation identify C++20.
- [-] Full Debug build uses MSVC `/std:c++20`; the current build remains
  blocked by the recorded MSVC 14.34 `AssetImport` compiler ICE.
- [ ] Full CTest suite is green; the current configured inventory is 723 tests:
  673 passed, one known fixture failure remains in
  `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures`, and 49 were not run
  because their executables depend on the blocked `AssetImport` or unavailable
  Live2D core build path.
- [x] Replace C++20-deprecated `std::atomic_*` free functions for
  `std::shared_ptr` with `std::atomic<std::shared_ptr<T>>`.
- [ ] Before replacing or removing a legacy raw-pointer interface, audit all
  repository call sites, preserve observable behavior and ownership, add a
  focused compatibility test, and only then change the API. Do not infer a
  performance benefit without measurement.
- [ ] Keep historical `.spec/` journal and review records unchanged; they are
  evidence of their original C++17 validation environment.

## Module checklist

### Project and build system — `C20-BUILD`

- [x] Set the engine baseline to C++20 in `CMakeLists.txt`.
- [x] Reconfigure and confirm generated MSVC projects use `stdcpp20`.
- [ ] Check every maintained standalone target for a local C++17 override.
- [ ] Add a documented compiler minimum for the supported MSVC toolchain.
- [ ] Add a CI or scripted check that rejects accidental C++17 overrides.
- [ ] Validate Debug and Release configurations.

### Runtime core — `C20-CORE`

- [x] Logging accepts `std::string_view` at synchronous boundaries and copies
  into `LogEntry` exactly where ownership begins.
- [-] Logging exposes `LogFormat`/`KP_LOGF` through C++20 `std::vformat`; the
  current MSVC 14.34 library provides `vformat` but does not expose
  `std::format_string`.
- [ ] Migrate legacy `%` call sites to `KP_LOGF` after each consuming module is
  reviewed.
- [x] Audit active common types, math, config, async, data, database, and
  resource utilities for C++20-safe interfaces; opaque native handles and
  queue ownership remain unchanged.
- [x] Math scalar helpers use `std::floating_point` constraints and `constexpr`
  arithmetic where the standard library permits it.
  - [x] Vector, matrix, quaternion, rotator, and transform templates use
    `std::floating_point` constraints instead of class-body type assertions.
  - [x] Vector inputs and matrix rows provide fixed-extent `std::span` boundaries;
    legacy vector raw-data constructors and `Data()` access were removed because
    they could not express or guarantee a valid fixed-size view.
  - [x] Matrix `operator[]` returns a fixed-extent row span, preserving
    `matrix[row][column]` syntax without exposing a raw row pointer.
- [x] Vector component indexing no longer relies on pointer arithmetic across
  separate data members.
- [x] Matrix initializer lists validate their exact element count.
- [x] `AssetImport` compiles with the constrained math templates and the
  centralized Matrix4 implementation unit; standalone Math validation passes.
- [x] Convert the core MurmurHash byte API to `std::span<const std::byte>`;
  return its fixed two-word digest by value without changing hash composition.
- [x] Convert synchronous texture mip source pixels and database BLOB binding
  to borrowed `std::span` inputs; persistent texture/database ownership is
  unchanged.
- [x] Convert synchronous shader-cache writes to `std::span` and explicitly
  copy into the cache owner.
- [x] Review active core `std::byte`, `std::string_view`, and `constexpr`
  opportunities; adopted only boundaries with explicit synchronous lifetime.
- [ ] Do not store a `std::span` across a queue, thread, or asynchronous task
  without an explicit owner/lifetime contract.
- [x] Run focused core unit tests and compile active core targets with warnings
  enabled.

### Asset — `C20-ASSET`

- [x] Pilot `std::span<const std::byte>` for SHA-256/hash input APIs and
  preserve the existing digest algorithm and hash identity.
- [x] Review native model/texture, material, archive, decoder, and staging byte
  interfaces; synchronous readers now accept safe borrowed views without
  changing serialized format versions.
- [x] Keep asset ownership, loading, dependency tracking, and CPU lifetime
  unchanged; read-only spans are copied only at existing ownership boundaries.
- [-] Add empty-input and buffer-boundary tests for migrated APIs; hash and
  native-model coverage pass, while texture/import execution remains blocked by
  the MSVC 14.34 `AssetImport` compiler ICE.
- [-] Run Asset, import, archive, model, texture, and level tests; model
  format tests pass, but `AssetImport`-dependent tests cannot be built until
  the compiler ICE is resolved.

### Resource processing — `C20-RESOURCE`

- [x] Audit shader, material, image, and environment processing interfaces for
  borrowed contiguous input; synchronous engine-owned paths now use spans.
- [x] Use `std::span` only at engine-owned boundaries; adapt to third-party
  pointer-plus-size APIs at the edge.
- [x] Review `std::ranges` or concepts only where they make validation intent
  clearer than existing loops; existing explicit validation remains clearer.
- [x] Verify deterministic output and cache/hash identity remain unchanged in
  focused resource, asset-format, and render-processing tests.

### Graphics/RHI — `C20-GRAPHICS`

- [x] Graphics/RHI and Vulkan source compile under C++20.
- [x] No Graphics-owned published shared-pointer snapshot remains; Render and
  Gameplay publish their snapshots with the C++20 specialization.
- [x] Review synchronous frame upload and buffer initialization APIs: the
  common boundary now uses `std::span<const std::byte>`; mapped addresses and
  backend-internal upload adapters remain raw by design.
- [ ] Keep common contracts free of Vulkan/OpenGL implementation types.
- [x] Validate the common graphics contract and compile both backend adapters.
- [ ] Run OpenGL and Vulkan runtime smoke validation.

### Render — `C20-RENDER`

- [x] Replace `published_metrics_` atomic shared-pointer free functions with
  the C++20 atomic shared-pointer specialization.
- [x] Preserve acquire/release publication semantics and immutable snapshots.
- [x] Audit the light GPU frame builder: consume borrowed light arrays through
  `std::span<const Light>` without retaining the view.
- [ ] Measure any claimed performance improvement; do not infer it from API
  replacement alone.
- [x] Run Render, submission, and graphics contract tests.
- [ ] Run backend smoke validation.

### Gameplay and runtime world — `C20-GAMEPLAY`

- [x] Replace `latest_snapshot_` atomic shared-pointer free functions with the
  C++20 atomic shared-pointer specialization.
- [x] Preserve snapshot immutability, game-thread application, and bridge
  lifetime rules.
- [x] Review component and reflection helper interfaces for `std::span` or
  concepts only when the ownership contract becomes clearer; no additional
  view is safe where the interface stores or queues data.
- [x] Run Gameplay, Gameplay Reflection, and Editor Bridge tests.

### Runtime services — `C20-RUNTIME`

- [x] Audit bootstrap, command, host, input, level, platform, screenshot,
  script, stats, window, and image-I/O paths for local C++17 assumptions.
- [x] Preserve existing `std::string_view` launch/command boundaries and
  owning strings in queued command/input data.
- [x] Use `std::span<const std::byte>` for synchronous in-memory image decode
  and probe; do not retain the view beyond the codec call.
- [x] Review threading code for C++20 deprecations without changing scheduling
  or ownership as part of this migration.
- [x] Run the corresponding focused runtime service tests.

### Editor — `C20-EDITOR`

- [x] Audit editor UI, settings, asset browser, actor, profile, and gizmo code
  for C++17-only assumptions; no unsafe raw-data or deprecated atomic boundary
  was found.
- [x] Use views only for synchronous rendering/UI calls; editor state owns all
  data that persists between frames.
- [x] Review designated-initializer opportunities only where construction sites
  and aggregate layout make the change safe; none was needed for this pass.
- [x] Run editor model, lifecycle, layout, and UI tests.

### Optional modules — `C20-OPTIONAL`

- [x] TTS example compiles under C++20 after explicit `char8_t` to `char`
  conversion at the existing `std::string` boundary.
- [x] Audit Live2D and TTS interfaces for third-party ABI and encoding
  boundaries; engine-owned adapters keep explicit encoding conversions.
- [x] Keep vendored SDK code and third-party source untouched.
- [ ] Run optional-module tests and probes where their external SDKs are
  available.

### Tools and examples — `C20-TOOLS`

- [x] Audit asset tools, graphics examples, audio examples, and command tools
  for C++17 overrides or `u8`/`std::string` assumptions.
- [x] Keep examples aligned with public engine interfaces after the byte-span
  graphics boundary change.
- [ ] Build every maintained tool and example in Debug and Release.

### Tests and fixtures — `C20-TESTS`

- [x] All test targets are generated with C++20 through the root CMake setting.
- [x] Update test helpers only when a production interface is migrated.
- [x] Add compile-time coverage through the C++20-constrained math and span
  interfaces; focused test targets compile those contracts.
- [ ] Separate migration failures from existing fixture/data failures.
- [ ] Reach 723/723 CTest after resolving the missing level archive fixture,
  the AssetImport compiler ICE, and the unavailable Live2D test executable.

### Third-party integration — `C20-THIRDPARTY`

- [x] Confirm the dependencies exercised by the focused Debug targets build or
  link with the C++20 MSVC configuration.
- [x] Keep C++20 changes in engine-owned adapters, not vendored code.
- [x] Record dependency/toolchain limitations before changing public contracts;
  MSVC 14.34 lacks `std::format_string` and still ICEs in `AssetImport`.

## Per-module completion record

When a module is handled, append a short dated entry here with the module ID,
files changed, adopted features, validation commands, and remaining risks. Keep
implementation detail in the normal diff or a dated `.spec/journal/` entry.

| Date | Module ID | Result | Validation | Follow-up |
| --- | --- | --- | --- | --- |
| 2026-09-14 | `C20-BUILD` | C++20 baseline enabled | CMake configure; full Debug build | Check Release and standalone overrides |
| 2026-09-14 | `C20-OPTIONAL` | TTS `char8_t` boundary fixed | `TTSExample` target build | Audit remaining optional-module boundaries |
| 2026-09-14 | `C20-CORE` | Logger view/format boundary added; legacy API preserved | `LogUnitTest` build; `ctest -R LoggerTest` | Migrate module call sites; use `format_string` after toolchain upgrade |
| 2026-09-14 | `C20-CORE` | Math concepts, constexpr helpers, fixed spans, and bounds checks added | `MathUnitTest` build; `ctest -R MathTest` | Investigate MSVC 14.34 `AssetImport` ICE separately |
| 2026-09-14 | `C20-CORE` | MurmurHash input/output migrated to bounded byte span and value-returned digest; shader-cache writes use borrowed spans | `ResourceUnitTest` build; `ctest -R ResourceTest` | Audit remaining core pointer-plus-count APIs |
| 2026-09-14 | `C20-CORE` | Texture mip source and database BLOB inputs migrated to synchronous spans | `RenderPassScheduleTest` build; `ctest -R TextureMipmaps`; `DatabaseUnitTest` build; `ctest -R DatabaseTest` | Continue core audit; preserve owning vectors |
| 2026-09-14 | `C20-CORE` | Active core audit completed; synchronous views and value-returned data adopted without ownership changes | `MathUnitTest`, `ResourceUnitTest`, `DatabaseUnitTest`, `TrieTest`, `RenderPassScheduleTest` builds; focused CTest runs | Full build remains blocked by MSVC 14.34 `AssetImport` ICE |
| 2026-09-14 | `C20-ASSET` | Product/hash, native model/texture/material, decoder, and staging readers migrated to synchronous byte spans | `NativeModelTest` build; `ctest -R NativeModelFormatTest`; `AssetImport` compile reached only the known MSVC 14.34 ICE | Texture/import tests remain blocked by compiler ICE; serialized formats and ownership unchanged |
| 2026-09-14 | `C20-RENDER` | Published metrics use the C++20 atomic shared-pointer specialization; light GPU frame inputs use synchronous spans | `RenderSystemTest` build; `ctest -R RenderSystem` (15/15) | Continue frame upload/readback review; backend smoke validation remains |
| 2026-09-14 | `C20-GRAPHICS` | Common buffer initialization, geometry upload, and frame-buffer write boundaries use `std::span<const std::byte>`; backend-internal raw adapters remain unchanged | `GraphicsContractTest`, `RenderSubmissionContractTest`, `RenderSubmissionExecutionTest`, and `RenderSystemTest` builds; focused CTest (31/31) | Runtime OpenGL/Vulkan smoke validation remains; full build still has the known AssetImport compiler ICE |
| 2026-09-14 | `C20-GAMEPLAY` | Gameplay editor snapshots use the C++20 atomic shared-pointer specialization with original acquire/release publication and shutdown clearing | `GameplayReflectionUnitTest` and `GameplayEditorBridgeUnitTest` builds; focused CTest (16/16) | Component/reflection view changes were not justified by ownership boundaries |
| 2026-09-14 | `C20-RUNTIME` | Synchronous image-memory decode/probe inputs use byte spans; queued command/input text remains owning | `ImageIOUnitTest` build; `ctest -R ImageIOTest` (4/4) | Continue runtime service smoke validation if external backends are available |
| 2026-09-14 | `C20-EDITOR/TOOLS/TESTS` | Audited editor/tool/optional boundaries, kept third-party code unchanged, and aligned test doubles with migrated interfaces | Editor model CTest (45/45); focused C++20 targets | Full maintained-tool Debug/Release matrix and backend runtime smoke remain |
