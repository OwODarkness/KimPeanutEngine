# MI1 — Native Model Import Journal

This journal records factual implementation and validation evidence for the
staged model-import work. The durable design and acceptance contract live in
[`.spec/specs/model-import.md`](../specs/model-import.md); the stage contracts
live under [`docs/asset/.plan/`](../../docs/asset/.plan/).

## 2026-09-06 — MI1.1 characterization started

- Scope: characterize the existing Assimp-backed runtime loader only.
- Added reviewable fixtures for OBJ/MTL, GLTF with an embedded buffer, a
  minimal ASCII FBX, a base64 representation of a minimal GLB, malformed GLTF,
  missing-buffer GLTF, and an ASCII STL dispatch baseline.
- Added focused `AssimpModelLoaderTest` coverage for vertex/index counts,
  section ranges and material slots, bounds, baked GLTF/GLB transforms,
  material metadata, source-relative OBJ texture paths, failure observations,
  and the unsupported STL extension.
- The GLB fixture is stored as base64 text so the binary payload remains
  reviewable; the test materializes the exact container bytes in the temporary
  directory before passing it to the unchanged loader.
- Fixture provenance: hand-authored minimal files using the documented OBJ,
  MTL, GLTF 2.0, GLB 2.0, FBX ASCII, and STL grammars. No third-party asset
  bytes are copied into the test tree.
- Validation is pending because the existing Visual Studio build is currently
  blocked before compilation by denied access to
  `C:\Users\17519\AppData\Local\Microsoft SDKs` while evaluating
  `GetLatestSDKTargetPlatformVersion`.
- A standalone MinGW syntax-only check of `assimp_model_loader_test.cpp` passed
  after supplying the generated-config and third-party include paths. The
  project MinGW build remains intentionally unavailable because the repository
  rejects the MSVC-only imported third-party configuration before compilation.
- The focused Visual Studio build command was attempted:
  `cmake --build build --config Debug --target AssetUnitTest`.
  It stopped in `Microsoft.Cpp.WindowsSDK.props` with the SDK-directory access
  error above. No Asset test binary result is claimed.
- The alternate `build-mingw` target was also attempted and stopped at the
  repository's explicit MSVC-only third-party guard; this is not source-test
  evidence.

## 2026-09-06 — MI1.1 landed

- Rebuilt the supported target with
  `cmake --build build --config Debug --target AssetUnitTest` successfully.
- Ran
  `ctest --test-dir build -C Debug --output-on-failure -R "AssimpModelLoader"`.
  All 10 discovered `AssimpModelLoaderTest.*` cases passed.
- Observed and recorded the current Assimp contract: OBJ/GLTF/GLB expose a
  default material at slot 0 and the authored material at slot 1; geometry is
  three vertices and three indices in each minimal fixture, with node
  transforms baked into positions and source-relative OBJ texture paths
  retained verbatim.
- The ASCII FBX fixture loads as one three-index section. Malformed GLTF and a
  missing external buffer fail during source loading; observations retain the
  stable filename, phase, and rejection diagnostic. `.stl` remains
  unsupported by `AssetManager` extension dispatch.
- MI1.1 is complete. Native serialization, SQLite, pure decoder extraction,
  and runtime migration remain assigned to MI1.2 through MI1.8.
- Regression validation also passed for the complete Asset test family: 36/36
  discovered Assimp, load-observation, material, level, and shader-variant
  tests passed with
  `ctest --test-dir build -C Debug --output-on-failure -R
  "(AssimpModelLoaderTest|AssetLoadObservation|MaterialLoaderTest|LevelLoaderTest|ShaderProgramVariantTest)"`.

## 2026-09-06 — MI1.2 landed

- Added the Asset-owned `ModelArchiveDatabase` boundary and a dedicated SQLite
  dependency target. The repository creates schema version 1 with foreign keys,
  prepared statements, checked transactions, WAL mode, full synchronous writes,
  bounded busy timeout, and recorded SHA-256/canonical-path metadata. Newer
  schema versions and malformed databases are rejected with stable archive error
  codes.
- Added stable SHA-256 hashing, canonical lower-case Asset-relative paths,
  ordered source-package fingerprints, import-key hashing, and hash-named model,
  material, and texture product path helpers.
- Added source, dependency, product, source-product, material-override, and
  archive metadata persistence. Product files are staged before the short
  metadata transaction and are verified by size and hash; immutable product
  metadata is never silently rewritten.
