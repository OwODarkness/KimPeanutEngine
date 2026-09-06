# MI1.7 — Runtime Migration Overview Review

- Review date: 2026-09-06
- Task: [MI1.7 — Runtime Migration](../.plan/MI1.7.md)
- Parent design: [MI1 — Content-Addressed Native Model Import](../.plan/MI1.md)
- Review status: **changes required before implementation**

## Review scope and baseline

This review covers the complete handoff from the landed MI1.6 offline importer
to the MI1.7 runtime migration:

```text
foreign source package
  -> ModelImportService
  -> immutable archive products
  -> hash-named .model
  -> NativeModelLoader
  -> AssetManager dependency graph
  -> LevelInstance material selection
  -> Gameplay / Render
```

The baseline is the working tree after MI1.1–MI1.6. It already contains
`ModelImportService`, native Model V1, native Material V2, archive publication,
and `.model` dispatch. All reviewed MI1 files are uncommitted or modified, so
this review does not treat the working tree as a clean release baseline.

Evidence inspected:

- `engine/runtime/asset/model_import_service.cpp`
- `engine/runtime/asset/native_model.cpp` and `native_model_loader.cpp`
- `engine/runtime/asset/asset_manager.cpp` and `asset.h`
- `engine/runtime/asset/material_loader.cpp` and `native_material.cpp`
- `engine/runtime/asset/level_loader.cpp`
- `engine/runtime/level/level_instance.cpp`
- `engine/runtime/render_asset_preparer.cpp`
- Asset and runtime-level tests, current Levels, CMake targets, and `.gitignore`
- MI1 parent/stage plans, spec, journal, status, and validation matrix

No implementation was performed as part of this review.

## Review validation performed

- `git diff --check` reported no whitespace errors. Git emitted existing
  line-ending and inaccessible global-ignore warnings.
- `ctest --test-dir build -C Debug --output-on-failure -R
  "NativeModelFormatTest|NativeModelLoaderTest"` passed 4/4 tests.
- The five `ModelImportServiceTest` cases passed.
- The current `RuntimeLevelTest` executable passed 22/25 cases. Three baseline
  cases failed:
  `InstantiatesNonMeshRecordsAndAllowsEmptyInstance`,
  `EnvironmentRegistrationFailureRollsBackMixedActorsAndRetries`, and
  `SpotFactoryFailureRollsBackAndRetriesImmediately`. These failures were
  observed in the pre-existing dirty working tree and were not diagnosed or
  changed by this documentation-only review. MI1.7 must begin from a rebuilt,
  understood RuntimeLevel baseline rather than accepting them silently.
- No build, runtime launch, backend smoke, or visual capture was performed;
  those are implementation evidence, not evidence that this review document is
  well formed.

## Readiness verdict

The importer-to-product half is ready to feed MI1.7, and the runtime material
dependency path can work without querying SQLite. MI1.7 is not yet safe to
implement from its current stage page, however. The stage contract does not
resolve the build-boundary contradiction, does not define the material merge
algorithm, and does not require transactional cleanup of loader-created child
assets. The repository also cannot currently check in the native products that
the stage requires.

Resolve findings R1–R4 in the MI1.7 plan before editing runtime behavior. R5–R7
may be resolved in the same plan revision, but their tests must be present
before MI1.7 is declared landed.

## MI1.1–MI1.6 risk carry-forward ledger

This is not a retroactive rejection of the landed stages. Each earlier stage
met its bounded assignment. The table records only risks and deferred evidence
that cross the MI1.7 runtime-migration seam; the implementation history remains
in the [MI1 journal](../../../.spec/journal/model-import.md).

