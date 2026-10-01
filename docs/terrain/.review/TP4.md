# TP4 terrain code review

**Review date:** 2026-10-01. **Baseline:** `36e9d19ff0de0b85529aae0e7202b1fd8cd1b73d` (`feat(terrain): add CPU hydraulic erosion baseline`). **Status:** P1 findings R1-R3 and P2/P3 findings R4-R9 addressed in the working tree.

## Scope and method

Static review of the current TerrainCore, TerrainGeneration, hydraulic operator, viewer host, and bake path. This spans TP1–TP4 because TP4's solver shares evaluation, preview, and asset-lifetime paths with earlier stages. I inspected the current source and stage contracts; I did not run a new build, numerical test, renderer capture, or performance profile. Costs below are complexity or allocation estimates, not measured timings. The checkout also contains unrelated working-tree edits, which this review leaves untouched.

The review question is whether current code preserves numerical and memory contracts as TP4 grows, and where to improve throughput or isolate responsibilities without changing the heightfield-first architecture. The highest-priority acceptance evidence is a nonzero-datum open-boundary fixture, an evaluator memory-budget test with cache hits, and a failed preview-catalog promotion test.

## Findings

### TP4-R1 · P1 · Open-boundary hydraulics depends on the world datum

[`terrain_hydraulic_erosion.cpp:262`](../../../engine/module/terrain/evaluation/terrain_hydraulic_erosion.cpp#L262) uses `domain.datum_y_m` as the outside water head. The solver's `surface` is bedrock elevation plus material thickness, while mesh conversion adds `datum_y_m` only when placing vertices in world space ([`terrain_core.cpp:345`](../../../engine/module/terrain/product/terrain_core.cpp#L345)). Thus translating the same terrain vertically changes its open-boundary flux. This is a correctness defect for nonzero datum, and can invert or suppress export. Define the outside head in the same local elevation frame as `surface` (or convert both heads to world space), then compare otherwise identical open-boundary fixtures with different datums. Closed boundaries do not exercise this branch.

### TP4-R2 · P1 · Cache hits bypass the result-memory limit

[`terrain_generation.cpp:1367-1380`](../../../engine/module/terrain/evaluation/terrain_generation.cpp#L1367) inserts cached node outputs into `result.nodes` and continues before the `retained_result_bytes` check at [line 1429](../../../engine/module/terrain/evaluation/terrain_generation.cpp#L1429). A result assembled from cache hits can therefore exceed `maximum_result_bytes`, and a mix of hits and misses counts only misses. Shared ownership may prevent duplicate physical allocations within one evaluation, but the configured result bound and downstream snapshot retention no longer reflect what the result holds. Count retained unique value objects on both paths, or define and enforce a separate aggregate result-size contract. Test a cached multi-node graph with a result limit below the sum of its distinct outputs.

### TP4-R3 · P1 · Preview promotion has no terminal failure path

The viewer waits for `GetAppliedPreparedAssetsUpdate() >= pending_catalog_serial_` at [`terrain_viewer_host.cpp:828`](../../../engine/module/terrain/tools/terrain_viewer_host.cpp#L828); regeneration is refused while `pending_preview_` exists at [line 1072](../../../engine/module/terrain/tools/terrain_viewer_host.cpp#L1072). Queuing reports initial preparation failure, but this polling path has no explicit rejected/failed promotion outcome. If a queued catalog cannot be applied, the viewer can remain in “Waiting for render boundary” with the old preview visible and authoring blocked. This is already an open TP3 acceptance item. Expose a serial's applied/failed status through the Runtime/Render contract; on failure, unregister pending assets, retain the committed preview, and allow retry. Inject the failure at promotion, not only at initial preparation.

### TP4-R4 · P2 · Hydraulic iterations rebuild and validate full fields

Every substep allocates `surface_samples`, constructs a validated `ScalarField2D`, computes another slope vector, and converts that vector to `double` ([`terrain_hydraulic_erosion.cpp:230-238`](../../../engine/module/terrain/evaluation/terrain_hydraulic_erosion.cpp#L230)). The cost is O(samples × substeps) with repeated allocation, full-field validation, and trigonometry. `maximum_scratch_bytes` checks only `count * 256` at [line 154](../../../engine/module/terrain/evaluation/terrain_hydraulic_erosion.cpp#L154), not the complete peak including these transient fields, published outputs, evaluator retention, and cache. Compute slopes directly from the solver's numeric surface buffer with reusable scratch, keeping the immutable-field validation at the published boundary. Establish peak-memory and solve-time baselines at 128², 256², and one larger grid before choosing SIMD or GPU work.

### TP4-R5 · P2 · Mesh projection scales with samples times triangles

[`ProjectMeshToHeightfield`](../../../engine/module/terrain/product/terrain_core.cpp#L229) scans every source triangle for every grid sample ([lines 248-254](../../../engine/module/terrain/product/terrain_core.cpp#L248)). This becomes impractical for detailed imported geometry, which is important for the planned TP8 geometry input. Pre-bin triangles into overlapped XZ grid cells or use a spatial index, then preserve the current “highest vertical hit” semantics and no-hit policy. Benchmark a dense mesh over 256² and 1024² grids; keep the current method as a small-fixture reference.

### TP4-R6 · P2 · Preview rebuilds a closed mesh after publication

`PublishPreview` builds and registers a closed heightfield mesh at [`terrain_viewer_host.cpp:1343`](../../../engine/module/terrain/tools/terrain_viewer_host.cpp#L1343). After the catalog update, `Tick` rebuilds the same mesh solely to derive actor bounds at [line 863](../../../engine/module/terrain/tools/terrain_viewer_host.cpp#L863), even though `PublishPreview` already computed bounds and stored them on the mesh resource. Carry those bounds in `PreviewAssets` (or retrieve the registered resource) to avoid another full mesh allocation and normal/index generation. Separately, [`BuildHeightfieldMesh`](../../../engine/module/terrain/product/terrain_core.cpp#L287) always emits a bottom surface and sides; a top-only preview option could reduce preview CPU, upload, and draw cost while retaining closed geometry for native bake. Measure section counts and memory before changing the bake contract.

### TP4-R7 · P2 · Per-domain diagnostic fields inflate hydraulic results

The solver materializes sixteen float output arrays at [`terrain_hydraulic_erosion.cpp:402`](../../../engine/module/terrain/evaluation/terrain_hydraulic_erosion.cpp#L402), duplicates five channels into the layered state at [line 463](../../../engine/module/terrain/evaluation/terrain_hydraulic_erosion.cpp#L463), and creates six spatially constant full-grid fields for substep/time and aggregate budgets at [lines 454-492](../../../engine/module/terrain/evaluation/terrain_hydraulic_erosion.cpp#L454). At 1024², each float grid is about 4 MiB; the six constants alone occupy about 24 MiB before cache/snapshot retention. Add scalar diagnostic metadata or a typed scalar value in a versioned operator contract; preserve spatial maps only for quantities that vary by cell. Do not silently change version-1 recipe ports.

### TP4-R8 · P2 · Submission rejects a replaceable pending job

[`GenerationExecutor::Submit`](../../../engine/module/terrain/evaluation/terrain_generation.cpp#L1487) checks `pending_.size() >= pending_capacity_` before it cancels and clears older pending jobs at [line 1497](../../../engine/module/terrain/evaluation/terrain_generation.cpp#L1497). When the queue is full of stale work, a newer revision is rejected despite the intended replacement behavior. Move the capacity decision after stale-job removal, while preserving monotonic revision checks. Exercise a paused worker and a full pending queue with a newer revision. The current viewer often guards repeated requests, but this executor is a reusable module contract.

### TP4-R9 · P3 · Viewer host concentrates unrelated lifecycle decisions

[`terrain_viewer_host.cpp`](../../../engine/module/terrain/tools/terrain_viewer_host.cpp) mixes recipe edits and execution controls, progress snapshots, bake futures, AssetManager registration, Runtime catalog replacement, Gameplay actor teardown/recreation, camera/sky setup, and command completion. The most fragile seam is the preview transaction across [`PublishPreview`](../../../engine/module/terrain/tools/terrain_viewer_host.cpp#L1335) and [`Tick`](../../../engine/module/terrain/tools/terrain_viewer_host.cpp#L828): ownership of pending/committed assets and failure rollback is spread across both. After R3 is fixed, extract a small preview-publication state object with explicit pending, applied, failed, and retired transitions. Keep generation headless; keep the Gameplay/Render adapter above TerrainGeneration. A full host rewrite is not a prerequisite.

## Recommended order and evidence

1. Fix R1 and R2 with focused headless regression tests. These directly affect correctness and boundedness.
2. Close R3 with a Runtime/Render promotion result and injected failure; verify old actor/catalog retention and asset release on Vulkan and OpenGL.
3. Profile R4, R5, R6, and R7 under matched RelWithDebInfo conditions before selecting optimization work. Record domain size, node graph, substeps, mesh triangle count, peak CPU memory, generation time, mesh-build time, and bake size.
4. Fix R8 as a narrow executor contract change. Extract the R9 preview transaction only once its success/failure lifecycle is explicit.

The heightfield representation remains appropriate for TP4. TP8 mesh processing and later voxel terrain should add representation-specific operators and conversions behind typed ports, rather than make the current grid solver claim geometry independence. No Render, Graphics/RHI, or Gameplay dependency belongs in the headless solver.

## P1 resolutions — 2026-10-01

- **R1:** Open-boundary hydraulic flow now uses zero as its outside head in
  the solver's local elevation frame. A 3×3 regression evaluates identical
  wet terrain at datum 0 m and 1000 m and checks equal nonzero export and final
  water fields.
- **R2:** Evaluation accounting now counts unique retained terrain-value
  objects for both cache hits and newly evaluated nodes. A regression warms the
  cache, adds a distinct node, and verifies the combined result is rejected
  when it exceeds the configured byte limit.
- **R3:** RenderSystem records per-serial pending, applied, failed, and
  superseded catalog outcomes with diagnostics. TerrainViewer now handles
  terminal failure by unregistering pending assets, clearing its pending state,
  and retaining the already committed preview so the user can retry. A fake
  backend test injects promotion failure and verifies status reporting and
  retry. The viewer rollback path was compiled; no interactive runtime failure
  injection was performed.

## P2/P3 resolutions — 2026-10-01

- **R4:** Hydraulic slope is now computed directly from the reusable double
  surface buffer. The solver no longer constructs and validates a temporary
  full-grid field or converts a newly allocated float slope grid back to
  double. The scratch preflight uses 320 bytes per sample for solver buffers,
  output fields, state copies, and headroom. This is a conservative estimate,
  not a measured peak-memory profile.
- **R5:** Mesh projection builds a median-split XZ triangle BVH, then queries
  only overlapping leaves per heightfield sample. Existing vertical-hit
  behavior is preserved, including choosing the topmost overlapping surface.
  The dense-mesh performance benchmark remains follow-up evidence.
- **R6:** Preview bounds are carried in `PreviewAssets`; the applied path no
  longer rebuilds the closed terrain mesh to obtain its bounds.
- **R7:** Hydraulic operator v1 retains its original spatial diagnostic ports.
  Version 2 publishes six aggregate diagnostics as scalar metadata instead of
  six repeated full-grid fields. The editor displays metadata alongside node
  outputs. A regression checks the v2 metadata and retained v1 registration.
- **R8:** Submission removes stale queued revisions before testing pending
  capacity. A gated-worker regression confirms a new revision replaces a full
  queue of obsolete pending work.
- **R9:** `TerrainViewerHost::PreviewPublication` now owns the preview asset
  bundle and catalog serial, with explicit pending, applied, failed, retired,
  and completed transitions. `Tick` handles promotion outcomes and the adapter
  performs cleanup through this state object.

## Validation of this review

- `cmake --build build --config Debug --target TerrainGenerationTest RenderSystemTest TerrainViewer` — passed.
- `TerrainGenerationTest.exe --gtest_filter=TerrainProjectionTest.VerticalMeshProjectionSamplesTopmostSurfaceAndUsesExplicitFallback:TerrainEvaluationTest.CachedOutputsCountTowardRetainedResultBudget:TerrainHydrologyTest.OpenBoundaryIsInvariantToWorldDatumTranslation:TerrainExecutorTest.NewRevisionReplacesPendingJobBeforeCapacityCheck` — 4 passed.
- `RenderSystemTest.exe --gtest_filter=RenderSystemLifecycleTest.ReportsFailedCatalogPromotionAndAllowsRetry` — 1 passed with injected render-target failure.
- `cmake --build build --config Debug --target KimPeanutEngine` — passed.
- Launched the Debug engine with Vulkan in `terrain-viewer` mode on the `Default`
  desktop. Runtime exported
  `save/screenshots/validation/terrain-p2-p3-viewer.png`; the foreground window
  was the engine's `GLFW30` window. The capture shows the terrain mesh, heightmap
  debug, performance profile, controls, and log panes in the expected docked
  layout, with generation status `Ready`.
- R4 memory and R5 dense-mesh timings were not profiled; the review fixes have
  focused functional evidence but not RelWithDebInfo performance measurements.
