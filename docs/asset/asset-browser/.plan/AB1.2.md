# AB1.2 — Asset Browser Window and Editor Composition

- Status: proposed
- Parent design: [Asset Browser Plans](../PLANS.md)
- Roadmap: [Asset Browser TODO](../TODO.md)
- Prerequisites: [AB1.0 catalog contract](AB1.0.md), [AB1.1 snapshot provider](AB1.1.md), [AB1.2a imported content boundary](AB1.2a.md)

## Objective

Consume the AB1.2a imported-content snapshot through Runtime composition and add
a readable Asset Browser panel to the Editor's tool row. The browser presents
successfully imported logical content records without displaying raw sources,
hash-named archive products, shaders, or runtime-only objects.

AB1.2 is complete when **View > Asset Browser** and the tab close button control
one visibility state, explicit refresh replaces one immutable snapshot, and
search/filter/sort/selection behavior is covered by headless model tests.

"Movable" is satisfied by the tool row's detach: the browser tab can be isolated into
a standalone window and re-docked, per [ED1](../../../editor/.plan/ED1.md). The browser
does not own window geometry of its own.

## Design question

How can the Editor present a Unity/Unreal-style asset inventory while remaining
a read-only consumer of one Asset-owned value snapshot and preserving the
existing Runtime/Editor teardown boundary?

## Scope boundary

AB1.2 owns:

- Runtime ownership and publication of `AssetCatalogSnapshotProvider`;
- one Editor-owned `AssetBrowserModel` that holds the current snapshot by value;
- reactive View-menu/window visibility state;
- imported-content/path navigation, table and compact-tile presentations;
- readable primitive icons, search, filters, sorting, selection, details, status,
  and explicit refresh;
- a callback seam for AB1.3 to open a selected asset as a reference root.

AB1.2 does not own reference traversal, reverse adjacency, Tree/Text rendering,
clipboard export, thumbnails, drag/drop, import/reimport, filesystem watching,
or any Asset mutation. Until AB1.3 binds the callback, no inert **Open
References** control is rendered.

## Runtime and Editor composition

`RuntimeContext` owns one provider after scene services exist and destroys it
before the Asset runtime can be torn down. Add the narrow accessor:

```cpp
asset::IAssetCatalogSnapshotSource *GetAssetCatalogSnapshotSource() noexcept;
```

The concrete provider remains invisible to Editor headers. Runtime creates it
with `AssetManager::GetInstance()`, `GetAssetDirectory() / ".archive"`, and the
AB1.1 default limits/50 ms SQLite busy timeout. Provider creation does not open
the database or capture a snapshot; both happen only on `CaptureAssetCatalog()`.

`Editor::PromoteEditorWorkspace()` passes the borrowed interface immediately
before `EditorUI::PromoteToWorkspace()`, matching the existing actor-inspection
commit barrier. Add `asset_catalog_source` to `EditorUIInitInfo` and a
`SetAssetCatalogSnapshotSource()` pre-promotion setter for the staged startup
path. Null is supported for lifecycle tests and produces a disabled browser
with the message `Asset catalog unavailable`.

`EditorUI` owns objects in this lifetime order:

```text
RuntimeContext
  owns AssetCatalogSnapshotProvider
      ^ borrowed by
EditorUI
  owns EditorWorkspaceViewState
  owns AssetBrowserModel
  owns menu and AssetBrowserComponent
```

Declare the view state and model before `components_`, so component destruction
precedes borrowed-state destruction. `BeginClosing()` and `Close()` clear the
components, then the model, then the borrowed interface in `init_info_`.
Neither Runtime nor Asset includes an Editor header. The existing RuntimeLib ↔
EditorLib relationship is not widened beyond the narrow Asset contract already
needed by Runtime composition.

## Reactive window and menu state

Already landed by [ED1](../../../editor/.plan/ED1.md); do not re-add. The type is
`editor/ui/component/editor_window_visibility.h` (header-only, ImGui-free), and the
queried `is_selected` binding and the View menu both exist:

```cpp
class EditorWindowVisibility
{
public:
    explicit EditorWindowVisibility(bool open = false) noexcept;
    bool IsOpen() const noexcept;
    void SetOpen(bool open) noexcept;
    void Toggle() noexcept;
};
```

`EditorWindowComponent` optionally borrows this state. At render start it skips
a closed window; when `ImGui::Begin()` changes its local close flag, it writes
the result back through `SetOpen()`. Existing windows without external state
retain their current behavior.

Replace the copied `MenuItem::selected` display flag with an optional queried
binding:

```cpp
std::function<bool()> is_selected;
```

An empty binding renders an unchecked command. The Asset Browser menu item
queries the same `EditorWindowVisibility` and toggles it in `on_click`. All
callbacks are render-thread-only and borrow `EditorUI` state whose lifetime
outlasts the menu component. AB1.2 adds a **View** menu without changing File,
Edit, Tool, or Help command semantics.

