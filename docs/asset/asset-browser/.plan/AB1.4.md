# AB1.4 — Asset Browser Hardening and Dual-Backend Acceptance

- Status: proposed
- Parent design: [Asset Browser Plans](../PLANS.md)
- Roadmap: [Asset Browser TODO](../TODO.md)
- Prerequisites: [AB1.2 browser](AB1.2.md), [AB1.3 reference viewer](AB1.3.md)

## Objective

Close AB1 with integration evidence, measured bounds, failure-state coverage,
and fresh Vulkan/OpenGL captures of both windows against the checked-in Sponza
level. AB1.4 fixes only defects needed to satisfy the frozen AB1 contracts; it
does not add a new asset workflow.

The stage is complete only when focused tests pass, the real archive/live graph
is readable on both backends, resize/close/shutdown are safe, and the evidence
is recorded in one dated implementation journal.

## Scope boundary

AB1.4 owns:

- integration defects in AB1.0–AB1.3;
- measured refresh/projection/render behavior at contract limits;
- archive-unavailable and Partial-catalog visual states;
- keyboard/mouse interaction, resize, menu synchronization, and teardown checks;
- dual-backend Sponza screenshots and a dated journal/status summary.

It does not add thumbnails, automatic refresh, background jobs, graph-canvas
layout, mutation commands, saved layouts, or generalized UI automation
commands. If measurements require architectural work such as asynchronous
capture, stop and create a follow-up stage rather than hiding it in validation.

## Acceptance fixture and evidence names

Use the checked-in `asset/level/sponza.level` through the public launch override:

```powershell
build/Debug/KimPeanutEngine.exe `
  --graphics-api <vulkan|opengl> `
  --startup-level level/sponza.level `
  --agent-port 37373
