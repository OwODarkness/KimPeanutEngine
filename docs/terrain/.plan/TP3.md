# TP3 — authoring session, preview replacement and native bake

## Goal

Turn the fixed TP2 preview into a small, replayable Terrain authoring slice.
The default project is a seeded coherent-noise heightfield at 256x256 samples.
Changing its seed or primary landform controls schedules bounded background
evaluation, updates field diagnostics, and replaces the preview through the
existing Gameplay/Render path. A successful bake is a normal content-addressed
native model and layered PBR materials that a fresh non-Terrain startup can
load.

## Boundaries

- Keep `TerrainCore` and `TerrainGeneration` free of Runtime, Gameplay, Asset,
  Editor and Graphics dependencies. Workers receive copied recipe values and
  publish immutable evaluation results.
- `TerrainAuthoringSession` owns revision, cancellation, pause/step controls,
  latest result and bounded job lifecycle. Render UI submits value commands and
  reads copied snapshots; it never inspects worker state.
- `TerrainViewerHost` is the Runtime integration adapter. It owns transient
  Asset registrations, standard Actors, camera controls and staged publication.
- Render catalog updates are accepted only at a render frame boundary. The
  baseline update may wait for submitted GPU work and rebuild scene-owned
  resolver/material state before making the new immutable catalog visible.
  Graphics resources remain owned and retired by Render/Graphics.
- Bake converts committed CPU `MeshData` and the data-driven Terrain layer
  material profile to existing native product formats, writes
  content-addressed model/material/texture files, validates them, then replaces
  the Terrain source record in `ModelArchiveDatabase` last. Layer count remains
  profile data and all layers use ordinary PBR materials.
- The generation graph remaps normalized height values before mesh conversion;
  material height layers blend only in narrow smootherstep bands around their
  boundaries. Slope overlays are gated
  by an elevation range so water/ground remain height-driven. The starter profile
  uses a blue lowland material, the supplied meadow PBR maps, highland ice/quartz,
  and a slope-selected rock overlay; the water layer is a visual surface mask,
  not a simulated river network or water body.
- Save each bake under a stable Terrain source identity and content revision.
  The checked-in default recipe remains an input fixture; authored copies live
  under the project Terrain content folder. Export a small Level JSON fixture
  using the archive's logical model reference so users can verify normal
  startup without Terrain authoring enabled.
- The authoring UI exposes seed, amplitude, frequency, regenerate/cancel,
  field selection and job status. The viewport supports orbit/zoom around the
  generated bounds. Node reports include cache status, elapsed time, bytes,
  finite/range summary and stable output hash. Intermediate fields can be
  selected by node/output.
- TP2 has no iterative erosion solver. Pause/step applies at deterministic DAG
  node boundaries; solver-iteration controls remain for the solver stages.

## Transaction and lifetime

1. UI/command edits are queued as value changes. The game thread increments the
   revision and submits the immutable recipe to a bounded executor. New work
   cancels older revisions; queue pressure reports `busy` instead of growing.
2. The host accepts only a completed result whose revision matches the current
   recipe. Failure/cancellation leaves the last committed field, mesh and Actor
   visible.
3. Build new CPU mesh/material assets and prepare a new catalog including both
   old and new roots. Create the replacement Actor/source before retiring the
   previous Actor. Queue catalog replacement for the next safe Render boundary;
   the RenderSystem waits for submitted use before destroying old scene GPU
   resources. Keep old CPU registrations through the source handoff and release
   them only after the replacement is applied.
4. A bake snapshots the last committed result. Product files are staged and
   validated before immutable content-addressed publication. The source record
   is the commit point. On cancellation or failure, the prior archive source
   remains the current logical bake and staged files are removed where safe.

## Validation

- Evaluate the fixed-seed 256x256 recipe twice and compare output hashes; inspect
  per-node range, finite state, timing and memory summaries.
- Verify pause/resume/step, superseded revision cancellation, bounded queue
  behavior, latest-result-only publication and failure preservation.
- Repeatedly regenerate and resize the Terrain view on Vulkan Debug validation
  and OpenGL; check that old GPU resources retire after the catalog handoff and
  the camera remains framed on the terrain.
- Bake, load the logical model in a fresh normal Scene3D startup with Terrain
  disabled, and compare bounds/material/sample provenance with the committed
  recipe result. Inject failure before archive commit and confirm the previous
  logical bake still resolves.
- Run applicable CPU/Asset contracts and Debug build. Inspect Runtime stats and
  captures from `save/screenshots/validation/`; do not infer visual correctness
  from compilation or pass counters alone.

## Reference note

Piccolo's `main` branch `engine/source/runtime/resource/asset_manager/asset_manager.h`
keeps its baseline asset manager as explicit file-to-JSON load/save. KimPeanut's
native model path has a stronger existing boundary: stage and validate
content-addressed products, then commit their source/product links with
`ModelArchiveDatabase::ReplaceSource`. TP3 uses that local transaction instead
of adding file writes to Runtime or making the viewer an AssetManager owner.