- Added archive probing for source/package/importer/settings/native-schema
  changes and missing/corrupt products. A failed busy-writer transaction leaves
  the previous source record unchanged.
- Validation passed:
  - `cmake --build build --config Debug --target ModelArchiveDatabaseTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R "ModelArchive"` —
    5/5 archive tests passed.
  - `cmake --build build --config Debug --target DatabaseUnitTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R "DatabaseTest"` —
    6/6 matching database/archive tests passed.
  - `cmake --build build --config Debug --target AssetUnitTest`
  - Asset family — 36/36 tests passed.

## 2026-09-06 — MI1.3 landed

- Extracted Assimp parsing into the pure `AssimpModelDecoder`, which returns a
  value-owned `ImportedModelDocument` without `AssetID`, Asset-cache,
  filesystem-write, archive, or SQLite access. The existing
  `Assimp_ModelLoader` now only adapts that document into the legacy
  `MeshResource` registration path.
- The imported document preserves mesh vertices, face-order indices, section
  material slots, baked static node transforms, inverse-transpose normal
  transforms, material metadata, external texture/OBJ-MTL dependencies, glTF
  URI dependencies, and embedded dependency bytes. STL is supported at the
  explicit decoder boundary with a default material slot; runtime extension
  dispatch remains unchanged for the later native-model migration.
- An existing checked-in OBJ references an absent optional MTL; the decoder
  records a `MissingOptionalDependency` warning and preserves Assimp's default
  material fallback so existing runtime level loading remains compatible.
- Reference gate: the local engine-reference index identified Godot and Piccolo
  as the relevant Asset/resource comparisons. Godot's `ResourceImporterScene`
  separates format import from resource saving, while Piccolo keeps resource
  payload types below its asset manager. Those ownership patterns apply; their
  scene graph and runtime resource systems do not, so MI1.3 uses small local
  value types instead of importing either framework's model.
- Validation passed:
  - `cmake --build build --config Debug --target ModelDecoderTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R "ModelDecoderTest"` —
    5/5 tests passed.
  - `cmake --build build --config Debug --target AssetUnitTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R "AssimpModelLoaderTest"` —
    10/10 characterization tests passed.

## 2026-09-06 — MI1.4 landed

- Added `NativeModelData` and a canonical V1 `.model` container. Serialization
  writes explicit little-endian fields only: fixed magic/version/features,
  total size, chunk directory, vertex/index/section payloads, bounds, and an
  ordered `{AssetType::KPAT_Material, SHA-256}` reference table. The embedded
  integrity digest is computed with its own field zeroed; the product hash is
  the SHA-256 of the final bytes and therefore remains the archive filename
  identity.
- The decoder validates the complete header, supported version/features,
  count and size limits, chunk bounds/non-overlap, element sizes, finite
  numeric values, index/section ranges, material enum values, and the digest
  before allocating decoded vectors.
- Added `NativeModelLoader` for `.model` dispatch. It creates the existing CPU
  mesh compatibility sub-resource and declares each ordered hash-named
  `.material` path through `AssetRegisterInfo::dependency_requests`; the
  AssetManager resolves those requests after releasing `load_mutex_`. The
  native loader does not import foreign files, query SQLite, mutate the
  archive, or create GPU state.
- Reference gate: Godot's binary resource reader/saver uses explicit format
  versioning, fixed headers, external-resource tables, and rejects newer
  formats; Piccolo's asset manager keeps resource payload implementations
  below the manager boundary. Those validation and ownership patterns apply,
  while KimPeanutEngine uses a smaller fixed chunk format and content hashes
  rather than a general Variant/object graph.
- Validation passed:
  - `cmake --build build --config Debug --target NativeModelTest AssetUnitTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R "NativeModel"` —
    4/4 tests passed.
  - `ctest --test-dir build -C Debug --output-on-failure -R
    "NativeModel|ModelArchive|ModelDecoderTest|AssimpModelLoaderTest|
    AssetLoadObservation|MaterialLoaderTest|LevelLoaderTest"` — 49/49 passed.

## 2026-09-06 — Importer boundary clarification

- Clarified the remaining MI1 plan stages after review: the importer is an
  offline Asset-owned library/tool, not an `AssetManager` feature. It may run
  from a CLI or editor subprocess while the engine application is closed and
  may depend on Core, Assimp, ImageIO, serialization, and the archive
  repository, but must not construct `AssetID`/runtime cache state or depend on
  Runtime, Editor, Render, or Graphics.
