# Render System R1.4 Journal — 2026-09-02

## Objective

Move Asset loading and Resource CPU preparation out of Render. Runtime now
publishes one immutable, typed, lifetime-pinning catalog before Render
initialization; Render resolves only catalog payloads and creates GPU state.

## Reference gate

Reviewed the R1.4 plan's recorded source studies before implementation:

- O3DE AssetManager (commit `1242766`) — readiness is explicit and lookup does
  not itself imply loading. Applied as producer-readiness guidance; rejected a
  broad event-bus/catalog import.
- Godot (commit `6220ead`) — rendering consumes materialized resource data and
  creates GPU objects at the rendering boundary. Applied to the CPU-data/GPU
  ownership split; rejected its general Resource/RID hierarchy.
- bgfx (commit `780f7bf`) — GPU creation is driven by descriptions and opaque
  handles. Applied to the Render/Graphics handoff; rejected internal submission
  threading as outside R1.4.

The local plan records the source links and the limitation that GitHub MCP was
unavailable during the gate.

## Landed changes

- Added `PreparedRenderAssetCatalog` with seven closed built-in roles, typed
  payload records, copied dependency IDs, prepared environment IBL data, and
  transactional validation.
- Added Runtime-owned `RenderAssetPreparer`. It loads the selected level's
  dependency closure and built-ins, processes all reachable shaders for the
  selected API, prepares authored environment IBL, and publishes only on total
  success.
- Injected the catalog into `RenderSystem`, `MaterialAssetResolver`,
  `RenderResourceResolver`, and `DeferredRenderer`.
- Removed Render-side `LoadSync`, `ProcessShader`, `ProcessEnvironmentIbl`,
  global AssetManager lookup, lazy pass shader loading, request drain/cache,
  and the obsolete `AssetLoadRequest` header.
- Updated test fixtures, added catalog contract tests, migrated the standalone
  graphics example, and marked the old async queue proposal superseded.
- Closed review findings F1–F5: selected-API shader/program validation,
  const-safe catalog lookup, ordinal built-in role checking, an injectable
  preparation seam with Vulkan/OpenGL success and transactional failure tests,
  and corrected durable Asset/Render ownership records.

## Validation

- `cmake --build build --config Debug --target RenderSystemTest -j 1` — passed.
- `cmake --build build --config Debug --target RuntimeStartupTest -j 1` — passed.
- `cmake --build build --config Debug --target RenderPassScheduleTest -j 1` — passed.
- `cmake --build build --config Debug --target GraphicsExample -j 1` — passed.
- `cmake --build build --config Debug -j 1` — passed.
- Focused RenderSystem, fixed-pass, DeferredRenderer, material, catalog, and
  preparer tests — passed (78/78 RenderPassSchedule, 12/12 RenderSystem,
  4/4 RenderAssetPreparer).
- Full Debug build and CTest — passed, 253/253 tests.
- Static audit found no Render references to `LoadSync`, path helpers,
  `ProcessShader`, `ProcessEnvironmentIbl`, AssetManager lookup,
  `AssetLoadRequest`, request cache, or request-ID polling.
- Direct Vulkan/OpenGL `GraphicsSmoke` execution launched both APIs, completed
  the capture paths, and wrote fresh D5 images. The final automated silhouette
  comparator rejected a small cross-backend edge difference; visual inspection
  shows the same rock-and-floor composition on both captures. The `kp.ps1
  smoke` wrapper also has an unrelated empty-argument binding defect.
- The built `KimPeanutEngine` was launched with the checked-in
  `level/pbr_showcase.level` fixture and Vulkan. Runtime loaded the level,
  published a catalog containing 9 prepared shaders, initialized Render, and
  entered the normal engine loop successfully.

## Remaining evidence and risk

The GraphicsSmoke process did not reach exit code zero because its strict
cross-backend silhouette comparator rejected the fresh captures. The required
Runtime command-transport captures for PBR, point-shadow, and spot-shadow were
rerun on both APIs and visually inspected; all six exported successfully.
General streaming, hot reload, cancellation, and eviction remain deferred.
