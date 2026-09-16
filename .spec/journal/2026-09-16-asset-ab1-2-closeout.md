# AB1.2 Closeout — Asset Browser

## Objective

Close the read-only Asset Browser milestone after AB1.2a imported-content
metadata preparation and AB1.2b icon-first presentation.

## Completed

- Runtime owns `AssetCatalogSnapshotProvider`; Editor consumes only the narrow
  borrowed snapshot-source interface.
- `AssetBrowserModel` stores validated snapshots by value and performs refreshes
  only during initial promotion or explicit **Refresh**.
- The browser presents imported logical content and filters raw sources, archive
  products, runtime-only objects, and internal shaders from normal rows.
- Search, folder/category navigation, type and availability filters, deterministic
  sorting, stable-key selection preservation, and explicit refresh are implemented.
- Compact Tiles show only an asset icon and readable name. Type, state, size, paths,
  hashes, provenance, dependency coverage, and diagnostics remain in Table mode or
  the details surface.
- **View > Asset Browser**, the tab close button, tool-row detach, and panel
  commands use the shared Editor visibility state.
- The AB1.3 stable-key open-reference callback seam exists without shipping an
  unbound reference-viewer control.

## Validation evidence

- The focused Asset Browser/catalog tests passed: Content Metadata 2/2,
  Content Catalog Builder 2/2, Asset Catalog Provider 10/10, Asset Browser
  Model 21/21, and Asset Browser Size Format 1/1 (36/36 discovered cases).
- The focused Editor tests passed: Editor Tool Row Model 17/17, Editor Panel
  Command Provider 4/4, and Editor Layout Model 12/12.
- `EditorUILifecycleTest` was built with a clean MSBuild environment and its
  executable passed 4/4 tests. The current generated CTest registry does not
  discover that target, so it was invoked directly.
- A Debug Editor startup smoke used the test fixture
  `level/performance_profile.level` on Vulkan. Runtime panel commands opened and
  listed Asset Browser, and the captured validation frame showed the browser's
  icon tile presentation. Sponza was not used for this milestone validation.
- `git diff --check` passed after the documentation closeout. A normal MSBuild
  invocation still hits the known inherited Windows `PATH`/`Path` duplicate;
  the clean-environment target build passed.

## Design decisions carried forward

The content namespace is the user-facing logical namespace; source files and
generated archive products are implementation data. Metadata is the stable
Editor-facing record, while product hashes remain identity/details data rather
than labels. The browser is an inventory and selection surface, not an asset
loader or mutator. Reference traversal and opening are deferred to AB1.3.

## Remaining work

AB1.3 implements the bounded dependency/referencer viewer and binds the existing
stable-key callback. Thumbnail generation, drag/drop, mutation commands,
filesystem watching, and package/streaming views remain later follow-up work.
