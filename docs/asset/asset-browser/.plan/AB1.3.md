# AB1.3 — Asset Reference Viewer

- Status: implemented; AB1.4 is next
- Parent design: [Asset Browser Plans](../PLANS.md)
- Roadmap: [Asset Browser TODO](../TODO.md)
- Prerequisites: [AB1.0 catalog contract](AB1.0.md), [AB1.2 browser integration](AB1.2.md)

## Objective

Add a second Editor window that explains what an asset needs and what points to
it. One selected catalog node is rendered in either an expandable Tree or a
deterministic Text view, with explicit handling for shared nodes, cycles,
missing references, unknown coverage, and traversal limits.

AB1.3 is complete when selecting an imported level or model in the browser can
open a readable Level → Model → Material → Texture closure without loading
anything, and the same copied graph can be inverted to answer Referencers.

## Implemented shape

The reference viewer is an Editor-owned, standalone window. Its model copies
only node labels, availability, stable keys, and edge identity from the browser's
immutable snapshot. It builds deterministic forward/reverse indexes, flattens a
bounded depth-first tree, and generates the Text export from the same facts.

The UI uses a horizontal connected-card tree: the root occupies the left
column and descendants occupy depth columns to the right. Parent-centered
vertical placement and elbow connectors expose the hierarchy while each
occurrence retains a readable type/name, state dot, relation/annotation line,
and expand affordance. It supports
Dependencies/Referencers, Tree/Text, Expand All, Collapse All, Copy, root details,
double-click rerooting, and Locate in Browser. The viewer is closed by default,
has a reactive close state, and is also addressable as the debug panel id
`asset_reference_viewer`.

## Scope boundary

AB1.3 owns Editor-side adjacency indexes, root/direction/mode state, bounded
traversal, Tree/Text presentation, text copy, reference-row selection/details,
and browser-to-viewer navigation. It does not recapture Asset state, query the
archive, infer unavailable archive dependencies, mutate assets, or add graph
canvas layout. A free-form node-link canvas is deferred; Tree mode is the first
node presentation because it preserves authored order and remains legible.

## Model additions

Extend the Editor asset model with pure reference-view state:

```cpp
enum class AssetReferenceDirection { Dependencies, Referencers };
enum class AssetReferencePresentation { Tree, Text };

struct AssetReferenceTraversalLimits
{
    std::uint32_t max_depth{32};
    std::uint32_t max_rows{4096};
};

enum class AssetReferenceRowKind
{
    Root,
    Edge,
    SharedLeaf,
    CycleLeaf,
    MissingLeaf,
    UnknownCoverageLeaf,
    Truncation,
};
```

`AssetReferenceViewModel` borrows `AssetBrowserModel` and owns only opaque root
and selection stable keys, direction, presentation, expansion keys, limits,
flattened rows, and exported text. It never stores pointers into a snapshot.
When the browser snapshot revision changes, it rebuilds indexes once before the
next reference projection; unchanged frames perform no graph rebuild.

Build these immutable Editor indexes from the one canonical edge table:

- forward adjacency groups by `from`, then relation numeric value, ordinal, and
  target stable key;
- reverse adjacency groups by `to`, then source stable key, relation numeric
  value, and ordinal;
- stable-key lookup resolves roots after dense node IDs change.

Referencers are strictly the reverse projection of forward edges. AB1.3 does not
read `Asset::ref_assets`, create provenance edges, or invent edges for nodes
whose `dependency_coverage` is Unknown.

## Root and refresh behavior

`SetRoot(stable_key)` validates only that the key is non-empty, stores it, opens
the Reference Viewer, selects its root, and rebuilds the current projection.
The Asset Browser double-click/context command is bound to this method in AB1.3.

If refresh preserves the key, the viewer follows the corresponding new node ID.
If the key disappears, retain the requested key and show `Root is not present in
catalog revision N`; do not silently choose another node. This lets a later
refresh restore the same root. A source-unavailable initial state is distinct
from a missing former root.

Changing direction, presentation, or expansion state is an Editor-only
operation. It never calls `CaptureAssetCatalog()`.

