# Terrain PCG architecture

**Status: TP1-TP3 authoring, preview replacement, and native bake implemented; TP4 postprocess prototypes started; catalog-promotion failure injection and high-count lifecycle stress remain open.** User scope: heightfield terrain,
authoring in a dedicated tool, baking into engine assets, extensible operators,
future processing of other geometry, and rain/hydraulic and wind erosion.
The roadmap is [TODO.md](TODO.md); source findings and algorithm choices are in
[references.md](references.md). The execution contract is
[the terrain spec](../../.spec/specs/terrain-pcg-authoring.md).
The stage designs are [TP1](.plan/TP1.md), [TP2](.plan/TP2.md), and
[TP3](.plan/TP3.md).

## Decision and boundary

Implement an optional compiled module under `engine/module/terrain`, following
the existing optional-module composition model. Proposed build switch:
`KPENGINE_ENABLE_TERRAIN`. This is an engine feature module, not a Codex plugin.
DLL discovery, binary ABI stability, and hot unloading are outside the first
release. Operator registration is the useful extension seam now.
Extract a shared procedural-geometry library only after terrain and a second
implemented consumer establish which contracts are actually common.

Use a headless CPU library for generation, a separate Asset import/bake adapter,
and a dedicated terrain authoring workspace. The first interactive integration
is a `terrain-viewer` application mode in the existing engine executable. It shares
3D scene rendering and RHI infrastructure; a second terrain-specific scene
renderer would make preview and runtime disagree.

Proposed source/target boundaries, to be created when their consumer lands:

| Area | Responsibility | Allowed dependencies |
| --- | --- | --- |
| `product/` / `TerrainCore` | Domain, fields, mesh values, recipe schema, results | Bounded Core/math utilities; no AssetManager, Editor, Render, RHI |
| `evaluation/`, `operators/` / `TerrainGeneration` | Typed operator registry, DAG evaluation, solvers, cache, jobs | TerrainCore and CPU job infrastructure |
| `import/` / `TerrainImport` | Source dependency decoding, native bake, archive publication | TerrainGeneration and Asset import/cook |
| `tools/` / `TerrainTools` | Authoring session, UI, preview requests, host adapter, commands | Generation, import, Runtime/Gameplay/Render, Editor UI |
| Future runtime adapter | Specialized terrain residency/LOD when justified | Baked CPU products and value-only Render source contracts |

Normal baked mesh terrain must load with terrain authoring disabled. Asset owns
registered CPU asset lifetime and dependencies; Resource conversion owns CPU
render preparation; Render owns scene policy and rendering resource owners;
Graphics owns GPU objects and retirement. Generation jobs own their temporary
buffers and publish immutable values. Neither operators nor UI own backend
buffers or reach into a Vulkan/OpenGL renderer.

## Gameplay representation and renderer reuse

PCG owns algorithms and CPU data: fields, drainage, mesh values and placement
values. TerrainCore/TerrainGeneration must not depend on Gameplay, Render,
Graphics or Editor. The viewer/runtime integration adapter consumes a finished
result, registers its CPU assets through Asset and prepares them for Render,
then creates or updates Gameplay-owned Actors/components using logical AssetIDs.

```text
TerrainGeneration -> immutable fields / MeshData / placement values
  -> Asset registration + Resource/Render preparation
  -> preview GameplayWorld -> Actor + MeshComponent
  -> copied Render source descriptions -> RenderSystem -> Graphics/RHI
```

A terrain instance can be represented as an Actor composition. Initially use
`CreateStaticMeshActor` and the existing MeshComponent; a multi-chunk terrain
can use an Actor with multiple mesh components or an adapter-managed Actor group. Add a module-owned
TerrainComponent/factory only when terrain-specific chunk management, queries,
editing or collision integration justify it. Gameplay remains unaware of PCG
algorithms, and the Actor/component never owns GPU objects or runs generation
inside rendering. The authoring session owns generation jobs and applies
completed revisions through the integration adapter at the owning thread's
safe point.