| Source stage | Landed guarantee | Risk carried into MI1.7 or MI1.8 | Review mapping |
|---|---|---|---|
| [MI1.1](../.plan/MI1.1.md) | Minimal OBJ, FBX, GLTF, and generated GLB fixtures freeze the legacy Assimp geometry/material/failure contract; STL runtime rejection is explicit. | The fixtures prove characterization, not native-product parity, packaging, or rendering. The GLB fixture is materialized by a test, and no tracked runtime Level covers FBX or GLB independently. Retained foreign routes still need deprecation and regression evidence. | R6, R7 |
| [MI1.2](../.plan/MI1.2.md) | Stable hashes, canonical product paths, SQLite schema/repository, transactions, and archive diagnostics are available to import code. | SQLite remains compiled/linked through the same `Asset`/`Core` build boundary consumed by Runtime. Archive integrity/rebuild is tooling work, and the database remains local derived authoring state rather than a runtime recovery source. | R1, R6 |
| [MI1.3](../.plan/MI1.3.md) | Assimp decoding is a pure import document with source dependency discovery and stable failures for the characterized formats. | Support is intentionally static-mesh and semantic-subset based. Assimp-version behavior and dependency discovery beyond the minimal fixtures are not broad compatibility proof. The legacy runtime adapter still calls this decoder directly. | R1, R6, R7 |
| [MI1.4](../.plan/MI1.4.md) | Native Model V1 has canonical bytes, defensive parsing, an integrity digest, and ordered typed Material references. | Runtime loading does not verify that the product hash matches the filename, assumes the archive layout implicitly, and registers a Mesh before the owning Model transaction succeeds. The loader test bypasses full AssetManager integration. | R3, R5, R7 |
| [MI1.5](../.plan/MI1.5.md) | Deterministic Material V2 conversion preserves supported PBR channel/color-space, alpha, embedded-image, and deduplication semantics. | Emissive textures remain a precise import failure. External textures remain ordinary Asset paths outside the immutable archive, so runtime packaging must include them. Runtime archive-Material hash-name verification is absent, and visual channel/alpha fidelity is not yet proven end to end. | R4, R5, R7 |
| [MI1.6](../.plan/MI1.6.md) | The importer verifies cache hits, stages privately, publishes immutable products, and commits archive roots last while preserving the previous committed root on failure. | There is no external CLI yet. Per-source locking is per service instance; cross-process safety depends on create-if-absent publication and SQLite transactions. A crash can intentionally leave unreachable products, and final integrity/rebuild UX remains MI1.8. The importer still shares the runtime Asset target. | R1, R4, R6, R7 |

Cross-stage conclusions:

- No MI1.1–MI1.6 risk requires reopening the native file formats before MI1.7.
- R1, R3, and R5 are integration defects exposed by composing otherwise
  bounded stages; they were not part of MI1.2/MI1.4's isolated done checks.
- Emissive-texture support, archive cleanup, database rebuild UX, and a public
  CLI remain deliberate MI1.8 or later work. MI1.7 must preserve their precise
  failure behavior but must not absorb those features.
- MI1.7 must not claim full OBJ/FBX/GLTF/GLB/STL end-to-end compatibility from
  the earlier focused tests. That final evidence belongs to MI1.8, while MI1.7
  owns native runtime parity and regression coverage for any direct foreign
  path it retains.

## Current flow

### Offline importer and archive

`ModelImportService` normalizes an Asset-root-relative source, probes the prior
SQLite record, verifies recorded dependencies and products, and either returns
`UpToDate` or decodes through Assimp. On a miss it converts Materials/images,
serializes a Model whose ordered material table contains typed content hashes,
publishes immutable products, and updates the database root last. This is the
correct authoring-side owner.

The runtime does not need the source row or material names. Given
`.archive/models/<hash>.model`, `NativeModelLoader` derives each dependency as
`.archive/materials/<material-hash>.material`. `MaterialLoader` then resolves
the generated Material's shader and texture references relative to the
Material file. AssetManager recursively loads those declared paths and records
the resulting AssetIDs as ordinary dependency edges. That data flow requires
no SQLite query.

### Runtime registration and consumption

`.model` already dispatches to `NativeModelLoader`; OBJ, FBX, GLTF, and GLB
still dispatch to the Assimp adapter. The native loader deserializes geometry,
registers a Mesh immediately, stores that Mesh as dependency zero, and declares
Material requests after it. AssetManager resolves declared requests outside
`load_mutex_`, registers the Model, and adds reverse references.

`RenderAssetPreparer` recursively visits all Asset dependency edges, so native
Material, shader, and texture payloads can reach the immutable render catalog.
The missing link is `LevelInstance`: it resolves only Level-authored fallback
and per-slot Material dependencies. It never reads the native Model's material
dependency indices.

## Findings

### MI1.7-R1 — P1 — Runtime and offline-import build ownership are not separated

Status: open; blocks implementation.

