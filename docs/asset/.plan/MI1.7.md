# MI1.7 — Runtime Migration

- Status: runtime integration seam landed through MI1.7-R8; checked-in product
  packaging and cross-backend visual validation remain
- Parent design: [MI1](MI1.md)
- Prerequisites: MI1.4 and MI1.6
- Unblocks: MI1.8 final validation
- Overview review: [MI1.7 runtime-migration review](../.review/MI1.7.md)

## Assignment

Make native `.model` the normal read-only runtime asset path, migrate checked-in
Levels/bootstrap references, and preserve deterministic per-slot overrides.

## Landed review-risk work

- [x] **MI1.7-R1 — separate build ownership.** `AssetProduct` contains the
  database-free content-hash/product-path layer; `AssetNative` contains native
  Model serialization; `AssetRuntime` contains runtime AssetManager/loading;
  `AssetArchive` and `AssetImport` own SQLite-backed archive and offline import
  code. `AssetImport` does not link `AssetRuntime`, and `Core` no longer exports
  `Database`. Legacy Assimp runtime loading is explicitly controlled by
  `KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT` and remains enabled only for the
  migration window.
- [x] **MI1.7-R2 — native material selection.** `LevelInstance` resolves the
  Model's ordered Material dependency indices and merges them with dense
  Level per-slot overrides using the authoritative precedence:
  `Level override -> Model slot -> authored fallback -> engine error Material`.
  A serialized native `.model` Level may omit `material`; the loader inserts
  `material/error.material` as an implicit fallback dependency. Legacy foreign
  model Levels still require an authored `material`. The dense `materials`
  array remains a prefix of slot overrides; omitted trailing slots fall
  through to Model/fallback selection.
- [x] **MI1.7-R3 — transactional native Model children.** `NativeModelLoader`
  is declaration-only: it returns the CPU Model plus an inline Mesh payload and
  never calls `AssetManager`. `AssetManager` resolves external Material
  requests first, registers inline children only after those requests succeed,
  binds their assigned IDs, and installs the parent with the child IDs as
  leading dependencies. Parent unload removes those owned children in reverse
  order; failed or deduplicated loads do not leave a loader-created Mesh.
- [x] **MI1.7-R5 — archive product integrity and layout.** Runtime Model and
  generated Material loading validates `.archive/models|materials` layout and
  the lowercase SHA-256 filename against the complete product bytes before
  parsing. Native `.model` failure remains terminal for that dispatch and does
  not open SQLite or fall through to foreign loading; non-archive authored
  Materials remain supported. AssetManager supplies `asset/.archive` as the
  native Model product root, so arbitrary non-archive runtime `.model` paths
  cannot bypass the check; isolated direct-loader fixtures may omit that root.
- [x] **MI1.7-R6 — bounded foreign compatibility.** OBJ, FBX, GLTF, and GLB
  remain direct runtime compatibility inputs only while
  `KPENGINE_ENABLE_FOREIGN_MODEL_COMPAT` is enabled. Every such request emits
  the stable `asset.runtime.foreign_model_compatibility.deprecated` diagnostic;
  disabled builds reject them with the `.disabled` variant. STL remains
  offline-import-only, and native Model/dependency failures never auto-import
  or fall through to a foreign loader. Removal requires zero tracked foreign
  Level references, native equivalents for the four characterized formats,
  and explicit packaging evidence.
- [x] **MI1.7-R7 — runtime integration seam.** A checked-in multi-material
  fixture contract now drives an `AssetManager::LoadSync` test over canonical
  hash-named Model/Material products. It verifies ordered Mesh/Material
  dependencies, recursive shader/texture resolution, reverse references,
  concurrent deduplication, and owned-Mesh cleanup; final package launch and
  Vulkan/OpenGL capture remain MI1.8 evidence.
- [x] **MI1.7-R8 — Level integration seam.** The native runtime fixture now
  registers a readable logical model key in a test-owned archive, loads its
  Level through `AssetManager::LoadSync`, verifies the native Model identity
  and implicit error-Material fallback, and checks that runtime loading leaves
  both the Level and archive bytes unchanged. Product promotion and backend
  capture remain MI1.8 evidence.
- [ ] Complete the remaining MI1.7 migration evidence: package a native Level
  and reachable product closure, migrate the checked-in Level references, and
  capture the fixture through both runtime graphics backends.

## Deliverables

- `.model` dispatch through `NativeModelLoader` with no Assimp, SQLite, Editor,
  or project-write dependency in the loader. Level parsing may use the archive
  through a strict read-only logical-name resolver before dispatch. The loader
  consumes products made by the offline importer; it never invokes the importer.
- Default resolution from Model material slots, then per-slot Level override,
  authored fallback, and engine error material as defined by the parent plan.
- Checked-in native products and migrated validation Levels/bootstrap.
- A bounded and visibly deprecated foreign runtime-load path only if migration
  evidence proves it is temporarily required.

## Boundaries

Do not resolve ordinary Model Material/texture dependencies through the archive
database, modify immutable Model products for a Level override, remove
compatibility before fixtures are migrated, or expand the Runtime/Editor
dependency cycle. The only archive lookup allowed in Runtime is the read-only
logical Level-model-key to verified native-product resolution.

## Done when

- [ ] Checked-in Levels load native Models and all declared Materials.
- [ ] Runtime model loading performs no import work or project writes.
- [ ] Existing multi-section override behavior remains deterministic.