The dedicated terrain mode reuses a minimal GameplayWorld for preview mesh,
camera and light Actors, plus the shared RenderSystem. It skips ordinary game
Level instantiation and player-controller setup. Its viewer UI reuses EditorUI's
ImGui backend and shared dock host for the terrain view, heightmap, performance
profile, log, and an initially empty controls panel. Mode isolation is isolation
of composition and session state, not a replacement world or renderer. Runtime
coordinates ownership and tears down preview Actors and their source tokens
before Render shutdown. Reusing MeshComponent does not remove the separate
generated-asset/catalog publication prerequisite.

## Source-grounded integration constraints

Current code already provides:

- Generic statically linked module contributions through
  [`module_registration.h`](../../engine/module/module_registration.h): each
  optional module owns its registration callback; the build composition adds
  the enabled registration objects, and `module_bootstrap.cpp` only dispatches
  the registry.
- A host lifecycle/factory contract in
  [`application_host.h`](../../engine/runtime/host/application_host.h).
- Custom Asset type and import-provider registries, without terrain cases in
  Asset core: [type registry](../../engine/runtime/asset/asset_type_registry.h),
  [import registry](../../engine/runtime/asset/asset_import_registry.h).
- Explicit mesh values and native serialization:
  [`data/mesh.h`](../../engine/runtime/core/data/mesh.h),
  [`native_model.h`](../../engine/runtime/asset/native_model.h).
- Logical static-mesh sources and prepared CPU catalogs:
  [`render_source.h`](../../engine/runtime/render/render_source.h),
  [`prepared_render_asset_catalog.h`](../../engine/runtime/render/prepared_render_asset_catalog.h).

The `terrain-viewer` host composes Runtime's existing scene services without
loading a game Level or promoting the full scene editor workspace. It initializes
the lightweight Terrain viewer panels through the shared EditorUI presentation
backend. Runtime prepares explicit host asset roots into the same immutable
catalog used by Level startup, then follows the normal renderer promotion path.
`RuntimeContext::ReloadStartupLevel` still
reinstantiates the loaded Level; it does not reload disk contents or republish
generated geometry into the prepared Render catalog. TP3 owns transactional
replacement of generated preview assets.

## Generation data flow

```text
Recipe + imported inputs + authored control maps
  -> validate types, units, dependencies and operator versions
  -> compile CPU evaluation DAG
  -> evaluate immutable inputs into job-owned outputs
  -> TerrainResult (fields, drainage features, optional instance placements)
       -> mesh/material conversion -> prepared preview -> shared 3D Render
       -> TerrainImport -> native products/archive -> normal Level startup
```

This CPU evaluation DAG is separate from the GPU RenderGraph. Solver iterations
occur inside an operator; recipe cycles are rejected. Start with a serialized
recipe and an ordered operator inspector. A visual node canvas is optional UI
after the execution model proves useful.

## Representation and extension contract

Use explicit typed ports, rather than a single universal mutable terrain
object. First ports are `Heightfield`, `ScalarField2D`, `VectorField2D`,
`DrainageNetwork`, `SurfaceMesh`, and `InstanceSet`. Only implemented types and
operators are advertised. A future `VolumeField` is a separate representation,
introduced with a real consumer.

Each operator descriptor carries stable ID/version, typed inputs/outputs,
parameter schema, required channels, supported representation, neighborhood or
whole-domain requirement, memory estimate, and execution/determinism policy.
The registry rejects duplicate IDs, missing versions, incompatible ports, and
unknown required operators. Unknown recipe payloads may be preserved for
editing, but cannot silently participate in a bake.

Heightfield storage has explicit width/height, horizontal origin and spacing,
vertical datum, engine-axis mapping and units in meters. Vertex heights and
cell-centered simulation fields use distinct dimensions; adapters define the
conversion. Do not infer dimensions from the square root of buffer length.
Bedrock elevation, loose sediment thickness, erodibility, rainfall, water depth,
suspended sediment, and velocity are distinct channels with stated units.

Mesh support is incremental. TP2 vertically projects imported `MeshData` into
a heightfield by selecting the highest triangle at each X/Z sample; overhangs
and underside surfaces are lost and no-hit samples use an explicit fallback
height. TP8 adds actual mesh modification. A heightfield hydraulic
operator cannot accept an arbitrary mesh just because both are geometry.
Mesh erosion needs its own adjacency/discretization and transport rules;
topology-changing cuts need remeshing or a volume representation.