The `Asset` target compiles `model_archive.cpp`, `model_import_service.cpp`, the
Assimp decoder/adapter, and all runtime loaders into one static library, then
links both `Database`/SQLite and Assimp. The `Core` umbrella also includes
`Database`. Therefore the current build graph cannot substantiate MI1.7's
statement that Runtime has no Assimp or SQLite dependency. It only proves that
the `.model` loader does not call SQLite.

Backward compatibility creates an additional ambiguity: direct runtime loading
of OBJ/FBX/GLTF/GLB necessarily retains an Assimp dependency. The plan must say
whether “no Assimp dependency” means the native load call path or the shipped
runtime binary.

Required decision:

- Split native runtime Asset code from offline model-import/archive code.
- Make the import/tool target the explicit owner of Assimp and SQLite.
- Do not export `Database` through the general `Core` umbrella merely because
  the importer uses it.
- If direct foreign runtime loading remains temporarily available, isolate it
  behind an explicit compatibility target/build option and acknowledge that a
  compatibility-enabled binary links Assimp. The normal native path must not
  call it.

Evidence: `engine/runtime/asset/CMakeLists.txt:1-23`,
`engine/runtime/core/CMakeLists.txt:1-14`, and
`engine/runtime/asset/asset_manager.cpp:1122-1133`.

### MI1.7-R2 — P1 — Native Model material dependencies are loaded but never selected

Status: open; blocks implementation.

`NativeModelLoader` records one dependency index per native material slot, but
the only production consumer of `GetMaterialDependencyIndices()` is the unit
test. `LevelInstance::BuildStaticMeshDescription` requires the Level-authored
fallback Material and copies only the Level's explicit `materials` array. A
native Model therefore renders exactly like the legacy foreign Model unless a
Level separately authors every desired Material.

The parent plan gives this precedence:

```text
per-slot Level override
  -> Model material at MeshSection::material_index
  -> authored Level fallback
  -> engine error material
```

The MI1.7 stage page phrases the same rule in a different order, and the code
has no engine error-material Asset. Before implementation, freeze one algorithm
and its schema consequences. The parent order should remain authoritative:

1. resolve the Model's ordered Material dependency indices;
2. for each section material slot, use a valid Level per-slot override when
   present;
3. otherwise use the corresponding Model Material;
4. otherwise use the authored Level fallback;
5. otherwise use a defined engine error Material.

The plan must also state whether `material` remains required for legacy Levels
and becomes optional only for native Models, and how a sparse override is
represented. The current dense JSON array can only omit a trailing range.

Evidence: `engine/runtime/asset/native_model_loader.cpp:89-107`,
`engine/runtime/asset/model.cpp:31-38`, and
`engine/runtime/level/level_instance.cpp:383-433`.

### MI1.7-R3 — P1 — Loader side effects can leave orphan Mesh assets

Status: open; blocks implementation.

`NativeModelLoader::Load` calls the global AssetManager and registers the Mesh
before any Material dependency is resolved and before the parent Model wins the
final cache race. If a Material is missing/corrupt, the Model load fails after
the Mesh registration. If concurrent callers decode the same uncached Model,
the losing parent is deduplicated only after each loader invocation has already
created a separate Mesh. These Mesh assets have no path-index entry and no
owning Model reference, so normal path lookup cannot recover or clean them.

The loader contract should be declaration-only. Either let AssetManager install
the Model and its inline Mesh child as one registration transaction, or add an
explicit manager-owned rollback token for newly created child assets. Do not
apply a blanket “unregister every dependency on failure” rule: dependency
requests may have resolved to pre-existing cached assets shared by other roots.

Required tests cover missing first/later Material, concurrent duplicate Model
loads, parent registration failure, and unload order. After every failed or
deduplicated load, no unreachable Mesh slot may remain.

Evidence: `engine/runtime/asset/native_model_loader.cpp:66-88` and
`engine/runtime/asset/asset_manager.cpp:695-815`.

### MI1.7-R4 — P1 — The required checked-in migration products are ignored

Status: open; blocks reproducible migration.

All tracked runtime Levels currently reference foreign OBJ, FBX, or GLTF files.
The general `/asset/*` ignore rule excludes `.archive/models`,
`.archive/materials`, `.archive/textures`, ordinary materials, textures, and
source models; only selected Levels and shaders are re-included. Consequently,
MI1.7 cannot satisfy “checked-in native products and migrated validation
Levels” in a fresh checkout without changing repository packaging policy.

