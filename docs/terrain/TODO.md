# Terrain PCG roadmap

**Status: TP1-TP3 authoring, preview replacement, and native bake implemented; catalog-promotion failure injection and high-count lifecycle stress remain open.** Architecture: [PLANS.md](PLANS.md). Research:
[references.md](references.md). Execution contract:
[spec](../../.spec/specs/terrain-pcg-authoring.md). Planning evidence:
[journal](../../.spec/journal/2026-09-29-terrain-pcg-plan.md).
TP1 execution evidence: [journal](../../.spec/journal/2026-09-29-terrain-pcg-TP1.md).
TP2 execution evidence: [journal](../../.spec/journal/2026-09-29-terrain-pcg-TP2.md).
TP3 execution evidence: [journal](../../.spec/journal/2026-09-30-terrain-pcg-TP3.md).

## TP0 — scope and integration diagnosis

- [x] Inspect Pangolin generation, erosion worker and editor preview source.
- [x] Confirm heightfield authoring/baking first; rain/hydraulic meaning.
- [x] Inspect optional module, host, Asset and Render source seams.
- [x] Identify modern algorithm candidates and pin inspected source revisions.
- [ ] Before implementation, freeze the domain/units, preview publication
  contract and initial fixtures; resolve the affected ownership dependencies.

## TP1 — headless data and evaluation

- [x] Add TerrainCore/TerrainGeneration with an optional build switch; generation
  tests require neither Editor nor a graphics backend.
- [x] Implement explicit grid metadata, immutable fields, recipe serialization,
  typed operator registration, DAG validation and bounded job/result ownership.
- [x] Establish stable per-node RNG seeds, revision cancellation and bounded caches.
- [x] Pass rectangular/invalid-size, units, incompatible-port, cycle,
  repeatability, worker-count, cancellation and shutdown contract tests.
- [x] Independently register a scalar offset operator without changing
  evaluator code; confirm only its dependent nodes reevaluate.

## TP2 — controllable landforms, derived fields and first view mode

- [x] Implement ridge/valley curves, height-raster resampling, vertical mesh
  projection, domain warp/ridged detail, remapping and blends.
- [x] Compute physical-space slope/curvature and deterministic D8 priority-flood
  drainage, with perimeter/lake outlets and routing-only depression fill.
- [x] Preserve world-space positions, globally derived mesh normals and UVs;
  resample raster inputs at the recipe's physical sample locations.
- [x] Check plateau, mountain-basin and coastal-plain fixture samples. Verify
  drainage is acyclic and accumulation conserves upstream cell counts.
- [x] Implement `terrain-viewer` using the shared 3D renderer without gameplay
  Level instantiation. Runtime prepares generated mesh and material roots
  through its normal immutable render catalog; Terrain mode uses a docked
  EditorUI viewer with heightmap, log, and performance panels.

- [x] Reuse a minimal GameplayWorld and ordinary Actor/MeshComponent plus camera
  and light source contracts for the dedicated preview; no game Level startup.
  The adapter remains above the headless generation targets. Verify source
  replacement and teardown as part of TP3; add terrain-specific components only
  for justified instance behavior.

## TP3 — authoring workspace, preview and bake vertical slice

- [x] Define the prepared-asset preview commit boundary, stale completion
  rejection, prior-preview retention and submitted-resource retirement path.
- [x] Add terrain-mode recipe controls, orbit/zoom camera, field diagnostics and
  job status; normal Scene3D startup does not construct these authoring services.
- [x] Support reproducible fixture replay, operator isolation, solver pause/step,
  intermediate-field inspection and per-node time/memory/numerical diagnostics
  through snapshots and commands, with a CPU-only evaluation path.
- [x] Enable regenerate/cancel/replace without hanging event processing;
  cancel retained the prior preview, an invalid generation request left its
  rendered image byte-identical, and replacements completed on both APIs.
- [x] Lock the archive database to reject a bake at source commit; confirm its
  prior source hashes/status remain unchanged and reload that bake afterward.
- [ ] Inject prepared-catalog promotion failure and verify preview rollback.
- [x] Integrate generated-model serialization/publication via Asset import;
  produce versioned provenance and native model/material/texture dependencies.
- [x] Reload baked products through a fresh normal Scene3D startup with terrain
  authoring disabled on Vulkan and OpenGL.
- [x] Capture Debug Vulkan and OpenGL raster previews; inspect world normals,
  material, scale, resize and orderly teardown. Chunk-edge checks do not apply
  until terrain is partitioned into chunks in TP7.
- [ ] Stress high-count repeated replacement and verify resource retirement;
  three back-to-back Vulkan swaps plus OpenGL replacement/cancel passed, but no
  catalog-promotion fault was injected.

**First usable slice:** TP1-TP3, including the dedicated `terrain-viewer` mode
and native assets loadable by normal gameplay. Scene3D authoring integration
is not required; the fixed TP2 viewer is now the first shared-renderer preview.

