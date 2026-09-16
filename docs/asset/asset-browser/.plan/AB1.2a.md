# AB1.2a — Imported Content Metadata and Catalog Boundary

- Status: implemented prerequisite for [AB1.2 Asset Browser](AB1.2.md)
- Parent design: [Asset Browser Plans](../PLANS.md)
- Roadmap: [Asset Browser TODO](../TODO.md)
- Prerequisite: [AB1.1 Asset catalog snapshot provider](AB1.1.md)

## Objective

Define the user-facing imported-content namespace before implementing the
Asset Browser window. The browser must show logical assets that have completed
import, not raw source files and not the hash-named products in `.archive`.

This stage freezes metadata, identity, reference, visibility, and publication
rules. It adds no Editor UI and does not change Runtime startup loading.

## Directory ownership

The first project layout is:

```text
asset/                         raw source and engine-owned input data
  model/
  texture/
  ...
content/                       isolated logical-content namespace
  model/sponza.kpmeta
  material/bricks.kpmeta
  shader/                      authored shader source; hidden from normal browser rows
  .archive/                    generated products, compiler cache, and archive DB
    archive.sqlite3
    models/<content-hash>.model
    materials/<content-hash>.material
    textures/<content-hash>.texture
    compiler/<api>/<content-hash>.bin
```

`content/` is the browser's virtual root. The browser does not recurse
through the raw `asset/` tree and does not expose `.archive/`. Existing raw
directories remain valid during migration; moving them is outside this stage.

The content directory is isolated from raw Asset input and is the browser's
only namespace once it exists. Metadata is generated and Asset-owned. Shader
source is user-facing content storage but has an internal browser-visibility
policy. During migration, projects without a content directory retain the
AB1.1 archive/live projection; creating the content directory switches the
provider to the content namespace, including the empty-content case.
The content/.archive directory is generated compiler/archive data and must be
ignored by source control and excluded from every content enumeration. The
current asset/shader/cache compiler-cache path is legacy; moving it to
content/.archive/compiler is a later implementation step in this stage.

## Metadata contract

Each successfully imported logical asset has one metadata record. A record
contains values needed by an editor or an offline tool:

```json
{
  "schema": 1,
  "id": "content-id",
  "type": "Material",
  "name": "Bricks",
  "source": "material/bricks.material",
  "references": {
    "base_color": { "asset_id": "texture-content-id", "srgb": true },
    "normal": { "asset_id": "normal-content-id", "srgb": false }
  },
  "products": {
    "material": "product-content-hash"
  }
}
```

Rules:

- `ContentID` is the persistent logical identity and is authoritative for
  metadata references.
- `name` and the content-relative path are readable labels; they are not
  identity.
- `ContentHash` identifies exact immutable product bytes and is used for
  archive filenames, cache validation, and packaging.
- Runtime `AssetID` is temporary and must never be serialized into metadata.
- A metadata reference may carry a readable path as a diagnostic hint, but
  resolution uses `ContentID` and rejects an ambiguous or missing ID.
- Reimport may replace product hashes without rewriting dependent metadata.
- Rename and move preserve `ContentID`; the registry updates the path index.

Material, Model, and other visible content records reference other content
records by ID. The importer resolves those references while producing native
products; native products may store product hashes for runtime efficiency.
User-facing metadata must not depend on `.archive` paths.

## Browser visibility

The normal browser projection contains only metadata records whose import
transaction has published a valid product closure:

```text
Visible:       imported Model, Material, Texture, Level, registered content
Hidden:        raw FBX/OBJ/PNG files, archive products, runtime-only objects
Hidden type:   Shader and other internal implementation dependencies
Diagnostics:   stale, failed, missing-product, and orphaned records
```

Diagnostics may be exposed by an explicit Problems view, but a failed or stale
record must not masquerade as a healthy imported asset. Internal shader
references remain available to validation and technical details without
becoming top-level browser rows.

## Ownership and data flow

```text
AssetTool/import transaction
  -> immutable archive products
  -> content metadata record
  -> Asset-owned ContentRegistry index
       + read-only archive verification
       + optional copied runtime state
       -> immutable Editor ContentCatalogSnapshot
            -> AssetBrowserModel and UI
```

Asset owns metadata publication, ID/path indexes, product verification, and
diagnostics. Editor receives value-only records and never opens metadata files,
the SQLite database, raw source tree, or AssetManager directly.