The Asset Browser is a **tab in the Editor tool row**, added by
[ED1](../../../editor/.plan/ED1.md), not a floating window with its own geometry. It
registers as a third tool-row entry beside Log and Console, so it inherits the row's
band, its close button, its drag-to-isolate behavior, and its shared
`EditorWindowVisibility`: **View > Asset Browser** and the tab's close X are one state,
and the row's rendering decides visibility, so AB1.2 must not add its own window
geometry or its own open/closed flag.

The browser starts **closed**, so the default workspace shows only the Log tab. That
satisfies this stage's original intent — the current workspace is unchanged on startup —
without occupying pixels no window owns. Opening it selects its tab and does not change
any other panel's geometry.

## Editor model contract

Add `engine/editor/asset/asset_browser_model.h/.cpp`. It contains no ImGui calls
and owns:

```cpp
enum class AssetBrowserPresentation { Table, CompactTiles };
enum class AssetBrowserSortColumn { Name, Type, Availability, Size, Path };
enum class AssetBrowserLocation { All, ArchiveProducts, RuntimeOnly, Missing };

struct AssetBrowserQuery
{
    std::string search;
    AssetBrowserLocation location{AssetBrowserLocation::All};
    std::vector<std::string> included_type_names;
    std::vector<asset::AssetCatalogAvailability> included_availability;
    AssetBrowserSortColumn sort_column{AssetBrowserSortColumn::Name};
    bool ascending{true};
};
```

`AssetBrowserModel` stores the source pointer, snapshot by value, query,
presentation, selected stable key, visible node IDs, derived folder entries,
last successful refresh time, and an Editor-local refresh diagnostic. Public
accessors return values or immutable views only. UI never receives an `Asset*`,
database object, or mutable catalog node.

### Refresh transaction

Refresh is synchronous in AB1.2 and occurs only:

1. once during `BuildAssetBrowserWindow()` after workspace promotion; and
2. when the user presses **Refresh**.

It never runs from `Render()` merely because a frame elapsed. Capture into a
local snapshot, validate it, derive indexes/visible rows in local temporaries,
then atomically replace the model's prior values on the render thread. Preserve
selection when its opaque stable key still exists; otherwise clear selection.

If the source is null, capture throws unexpectedly, or public validation fails,
preserve the last valid snapshot and show an Editor-local error. On first
failure, show the empty unavailable state. Provider `Partial` status is a valid
refresh and replaces the old snapshot while surfacing its diagnostics. The
model never retries automatically.

## Navigation and projection rules

The left navigation is an imported-content projection, not a raw filesystem or
archive-product browser:

```text
Imported Content
  <content-path folders>
Problems
  stale / failed / missing-product records
```

- Imported Content contains only metadata records with a successfully published product closure.
- Problems contains stale, failed, missing-product, and orphaned metadata.
- Folders derive from normalized content-relative metadata paths.
- Raw source files, archive products, runtime-only objects, and internal shaders
  do not become normal rows.
- Product hashes and source paths remain available in details.
- Folder counts are derived from the current filtered snapshot and sort by
  bytewise display path.

Search uses locale-independent ASCII case folding over display name, type name,
aliases, logical path, product path, and provenance source paths. Non-ASCII UTF-8
bytes remain unchanged and are matched bytewise. Multiple search terms use AND
semantics. Type and availability filters use OR within one filter group and AND
between groups. Empty filter lists mean All.

Presentation sorting uses the chosen visible column, then display name, type
name, and stable key as bytewise deterministic tie-breakers. Missing size sorts
before known size ascending and after it descending. Changing query or sort
rebuilds only Editor indexes; it never recaptures.

## Window presentation

`EditorAssetBrowserComponent` derives from `EditorWindowComponent` and renders:

```text
+ Asset Browser --------------------------------------------------------+
| All Assets / Imported     [Search assets...] [Table|Tiles] [Refresh] |
| [All] [Level] [Model] [Material] [Texture] [More...]  [State v]      |
|----------------------------------------------------------------------|
| locations | Name              Type       State        Size           |
|           | [icon] Sponza     Model      Loaded       8.2 MB         |
|           | [icon] bricks     Material   Archive      3 KB           |
|----------------------------------------------------------------------|
| 214 shown / 388 assets | revision 4 | Partial: 1 warning             |
+----------------------------------------------------------------------+
```

Table mode uses an ImGui table with frozen Name column and columns Name, Type,
State, Size, and Path. Compact Tiles use a fixed minimum tile width and always
show icon, elided display name, Type, and State text. Both use an ImGui clipper
and stable keys for widget IDs.

Icons are drawn with `ImDrawList` primitives: a tabbed folder for navigation, a
document outline for assets, and a small type-colored badge. The badge color is
derived deterministically from `type_name`; built-in types may use theme colors
but unknown registered types require no enum switch. Color is never the only
signal: readable Type and State text is always present.

State labels are exactly `Loaded`, `Archive`, `Runtime`, and `Missing`.
`FormatAssetByteSize()` uses binary units and `—` when a size is unavailable.
Hover or the lower details pane exposes the full unelided logical path, product
path, stable display metadata, provenance, dependency coverage, and diagnostics.
Hashes and packed IDs stay in details, never as the primary label.