## Bounded traversal algorithm

Generate a flat `std::vector<AssetReferenceRow>` before ImGui rendering. Each
row contains occurrence key, node stable key when present, depth, incoming
relation/label/ordinal, kind, has-children, expanded state, and annotation.

Depth-first projection uses these rules:

1. Emit the root at depth 0.
2. Visit adjacency in the frozen direction-specific order.
3. If the next stable key already exists in the current ancestor stack, emit a
   non-expandable `CycleLeaf` annotated `cycle to depth <n>`.
4. Otherwise, if it was expanded earlier in this traversal, emit a
   non-expandable `SharedLeaf` annotated `shared; first shown at row <n>`.
5. MissingReference nodes emit `MissingLeaf` and never expand.
6. A visible node with Unknown dependency coverage emits its known edges, then
   an `UnknownCoverageLeaf` reading `additional archive dependencies unknown`.
   This applies only in Dependencies direction.
7. Stop before exceeding depth 32 or 4096 emitted rows and emit exactly one
   `Truncation` row at the affected parent.

The root counts toward the row limit. A depth of 32 permits rows at depths
0–32; attempted children become the truncation row. Cycle detection uses the
ancestor stack, while shared detection uses a traversal-global first-row map.
Occurrence keys derive from parent occurrence key plus edge direction,
relation, ordinal, and target stable key, so repeated edges have distinct ImGui
IDs without becoming distinct asset nodes.

Collapsed Tree branches are not traversed for visible rows. **Expand All** runs
the same bounded walk and records only occurrence keys reached before the
limit. **Collapse All** retains the root and clears descendant expansion state.
Changing root clears expansion state; changing direction keeps separate
expansion sets for each direction.

## Deterministic text format

Text mode uses the same bounded walk with all branches logically expanded; it
does not depend on current Tree expansion. UTF-8 output uses `\n` line endings
and ends with one newline. The normative shape is:

```text
Dependencies: Level level/sponza.level [Loaded]
  dependency -> Model Sponza [Loaded] (model/sponza)
    dependency -> Material bricks [Loaded]
      dependency -> Texture bricks_albedo [Archive; coverage unknown]
      dependency -> Texture shared_mask [Archive] {shared; first row 4}
    dependency -> Level level/sponza.level [Loaded] {cycle to depth 0}
  ... {truncated: row limit 4096}
```

Use lowercase relation tokens `dependency` and `owned-child`. Prefer logical
path, then product path, and omit the parenthesized path when it equals the
display name. Availability labels match AB1.2. An unresolved node uses
`<missing>` plus its expected Type. Escape backslash, newline, carriage return,
tab, `{`, and `}` in labels so one edge remains one line. Copy writes exactly
the model-produced text through ImGui clipboard support.

Text export is byte-identical for the same canonical snapshot, root, direction,
and limits, regardless of prior Tree expansion or selection.

## Window presentation

Add **View > Asset Reference Viewer** using the same reactive visibility
contract as AB1.2. It starts closed and uses unlocked first-use geometry
`x=0.24, y=0.14, width=0.56, height=0.68`.

```text
+ Asset Reference Viewer ----------------------------------------------+
| Root: Level  level/sponza.level             [Locate in Browser]      |
| [Dependencies|Referencers] [Tree|Text] [Expand All] [Collapse] [Copy]|
|----------------------------------------------------------------------|
| v Level  sponza.level      ─┬─ v Model Sponza                       |
|                             ├─ v Material bricks                    |
|                             └─ ↳ Texture shared_mask                 |
|----------------------------------------------------------------------|
| product path / provenance / relation / diagnostic details            |
+----------------------------------------------------------------------+
```

Tree rows use the AB1.2 primitive type icon and readable name/type/state text.
Cycle (`↻`), shared (`↳`), missing (`!`), unknown (`?`), and truncation (`…`)
also have text labels and tooltips; glyph availability is not required for
meaning. Relation labels appear when non-empty. Owned children are visually
distinguished from dependencies by the `Owned` text badge.