- Clarified the distinction between `NativeModelLoader` and the future importer:
  `NativeModelLoader` is a read-only runtime product consumer that declares
  Material dependencies; MI1.6 is responsible for creating and publishing
  `asset/.archive/models/<hash>.model` and related products.
- Updated `PLANS.md`, `MI1.md`, `MI1.6.md`, `MI1.7.md`, `MI1.8.md`, the MI1
  spec, and `asset_module.md`. No runtime or product-generation code changed in
  this clarification.

## 2026-09-06 — MI1.5 landed

- Added `ConvertImportedMaterials`, a pure Asset-side conversion stage that
  returns canonical Material V2 bytes and in-memory embedded image products.
  It does not construct AssetIDs, touch AssetManager, depend on Runtime/Render/
  Graphics, write products, or access the archive database, so it can run in a
  closed-engine importer process.
- Extended Material V2 with alpha mode/cutoff and explicit texture
  color-space/channel metadata. Packed metallic-roughness remains one image;
  the generated material references its blue metallic and green roughness
  channels without splitting or duplicating the image.
- Added ImageIO memory decode and PNG encode contracts. Assimp raw embedded
  texels are normalized to RGBA8 before conversion; encoded embedded images are
  validated and deduplicated by content hash.
- Extended the StandardPbr resolver and G-buffer ABI with channel selectors,
  normal scale, and alpha-mask discard. Emissive textures are rejected with a
  precise unsupported-semantics error because the current G-buffer does not
  carry emissive output.
- Validation passed:
  - `cmake --build build --config Debug --target Asset NativeMaterialTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R "NativeMaterialTest"` —
    4/4 tests passed.
  - `cmake --build build --config Debug --target Render RenderSystemTest RenderPassScheduleTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R
    "NativeMaterialTest|AssetUnitTest|MaterialAssetResolverTest|MaterialSystemTest"` —
    23/23 tests passed.

## 2026-09-06 — MI1.6 landed

- Added the standalone `ModelImportService`/`ImportModel` boundary. The request
  supplies an Asset root, archive root, source path, and canonical import
  settings; the implementation constructs no runtime Asset identity and does
  not call AssetManager, Runtime, Editor, Render, or Graphics.
- The cache path loads the prior SQLite snapshot, hashes its recorded
  filesystem dependencies, probes importer/settings/schema fingerprints,
  verifies product hashes, deserializes the root native Model, validates
  material JSON and embedded image products, and returns `UpToDate` without
  invoking Assimp or creating staging files.
- Cache misses decode and convert outside any archive transaction. The service
  builds canonical Model/Material/embedded-image products, validates them in
  memory, writes only under an operation-private staging directory, publishes
  hash-named products with create-if-absent hard links, verifies a concurrent
  winner, and calls the archive's short metadata transaction last. Old
  products are never deleted or overwritten.
- Added standalone validation for generated Material V2 bytes and focused
  importer tests for initial publication/cache reuse, stale dependency
  reimport, failure preservation, shared product deduplication, per-source
  concurrent calls, and Asset-root path escape rejection.
- Validation passed:
  - `cmake --build build --config Debug --target ModelImportServiceTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R "ModelImportServiceTest"` —
    5/5 tests passed.
  - `cmake --build build --config Debug --target Asset` — passed.
  - Combined Asset regression filter covering Assimp, observation, material,
    level, shader-variant, archive, decoder, native-format, and importer
    tests — 50/50 tests passed.

Remaining MI1.6 risk: no external CLI executable has been added yet; MI1.8
owns the tooling and end-to-end command path. Cross-process publication is
covered by SQLite/product collision rules, while the in-process coordination
map is owned by each `ModelImportService` instance.

## 2026-09-06 — MI1.7-R1 build ownership fix

- Split the Asset CMake graph into `AssetProduct` (database-free hashes and
  product paths), `AssetNative`, `AssetRuntime`, `AssetArchive`,
  `AssetForeignDecode`, and `AssetImport`. The compatibility `Asset` target is
  retained as a thin facade forwarding to `AssetRuntime` for existing
  consumers and Visual Studio build commands.