## TP4 — rain erosion, fluvial incision and solver selection

Current cross-stage code findings and recommended fixes: [TP4 review](.review/TP4.md).

- [x] Establish erosion as a postprocess after base heightfield preparation;
  add conservative talus relaxation, two-pass eight-neighbor CPU thermal flux,
  implicit stream-power incision prototypes, and normalized monotone height
  remap. Thermal flux is optional and bypassed in the default viewer fixture.
  Recipes with the node run it on the generation worker and show pre/post
  heightfields. This is an implementation start, not solver acceptance.
- [x] Integrate a versioned CPU virtual-pipe hydraulic operator with explicit
  layered bedrock/soil/sand/water/suspended-sediment state and source/export
  ledgers. Compare it with thermal flux on the matched 256² island fixture.
- [ ] Complete broader numerical contracts for talus, stream-power and
  hydraulic timestep/boundary convergence; add artist-facing method controls.
- [ ] Run a bounded 2026 stochastic-transport experiment. Compare basin
  morphology, deposits/meanders, conservation error, resolution sensitivity,
  memory and bake time. Record adopt/defer decision; no CUDA dependency by default.
- [ ] Pin solver state/units, time integration, outlets and water/sediment budget
  before implementation. Repeat routing as morphology changes.
- [ ] Verify flat/plane/basin/channel fixtures, nonnegative water/sediment,
  bounded erosion, sediment/water balance including sources/sinks, no NaNs,
  timestep convergence and reproducibility. Define numerical tolerances in
  the stage design from the chosen scheme; screenshots alone do not prove this.
- [ ] Publish flow, water, suspended sediment and deposition diagnostics.

## TP5 — wind and loose sediment

- [ ] Add authored wind fields, sediment availability/erodibility, exposure,
  saltation/deposition and angle-of-repose relaxation.
- [ ] Distinguish bedrock, loose sand, static obstacles and vegetation motion.
- [ ] Verify zero-wind, uniform-wind, reversing-wind, dune and obstacle fixtures;
  account for material entering/leaving open boundaries.
- [ ] Evaluate 2024 obstacle-aware 3D wind simulation as an offline quality tier;
  establish its memory/time cost before adding a fluid solver dependency.

## TP6 — biomes, materials and vegetation placement

- [ ] Use elevation/slope/wetness/curvature/exposure and authored masks for
  biome weights; recompute descriptors after final height modification.
- [ ] Bake initial diagnostic albedo/roughness products via ordinary materials;
  add live multilayer terrain shading only through a separate Render contract.
- [ ] Produce deterministic spatial placements with density, spacing, exclusion
  and river/shore policies. Store IDs/transforms/model/material dependencies.
- [ ] Validate placement stability across chunk splits and bounded-count normal
  Level integration; dense forests require measured batching/instancing support.

## TP7 — runtime packaging, chunking and LOD

- [ ] Partition one solved domain into fixed-resolution mesh products with
  bounds, shared-edge normals and global UV conventions.
- [ ] Validate mixed baked LOD neighbors with an explicit stitch/skirt policy
  and measured height error; bound residency and selection hysteresis.
- [ ] Compare viewer and normal-runtime output for the same bake; run reload,
  resize and shutdown lifecycle validation on both graphics APIs.
- [ ] Record RelWithDebInfo generation time, peak CPU memory, product size and
  runtime frame times under matched settings. Set budgets from the baseline.

**First authoring release:** TP1-TP7. Gameplay collision, animated water,
infinite generation/streaming and dynamic erosion remain separate features.

## TP8 — additional geometry consumer

- [ ] Add SurfaceMesh input/output and a mesh-specific deformation or weathering
  operator with adjacency, normals, boundaries and material channels.
- [ ] Demonstrate an imported rock/geometry model processed without a lossy
  height projection; reject unsupported representations explicitly.
- [ ] Test manifold/degenerate/open mesh handling, bounds, normals, material
  preservation and self-intersection limits. Explain any displacement limits.
- [ ] Design SDF/volume conversion/remeshing only for topology-changing erosion
  with an actual requested consumer. Never advertise heightfield erosion as
  representation-independent mesh erosion.

## TP9 — optional acceleration and advanced authoring

- [ ] Profile first, then evaluate SIMD/task parallelism and GPU kernels on
  the same domain/settings/quality; retain the deterministic CPU reference.
- [ ] Audit RHI compute/readback capabilities before any GPU port. Define
  numerical parity and resource ownership; no backend types in operators.
- [ ] Consider oriented procedural detail, node canvas, paint/sculpt layers,
  and sketch-conditioned learned generation as independently justified tools.

## Future work — voxel terrain

- [ ] Explore voxel terrain as a future extension of the terrain module and
  dedicated viewer. Outside the initial heightfield authoring release.
  Representation, meshing, erosion, editing, storage and schedule are undecided;
  this entry does not authorize implementation or add first-release prerequisites.