Single click selects. Keyboard Up/Down moves within the visible projection.
Double-click and a context-menu **Open References** entry invoke an injected
`std::function<void(std::string_view stable_key)>` only when AB1.3 supplies it;
the callback receives the stable key and cannot load the asset.

## Concrete file changes

| File | AB1.2 change |
| --- | --- |
| `engine/runtime/runtime_global_context.h/.cpp` | Own/publish the concrete catalog provider with teardown ordering. |
| `engine/runtime/CMakeLists.txt` | Add the private AssetRuntime dependency required by provider composition if not already explicit. |
| `engine/editor/editor.cpp` | Pass the source at the workspace commit barrier. |
| `engine/editor/ui/editor_ui.h/.cpp` | Own view/model state and register the browser panel with the tool row; add its item to the View menu ED1 created. |
| `engine/editor/ui/component/editor_tool_row_component.h/.cpp` | No change expected: `AddPanel` already takes a panel, title, and initial visibility. |
| `engine/editor/ui/component/editor_window_component.h/.cpp` | No change: ED1 already added the visibility binding and the non-latching close. |
| `engine/editor/ui/component/editor_menubar_component.h/.cpp` | Query live menu check state. |
| `engine/editor/asset/asset_browser_model.h/.cpp` | Add pure snapshot/query/selection projection. |
| `engine/editor/asset/editor_asset_browser_component.h/.cpp` | Add the ImGui browser panel and primitive icons. |
| `engine/editor/CMakeLists.txt`, `engine/editor/ui/CMakeLists.txt` | Compile the model/component without backend-specific Asset code. |
| `engine/test/unit/editor/asset_browser_model_test.cpp` | Cover pure browser behavior with the AB1.0 fixture. |
| `engine/test/unit/editor/editor_ui_lifecycle_test.cpp` | Cover null source, promotion, and teardown. |
| `engine/test/unit/editor/CMakeLists.txt` | Add the focused model test target. |

## Implementation sequence

0. Complete [AB1.2a](AB1.2a.md), including metadata publication, ContentRegistry
   validation, and the import/startup isolation tests.
1. Publish the content provider from RuntimeContext and pass only its interface into
   Editor startup.
2. Add queried menu selection and external window visibility with regression
   tests for existing command/window behavior.
3. Implement `AssetBrowserModel` refresh replacement, stable-key selection, and
   pure projections.
4. Add category/folder navigation, search, filters, sorting, and formatting.
5. Add the table/tile component, primitive icons, details pane, and status row.
6. Wire **View > Asset Browser**, initial explicit capture, and the unbound
   reference-open callback seam.
7. Review destruction order and verify no per-frame capture or Asset mutation.

## Focused test matrix

- Null source yields one stable unavailable state and no crash.
- Initial and explicit refresh call the fake source exactly once each; ordinary
  frames and query changes call it zero times.
- A valid Partial snapshot replaces the prior snapshot and exposes diagnostics.
- Invalid/throwing refresh preserves the last valid snapshot and selection.
- Selection follows stable key across reordered dense node IDs and clears only
  when the key disappears.
- Search covers names, aliases, paths, provenance, and type with the specified
  ASCII behavior and AND terms.
- Location, type, and state filters compose exactly as specified.
- Every sort direction is deterministic under equal display fields.
- Unknown custom types receive readable text and a deterministic fallback badge.
- Menu activation, title close, and reopen observe one visibility state.
- Browser construction and teardown do not load, unload, import, query SQLite
  from Editor, or retain any Asset lock.

## Acceptance criteria

- [ ] Runtime owns the concrete provider; Editor receives only a borrowed
  `IAssetCatalogSnapshotSource*`.
- [ ] The model holds one validated snapshot by value and refreshes only at
  promotion or explicit user request.
- [ ] Archive, loaded, runtime-only, missing, and custom-type nodes are readable
  in both Table and Compact Tiles modes.
- [ ] Search, navigation, filters, sorting, and stable-key selection match the
  deterministic rules above.
- [ ] **View > Asset Browser** and the tab close button share live state, both routed
  through the tool row's `EditorWindowVisibility`.
- [ ] The browser performs no asset load, mutation, filesystem enumeration, or
  direct database access.
- [ ] The AB1.3 open-root callback exists but no dead reference-viewer UI ships.
- [ ] Focused Asset/Editor tests and Editor lifecycle tests pass.

## Validation commands

```powershell
.\tools\kp.ps1 build AssetCatalogProviderTest
.\tools\kp.ps1 build AssetBrowserModelUnitTest
.\tools\kp.ps1 build EditorUILifecycleTest
.\tools\kp.ps1 test Asset
.\tools\kp.ps1 test Editor
git diff --check
```

Run one Editor startup smoke on the active backend to verify the View binding and
window render. Dual-backend screenshots and full interaction evidence are the
AB1.4 gate.