```

Do not use `save/sponza-stage-invalid` as normal success evidence; it is useful
only for an explicit failure scenario and is not a checked-in source fixture.
Create fresh captures through `capture.screenshot` and `poll` under:

```text
save/screenshots/validation/ab1-4-sponza-vulkan-browser.png
save/screenshots/validation/ab1-4-sponza-vulkan-references-tree.png
save/screenshots/validation/ab1-4-sponza-vulkan-references-text.png
save/screenshots/validation/ab1-4-sponza-opengl-browser.png
save/screenshots/validation/ab1-4-sponza-opengl-references-tree.png
save/screenshots/validation/ab1-4-sponza-opengl-references-text.png
```

Preserve unrelated captures. Evidence must show the application window, not a
cropped reconstruction. Use ordinary Editor input to arrange state, then the
Runtime command transport for capture; do not scrape the console or access a
graphics backend object.

## Required test and review gates

### 1. Contract and model gate

Run all AB1 focused tests plus existing Asset and Editor lifecycle suites.
Review exact snapshot and export determinism using the AB1.0 synthetic fixture.
No flaky ordering or timing assertion is accepted.

### 2. Real Sponza graph gate

On each backend, refresh once and verify:

- `level/sponza.level` is readable and opens a Model dependency;
- Model materials and Material textures appear in authored order where the live
  catalog provides complete coverage;
- archive products and loaded products join rather than render as duplicates;
- runtime-only Mesh children appear as Owned nodes;
- archive-only Texture products remain visible and report Unknown coverage
  instead of fabricated children;
- Dependencies and Referencers change direction without a recapture;
- Tree and Text show consistent root, edge, state, and diagnostic facts.

The journal records actual node/edge counts, snapshot status, diagnostics, and
the root stable key prefix; it does not paste the entire catalog or depend on a
specific content hash in acceptance text.

### 3. Interaction and layout gate

At 1920×1080, 1280×720, and one high-DPI configuration available on the test
host, verify:

- both windows open from View, close from title bar, and reopen with matching
  checkmarks;
- unlocked move/resize persists for the current process and never moves other
  Editor windows;
- narrow widths elide labels but expose full text by tooltip/details;
- Table and Compact Tiles clip large result sets without overlapping the footer;
- keyboard selection stays within visible rows after filters change;
- browser-to-viewer open, reroot, Locate in Browser, direction, mode,
  expand/collapse, and Copy behave as AB1.2/AB1.3 specify;
- missing/unknown/custom types remain understandable without relying on color or
  a special glyph.

### 4. Failure and refresh gate

Use test injection for deterministic null, throwing, invalid, Partial, missing,
and limit-exceeded snapshots. For one runtime smoke, temporarily point the
provider at a known nonexistent archive path through the existing test/config
seam—do not rename or delete the user's archive. Verify that the live graph is
still visible, status is Partial, the archive diagnostic is readable, and a
later successful explicit refresh replaces it while preserving stable
selection/root when possible.

No test may corrupt, rewrite, import into, or delete the real project archive.

### 5. Performance and lifetime gate

Measure Release or RelWithDebInfo behavior after one warm-up, reporting median
and maximum over at least five explicit refreshes:

- provider capture duration and node/edge counts;
- Editor index/projection rebuild duration;
- visible Tree/Text projection duration at normal Sponza size and at the
  4096-row synthetic bound;
- incremental closed-window and unchanged-frame UI cost.

These are regression observations, not invented platform-independent promises.
The hard pass conditions are behavioral:

- closed windows perform no catalog capture or traversal rebuild;
- unchanged open frames perform no catalog capture or adjacency rebuild;
- traversal allocation and emitted rows remain bounded by the frozen limits;
- a busy archive respects AB1.1's 50 ms SQLite wait envelope;
- shutdown after open windows, copied text, and Partial refresh produces no
  callback, use-after-free, retained lock, or backend validation error.

If explicit refresh causes an unacceptable measured Editor stall, record the
numbers and create an asynchronous-refresh follow-up with an ownership/threading
contract. Do not add an unreviewed worker thread during AB1.4.

## Architecture audit

Review includes and link interfaces after implementation:

```text
Editor -> IAssetCatalogSnapshotSource + AssetCatalogSnapshot values
Runtime -> concrete AssetCatalogSnapshotProvider
Asset -> archive + AssetManager capture
Render/Graphics -> unchanged
```

Reject any Editor include of `asset_manager.h`, archive database headers,
provider internals, SQLite, Asset payload classes, or cache internals. Reject
any Asset dependency on Editor, ImGui, Render, or Graphics. Confirm no browser
action calls Load/Unload/Import/Reimport and no copied snapshot retains pointers,
callbacks, handles, or locks.

## Defect policy

AB1.4 may change AB1 files when a failed gate demonstrates a contract defect.
Every correction must be added to a focused regression test and noted in the
journal. Changes to identity, edge semantics, limits, text grammar, ownership,
or public stage scope require updating the owning plan first; they are not
silent polish.

Unrelated visual preferences and feature requests move to Deferred Follow-up.

## Concrete artifacts

| Artifact | AB1.4 action |
| --- | --- |
| AB1 production/test files | Minimal fixes and regression coverage for failed gates only. |
| `save/screenshots/validation/ab1-4-*.png` | Fresh six-image dual-backend evidence set. |
| `.spec/journal/YYYY-MM-DD-asset-browser-ab1.md` | Commands, results, measurements, corrections, captures, risks. |
| `docs/status.md` | One concise AB1 completion summary linking plans, TODO, and journal. |
| `docs/asset/asset-browser/TODO.md` | Mark only acceptance items proved by evidence. |

Do not create the journal or mark TODO items complete before implementation
evidence exists.

## Execution sequence

1. Review the final AB1.0–AB1.3 diff for ownership and public-contract drift.
2. Build and run focused Asset/Editor tests; fix failures with regressions.
3. Run the synthetic failure, bound, refresh, and lifecycle matrix.
4. Measure refresh/projection behavior and record actual values.
5. Run Vulkan Sponza, exercise both windows, and take the three fresh captures.
6. Repeat from a clean process on OpenGL and take the three fresh captures.
7. Exercise resize, close/reopen, Partial refresh recovery, and shutdown.
8. Run the architecture include/link audit and `git diff --check`.
9. Write the dated journal, update TODO/status, and review every claimed checkbox
   against a command, test, observation, or capture.

## Acceptance criteria

- [ ] All focused AB1 catalog, provider, browser, reference, and lifecycle tests pass.
- [ ] The affected Editor/runtime targets build in Debug with no new dependency cycle.
- [ ] Fresh Vulkan and OpenGL Sponza runs show readable browser, Tree, and Text states.
- [ ] The six named command-path screenshots exist and were visually inspected.
- [ ] Search/filter/sort/selection, both View toggles, close synchronization,
  navigation, direction/mode, expansion, copy, resize, and shutdown pass.
- [ ] Archive failure preserves the live graph with a readable Partial diagnostic
  and successful explicit refresh recovery.
- [ ] Closed/unchanged frames do no capture or graph rebuild, and traversal stays bounded.
- [ ] Architecture audit confirms Editor sees only the value/interface contract
  and Asset gains no Editor/Render/Graphics dependency.
- [ ] Actual measurements, skipped checks, environment blockers, corrections,
  remaining risks, and capture paths are recorded in the dated journal.

## Validation commands

```powershell
.\tools\kp.ps1 build AssetCatalogProviderTest
.\tools\kp.ps1 build AssetBrowserModelUnitTest
.\tools\kp.ps1 build AssetReferenceViewModelUnitTest
.\tools\kp.ps1 build EditorUILifecycleTest
.\tools\kp.ps1 test Asset
.\tools\kp.ps1 test Editor
.\tools\kp.ps1 build KimPeanutEngine
git diff --check
```

Because AB1.2 changes public composition/CMake boundaries, run the full Debug
build and CTest if the affected-target builds reveal transitive fallout. Runtime
and screenshot results must be reported separately from compile/test results.
