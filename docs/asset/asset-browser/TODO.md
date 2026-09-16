# Asset Browser TODO

**Status: AB1.0–AB1.2b implemented; AB1.2 active.** [AB1.2b](.plan/AB1.2b.md) completes the icon-first visual browser milestone. Architecture is in [PLANS.md](PLANS.md). AB1.3–AB1.4 remain proposed. Parent work remains in [Asset Module TODO](../TODO.md).

## AB1 — Read-only Asset Browser and Reference Viewer

- [x] **[AB1.0 — Freeze the catalog and interaction contract](.plan/AB1.0.md).** → [journal](../../../.spec/journal/2026-09-13-asset-catalog-contract.md)

  - [x] Define snapshot-local node identity separately from runtime `AssetID`.
  - [x] Define kind, readable type/name, residency, dependency coverage,
    provenance, paths, product metadata, one ordered edge table, and diagnostics.
  - [x] Define readable registered-custom-type names without central enum cases. Fallback icons remain AB1.2 Editor presentation.
  - [x] Add a synthetic Level -> Model -> Material -> Texture fixture with a shared Texture, unresolved edge, and cycle.
  - [x] Freeze deterministic node/edge/diagnostic ordering. Traversal depth and emitted-row limits belong to the AB1.3 bounded projection and are frozen there.

- [x] **[AB1.1 — Publish the Asset-owned immutable catalog](.plan/AB1.1.md).** → [journal](../../../.spec/journal/2026-09-13-asset-catalog-provider.md)

  - [x] Add deterministic strict read-only archive enumeration.
  - [x] Copy live AssetManager metadata and edges under the state lock, then release it before graph assembly.
  - [x] Join archive/live nodes by canonical product identity and retain readable aliases without duplicating products.
  - [x] Keep one canonical forward edge table and prove that its reverse
    projection produces the expected referencers.
  - [x] Preserve live nodes and publish a diagnostic when archive access fails.
  - [x] Prove no payload pointer, `Asset*`, cache reference, database handle, or retained Asset lock crosses the boundary.
  - [x] Test ordering, joins, aliases, reverse edges, partial failure, concurrent read/unload, and custom types.

- [ ] **[AB1.2 — Implement the Asset Browser](.plan/AB1.2.md).**

  - [x] **Prerequisite: [AB1.2a — Imported Content Metadata and Catalog Boundary](.plan/AB1.2a.md).** → [journal](../../../.spec/journal/2026-09-16-asset-ab1-2a.md)
    Define content as the browser root, generated metadata records,
    ContentID references, product-hash details, internal shader visibility,
    and the import/startup isolation gates before changing the UI projection.

  - [x] **Visual substage: [AB1.2b — Visual Tile Presentation](.plan/AB1.2b.md).** → [journal](../../../.spec/journal/2026-09-16-asset-ab1-2b.md)

  - [ ] Add one Editor-owned model consuming only imported-content snapshots.
  - [ ] Register the browser as a tab in the Editor tool row (ED1) with navigation,
    readable rows, and a status footer. Movability comes from the row's isolate, not
    from the browser owning window geometry.
  - [ ] Draw cross-backend file/folder/type icons beside readable Name and Type text.
  - [ ] Show State, Size, and full paths for archive, loaded, and runtime-only nodes.
  - [ ] Add case-insensitive search, type/state filters, deterministic sorting, selection, and explicit refresh.
  - [ ] Add **View > Asset Browser** to the existing View menu, with a live checkmark synchronized with the tab close.
  - [ ] Publish the selected stable key through the AB1.3 open-root callback seam;
    do not render an inert action before the viewer is bound.
  - [ ] Test search, filters, sorting, selection preservation, and refresh.

- [ ] **[AB1.3 — Implement the Asset Reference Viewer](.plan/AB1.3.md).**

  - [ ] Add **View > Asset Reference Viewer** with synchronized visibility.
  - [ ] Support Dependencies and Referencers directions.
  - [ ] Support Tree and Text modes in one window.
  - [ ] Preserve ordered, readable Level -> Model -> Material -> Texture labels for the checked-in Sponza closure.
  - [ ] Mark shared, cyclic, unresolved, archive-only, and runtime-only nodes.
  - [ ] Bound traversal and render a clear truncation row.
  - [ ] Add expand/collapse, selection, path/details footer, and text copy.
  - [ ] Bind browser double-click and **Open References** to the selected root
    without loading it.
  - [ ] Ensure direction/mode changes never mutate or load Asset state.
  - [ ] Test traversal, cycles, sharing, missing targets, inversion, bounds, and deterministic text export.

- [ ] **[AB1.4 — Integrate and validate both Editor backends](.plan/AB1.4.md).**

  - [ ] Build affected Asset, Editor, and focused test targets.
  - [ ] Run focused catalog and Editor model tests.
  - [ ] Run the checked-in Sponza fixture on Vulkan and OpenGL.
  - [ ] Capture both panels under `save/screenshots/validation/` through the Runtime command path.
  - [ ] Verify icons/names, search/filter/sort, window toggles, close/menu synchronization, Tree/Text, Dependencies/Referencers, resize, and shutdown.
  - [ ] Verify archive failure still shows the live graph and diagnostic.
  - [ ] Confirm Editor depends only on the narrow snapshot contract and Asset gains no Editor/Render/Graphics dependency.
  - [ ] Record implementation evidence in a dated `.spec/journal/` entry.

## Deferred follow-up

- [ ] Thumbnail generation and caching.
- [ ] Drag/drop into scene, inspector, or material slots.
- [ ] Rename, move, delete, duplicate, import, and reimport commands.
- [ ] Archive/filesystem notifications and automatic refresh.
- [ ] Package membership, residency, and streaming visualization.
- [ ] Saved filters, favorites, collections, and layout persistence.
- [ ] External editor or OS file-browser integration.

Each deferred item needs its own owner, mutation/lifetime contract, and acceptance criteria before joining AB1.