Choose and document one reproducible product policy:

- re-include verified immutable archive product directories while continuing
  to ignore `archive.sqlite3`, WAL/SHM files, and staging; or
- keep a small, tracked native integration-fixture tree and copy it into an
  operation-private Asset root during tests.

Real runtime smoke also needs all reachable external textures and built-in
default textures to be available in the checkout or an explicitly provisioned
fixture package. Large representative assets may require a separately agreed
LFS/package policy; MI1.7 must not assume a developer's ignored local files.

Evidence: `.gitignore:40-66` and all tracked `asset/level/*.level` model fields.

### MI1.7-R5 — P2 — Runtime content-address verification and layout rules are incomplete

Status: resolved.

`DeserializeNativeModel` validates the embedded integrity digest, but
`NativeModelLoader` does not require the filename stem to equal the SHA-256 of
the complete product. A valid product copied or maliciously rewritten under a
different 64-hex name is therefore accepted. Generated `.material` files have
the same issue because the generic Material loader validates schema but not an
archive filename/content match.

The native loader also finds Materials by taking `model.parent.parent` as the
archive root. This is correct only for `.archive/models/<hash>.model`; the
assumption is implicit and unvalidated.

MI1.7 should make the runtime contract explicit:

- hash-addressed archive Models require a lowercase 64-hex stem matching the
  product bytes;
- archive Materials require the same check before parsing;
- the product must be in the canonical `models`/`materials` layout, or an
  explicit product-root value must be supplied by the Asset boundary;
- a native corruption/layout failure returns a failed load and never falls back
  to a similarly named foreign source or opens SQLite.

Authored non-archive `.material` files remain exempt from the hash-name check.

Evidence: `engine/runtime/asset/native_model_loader.cpp:18-43,55-105` and
`engine/runtime/asset/material_loader.cpp:330-404`.

Resolution: `AssetProduct::VerifyArchiveProduct` now validates archive
products before format parsing. A product under `.archive` must use the exact
`.archive/models|materials/<lowercase-sha256>.<ext>` layout, and the filename
stem must match the SHA-256 of the complete bytes read from disk. Native Model
and Material loaders fail immediately on a mismatch or layout violation;
the runtime Asset boundary supplies `asset/.archive` as the native Model
product root, while non-archive authored Materials and explicit isolated
loader fixtures remain exempt. Native Model dependency paths are derived only
after this validation, and `.model` dispatch still returns failure directly
without foreign fallback or SQLite access.

Evidence: `engine/runtime/asset/asset_product.h`,
`engine/runtime/asset/model_archive.cpp`,
`engine/runtime/asset/native_model_loader.cpp`,
`engine/runtime/asset/material_loader.cpp`, and the focused archive validation
tests in `engine/test/unit/asset/native_model_test.cpp` and
`engine/test/unit/asset/native_material_test.cpp`.

### MI1.7-R6 — P2 — Compatibility and migration failure behavior are not a contract

Status: resolved.

OBJ, FBX, GLTF, and GLB are still silently accepted by the runtime, while every
tracked Level depends on that behavior. MI1.7 says the compatibility path is
optional and “visibly deprecated” but defines neither visibility, duration,
nor removal gates. STL is correctly import-only today and should not be added
to runtime dispatch merely for symmetry.

Adopt a bounded matrix:

| Input | MI1.7 behavior |
|---|---|
| hash-named `.model` | Normal read-only runtime path. No import, database access, or writes. |
| OBJ/FBX/GLTF/GLB | Transitional direct load only when the compatibility option is enabled; emit a stable deprecation diagnostic. |
| STL | Offline import only. |
| missing/corrupt `.model` or dependency | Fail the owning load transaction; do not auto-import or fall back to foreign input. |
| missing archive database | Native runtime load is unaffected. |
| newer/corrupt archive database | Import tooling fails with its archive diagnostic; native runtime load is unaffected. |

Removal of the compatibility option requires zero tracked Level references,
passing native equivalents for the four characterized formats, and explicit
packaging evidence. Compatibility means that existing source formats remain
importable; it need not mean that every shipping runtime permanently embeds
Assimp.

Evidence: `engine/runtime/asset/utility.h:35-38`,
`engine/runtime/asset/asset_manager.cpp:1122-1133`, and tracked Levels.

