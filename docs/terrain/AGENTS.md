# Terrain planning and implementation guide

Read [PLANS.md](PLANS.md), [TODO.md](TODO.md), [references.md](references.md),
the root [agent guide](../../AGENTS.md) and the
[validation matrix](../validation_matrix.md) before implementation.

- The optional module, headless generation targets, Terrain Viewer, authoring
  controls and native bake path are implemented. Keep catalog-promotion fault
  injection and high-count replacement stress open until those checks pass.
- Initial product is finite heightfield authoring and native baking. Preserve
  the typed operator extension seam and explicit representation capabilities.
- The first interactive consumer is `terrain-viewer`, with an isolated preview
  scene and session. Share neutral 3D services without loading game startup or
  terrain authoring in ordinary Scene3D; do not substitute a Scene3D tool panel.
- Reuse Gameplay Actor/component and RenderSystem contracts in the viewer/runtime
  adapter. Keep Gameplay/Render dependencies out of TerrainCore/TerrainGeneration;
  avoiding game Level startup does not prohibit a minimal preview GameplayWorld.
- CPU generation must remain headless. Workers receive immutable values;
  UI, AssetManager and backend objects never enter generation kernels.
- Asset owns loading/publication/dependencies, Resource owns CPU preparation,
  Render owns policy and rendering resource owners, Graphics owns GPU objects
  and submission-safe release. Do not expand Runtime-to-Editor dependencies.
- Read source before assuming Level reload or logical source creation can
  publish newly generated geometry. TP3 owns that integration prerequisite.
- Preserve stable seeds, units, domain coordinates, operator versions,
  boundaries, quality tiers and reproducibility scope in bake provenance.
- Distinguish simulations from appearance approximations. A new solver must
  document its state, discretization, conservation rules and numerical tests.
- Keep erosion as a PCG postprocess over a prepared base heightfield; do not
  bake solver behavior into a particular noise operator. Treat the current
  talus node as a prototype until conservation and resolution tests pass.
- Global drainage/erosion is not a local tile filter; declare dependencies and
  solve the finite domain before partitioning until a basin decomposition is proven.
- Mesh/volume operators require their own numerical and topology contracts;
  do not silently project away geometry to satisfy an incompatible port.
- Use stage designs before substantial implementation; record factual results
  in `.spec/journal`, current acceptance in TODO, durable design in PLANS.
- For CPU work, build/test affected targets without graphics. For preview,
  asset and runtime integration follow the project's compile/contract/runtime
  matrix, including Debug Vulkan validation and OpenGL coverage.
- Runtime launches require the approved path outside the sandbox on the Default
  desktop. Use checked-in fixtures and Runtime commands; captures stay beneath
  `save/screenshots/validation/`. No build or test runs during performance samples.
- Performance acceptance uses rebuilt RelWithDebInfo and matched provenance;
  distinguish generation/bake cost from rendering cost and quality changes.
