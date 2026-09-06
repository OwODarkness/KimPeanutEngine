# MI1 — Content-Addressed Native Model Import

- Status: proposed
- Parent roadmap: [Asset Module TODO](../TODO.md#mi1--content-addressed-native-model-import)
- Architecture map: [Asset Module Plans](../PLANS.md)

## Objective

Add an engine-owned model import pipeline that converts foreign model sources
such as STL, OBJ, FBX, GLTF, and GLB into versioned native `.model` assets.
The import also creates native `.material` assets and records each material
reference inside the resulting Model asset.

Generated `.model` and `.material` filenames are content hashes. An
Asset-owned SQLite database under `.archive` maps human-readable source and
material names to those hashes. Import computes a dependency-aware fingerprint
first; when the database record and every referenced product already match,
import is a true no-op.

Assimp remains one foreign-format decoder. It does not write native assets,
choose cache names, update the archive, register Render objects, or decide
whether an import is current.

## Current state and concrete problem

The runtime currently treats OBJ, FBX, GLTF, and GLB directly as
`KPAT_Model`. Assimp parses them each time they are cold-loaded, creates a
Mesh sub-asset, and preserves basic material metadata, but no persistent engine
Model product exists. STL is not currently dispatched as a model extension.

This creates several problems:

- runtime loading depends on foreign parsers and source-side companion files;
- imported geometry has no versioned, validated native serialization;
- a Model has no persistent ordered material-reference table;
- materials must be authored separately in Levels;
- repeated imports cannot cheaply prove that all source dependencies, importer
  rules, and generated products are unchanged;
- path-derived output names duplicate equivalent content and make renames
  invalidate otherwise identical assets;
- GLB/data-URI images cannot enter the current file-only image decode path.

The MI1 design question is: how should KimPeanutEngine turn a changing package
of foreign source files into immutable, reusable native Model and Material
assets while preserving readable names, dependency ownership, and safe
reimport behavior?

## Chosen architecture

Separate offline source import from runtime loading. The importer is an
Asset-owned library/tool that can run in a CLI or editor subprocess while the
engine application is closed; `AssetManager` is only a runtime product
consumer:

```text
Offline importer (no AssetManager or engine application)
  Foreign source package
  STL / OBJ+MTL / FBX / GLTF+BIN+images / GLB
    -> source-path hash and SQLite archive lookup
        prior row -> hash root + recorded source dependencies
        matching import key + verified products -> no-op
        miss/stale/missing product              -> import
    -> Assimp decoder
    -> current source-dependency discovery and package fingerprint
    -> ImportedModelDocument
    -> engine material conversion
    -> canonical serialization
    -> content-addressed .material products
    -> content-addressed .model product containing material references
    -> validate staged products
    -> atomically publish products, then commit the SQLite transaction

Runtime AssetManager
  readable Level model key
    -> read-only archive lookup
  verified hashed .model path
    -> NativeModelLoader
    -> declared hashed .material dependencies
    -> AssetManager registration
    -> Gameplay / Render
```

Foreign extensions are import-source formats, not the final runtime Asset
contract. The native `.model` format becomes the normal `KPAT_Model`
extension. A temporary migration path may still accept foreign files, but it
must be visibly deprecated and must not silently write project content from
`AssetManager::LoadSync`.

## Ownership boundaries

| Owner | Responsibility |
|---|---|
| Assimp decoder | Parse one supported foreign source into import-only geometry, material, image, node, and dependency descriptions. |
| Standalone model import pipeline | Discover the source closure, fingerprint inputs/settings, convert data, serialize canonical products, stage/validate/commit, and update the archive. It may run as a library or CLI while the engine is closed, but never depends on `AssetManager`, `AssetID`, Runtime, Editor, Render, or Graphics. |
| SQLite archive database | Index logical source/material names, source fingerprints, immutable product hashes, dependency rows, overrides, and reimport diagnostics. |
| Native Model loader | Read `.model`, validate its version/chunks, and declare referenced Material dependencies. |
| AssetManager | Own runtime Asset identity, cache registration, dependency edges, payload lifetime, and unloading. |
| Render/Graphics | Resolve loaded materials and create/retire GPU resources; never import foreign files. |
| Editor or CLI front end | Launch or call the standalone importer, show names/status/diagnostics, and request explicit cleanup or overrides. It is not required for the importer to operate. |

The import implementation belongs to standalone Asset infrastructure and must
not depend on `AssetManager`, `AssetID`, Runtime, Editor, Render, Graphics, or
backend types. Editor and CLI are replaceable callers; the engine application
does not need to be running.

## Archive layout

MI1 treats `asset/.archive/` as an engine-managed local import store:

```text
asset/.archive/
  archive.sqlite3
  archive.sqlite3-wal
  archive.sqlite3-shm
  models/
    <model-content-hash>.model
  materials/
    <material-content-hash>.material
  textures/
    <image-content-hash>.<source-or-native-extension>
  staging/
    <import-operation-id>/...
```

The SQLite database replaces the previously proposed per-source JSON catalogs.
A unique source-path hash and normalized source path remain stable lookup keys
when source content changes. Product filenames remain hashes of their canonical
serialized contents.

Use lowercase full-length SHA-256 hex for MI1. Record the algorithm in database
metadata. Do not use `std::hash`: it is not a persistent cross-build
format. Hash collisions are handled by verifying existing file bytes against
the expected digest before reuse; a mismatching file at the same address is a
hard corruption error.

The directory names, database path, and object lookup policy must live in one
Asset-owned archive helper. Decoders and native product loaders do not execute
SQL or assemble archive paths independently; the Level logical-key resolver is
the only narrow runtime read-only archive consumer.

## SQLite archive schema

The database is metadata and an authoring index, not a runtime Asset. Use
`PRAGMA user_version` for schema migration and normalized relational tables
equivalent to:

```sql
CREATE TABLE archive_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE sources (
    id                    INTEGER PRIMARY KEY,
    normalized_path       TEXT NOT NULL UNIQUE,
    path_hash             BLOB NOT NULL UNIQUE,
    display_name          TEXT NOT NULL,
    package_hash          BLOB NOT NULL,
    importer_id           TEXT NOT NULL,
    importer_version      INTEGER NOT NULL,
    settings_hash         BLOB NOT NULL,
    native_model_version  INTEGER NOT NULL,
    status                INTEGER NOT NULL,
    diagnostic            TEXT NOT NULL DEFAULT ''
);

CREATE TABLE source_dependencies (
    source_id        INTEGER NOT NULL
                         REFERENCES sources(id) ON DELETE CASCADE,
    normalized_path TEXT NOT NULL,
    content_hash     BLOB NOT NULL,
    PRIMARY KEY (source_id, normalized_path)
);

CREATE TABLE products (
    content_hash   BLOB NOT NULL,
    asset_type     INTEGER NOT NULL,
    relative_path  TEXT NOT NULL UNIQUE,
    byte_size      INTEGER NOT NULL,
    schema_version INTEGER NOT NULL,
    PRIMARY KEY (content_hash, asset_type)
);

CREATE TABLE source_products (
    source_id    INTEGER NOT NULL
                     REFERENCES sources(id) ON DELETE CASCADE,
    content_hash BLOB NOT NULL,
    asset_type   INTEGER NOT NULL,
    role         INTEGER NOT NULL,
    slot         INTEGER NOT NULL DEFAULT -1,
    display_name TEXT NOT NULL,
    PRIMARY KEY (source_id, role, slot),
    FOREIGN KEY (content_hash, asset_type)
        REFERENCES products(content_hash, asset_type)
);

CREATE TABLE material_overrides (
    source_id     INTEGER NOT NULL
                      REFERENCES sources(id) ON DELETE CASCADE,
    slot          INTEGER NOT NULL,
    authored_path TEXT NOT NULL,
    PRIMARY KEY (source_id, slot)
);
```

Store 256-bit hashes as fixed-size BLOB values and validate their length at the
repository boundary. Add secondary indices only for demonstrated browsing or
maintenance queries. Use slot `-1` for singleton roles such as the root Model;
material roles use their nonnegative source material slot.

The database maps readable logical model paths to hashes for authoring tools and
Level loading, but runtime correctness still comes from verified native product
bytes and typed product references inside `.model`. Logical names are not the
immutable product identity. The database must be rebuildable by
reimporting foreign sources and scanning verified products; it is derived state
rather than the sole copy of authored information.

Do not commit or merge `archive.sqlite3`, its WAL/SHM sidecars, or staging
files as source documents. Build/package steps include reachable native
products, not the authoring database. If team-wide mergeable import metadata is
needed later, export a deterministic text manifest rather than merging SQLite
files.

## SQLite configuration and access

Use a narrow `ModelArchiveDatabase` repository rather than exposing raw
`sqlite3*` handles:

- one connection per importing process, owned by the import service;
- `PRAGMA foreign_keys=ON`;
- `PRAGMA journal_mode=WAL` for concurrent readers and a short metadata
  writer;
- a bounded busy timeout with an actionable `ArchiveBusy` diagnostic;
- `PRAGMA synchronous=FULL` for commits that publish new roots;
- prepared statements and bound parameters only;
- explicit transactions and checked return codes for every schema/data change;
- versioned forward migrations inside one transaction; reject a database whose
  schema is newer than the tool;
- integrity-check and rebuild tooling for corruption recovery.

SQLite permits many readers but only one writer. Assimp decode, hashing, image
processing, and product serialization therefore happen outside the database
transaction. The final writer transaction contains only product rows,
dependency rows, and the source-root update and must remain short.

Keep the database on a local filesystem. Direct multi-machine access through a
network share is outside MI1 because reliable SQLite locking cannot be assumed
there.

## Hash and no-op rules

Compute three distinct hashes:

1. **Source path hash** — normalized Asset-relative source path. Locates the
   stable SQLite `sources` row.
2. **Source package hash** — canonical ordered hash over the primary source and
   every discovered source dependency. It detects changes to GLTF buffers and
   images, OBJ MTL/images, FBX sidecars, and other decoder inputs.
3. **Product content hash** — hash of final canonical `.model`,
   `.material`, or extracted image bytes. It names and deduplicates immutable
   native products.

The import key is equivalent to:

```text
SHA-256(
  source_package_hash
  + importer identity/version
  + canonical import settings
  + native model schema version
  + material schema/conversion version
)
```

Do not hash only the top-level source file. A GLTF JSON file may remain
unchanged while its BIN or image changes. Dependency paths are normalized,
deduplicated, sorted, and hashed with both path and content so two different
packages cannot alias accidentally.

The no-op path uses the dependency rows recorded by the previous successful
database transaction: hash the primary source and those dependencies before
invoking Assimp. If they match, the previous decode discovered the same source
closure and import can be skipped. If the root or any recorded dependency
changed, decode the source, rediscover the current closure, and replace the
recorded rows only after successful commit. This makes a real cache hit avoid
Assimp while still detecting changed companion files.

Import is a no-op only when all of these are true:

- the source row exists and its normalized path matches the request;
- the source package, importer, settings, and schema fingerprints match;
- the referenced `.model`, `.material`, and archived embedded-image files
  all exist;
- every existing product's bytes match its filename/content hash;
- every Model material slot agrees with the database product rows.

A timestamp may avoid unnecessary preliminary file reads in a later
optimization, but timestamps are never correctness evidence.

## Native model format

`.model` is the engine runtime format. MI1 defines a versioned, bounds-checked
binary container with at least:

- magic and native format version;
- endianness and required feature flags;
- vertex layout identifier and canonical vertex/index buffers;
- section table with index ranges and material-slot indices;
- local bounds;
- ordered typed Material references;
- optional readable model and material-slot names for diagnostics;
- chunk offsets/sizes and a payload integrity digest.

Each material reference stores the Material content hash or the canonical
Asset-root-relative path derived from that hash. The Native Model loader
converts these references into `AssetRegisterInfo::dependency_requests`;
`AssetManager` resolves them after releasing `load_mutex_`. The Model Asset
therefore owns the Material dependency edges. Mesh owns geometry and section
slot indices, not materials.

Canonical serialization is required before hashing:

- fixed little-endian scalar encoding;
- explicit field/chunk order;
- no hashing or writing raw C++ struct memory, padding, pointers, capacity, or
  unordered-container iteration;
- validated integer overflow, count, offset, and allocation limits;
- defined float handling, including rejection or canonicalization of NaN and
  negative zero where relevant.

Changing canonical bytes creates a new hash-named Model rather than mutating an
old product.

## Foreign decoder result

Assimp returns an import-only document equivalent to:

```cpp
struct ImportedModelDocument
{
    ImportedMeshSource mesh;
    std::vector<ImportedMaterialSource> materials;
    std::vector<ImportedImageSource> images;
    std::vector<ImportedSourceDependency> source_dependencies;
};
```

Material order matches the source/Assimp material index used by every Mesh
section. Image identity is separate from how a material samples it. The result
contains no `AssetID`, GPU handle, Render material instance, output path, or
archive mutation.

Format-specific expectations:

- GLTF/GLB follows normative metallic-roughness, image, sampler, alpha, and
  extension semantics.
- OBJ includes its MTL and referenced images in the source package hash.
- FBX includes embedded media and any resolved external media dependencies.
- STL normally produces geometry with an explicit engine default material slot
  unless supported source color metadata is deliberately converted.
- Unsupported or ambiguous properties generate source-scoped diagnostics; the
  importer does not silently claim full fidelity.

## Native material products

Convert every supported source material into canonical engine Material data,
serialize it, then hash the final bytes:

```text
Imported material description
  -> canonical MaterialResource authoring representation
  -> deterministic .material JSON bytes
  -> SHA-256
  -> asset/.archive/materials/<hash>.material
```

Canonical JSON uses a fixed schema version, field order, numeric formatting,
path normalization, and parameter ordering. Semantically identical generated
materials should produce identical bytes and therefore share one file.

For core GLTF metallic-roughness:

| Source input | Engine semantic | Color space / channel |
|---|---|---|
| base-color factor/texture | `base_color`, `base_color_texture` | Linear factor; sRGB RGB texture |
| metallic factor | `metallic` | Linear scalar |
| roughness factor | `roughness` | Linear scalar |
| metallic-roughness texture | `metallic_roughness_texture` | Linear; roughness G, metallic B |
| normal texture | `normal_texture` plus scale | Linear normal data |
| occlusion texture | `occlusion_texture` plus strength | Linear; occlusion R |
| emissive factor/texture | `emissive`, `emissive_texture` | Linear factor; sRGB texture |
| double-sided | surface cull policy | No culling when true |
| alpha mode/cutoff | surface blend/mask policy | Reject unsupported modes rather than silently approximating |

Material Asset V2 and the current G-buffer shader cannot represent every row.
MI1 must extend the Material schema/resolver/shader or issue a precise import
failure for unsupported required semantics. Packed metallic-roughness remains
packed; do not generate duplicate metallic and roughness images by default.

## Existing product and user-edit policy

Hash-named products are immutable:

| State | Behavior |
|---|---|
| Matching database row and verified products | Return `UpToDate`; write nothing. |
| Product with desired content hash already exists | Verify and reuse it, even if another source created it. |
| Source/import settings changed | Create only missing new hash products, then repoint the source row. |
| Database references a missing/corrupt product | Rebuild and verify; never report a no-op. |
| Old product is no longer referenced | Leave it for explicit archive garbage collection. |
| User wants to edit a generated material | Copy/promote it to a non-archive authored `.material`, then record an explicit override and generate a new Model product referring to that asset. |
| Hash-named file was edited in place | Treat it as corruption because its bytes no longer match its name; never silently adopt it. |

This replaces filename-overwrite heuristics. A content-addressed product is
never regenerated in place. Reimport publishes a new hash and updates only the
source and relationship rows. Cleanup is a separate, explicit mark-and-sweep
operation over database roots and is outside MI1.

## Texture handling

- Resolve external references relative to the foreign source, then require the
  normalized result to remain inside the Asset root.
- Add ImageIO memory decode for GLB buffer-view images and GLTF data URIs.
- Store embedded images under a content hash; identical bytes are reused.
- External project textures may remain ordinary source Asset paths, but their
  content hashes participate in the source package fingerprint.
- One image used by both metallic-roughness and occlusion is stored once and
  referenced with two different material semantics.
- Image identity remains independent of sRGB/linear interpretation; Render
  selects color space from the Material binding.

## Runtime and Level contract

Runtime Levels reference readable logical model paths, never FBX/GLTF/STL
directly:

```json
"model": "model/brickwall/floor"
```

At Level-load time, Runtime opens `asset/.archive/archive.sqlite3` in read-only
mode, maps the logical path (the normalized source path without its foreign
extension) to the source's verified Model product, and then loads the
hash-named `.model`. Missing, ambiguous, corrupt, or unavailable mappings fail
the Level load; Runtime never imports or writes the archive. Direct native
`.model` paths remain valid for low-level fixtures and migration diagnostics.

The native Model material table supplies the default material per section slot:

```text
per-slot Level override
  -> Model material reference at MeshSection::material_index
  -> authored Level fallback
  -> engine error material
```

Existing Levels with explicit model/material paths remain valid during
migration. Level material overrides must not modify the immutable native Model.

## Transaction and concurrency

1. Normalize the source path, compute its path hash, and query its source row.
2. When a source row exists, fingerprint the root plus its recorded source
   dependencies and check the import key and product integrity.
3. Return `UpToDate` without writes or Assimp decode when everything matches.
4. On a miss, decode into a private import result and discover the current
   complete source dependency closure.
5. Compute the new source package/import key and convert native products.
6. Serialize products into `asset/.archive/staging/<operation-id>/`.
7. Hash and validate staged bytes with the native Model, Material, and Image
   loaders.
8. Publish missing immutable products using create-if-absent semantics.
9. Re-verify products that another concurrent import won.
10. Begin one short SQLite transaction, upsert product/dependency/relationship
    rows, update the source root last, and commit.
11. On failure, roll back the database transaction, remove only this
    operation's staging directory, and retain the previous source rows/products.

Lock imports by normalized source path, not through
`AssetManager::load_mutex_`. Different sources may decode and serialize
concurrently, then take turns in short SQLite writer transactions and
deduplicate the same final products safely. Do not hold an Asset cache or
database transaction while reading source files, invoking Assimp, or serializing
products.

Filesystem publication and SQLite commit cannot form one atomic transaction.
Publish immutable products first and database roots last. A crash between those
steps can leave only unreachable product files, which are safe and later
collectable; it must never leave a committed database row pointing to an absent
product.

## Reference findings

- [Godot ResourceFormatImporter](https://github.com/godotengine/godot/blob/master/core/io/resource_importer.cpp)
  derives imported-product paths from a readable source filename plus a path
  hash and stores import metadata separately. This supports separating logical
  names from imported storage; MI1 uses full content hashes for products rather
  than Godot's exact naming/layout.
- [Godot EditorFileSystem](https://github.com/godotengine/godot/blob/master/editor/file_system/editor_file_system.cpp)
  checks stored import hashes before deciding whether reimport is required.
  The skip/reimport separation applies, but modification times alone are not
  accepted as MI1 correctness evidence.
- [O3DE SceneBuilderWorker](https://github.com/o3de/o3de/blob/development/Gems/SceneProcessing/Code/Source/SceneBuilder/SceneBuilderWorker.cpp)
  covers FBX, GLTF, and STL scene sources and includes a builder-version
  fingerprint to force reprocessing when conversion behavior changes.
- [O3DE Asset Processor utilities](https://github.com/o3de/o3de/blob/development/Code/Tools/AssetProcessor/native/utilities/assetUtils.cpp)
  fingerprint an ordered list containing the source and dependencies rather
  than only the root file. MI1 adopts that dependency-aware principle without
  importing O3DE's database, job, platform, or builder framework.
- SQLite's [appropriate-use guidance](https://www.sqlite.org/whentouse.html)
  supports a device-local application database with low writer concurrency and
  warns against direct multi-machine network-filesystem access. That matches a
  local Editor/import service, not a runtime or shared-server dependency.
- SQLite [WAL documentation](https://www.sqlite.org/wal.html) and
  [application-file guidance](https://www.sqlite.org/appfileformat.html)
  support transactional metadata and concurrent reads. MI1 keeps large native
  model and texture payloads outside the database so they remain directly
  streamable and packageable.
- The [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
  remains the normative source for GLTF material channels, defaults, image
  references, and color-space conversion.

## Rejected designs

- **Assimp writes `.model` or `.material`:** decoder would own engine schema,
  archive layout, and transaction policy.
- **Runtime `LoadSync` imports on a cache miss:** a read API would mutate the
  project and could stall or partially update runtime state.
- **Hash only the primary source file:** misses companion buffers, MTL files,
  textures, settings, and importer-version changes.
- **Name products after source/material display names:** renames duplicate
  identical content and introduce collisions.
- **Mutate a hash-named product in place:** breaks content identity and every
  archive entry that shares it.
- **One global archive JSON file:** lacks indexed queries, relational
  constraints, safe concurrent updates, and transactional metadata changes.
- **Store Model/texture payloads as SQLite BLOBs:** rejected for MI1 because
  large runtime products should remain directly streamable, independently
  verifiable, and packageable without opening the authoring database.
- **Use SQLite from Runtime to resolve every asset:** rejected for ordinary
  product dependencies, because native Model files already contain typed
  Material references. A narrow read-only lookup for user-facing Level model
  keys is accepted; it never replaces product references or performs import.
- **Store only material display names in `.model`:** names are not stable or
  unique dependency identity.
- **Split packed GLTF metallic-roughness images:** duplicates content and loses
  source semantics without a demonstrated runtime need.

## Implementation sequence

Each numbered stage is an independently assignable work contract. Tell an
agent the exact ID and link below; the stage page defines its permitted scope,
dependencies, deliverables, and acceptance checks.

1. [**MI1.1 — contract characterization**](MI1.1.md): freeze current foreign
   loader behavior with focused fixtures and tests.
2. [**MI1.2 — hashing and SQLite archive core**](MI1.2.md): provide stable
   hashes, paths, schema, repository operations, migrations, and no-op queries.
3. [**MI1.3 — pure foreign-model decoder**](MI1.3.md): produce an
   `ImportedModelDocument` without Asset registration or archive mutation.
4. [**MI1.4 — native Model V1**](MI1.4.md): define deterministic native Model
   bytes, defensive loading, and typed Material dependencies.
5. [**MI1.5 — native material conversion**](MI1.5.md): generate canonical
   Material and image products with correct glTF semantics.
6. [**MI1.6 — transactional model importer**](MI1.6.md): orchestrate cache
   decisions, staging, immutable publication, and short database commits.
7. [**MI1.7 — runtime migration**](MI1.7.md): make `.model` the read-only
   runtime path and migrate checked-in consumers.
8. [**MI1.8 — tooling and end-to-end validation**](MI1.8.md): expose explicit
   import workflows, promotion/override tools, diagnostics, and visual proof.

Dependency graph:

```text
MI1.1 -> [MI1.2 + MI1.3]
              -> [MI1.4 || MI1.5]
              -> MI1.6 -> MI1.7 -> MI1.8
```

MI1.2 and MI1.3 may proceed in parallel after MI1.1. MI1.4 and MI1.5 may then
proceed in parallel after agreeing on the imported-document and Material-
reference seams. MI1.6 is the first integration stage and must not begin until
MI1.2 through MI1.5 have landed.

Each substage must remain buildable. Before implementation begins, create a
matching `.spec` and factual journal because MI1 changes persistent formats,
Asset identity, runtime loading, and several module boundaries.

## Acceptance criteria

- [ ] STL, OBJ, FBX, GLTF, and GLB are treated as foreign import sources and
  produce a validated native `.model` product.
- [ ] The source package fingerprint covers the root file, discovered
  dependencies, importer version, settings, and native schema versions.
- [ ] A matching SQLite source row with verified products returns `UpToDate`
  and performs no writes or Assimp decode.
- [ ] Native `.model` and generated `.material` filenames are full
  content hashes whose bytes validate against their names.
- [ ] The versioned SQLite schema maps readable source/material names and slots
  to immutable product hashes with foreign-key integrity.
- [ ] Native Model files contain ordered typed Material references and own the
  corresponding runtime Asset dependency edges.
- [ ] Equivalent generated materials and embedded images deduplicate across
  different model imports.
- [ ] Changed sources create new products and atomically repoint their database
  roots; old shared products are not overwritten or automatically deleted.
- [ ] SQLite transactions remain short, concurrent imports serialize only
  metadata writes, and busy/newer-schema/corrupt-database states have stable
  diagnostics and recovery paths.
- [ ] A crash boundary can leave at most unreachable immutable files; the
  committed database never references a missing product.
- [ ] User-authored materials live outside the immutable archive and can be
  selected through an explicit override/promotion workflow.
- [ ] Missing/corrupt products, unsupported source semantics, malformed native
  chunks, path escape attempts, and hash collisions fail with stable
  diagnostics and leave the previous import usable.
- [ ] Runtime loading of native `.model` performs no Assimp work and no
  project-file writes.
- [ ] Existing Level overrides remain deterministic, and checked-in
  multi-material native models render correctly on Vulkan and OpenGL.

## Validation

Minimum implementation evidence:

```powershell
.\tools\kp.ps1 build AssetUnitTest
.\tools\kp.ps1 test AssimpModelLoader
.\tools\kp.ps1 test NativeModel
.\tools\kp.ps1 test ModelArchive
.\tools\kp.ps1 test ModelArchiveDatabase
.\tools\kp.ps1 test Material
.\tools\kp.ps1 build RenderPassScheduleTest
.\tools\kp.ps1 test RenderPassScheduleTest
.\tools\kp.ps1 smoke
```

Add deterministic tests for hash vectors, canonical serialization, source
dependency ordering, SQLite schema migration/foreign keys/busy handling,
no-op imports, missing/corrupt products, concurrent deduplication, database
transaction rollback, filesystem/database crash boundaries, and recovery. Use
unique temporary directories for filesystem/database tests.

Runtime validation must load checked-in hashed `.model` products without
Assimp, render representative OBJ/FBX/GLTF/GLB imports on Vulkan and OpenGL,
and inspect captured `SceneColor`. Compilation alone does not prove persistent
format compatibility, material-channel fidelity, or archive correctness.