Single click selects an occurrence and fills the details footer. Double-click
on a real node makes that node the new root. **Locate in Browser** selects the
same stable key and changes the browser location/query only enough to reveal
it; it never clears the user's search silently—if the current query hides the
node, the UI offers `Clear filters to reveal`.

Text mode is a read-only multiline region with horizontal scrolling and a
visible copied/not-copied status. Expand/Collapse are disabled in Text mode;
Copy remains enabled whenever a root resolves.

## Concrete file changes

| File | AB1.3 change |
| --- | --- |
| `engine/editor/asset/asset_reference_view_model.h/.cpp` | Add adjacency, bounded traversal, expansion, and deterministic export. |
| `engine/editor/asset/editor_asset_reference_component.h/.cpp` | Add Tree/Text window, controls, details, and clipboard action. |
| `engine/editor/asset/asset_browser_model.h/.cpp` | Expose immutable snapshot/revision lookup and reveal-selection operation. |
| `engine/editor/asset/editor_asset_browser_component.*` | Bind double-click/context action to the viewer root request. |
| `engine/editor/ui/editor_ui.h/.cpp` | Own reference model/visibility and add the second View item/window. |
| `engine/editor/CMakeLists.txt`, `engine/editor/ui/CMakeLists.txt` | Compile reference model and component. |
| `engine/test/unit/editor/asset_reference_view_model_test.cpp` | Cover projection and exact text output with AB1.0 fixture. |
| `engine/test/unit/editor/CMakeLists.txt` | Add focused reference-model test target. |

No Runtime, Asset provider, archive schema, Render, Graphics, or asset payload
file changes belong to AB1.3.

## Implementation sequence

1. Build forward/reverse indexes and stable-key lookup from a copied snapshot.
2. Implement the bounded flat-row traversal with occurrence identity.
3. Freeze and test exact Text export independently of ImGui.
4. Add root, direction, mode, expansion, selection, and refresh reconciliation.
5. Build the reference window with Tree/Text controls and details.
6. Bind browser open-root and viewer locate-in-browser interactions.
7. Add the reactive View item and audit that all actions remain Editor-only.

## Focused test matrix

- Sponza-shaped Dependencies preserve Level → Model → ordered Material →
  Texture edges and distinguish OwnedChild.
- Referencers are an exact inversion and use deterministic source ordering.
- A shared target expands once and later occurrences become SharedLeaf rows.
- Ancestor recurrence becomes a CycleLeaf at the first recursive edge.
- Missing targets remain readable, selected, and non-expandable.
- Unknown coverage adds one honest leaf without inventing dependencies.
- Exact depth and row boundaries emit one deterministic truncation row.
- Repeated edges have unique occurrence IDs while sharing one node key.
- Text output is identical across runs and independent of Tree expansion.
- Refresh preserves a stable root, reports a disappeared root, and restores it
  if a later snapshot contains the key again.
- Direction, mode, expand/collapse, copy, browser reveal, and reroot operations
  call the snapshot source zero times and perform no Asset load or mutation.

## Acceptance criteria

- [x] Both Dependencies and Referencers derive from the AB1.0 forward edge table.
- [x] Tree and Text modes show the same facts with readable labels and state.
- [x] Shared, cycle, missing, unknown-coverage, runtime-only, archive-only, and
  truncation cases are explicit.
- [x] Traversal cannot exceed depth 32 or 4096 emitted rows.
- [x] Text export is deterministic and copied verbatim.
- [x] Browser double-click opens the selected stable key as the viewer root
  without loading it.
- [x] View-menu and title-close state stay synchronized.
- [x] Focused reference-model and existing Editor tests pass.

## Validation commands

```powershell
.\tools\kp.ps1 build AssetReferenceViewModelUnitTest
.\tools\kp.ps1 build AssetBrowserModelUnitTest
.\tools\kp.ps1 build EditorUILifecycleTest
.\tools\kp.ps1 test Editor
git diff --check
```

Run an active-backend Editor smoke with the test-level fixture to confirm the
viewer window opens through the Runtime command path. AB1.4 owns the checked-in
Sponza closure and dual-backend evidence gate.
