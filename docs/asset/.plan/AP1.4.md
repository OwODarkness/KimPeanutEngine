# AP1.4 — Scene/install Asset package

- Status: deferred until the engine has a publish/shipping consumer
- Parent: [AP1 — Startup Asset Loading Performance](AP1.md)
- Roadmap: [Asset Module TODO](../TODO.md#startup-performance-roadmap)
- Execution spec: [Asset Startup Loading Performance](../../../.spec/specs/asset-startup-loading-performance.md)

## Objective

Add an optional immutable runtime package that combines the existing
content-addressed native products into a locality-friendly file without
changing Asset identity, dependency ownership, editor authoring, or the loose
development workflow.

AP1.4 is a physical-storage optimization. It must make the existing
`.model`, `.texture`, `.material`, and `.level` products cheaper to locate and
read, but it must not make a Material own Texture bytes or make a package
entry become an `AssetID`.

## Design decision

Use two representations of the same products:

```text
Authoring/source data
        -> offline import/cook
Loose derived products in asset/.archive
        -> offline package build
Immutable .kpackage / .kpatch files
        -> read-only ProductStore
AssetManager -> Resource -> Render -> Graphics
```

- Loose products remain the development source of truth and are easy to
  reimport, inspect, and replace.
- A package is a derived install/runtime artifact and is never edited in
  place.
- A development overlay resolves a loose product before a package product.
- A shipping/runtime mount can use the package without exposing the SQLite
  archive or authoring files.
- The package contains independent entries for Level, Model, Material, and
  Texture. Material remains metadata; Texture remains an independent product.
- Model and Texture receive the main locality/range benefit. Material and
  Level are small metadata entries but are included so a runtime package is
  self-contained.

The package extension is deliberately distinct from the existing
`asset/.archive` product directory and `archive.sqlite3` authoring database.
The initial names are `.kpackage` for a base package and `.kpatch` for an
overlay package.

## Scope

### In scope

- Versioned package header and TOC with bounded integer fields.
- Independent product entries keyed by immutable product hash and type.
- Whole-product reads for the current native Model, Texture, Material, and
  Level formats.
- Optional texture mip/chunk range descriptors reserved for AP1.5.
- Offline package construction from a resolved dependency closure.
- Read-only package mounting and product lookup through a narrow ProductStore
  boundary.
- Loose-over-package overlay resolution for development iteration.
- Immutable patch packages for changed products; no in-place package editing.
- Product, TOC, range, and package-integrity validation before publication.
- Equivalence tests between loose and packaged loading.

### Not in scope

- A general virtual filesystem.
- Runtime import, recook, or package mutation.
- Loading every package entry into `AssetManager` at mount time.
- Low-mip scene commit, background streaming, or GPU residency changes; those
  belong to AP1.5.
- Changing native `.model`, `.texture`, `.material`, or `.level` schemas.
- Embedding Texture payloads inside Material products.
- Packaging shader source or API-specific shader caches; that remains under
  the shader/resource pipeline.

## Identity and references

The package must preserve the existing identity layers:

| Layer | Meaning | Package use |
| --- | --- | --- |
| ContentID | Stable user-facing authored asset identity | Optional logical alias/metadata |
| Product hash | Exact immutable native product identity | Primary TOC key |
| AssetID | Runtime-session handle and generation | Never serialized in the package |

Authoring metadata may refer to a stable ContentID. Cooked dependency records
must resolve to the exact product hash required by the product. A package mount
does not allocate `AssetID`s and does not publish Asset cache entries.

## Package format

The first format version is little-endian and fixed-width at its wire
boundary. Variable strings are not stored in entries; logical names remain in
the authoring/catalog layer. All offsets and sizes are checked for overflow
and must remain inside the package file.

```text
KPKG header
  magic, format_version, header_size
  package_flags, target_profile
  package_identity
  toc_offset, toc_size, entry_count
  toc_hash, package_data_end

TOC entry[n]
  product_hash
  AssetType
  native_product_version
  entry_flags
  data_offset, stored_size
  logical_payload_size
  entry_hash
  priority_class
  range_table_offset, range_count

optional range table
  product-relative range offset and size
  semantic kind (whole/header/mip/chunk)
  mip or chunk ordinal
  priority class

data region
  exact native product bytes
```

The first implementation stores each product as an exact byte blob. The
range table records native Texture directory information when available, but
AP1.4 does not expose a partially resident Texture. AP1.5 may consume those
ranges through a range-aware reader without changing product identity.

The package builder writes to a temporary path, validates the complete output,
then publishes with an atomic rename. A package is immutable after
publication. The reader validates the header, TOC bounds, entry bounds,
non-overlap policy, supported type/schema, and entry hash before returning
bytes to a native loader.

## ProductStore boundary

Add a package-specific reader without turning AssetManager into a filesystem
owner:

```text
ProductStore
  Resolve(canonical product request) -> ProductKey + source kind
  Read(ProductKey, WholeProduct) -> verified product bytes
  Describe(ProductKey) -> type, schema, sizes, ranges
```

Implementations:

- `LooseProductStore` reads the current hash-named files under
  `asset/.archive` and preserves strict development verification.
- `PackageProductStore` mounts a `.kpackage` or `.kpatch` read-only and reads
  the TOC-selected byte range.
- `OverlayProductStore` checks loose products first, then patch mounts from
  newest to oldest, then the base package.

Native Model/Texture loaders should consume a verified product-read result,
not know whether the bytes came from `ifstream` or a package range. Existing
`AssetManager::LoadSync(path)` remains compatible; path-to-product resolution
and runtime Asset registration stay above ProductStore.

Ownership remains:

- `AssetPackage`: package format, TOC reader/writer, bounds, and integrity.
- `AssetImport`: closure resolution and offline package construction.
- `AssetRuntime`: mounted ProductStore and native loader integration.
- `AssetManager`: AssetID, cache, dependency edges, rollback, and lifetime.
- `Runtime`: package selection and startup policy.
- `Editor`: authoring/catalog presentation and loose-file edit workflow.

The package reader must not depend on Editor, Render, Graphics, or
`AssetManager`.

## Offline base-package build

`PackageBuilder::Build` takes a package request containing:

- one or more root Level product paths;
- target platform/profile, including the selected Texture variant;
- package output path and format version;
- optional priority policy; and
- optional source archive/database used only to resolve logical references.

The builder performs this transaction:

1. Resolve the root closure through native product dependency metadata and
   archive mappings. Do not construct runtime Assets.
2. Canonicalize each product path and derive its product hash/type.
3. Deduplicate by `(product_hash, AssetType)` so shared Textures occur once.
4. Read and verify each source product, including native structure and hash.
5. Assign priority classes: Level/metadata first, Models next, Texture
   products by startup priority, and optional products last.
6. Copy exact product bytes to the temporary package data region.
7. Emit TOC entries and optional Texture range descriptors.
8. Validate closure completeness, duplicate identity consistency, all bounds,
   hashes, and deterministic entry ordering.
9. Atomically publish the package and a small build manifest containing the
   input product hashes and selected profile.

The builder may run quickly because it combines already-cooked products; it
does not Assimp-decode, recook, or regenerate native products.

## Patch package behavior

Changed products create a new product hash. The base package is never opened
for mutation. A patch build:

1. Resolves the changed product and any changed dependency closure.
2. Copies only new products into a new `.kpatch`.
3. Writes a patch TOC containing replacement product-hash mappings and the
   package/base identity it targets.
4. Mounts the patch before older patches and the base package.

A patch is valid only when its dependency closure is resolvable through the
   complete mount stack. Repacking can later fold patches into a new base
   package and remove unreachable old products. Garbage collection and patch
   compaction are follow-up tooling, not in-place mutation.

## Development and shipping behavior

### Editor/development

- Asset Browser reads the ContentCatalog/metadata snapshot, not package
  payloads.
- Loose products override packages, allowing a Material reimport or a new
  Model/Texture cook to be tested immediately.
- The editor can mount a package for runtime-preview and package-equivalence
  tests, but package entries remain read-only.
- Package staleness is reported when a loose product hash differs from the
  package manifest; no automatic destructive rebuild occurs.

### Runtime/shipping

- Runtime mounts the package and reads the TOC without registering all
  entries as Assets.
- Startup requests products by logical request/product hash; the ProductStore
  resolves them to package ranges.
- A shipping build may omit loose products and the SQLite authoring database.
- AP1.5 later uses package ranges for low-mip readiness and background
  residency.

This stage is intentionally deferred while the engine has no publish/shipping
pipeline. Development and editor startup continue to use the loose product
directory; AP1.5 improves that path without requiring a package mount.

## Implementation stages

### AP1.4a — Format and read-only TOC contract

- Add `AssetPackage` format types and bounded serialization.
- Add ProductKey, package entry, priority, and range descriptor validation.
- Add a reader that can open a package, inspect the TOC, and read one exact
  product blob.
- Add corruption tests for magic, versions, overflow, truncated TOCs,
  overlapping/out-of-file ranges, duplicate keys, and hash mismatches.

Exit: a package can be inspected and one product can be read safely without
AssetManager or Editor dependencies.

### AP1.4b — Offline base-package builder

- Resolve a checked-in Sponza closure from existing native products.
- Deduplicate shared products and produce deterministic ordering.
- Support the selected BC/portable profile as an explicit build input.
- Publish atomically and emit a manifest for staleness/equivalence checks.
- Add builder tests for shared Texture identity, missing products, wrong type,
  unsupported schema, and deterministic output.

Exit: the Sponza package contains one independently addressable entry per
resolved product and can be rebuilt without recooking.

### AP1.4c — Runtime mount and overlay

- Add Loose, Package, and Overlay ProductStore implementations.
- Route native Model/Texture/Material/Level product reads through the store.
- Preserve strict loose verification and package entry verification.
- Add patch package creation for changed products and mount precedence.
- Add packaged/loose Asset identity, dependency, rollback, and failure
  equivalence tests.

Exit: switching between loose and packaged stores changes physical reads only;
the resulting Asset graph and payload semantics are equivalent.

### AP1.4d — Startup locality evidence

- Launch the fixed Sponza level through the packaged store and record file
  opens, read ranges, bytes, peak memory, and Asset wall time.
- Compare Debug and RelWithDebInfo separately, with cold and warm filesystem
  cache conditions kept separate.
- Verify that package mounting itself does not load all product payloads.
- Export package/loose runtime captures on Vulkan and OpenGL if the visual
  path changes.

Exit: package locality is measured and the package path is ready for AP1.5's
range-aware scheduler; AP1.4 does not claim initial-mip readiness.

## Acceptance criteria

- [ ] The package format is versioned, bounded, endian-defined, and validated
  before any Asset publication.
- [ ] A package can be generated from existing loose products without import,
  Assimp decode, or Runtime startup.
- [ ] Shared Texture products occur once per package closure.
- [ ] Model, Texture, Material, and Level retain independent product hashes,
  types, and dependency semantics.
- [ ] Mounting a package reads only its header/TOC; it does not register every
  contained product or load every payload.
- [ ] Loose-over-package development overlays resolve changed products
  without modifying the base package.
- [ ] Patch packages contain only changed products and reject a mismatched
  base identity or incomplete dependency closure.
- [ ] Loose and packaged loads produce equivalent Asset identities,
  dependency edges, payload values, rollback, and corruption behavior.
- [ ] The package path reduces product file-open/seek work and reports the
  measured byte/read-range difference against loose Sponza loading.
- [ ] No AP1.4 change claims low-mip scene commit; that acceptance remains
  AP1.5.

## Dependencies and follow-up

- Depends on AP1.1's verified-product result, AP1.2's selected Texture profile,
  and AP1.3's native Model V3 products.
- AP1.5 depends on the ProductStore read-range contract and package range
  descriptors, then adds bounded jobs and progressive Texture residency.
- AP1.6 owns the final five-second RelWithDebInfo target, full closure
  streaming measurement, and cross-backend visual gate.

The first implementation should deliberately avoid a general VFS, in-place
package rewriting, and package-only editor behavior. Those additions would
increase the maintenance surface without improving the measured Sponza path.