Wind is a field input reusable by later operators. Aeolian erosion transports
loose material; wind animation bends vegetation; mesh abrasion changes exposed
surfaces. They are separate operators with separate state and validation.

## Algorithm direction

The default recipe should build coherent large forms before adding detail:

```text
Ridge/valley constraints + uplift + imported reference shapes
  -> macro elevation -> depression handling + drainage
  -> fluvial incision + local rain erosion/deposition + thermal relaxation
  -> optional wind/sediment transport
  -> optional flow-oriented detail -> recompute derived descriptors
  -> biomes/material weights -> vegetation placement -> bake
```

Noise remains a useful primitive for variation. Use ridge/valley constraints,
domain warping and ridged/multifractal signals as controls, not as a substitute
for drainage and transport. Evaluate pointwise noise in physical coordinates,
and bound frequency to the sampling resolution. Never normalize each tile
independently.

Select initial solvers through small reference experiments, not publication
date alone. The proposed dependable CPU baseline is drainage routing plus
implicit Stream Power Law incision, a conservative grid/virtual-pipe hydraulic
solver for local rain erosion, and talus relaxation. The 2024 FastFlow work
informs depression routing and a later GPU route. The 2026 stochastic
geomorphological transport work is a serious TP4 candidate for momentum-aware
transport; its public reference kernel is not a complete erosion system.
Compare it with the baseline before committing to a port. See
[the source-backed comparison](references.md#algorithm-selection).

Wind first uses authored direction/speed, exposure, sediment supply, saltation,
deposition and angle-of-repose relaxation. Obstacle-aware 3D wind simulation is
an optional offline quality tier, informed by the 2024 windblown-sand work.
Label the simpler field-based model as an approximation. Erosion-pattern
amplification can supply fast detail, but is not conservative water simulation.

Final material/biome policy uses elevation, slope, curvature, drainage/wetness,
exposure and authored masks. River features come from drainage topology and
carving constraints; a water height/color threshold alone does not create a
river network. Full animated water rendering is a separate feature.

Terrain surface authoring uses a data-driven `TerrainLayerMaterial` profile:
stable layer IDs reference ordinary PBR texture sets, and layer count is asset
data rather than a renderer constant. A monotone normalized-height remap is a
PCG heightfield operation before erosion, derived slopes and mesh conversion;
it can compress an authored ground band such as 0.2–0.5 into 0.2–0.25. Material
height bands blend only near their local configured thresholds, and slope
overlays affect only their configured elevation range, so low-water and grass
bands remain elevation-driven. The initial viewer/bake reuses the ordinary
mesh/material Render path. Future masks can add drainage, wetness and authored
data behind the same profile; a low elevation water material is only a visual
mask and does not create a river network or animated water body.

Erosion is a postprocess in the generation graph after base terrain
preparation, not a responsibility of a specific Perlin/noise generator. TP4
starts with deterministic talus relaxation and an implicit slope-exponent-one
stream-power incision operator that recomputes D8 routing after each iteration.
Hydraulic transport needs explicit water/sediment/bedrock state and source/sink
accounting; the height-only prototypes do not claim those solver contracts are
complete.

## Evaluation, determinism and lifetime

- An authoring session owns editable recipes; evaluation receives an immutable
  revision. Debounce edits, cancel superseded jobs, and publish only results
  matching the current revision. Retain the last valid preview on failure.
- Derive RNG streams from recipe seed, stable node ID and sample/particle ID.
  No process-global random generator. Fixed traversal and reduction order are
  required for the reference CPU bake. Worker count must not change it.
- Cache keys include schema/operator versions, canonical parameters, seed,
  input hashes, physical domain, spacing, boundary policy and quality tier.
  Preview and final bake are different evaluations and cache namespaces.
- Declare tile halos for local operators. Drainage and long-range erosion are
  basin/domain operations; bake the finite domain first, then partition it.
  Do not promise independent tile generation with globally coherent rivers.
- Use budgeted buffers and bounded completion queues. An approximate 2049²
  float field alone is 16 MiB; solver channels, ping-pong buffers, caches and
  preview meshes multiply this. Spill/discard intermediate results as needed.
- Deterministic CPU results are the initial reproducibility contract. GPU
  atomic floating-point reductions may require statistical/tolerance tests,
  and cannot inherit a bitwise reproducibility claim.
- Closing a session cancels and joins jobs, drains result queues, releases
  logical sources, then lets Render/Graphics retire submitted GPU resources.
  A worker never calls UI or mutates a displayed array.

## Dedicated viewer and preview transaction

Yes to a dedicated workspace: recipe/operator inspector, orbit/fly viewport,
height/slope/flow/sediment/biome views, profile and bake status, comparison of
the last result, and errors linked to the failed operator. Keep the terrain
session separate from the game Level being edited. UI only submits commands
and consumes snapshots. The same evaluator also runs without a window.

Reuse the host selection pattern from Live2D, but share the 3D renderer needed
by terrain. `TerrainViewerHost` owns its authoring session, preview scene,
camera, diagnostics and job lifecycle. Ordinary Scene3D startup must not load
terrain authoring or execute its generation jobs. Baked output is integrated
with normal Scene3D only after it works in the dedicated mode.

TP2 includes `terrain-viewer` as the requested first view mode. It creates a
minimal GameplayWorld with generated mesh, camera and light Actors, skips game
Level instantiation and the full Editor workspace, and promotes its generated
mesh and material through the shared RenderSystem. Terrain viewer mode has a
small docked UI on the existing EditorUI presentation path. A Scene3D terrain
panel is not the fallback release.

Debugging must be possible without a complete game scene. Expose a fixed
recipe/seed/camera fixture, pause and single-step solver iterations, inspect
each operator's fields, disable individual operators, and replay the same
evaluation. Publish node timings, buffer/memory usage, input/result hashes,
job revision/cancellation state and numerical budget diagnostics. These are
snapshot/command capabilities shared with tests and headless tools; UI does
not inspect worker memory directly. Shared renderer defects may still affect
the preview, so CPU-only evaluation remains independently testable.

Preview publication is a transaction: prepare immutable generated CPU
mesh/material payloads outside Render, publish a revision at a safe frame
boundary through a bounded prepared-asset update, replace logical source
handles, and retire superseded resources through existing Render/Graphics
owners. Existing source creation alone cannot add a missing catalog asset.
TP3 implements this baseline with a Render frame-boundary update and an
explicit synchronization point before scene-owned resolver/material teardown.
Injected catalog-promotion rollback and high-count replacement retirement stress
remain acceptance work. No direct AssetManager access from a terrain render
pass, in-place mutation of a published MeshResource, or private backend upload
shortcut.

## Authoring and baked products

Proposed `.terrainrecipe` JSON stores operator IDs/versions, parameters,
connections, seed, domain, authored constraints and source references. This is
tool source data, not something Render opens. Save referenced control maps
through the normal source/import boundary. Baking produces normal native
`.model`, `.material`, `.texture` products and a Level fragment/fixture, using
Asset serialization and publication contracts. A tooling manifest records
provenance, recipe/input hashes, product hashes, solver/quality/boundary settings
and generated placement IDs; it is not a new mandatory runtime terrain type.

Bake stages products, validates dependencies/integrity and publishes them
before updating archive metadata. Failure/cancellation must preserve the
previous usable bake. Extract a bounded generated-model publication entry
only if the current Assimp import path cannot consume generated MeshData;
do not forge archive records from the viewer. New content identities are
distinct from old loaded AssetIDs.

Start with fixed-resolution chunk meshes from one baked domain. Shared sample
positions and globally derived normals must match across chunk edges. TP7
adds baked LOD only with an explicit crack strategy and error criterion;
texture mipmaps are not geometric LOD. Specialized terrain shader blending,
instanced vegetation, collision, and volume terrain are separate consumers,
not prerequisites for a plain mesh bake. Dense vegetation remains gated on a
real batching/instancing path.

## Release gates

TP1-TP3 provide the first usable author/generate/preview/bake/reload slice.
TP4-TP7 expand it to the requested erosion, wind, biome and vegetation workflow.
TP8 proves an independently registered mesh operator without changing the
evaluator. TP9 investigates acceleration only after matched profiling.
All acceptance and prerequisites are in [TODO.md](TODO.md).