Resolution: Foreign OBJ, FBX, GLTF, and GLB runtime dispatch remains behind
`KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT`. Enabled requests emit the stable
`asset.runtime.foreign_model_compatibility.deprecated` diagnostic; disabled
builds reject them with the `.disabled` diagnostic. STL remains outside runtime
model dispatch and is accepted only by the offline decoder/importer. Native
Model failures remain terminal and do not invoke foreign loading or import.
The removal gate is now explicit: zero tracked foreign Level references,
native equivalents for the four characterized formats, and packaging evidence
for the reachable native product closure.

Evidence: `engine/runtime/asset/utility.h`,
`engine/runtime/asset/asset_manager.cpp`,
`engine/test/unit/asset/assimp_model_loader_test.cpp`, and the MI1.7/MI1.8
packaging and validation requirements.

### MI1.7-R7 — P2 — Existing tests stop before the runtime integration seam

Status: resolved for the runtime integration seam; final package launch and
cross-backend visual capture remain MI1.8 validation work.

The native loader test invokes `NativeModelLoader` directly, uses a non-hash
`triangle.model` name, and manually unregisters the loader-created Mesh. It
does not prove `AssetManager::LoadSync` dispatch, recursive Material/shader/
texture resolution, reverse references, rollback, Level material precedence,
absence of database access, packaging, or cross-backend rendering.

MI1.7 needs the validation matrix below. MI1.8 may own the final CLI UX and the
full five-format visual suite, but MI1.7 cannot be marked landed without at
least one checked-in multi-material native runtime fixture on both backends and
regression coverage for every legacy runtime extension it retains.

Evidence: `engine/test/unit/asset/native_model_test.cpp:112-140` and
`docs/asset/.plan/MI1.8.md:13-25`.

Resolution: Added a checked-in multi-material fixture contract under
`engine/test/unit/asset/fixtures/native_runtime/` and a composed
`NativeModelRuntimeIntegrationTest`. The test hashes the tracked Material
sources, publishes test-owned canonical products, loads the hash-named Model
through `AssetManager::LoadSync`, and verifies dependency zero is the owned
Mesh followed by ordered Materials. It recursively resolves the shared shader
program and texture, checks reverse references, proves concurrent requests
deduplicate to one Model/one Mesh, and verifies Model unload retires its owned
Mesh while ordinary dependencies remain available. Existing native rollback,
generation, Level precedence, and retained foreign-route tests provide the
adjacent regression coverage.

The test creates no archive database record and loads the native product path
directly, so the runtime graph is exercised without importer access or a
logical-name database lookup. The required final packaged-tree launch and
Vulkan/OpenGL SceneColor capture are intentionally left as MI1.8 evidence;
this keeps the unit seam deterministic while the repository still has legacy
foreign Level fixtures and no committed product package.

Evidence: `engine/test/unit/asset/native_model_test.cpp`,
`engine/test/unit/asset/fixtures/native_runtime/`, and the focused
`NativeModelRuntimeIntegrationTest`/`NativeModelLoaderTest` run.

## Required MI1.7 design amendment

Before implementation, expand the MI1.7 stage plan with these decisions:

1. Target split: runtime-native Asset, offline importer/archive, and optional
   foreign compatibility ownership.
2. Exact material-selection precedence and the authored fallback/error policy.
3. Manager-owned atomic registration/rollback for Model, Mesh, and declared
   dependencies.
4. Canonical product-root and hash-name verification rules.
5. Repository/package policy for immutable runtime products and dependencies.
6. Compatibility build option, diagnostics, removal gate, and no-fallback
   corruption behavior.
7. The minimum validation matrix and division of evidence between MI1.7 and
   MI1.8.

## Required validation

### Focused format and loader tests

- Native Model deterministic/golden bytes, malformed chunks, unsupported
  version/features, overflow limits, integrity mismatch, and hash-name mismatch.
- Archive Material hash-name verification without changing authored Material
  behavior.
- `AssetManager::LoadSync(<hash>.model)` produces a Model whose dependency zero
  is its Mesh and whose remaining ordered dependencies are Materials.
- Material dependencies recursively resolve their shader and textures with the
  archive database absent, renamed, locked, and corrupt.