- Kept archive/import implementation behind `AssetArchive` and `AssetImport`;
  `AssetImport` does not link `AssetRuntime`, `AssetManager`, Render, or
  Graphics. Removed `Database` from the public `Core` link interface.
- Added the explicit `KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT` option. It keeps
  the legacy Assimp runtime adapter available during migration but makes its
  presence a deliberate build choice; native `.model` loading remains the
  database-free runtime path.
- Validation passed:
  - `cmake --build build --config Debug --target AssetRuntime AssetArchive AssetImport AssetUnitTest ModelArchiveDatabaseTest ModelDecoderTest NativeModelTest NativeMaterialTest ModelImportServiceTest`
  - `ctest --test-dir build -C Debug -R "(ModelArchiveDatabaseTest|ModelDecoderTest|NativeModelTest|NativeMaterialTest|ModelImportServiceTest)" --output-on-failure` — 17/17 passed.
  - `ctest --test-dir build -C Debug -R "(AssetLoadObservation|MaterialLoaderTest)" --output-on-failure` — 17/17 passed.
  - `cmake --build build --config Debug --target RuntimeLib DatabaseUnitTest`
- Remaining MI1.7 review risks are intentionally not marked complete: loader
  transaction rollback, checked-in native product
  packaging, strict hash/layout enforcement, and runtime integration/visual
  validation.

## 2026-09-06 — MI1.7-R2 native material selection

- `LevelInstance` now resolves ordered Model Material dependency indices and
  selects each mesh section with the fixed precedence `Level override -> Model
  slot -> authored fallback -> engine error Material`.
- Native `.model` Level records may omit `material`; `LevelLoader` inserts the
  checked-in `material/error.material` as an implicit dependency. Foreign model
  records retain the required authored fallback contract. The serialized
  `materials` array remains a dense prefix of per-slot overrides.
- Added the engine-owned magenta Standard PBR error material and a Runtime
  startup handoff for its AssetID. Added focused LevelLoader and RuntimeLevel
  precedence/error-fallback tests.
- Validation passed: full Debug build; all 10 `LevelLoaderTest` cases; focused
  R2 selection tests (5/5); and the complete affected LevelLoader/RuntimeLevel
  run showed 34/37 passing. The three failures are the pre-existing RuntimeLevel
  baseline cases `InstantiatesNonMeshRecordsAndAllowsEmptyInstance`,
  `EnvironmentRegistrationFailureRollsBackMixedActorsAndRetries`, and
  `SpotFactoryFailureRollsBackAndRetriesImmediately`.

## 2026-09-06 — MI1.7-R3 transactional native Model children

- Changed `NativeModelLoader` to a declaration-only contract. It now returns
  the CPU Model and an inline CPU Mesh payload through `AssetRegisterInfo`,
  declares hashed Material requests, and binds the manager-assigned Mesh ID
  only after registration. The loader no longer calls `AssetManager` or
  creates a cache entry as a side effect of decoding.
- Added manager-owned inline-child registration. AssetManager resolves external
  dependency requests first, installs inline children under the same state
  lock, invokes the parent binder, prepends owned child IDs to the parent
  dependency layout, and rolls back only children created by that transaction.
  Parent unload removes its owned children in reverse order while ordinary
  external dependencies remain governed by reference tracking.
- Added focused coverage for declaration purity, missing and later Material
  rollback, preservation of an earlier shared dependency, concurrent duplicate
  Model loads, parent binding failure, and owned-child unload.
- Validation passed:
  - `cmake --build build --config Debug --target AssetUnitTest NativeModelTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R "NativeModel"` —
    11/11 tests passed, including the existing native Level fallback case.

Remaining MI1.7 risks are checked-in product packaging, canonical hash/layout
verification, compatibility diagnostics/removal gates, and integration/visual
validation. No SQLite or importer access was added to the NativeModelLoader
path; the later Level-key resolver is a separate read-only archive boundary.

## 2026-09-06 — Readable Level model keys

- Changed the Level-facing native Model reference from a SHA-256 product path to
  a readable logical key such as `model/brickwall/floor`. The key is derived
  from the normalized foreign source path without its extension, so the archive
  can map it without adding a second authoring-name table.
- Added a strict read-only SQLite archive mode and a resolver that verifies the
  selected Model product's canonical hash path, file size, and bytes before
  returning its native product path. Runtime does not import, write SQLite, or
  use the archive for Material/texture dependency resolution.
