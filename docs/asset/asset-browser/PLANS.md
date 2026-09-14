# Asset Browser Plans

**Status: proposed.** This document defines the Editor Asset Browser and Asset Reference Viewer. The imported-content boundary is frozen first by [AB1.2a](.plan/AB1.2a.md). Work is tracked in [TODO.md](TODO.md); parent architecture remains in [Asset Module Plans](../PLANS.md).

## Goal

Provide a readable Unreal/Unity-style view of assets already known to the engine:

- **Asset Browser:** searchable imported-content inventory backed by readable metadata.
- **Asset Reference Viewer:** Dependencies and Referencers in Tree or Text mode.

```text
level/sponza.level
  -> model/sponza
      -> Sponza Model product
          -> Material products
              -> Texture products
```

Both windows appear under View. **Open References** selects the browser row as the viewer root.

## Ownership

```text
Raw source + import publication + archive database + AssetManager
  -> Asset-owned ContentRegistry and immutable ContentCatalogSnapshot
      -> Editor-owned AssetBrowserModel
          -> Browser + Reference Viewer
```

Asset owns archive enumeration, live edges, archive/live identity joins, locking, and diagnostics. Editor owns visibility, selection, search, sorting, filters, icons, layout, and traversal state.

- `AssetManager` keeps runtime identity, payload lifetime, dependencies, and unload protection.
- `ModelArchiveDatabase` keeps SQLite; Editor never queries it directly.
- Browsing never loads, imports, renames, deletes, or republishes assets.
- Render/Graphics gain no browser types.

## Catalog contract

Each value-only node has snapshot-local ID, kind, readable type/name, logical/product/source paths, ArchiveOnly/Loaded/RuntimeOnly state, size/schema, ordered dependencies, and referencers. The snapshot has a revision and diagnostics.

Invariants:

- Archive-only products never receive fabricated runtime `AssetID`s.
- No payload pointer, `Asset*`, cache reference, database handle, or retained lock crosses the snapshot boundary.
- Missing targets become diagnostic leaf nodes, not dangling IDs.
- Model material-slot and Material texture-parameter order is preserved.
- Referencers derive from the copied forward graph.
- Content may have multiple readable aliases but one product identity.
- Archive failure preserves the usable live graph and adds a diagnostic.

`ModelArchiveDatabase` gains deterministic strict read-only source/product enumeration. `AssetManager` copies live metadata and edges under `state_mutex_`, releases the lock, then assembles the graph. Canonical product paths/hashes join both sources, so a Level's readable `model/sponza` reference can display “Sponza” although the product path is hashed. Inline Mesh children remain runtime-only.

Source-file dependencies are import provenance, not runtime references, and remain a separate details section.

## Windows

```text
+ Asset Browser --------------------------------------------------+
| Assets / Imported                     [ Search assets...       ] |
| [All] [Level] [Model] [Material] [Texture]       [State v]     |
|----------------------------------------------------------------|
| Name                 Type       State          Size             |
| [file] Sponza        Model      Loaded         8.2 MB           |
| [file] bricks        Material   Loaded           3 KB           |
| [file] albedo        Texture    Archive        1.4 MB           |
+----------------------------------------------------------------+
```

Icons use ImGui primitives or a bundled cross-backend font and always sit beside readable Name and Type text. Labels prefer archive display name, logical filename, then product filename. Search covers names, paths, and type. State uses text plus color. Refresh is explicit until change notification exists. Double-click/Open References never triggers a load.

```text
+ Asset Reference Viewer -----------------------------------------+
| Root: level/sponza.level        [Dependencies] [Referencers]     |
| View: (o) Tree  ( ) Text             [Expand] [Collapse] [Copy] |
|----------------------------------------------------------------|
| v Level  sponza.level                                           |
|   v Model  Sponza                                              |
|     v Material  bricks                                          |
|       - Texture  bricks_albedo                                  |
+----------------------------------------------------------------+
```

Tree mode preserves order, marks shared nodes, renders ancestor repeats as cycle leaves, shows unresolved leaves, and bounds depth/node count. Text mode emits readable indented edges such as `Level level/sponza.level -> Model model/sponza [Loaded]`, with shared/cycle/unresolved/truncation annotations.

Dependencies answers “what does this need?” Referencers answers “what points to it?”

## Editor integration

```text
View
  [x] Asset Browser
  [ ] Asset Reference Viewer
```

Menu checkmarks and title-bar close buttons share one visibility state; the menu needs a queried/toggled binding rather than a copied `selected` value. `EditorUI` owns one model and both panels, creates them after workspace promotion, and destroys them before Runtime teardown.

## Stages and evidence

- **[AB1.0 — Catalog contract and fixture](.plan/AB1.0.md):** freeze snapshot
  identity, edge/state/order rules, pure validation helpers, and a reusable
  shared/cyclic fixture.
- **[AB1.1 — Asset catalog snapshot provider](.plan/AB1.1.md):** implement
  transactional archive enumeration, bounded live graph capture, joins,
  diagnostics, and headless concurrency tests.
- **[AB1.2a — Imported content metadata and catalog boundary](.plan/AB1.2a.md):**
  freeze content, metadata identity/reference rules, internal shader
  visibility, and import/startup isolation before the browser UI.
- **[AB1.2b — Visual tile presentation](.plan/AB1.2b.md):** make the Editor browser icon-first by default with clipped tiles, readable labels, and content-folder navigation; table mode remains available.
- **[AB1.2 — Asset Browser window and Editor composition](.plan/AB1.2.md):**
  consume the imported-content snapshot through Runtime, add the snapshot-only
  Editor model, readable Table/Tiles UI, explicit refresh, and reactive View
  binding. Product nodes remain details/diagnostics, not default rows.
- **[AB1.3 — Asset Reference Viewer](.plan/AB1.3.md):** derive both graph
  directions, add bounded Tree/Text projections, copy, and browser navigation.
- **[AB1.4 — Hardening and dual-backend acceptance](.plan/AB1.4.md):** validate
  the real Sponza closure, failure/refresh states, layout, performance bounds,
  teardown, and fresh Vulkan/OpenGL captures.

Tests prove deterministic enumeration, pointer/lock-free snapshots, joining, reverse edges, shared/cycle/missing handling, bounds, and pure filtering/export. Runtime evidence uses the checked-in Sponza fixture and command-registry window captures under `save/screenshots/validation/` on both backends. Compilation alone is insufficient.

## Rejected and deferred

Reject Editor SQL, `Asset*` traversal, loading for display, mixing provenance with runtime edges, unbounded recursion, and icon-only/hash-only rows. Defer thumbnails, drag/drop, mutations/reimport, watching, package editing, and external launching until each has an ownership/lifetime contract.