- Missing/corrupt Model, Material, shader, or texture fails with a stable
  source-scoped diagnostic and leaves no orphan loader-created asset.
- Two concurrent loads of one native Model return one Model identity and one
  owned Mesh identity.
- Unregistering a Level, Model, Mesh, and Material respects reverse references
  and generation validation.

### Runtime Level and compatibility tests

- Native Model defaults with no per-slot overrides.
- A shorter override array: overridden prefix slots use Level Materials and
  remaining slots use Model Materials.
- Missing Model slot uses the authored fallback; missing fallback uses the
  defined error policy.
- Out-of-range section slots and stale/wrong-type Material dependencies fail
  before Actor creation.
- Existing OBJ, FBX, GLTF, and GLB characterization tests continue to pass when
  compatibility is enabled, and each route emits the deprecation diagnostic.
- Compatibility-disabled builds reject foreign runtime paths without affecting
  offline import support.

### Build, package, and runtime evidence

- Build the runtime-native Asset target without linking SQLite or importer
  objects. Build the normal native-only runtime without Assimp; separately
  validate the optional compatibility target when retained.
- Package a migrated Level plus its reachable `.model`, `.material`, shader,
  and texture closure without `archive.sqlite3`, WAL/SHM, staging, or foreign
  sources. Launch it from the packaged tree.
- Run focused Asset, RuntimeLevel, Render asset-preparation, and Material
  resolver tests.
- Run the migrated multi-material fixture through Vulkan and OpenGL. Capture
  `SceneColor` through the Runtime command registry and inspect geometry,
  section/material assignment, packed metallic/roughness channels, alpha-mask
  behavior, and failure presentation.
- MI1.8 final evidence must import and render representative STL, OBJ, FBX,
  GLTF, and GLB inputs; MI1.7 must at minimum prove the native runtime fixture
  plus regression coverage for every direct foreign route it keeps.

Suggested command families after the target split (exact target names belong
to the implementation plan):

```powershell
.\tools\kp.ps1 build AssetUnitTest
.\tools\kp.ps1 test NativeModel
.\tools\kp.ps1 test Material
.\tools\kp.ps1 test RuntimeLevelTest
.\tools\kp.ps1 build RenderPassScheduleTest
.\tools\kp.ps1 test RenderPassScheduleTest
.\tools\kp.ps1 smoke
```

## Reference discovery gate

The existing MI1 study already identifies Godot, O3DE, SQLite, and glTF
evidence. This review re-inspected a small set of source-backed precedents:

- [Godot `ResourceFormatImporter`](https://github.com/godotengine/godot/blob/master/core/io/resource_importer.cpp)
  resolves imported metadata to a generated product and reports corrupt or
  missing import state instead of silently treating it as another runtime
  format. Its runtime remap metadata is not adopted because MI1 deliberately
  embeds typed Material references in `.model` and excludes a runtime database.
- [Godot `ResourceFormatLoader` contract](https://github.com/godotengine/godot/blob/master/doc/classes/ResourceFormatLoader.xml)
  exposes dependency enumeration as a loader responsibility. This supports the
  native loader declaring Material dependencies while the cache owns loaded
  identities and lifetime.
- [Godot 3D import material workflow](https://github.com/godotengine/godot-docs/blob/master/tutorials/assets_pipeline/importing_3d_scenes/advanced_import_settings.rst)
  keeps extracted, user-edited Materials external and avoids overwriting them
  during reimport. This supports MI1's immutable generated product plus explicit
  promotion/override policy.
- [Urho3D `AssetImporter`](https://github.com/urho3d/Urho3D/blob/master/Source/Tools/AssetImporter/AssetImporter.cpp)
  uses Assimp in a tool that emits engine resources. Its name/timestamp-based
  overwrite options do not meet MI1's content-address and transaction rules,
  but the tool/runtime separation applies.

The references reinforce separation of import work, product loading, and cache
ownership. They do not resolve KimPeanutEngine's Model/Mesh registration
transaction or Level override precedence; those must follow the local Asset and
Gameplay boundaries above.

## Review conclusion

MI1.7 should proceed only after the stage contract incorporates R1–R4. The
implementation should then be a bounded runtime migration, not another import
stage: native products enter through AssetManager, dependencies resolve from
the serialized product graph without SQLite, Level policy chooses Materials,
and failures leave the cache and last published archive state coherent.