The existing product graph from AB1.1 remains useful for reference traversal
and diagnostics. AB1.2 must project imported content records as its primary
rows instead of exposing product nodes as the default `All` view. Product
records can remain attached as details and as an internal graph layer.

## Performance and lifecycle invariants

### Import-time isolation

- The browser adds no raw-tree scan, archive enumeration, decoding, hashing, or
  dependency traversal to an import.
- Import already knows the source name, ContentID, dependency references, and
  product hashes. Metadata publication serializes those existing values only.
- Metadata publication is bounded by the number of imported records and is
  part of the existing publication transaction; it must not reread source
  bytes or product bytes.
- If metadata publication fails, the import transaction fails atomically or
  leaves an explicitly diagnosable incomplete record. A silently missing
  browser entry is not acceptable.

### Startup isolation

- Runtime startup does not create the Editor ContentRegistry or capture a
  content snapshot.
- Runtime loading does not scan `content/`, enumerate the archive, or
  load metadata for the browser.
- The browser provider is created only when the Editor workspace is promoted,
  and capture occurs only on browser construction or explicit Refresh.
- Browser metadata and snapshots are bounded, read-only, and never retained by
  Runtime startup services.
- No new Runtime-to-Render, Runtime-to-Graphics, or Asset-to-Editor dependency
  is introduced.

### Snapshot safety

- `ContentCatalogSnapshot` contains strings, IDs, product hashes, statuses, and
  copied references only.
- No `Asset*`, payload pointer, database handle, filesystem iterator, mutex
  guard, or runtime cache object crosses the provider boundary.
- Content references are validated and unresolved IDs become diagnostic leaves.
- Snapshot capture is explicit and can be performed after startup without
  blocking the frame loop indefinitely; configured limits produce a Partial
  snapshot with diagnostics.

## Implementation sequence

1. Freeze `ContentID`, metadata schema version, content-relative path rules,
   visible/internal type policy, and imported-status values.
2. Add the Asset-owned metadata writer to the existing successful import
   publication path, reusing names, IDs, dependency information, and hashes
   already in memory.
3. Add a read-only `ContentRegistry` that indexes metadata records by ID and
   normalized path and verifies their referenced products through the archive
   contract.
4. Extend the Asset snapshot boundary with imported-content records and a
   product-detail mapping. Keep AB1.1 product nodes for diagnostics/reference
   traversal, but do not make them default browser rows.
5. Add headless tests for metadata round trips, ID-stable reimport, rename/move
   path updates, broken references, shader invisibility, atomic publication,
   and product-hash changes.
6. Only after this stage passes, revise AB1.2's Editor model and component to
   browse `content/` as a folder tree.

## Implemented boundary

`ContentRegistry::Capture()` is explicit and read-only. The provider builds a
value-only content snapshot from visible, ready metadata records, validates
the archive product closure by file existence, carries persistent ContentIDs
and product hashes into catalog nodes, and turns unresolved ContentID
references into validated diagnostic leaves. Internal records and shader
records are filtered before publication. No Runtime startup path constructs or
captures this registry.

The provider keeps the AB1.1 projection only for projects that do not yet have
a `content/` directory. This preserves existing checkouts during migration;
the authoritative content mode never falls back to raw archive/live rows.

## Acceptance criteria

- A raw file with no successful import metadata is absent from the normal
  Asset Browser.
- An imported asset has a readable name/path and persistent `ContentID` even
  when its derived product hash changes.
- Material-to-texture and model-to-material references resolve through
  `ContentID`, not metadata filenames or archive hashes.
- `.archive` products and shaders do not appear as normal browser rows.
- Metadata generation performs no second source/product read, decode, hash, or
  dependency walk during import.
- Runtime startup behavior and timing path are unchanged because no browser
  catalog is created or captured before Editor workspace promotion.
- The Editor consumes only an immutable Asset-owned snapshot and never opens
  raw files, metadata files, or SQLite directly.

## Validation

```powershell
.\tools\kp.ps1 build AssetCatalogProviderTest
.\tools\kp.ps1 test Asset
git diff --check
```

The import performance gate must compare cold and warm import metrics before
and after metadata publication. The startup gate must compare Runtime startup
with the browser closed and must show no content-provider capture before
workspace promotion. Editor visual and interaction validation belongs to
AB1.2/AB1.4 after this contract is implemented.