- Direct `.model` references remain accepted for low-level fixtures and
  migration diagnostics; foreign-extension references retain the transitional
  compatibility path until checked-in products are migrated.
- Validation passed:
  - `cmake --build build --config Debug --target AssetUnitTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R
    "(ModelArchive|LevelLoader)"` — 16/16 tests passed, including the
    injected-archive LevelLoader logical-key integration case.

## 2026-09-06 — MI1.7-R5 archive product verification

- Added the database-free `VerifyArchiveProduct` contract to the AssetProduct
  layer. Any product under `.archive` must use the canonical
  `.archive/models|materials/<lowercase-sha256>.<ext>` layout, and the filename
  hash must match the complete bytes read from disk.
- Native Model loading verifies the archive path and bytes before native
  deserialization. Material loading reads the complete product bytes and
  verifies them before JSON parsing. Invalid archive products fail directly;
  authored non-archive Materials and isolated non-archive loader fixtures keep
  their existing compatibility behavior.
- `AssetManager` now supplies `asset/.archive` as the NativeModelLoader product
  root, so runtime native Model requests outside that root are rejected rather
  than becoming an unverified alternate source.
- `.model` dispatch remains terminal on native failure, so a bad archive Model
  does not fall through to the foreign Assimp loader or access SQLite.
- Added focused mismatch/layout coverage for native Models and generated
  Materials; the canonical generated Material round-trip now uses its actual
  content hash filename.
- Validation passed:
  - `cmake --build build --config Debug --target NativeModelTest NativeMaterialTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R
    "(NativeModel|NativeMaterial)"` — 17/17 tests passed.

## 2026-09-06 — MI1.7-R6 bounded foreign compatibility

- Added the stable diagnostic family
  `asset.runtime.foreign_model_compatibility`. Enabled OBJ, FBX, GLTF, and
  GLB runtime requests emit the `.deprecated` warning; builds with
  `KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT=OFF` reject them with `.disabled`.
- Kept STL out of runtime model dispatch. It remains supported by the
  database-free offline decoder/import path only.
- Preserved terminal native failure behavior: a missing or corrupt `.model` or
  dependency does not auto-import or fall through to Assimp. Native runtime
  loading remains unaffected by archive database failure.
- Added coverage that every retained foreign extension emits the stable
  diagnostic family, while existing characterization tests continue to cover
  successful compatibility loads when the option is enabled.
- Documented the removal gates: zero tracked foreign Level references, native
  equivalents for OBJ/FBX/GLTF/GLB, and explicit native packaging evidence.
- Validation passed:
  - `cmake --build build --config Debug --target AssetUnitTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R
    "(AssimpModelLoaderTest|MaterialLoaderTest|LevelLoaderTest)"` — 29/29
    tests passed, including all four foreign-extension diagnostics and the
    existing OBJ/FBX/GLTF/GLB characterization loads.
  - Separate `build-r6-off` configuration with
    `KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT=OFF`: `AssetRuntime` and
    `AssetUnitTest` built; the filtered GoogleTest executable passed the
    four-extension `.disabled` diagnostic test. This configure did not
    register CTest tests, so the executable was invoked directly.

## 2026-09-06 — MI1.7-R7 runtime integration seam

- Added the checked-in native runtime fixture contract under
  `engine/test/unit/asset/fixtures/native_runtime/`: a logical-key Level and
  two Material sources, one of which declares a texture dependency.
- Added `NativeModelRuntimeIntegrationTest`, which publishes test-owned
  canonical hash-named Material and Model products, then loads the Model
  through `AssetManager::LoadSync`. It verifies the Mesh is dependency zero,
  ordered Materials follow it, shader/texture dependencies recurse through the
  manager, reverse references are present, concurrent loads share one Model
  identity and one owned Mesh, and Model unload removes only the owned Mesh.
- This seam intentionally does not create or query archive SQLite metadata;
  the test exercises direct native product loading. Final packaged-tree launch
  and Vulkan/OpenGL SceneColor capture remain MI1.8 work because the repository
  still has legacy foreign Level references and no committed native product
  package.
- Validation passed:
  - `cmake -S . -B build -G "Visual Studio 17 2022"`
  - `cmake --build build --config Debug --target NativeModelTest`
  - `ctest --test-dir build -C Debug --output-on-failure -R
    "NativeModelRuntimeIntegrationTest|NativeModelLoaderTest"` — 8/8 tests
    passed.
