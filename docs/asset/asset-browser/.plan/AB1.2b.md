# AB1.2b — Asset Browser Visual Tile Presentation

- Status: implemented
- Parent design: [Asset Browser Plans](../PLANS.md)
- Roadmap: [Asset Browser TODO](../TODO.md)
- Prerequisite: [AB1.2 — Asset Browser Window and Editor Composition](AB1.2.md)
- Content boundary: [AB1.2a — Imported Content Metadata and Catalog Boundary](AB1.2a.md)

## Objective

Make the Asset Browser a recognizable visual Editor tool rather than a text
list. The default presentation is an icon-first tile grid showing imported
logical assets with readable names. Table mode and the details pane remain
available for type, state, size, path, and diagnostics; the tile surface stays
compact as requested by the editor workflow.

AB1.2b completes the first usable visual browser milestone. It does not add
thumbnails, asset mutation, drag/drop into other tools, or reference traversal.

## Ownership and boundary

`engine/editor/asset/` owns tile geometry, icon drawing, layout, selection
feedback, elision, and presentation state. Asset and Runtime provide only the
immutable value snapshot. No ImGui type, GPU texture, thumbnail cache, or
filesystem path enumeration crosses into Asset.

Icon selection is an Editor presentation decision. Built-in type names map to
simple primitive icons; unknown registered types use a deterministic document
fallback. The Asset contract does not grow an enum or an ImGui-facing icon
handle for this stage.

## Visual contract

Each tile contains, in order:

1. a centered 40–56 px primitive icon or type badge;
2. a readable, clipped display name.

Type, state, size, and path are intentionally kept out of the tile surface and
remain available in Table mode and the selected-details pane.

The tile is selectable as one stable-key item. Selected state is visible from
the tile background/border and is never conveyed by color alone. Folder items
use a folder glyph and remain navigation controls, not asset rows.

The default view opens in Compact Tiles. Table mode is an explicit alternate
presentation and uses the same model rows, query, selection, and details.

## Navigation and performance

- The folder column is available whenever the current projection has folders,
  including the normal imported-content view; it is not restricted to an
  archive-only filter.
- Tile width has a fixed minimum and the number of columns derives from the
  available panel width, with a minimum of one column.
- Tile rows use `ImGuiListClipper`; rendering cost is proportional to visible
  rows, not the total catalog size.
- Widget IDs use the row stable key, never the dense vector index alone.
- Changing presentation, selection, or query never recaptures the Asset
  snapshot. Refresh remains explicit.

## Icon mapping

| Type | Editor icon | Fallback behavior |
| --- | --- | --- |
| folder | tabbed folder | not an asset row |
| model | cube/mesh outline | document glyph |
| material | shaded sphere | document glyph |
| texture | checkerboard | document glyph |
| level | framed scene | document glyph |
| custom type | document glyph with deterministic badge | readable type text |

Shaders and archive products remain hidden by the AB1.2a content projection;
AB1.2b must not make an internal product visible merely because it has an icon.

## Concrete changes

- Update `EditorAssetBrowserComponent::RenderTiles()` to render a clipped,
  icon-first grid with stable IDs and readable labels.
- Keep icon helpers local to the Editor component and backend-neutral at the
  contract level; use ImDrawList primitives only in the ImGui translation.
- Make Compact Tiles the initial presentation while retaining the Table toggle.
- Show derived content folders for the normal imported-content projection.
- Add a model regression test for the stable Compact Tiles default and ensure
  switching presentation does not recapture or rebuild the catalog.
- Add Editor startup visual evidence after the tile grid is wired to the test
  level; Sponza is not required for this stage.

## Acceptance criteria

- [x] Opening Asset Browser shows icon-first tiles by default.
- [x] Every visible asset tile has an icon and readable name; type/state/size
  remain available in Table mode and details.
- [x] Model/material/texture/level and unknown custom types have deterministic
  primitive fallback icons without Asset or Runtime changes.
- [x] Folder navigation is visible for the imported-content projection.
- [x] Tile rows are clipped and selection follows the existing stable-key model.
- [x] Table mode, search, filters, refresh, menu visibility, and docking remain
  functional.
- [x] No thumbnails, asset loading, importing, mutation, or direct SQLite/Asset
  access is introduced.

## Validation

```powershell
cmake --build build --config Debug --target KimPeanutEngine AssetBrowserModelUnitTest
ctest --test-dir build -C Debug -R "^(AssetBrowserModelTest|AssetBrowserModelUnitTest)\."
```

Run the Editor with the checked-in test level, open Asset Browser, and capture
the tile grid under `save/screenshots/validation/`. Sponza validation remains
the AB1.4 gate.
