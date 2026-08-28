# Gameplay Bootstrap and Material Asset Checkpoint

- Status: partial
- Date: 2026-08-28
- Spec: none; this journal records the GP0–GP5/bootstrapped Actor checkpoint
  and the first Material Asset V1 implementation slice.
- Parent TODO: [Gameplay module](../../docs/gameplay/TODO.md),
  [Material System M6](../../docs/render/material_system_TODO.md)

## What was done

- Added the small Gameplay module with World-owned Actors, component lifecycle,
  Scene/Primitive/Mesh components, and a factory for one static-mesh Actor
  composition.
- Established the copied Gameplay-to-Render source bridge. Gameplay stores only
  a source registration token; Render resolves logical mesh/material values and
  owns MeshProxy/GPU-facing state.
- Migrated the bootstrap renderable from a RenderSystem-owned special MeshProxy
  to a normal Gameplay Actor. Render startup prepares a logical source value;
  Runtime transfers it through the startup handshake and creates the Actor on
  the game thread.
- Moved concrete startup composition behind
  `RuntimeContext::FinalizeGameStartup`, leaving `Engine` responsible only for
  the startup/thread boundary.
- Added the first Material Asset V1 loading slice: strict version-1
  `*.material` JSON parsing, the CPU-only `MaterialResource`, AssetManager
  dispatch, a bootstrap material example, and Asset-side format documentation.

## What changed

- Architecture or behavior: Gameplay now owns Actor/component lifetime and
  publishes copied source values. Render owns source records, readiness,
  MeshProxy creation, material instances, and graphics resource resolution.
  The Material Asset loader owns only parsed authoring paths/values; it does not
  create AssetIDs recursively while holding AssetManager's loader lock.
- Important files/modules: `engine/runtime/gameplay/`,
  `engine/runtime/render/render_source*`, `render_system.*`, Runtime startup,
  `engine/runtime/asset/material*`, and `engine/test/unit/{gameplay,asset}/`.
- Public API or ownership changes: static mesh Actor construction remains a
  Gameplay factory; the current source payload still carries a transitional
  `MaterialInstanceHandle`. `KPAT_Material` and `MaterialResource` are now
  available from Asset, but Render-side conversion to an AssetID-based material
  source has not landed.

## Validation

- Required level: L3 for the Gameplay/render bridge and bootstrap migration;
  L2 for the Asset loader/schema slice.
- Command: `cmake --build build --config Debug --target RuntimeLib`
- Result: PASS — Runtime, Render, Gameplay, and Asset built after both startup
  and material-loader changes.
- Command: `ctest --test-dir build -C Debug -R "(GameplayWorldTest|RenderableSourceRegistryTest)" --output-on-failure`
- Result: PASS — 11/11 focused Gameplay/render-source tests.
- Command: `D:\C++Project\KimPeanutEngine\build\engine\example\graphics\Debug\GraphicsSmoke.exe`
- Result: PASS — Vulkan and OpenGL three-frame smoke, including gameplay actor
  create/move/visibility/destroy/resize/teardown.
- Command: `ctest --test-dir build -C Debug -R "(MaterialLoaderTest|BootstrapTest)" --output-on-failure`
- Result: PASS — 15/15 Material Asset loader and bootstrap regressions.
- Command: `git diff --check`
- Result: PASS.

## Remaining risks and unverified areas

- The interactive Engine bootstrap window was not manually run. GraphicsSmoke
  validates the bridge but not that exact configured startup scene.
- Material resource references remain paths until the next Render integration
  slice. A `*.material` file currently cannot yet drive a MeshComponent in the
  running engine.
- The first AssetUnitTest build could not run during CMake test discovery due
  to missing `assimp-vc143-mt.dll` (`0xc0000135`). The test target now copies
  its existing local Assimp runtime dependency; the subsequent build and CTest
  run passed.

## Remaining work

- Resolve MaterialResource-relative shader/texture paths in Render, create
  private template/default-instance records, then replace Gameplay's
  transitional `MaterialInstanceHandle` source field with material `AssetID`.
- Migrate the bootstrap configuration to `bootstrap.material` and remove
  `CreateDefaultTexturedMaterial` only after that resolver path is live.
- Add wrong-thread mutation/consumption assertions, a gameplay camera source,
  and then real shadow/lighting pass consumers before a general render graph.

## Documentation and follow-up

- Updated [Gameplay TODO](../../docs/gameplay/TODO.md),
  [Material System TODO](../../docs/render/material_system_TODO.md),
  [Material System design](../../docs/render/material_system.md),
  [Asset module design](../../docs/asset/asset_module.md),
  [asset custom-file README](../../engine/runtime/asset/README.md), and
  [project status](../../docs/status.md).

## Correction — 2026-08-28 (Material Asset M6 integration)

- RenderSystem now resolves `MaterialResource` paths to AssetIDs, then caches a
  private `MaterialTemplateHandle` plus default `MaterialInstanceHandle` per
  material AssetID. Gameplay source descriptors and static-mesh factories now
  carry material AssetIDs only.
- `bootstrap.json` preloads `material/bootstrap.material`, and the bootstrap
  actor uses that asset rather than `CreateDefaultTexturedMaterial`.
- M6 remains intentionally small: `base_color_texture` is the only accepted
  texture parameter and maps to the existing shader ABI binding 2; per-Actor
  override values and generalized binding metadata are still deferred.

## M6.1 validation — 2026-08-28

- Extracted the render-private material AssetID cache into
  `MaterialAssetResolver`. It still owns only derived template/default-instance
  records and is constructed by `RenderSystem`; Gameplay remains AssetID-only.
- Added contract coverage for one-record deduplication, invalid/unloaded/broken
  material references, unsupported material versions, and ready-proxy
  retirement after a source becomes failed.
- Validation: focused Asset/Render CTest suite (16 tests) and `GraphicsSmoke`
  on Vulkan and OpenGL passed. The interactive Engine bootstrap loop was not
  run in this checkpoint.
