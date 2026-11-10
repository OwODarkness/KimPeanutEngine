# Render owner boundaries and agent reuse workflow

- Date: 2026-10-22.
- Status: implemented; final evidence reconciliation in progress.
- Spec: [owner boundaries](../specs/render-owner-boundaries.md).
- Parent: [Render R5.3](../../docs/render/TODO.md).
- Starting state: `f1f2734` plus the separate, preserved Scene3D startup repair
  in `engine/runtime/engine.cpp` and its untracked journal. No commit requested
  for this follow-up.

## Changes and placement decisions

The six review findings are resolved together. Graph finalization stages PT
sample/RNG/history progress; the submission result publishes it along with
adaptive sampling. Rejection consumes pending work without advancing it. The
existing pure PT progress helpers are reused. Raster history retains the same
submitted-only contract.

`DeferredRenderer::PrepareFrame` performs the draw snapshot and RT scene
preparation before backend acquisition. One pure `RenderFrameMode` resolves
requested settings against pipeline availability, scene instances, TLAS and
capacity. History preparation, graph selection, TAA/AO eligibility and commit
use that same value, including raster fallback. Required histories still
prepare before recording. Material profiling resets at preparation start so
pre-frame scene work remains represented.

`RenderTarget` derives logical color sizes from its descriptions using existing
Core `GetTextureMipByteCount` and obtains optional allocation bytes through the
existing RHI query. The frame-target owner knows its motion/identity guides;
TemporalAaPass knows its histories. `TextureFootprint` combines values without
assuming a supported allocator query. Render profiling aggregates guide,
history and active AO footprints before pass recording. Disabled TAA therefore
still reports active guide/AO storage. Inactive histories are excluded.

The existing EnvironmentBindingsOwner moved from DeferredLightingPass to frame
composition; PT/deferred borrow the same environment values. GPU textures and
samplers remain resolver-owned. RenderCamera owns projection next to ray
construction; the renderer delegates. AO/TAA settings now have lightweight
value-only headers, and RenderSystem forward-declares DeferredRenderer. A
standalone-header translation unit checks that the public contract compiles.

Graph order, graph physical-binding assembly and transient-role selection stay
local to composition because they combine concrete owner outputs. No generic
utility bucket, virtual pass hierarchy, shared mutable context or backend-native
dependency was added. Ownership and reuse candidates are recorded in the spec.

## Shared agent harness

Root AGENTS.md requires a behavior/owner search, a compact placement decision
and final semantic ownership review. The linked implementation workflow is
shared with Claude through its existing AGENTS.md import. The completion
evidence template now asks for reuse/local-helper reasons.

`tools/agent/placement.ps1` is a read-only evidence command. It inventories
explicit source paths or HEAD-relative tracked changes plus untracked sources,
finds relevant guides, runs repository symbol searches and checks selected
dependency boundaries. HEAD comparison includes staged and unstaged changes.
Its narrow structural checks reject native/UI includes in common Render/RHI,
new Runtime→Editor includes and a concrete renderer include in RenderSystem's
public header. It installs no hooks and does not claim to automate semantic
ownership or prove reuse. Positive scans, a temporary forbidden Vulkan include,
a missing path and an invalid search pattern were exercised; negative cases
were rejected and the temporary fixture was removed.

## Reference

Read local gkNextEngine revision `4ba5b7cd106c282e7ed166ff853aeea87b392680`,
`src/Engine/Rendering/ViewCameraUboBuilder.hpp` and `RenderView.hpp`.
The useful pattern is a focused camera operation and named temporal lifetime,
separate from GPU resource ownership. Its Vulkan-only contracts and broad
mutable view-state layout were not adopted. No source copied.

## Validation evidence

- `cmake --build build --config Debug` passed, including engine, editor and
  consumers of the changed public header. Log:
  `save/logs/render-owner-full-debug-build.log`.
- `ctest --test-dir build -C Debug -L 'render|graphics' --output-on-failure`
  passed **258/258** (31 Graphics, 227 Render). It covers staged PT publication,
  duplicate/rejected commits, mode fallback combinations, submission failure
  recovery, footprint aggregation/unknown allocation, TAA-off accounting and
  perspective/orthographic projection round trips. Log:
  `save/logs/render-owner-focused-final.log`.
- The first focused run was 257/258: an early test asserted behind-camera
  rejection for an orthographic camera, whose homogeneous W is always one.
  The corrected test preserves the existing contract and limits that assertion
  to perspective; both projection modes still round-trip their viewport rays.
- Broad `ctest --test-dir build -C Debug --output-on-failure` ran all 1155
  registered tests inside the sandbox: 57 failed, six skipped. The first
  environment limitations were loopback-server behavior in TTS and denied
  temporary product/file publication (for example Live2D: "Access is denied").
  The 63 failed/skipped cases were retried outside the sandbox; 54 passed and
  the same nine Asset/Terrain failures from the baseline remain. The initial
  retry used an unsupported noncapturing CTest regex and ran no tests; it was
  corrected to a supported capturing group with `--no-tests=error`.
  Logs: `render-owner-full-tests.log`, `render-owner-external-retry.log` under
  `save/logs/`.
- Existing checked-in `tools/validation/r7/taa_validation.py` was reused against
  visible Debug Vulkan/OpenGL canonical `level/sponza.level` runs on Default
  desktop, via Runtime JSON-lines and capture/poll. Vulkan passed 10 lifecycle
  checks; OpenGL passed eight (PT unavailable). Checks cover effective history,
  disabled bypass/zero jitter, cut recovery, resize recovery, minimize/restore,
  reload and Vulkan PT exclusion/accumulation preservation across TAA toggles.
  Source hashes, settings, stats and captures are indexed in
  `save/diagnostics/render-owner-20261022/{vulkan,opengl}.json`.
- Inspected full composited on-window captures for both APIs: upright canonical
  Sponza, authored materials and diagnostic viewer remain intact. Base-color,
  scene-color on/off, rigid move/return and final captures use the unique
  `render-owner-20261022-<api>-` prefix under
  `save/screenshots/validation/`. Vulkan validation has no VUIDs and teardown
  reports zero AS/pipelines/bindings/address tables/temporary build batches.
- Vulkan active R7 totals remain payload **31,754,444** and allocation ranges
  **32,440,320** bytes at 1094×631, raster TAA with hybrid query shadows and AO.
  OpenGL native allocation remains unsupported. Debug timing samples are
  diagnostic only; no new performance claim or quality-threshold comparison.

## Remaining failures and limits

The external retry still fails:

- LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures
- AssetCatalogProviderTest.MissingArchivePublishesAValidPartialLiveSnapshot
- AssetCatalogProviderTest.UnreadableArchiveFileIsReportedWithoutThrowing
- AssetCatalogProviderTest.LiveAssetsAreCopiedWithoutPayloadDataCrossingTheContract
- AssetCatalogProviderTest.DestroyingTheProviderLeavesTheManagerUsable
- NativeModelLoaderTest.ConcurrentLoadsShareModelAndUnloadOwnedMesh
- NativeModelRuntimeIntegrationTest.LoadsMultiMaterialGraphThroughAssetManager
- AssetImportRegistryTest.ExecuteReturnsProviderIdentityAndLateRegistrationIsRejected
- TerrainMeshTest.CoarseAndFineSamplingShareWorldPositionsAndGlobalNormals

Actual GPU queue-submit rejection and a real over-capacity PT scene were not
forced; rejection/fallback have unit contracts plus source wiring review.
No RelWithDebInfo performance rerun, new numeric quality acceptance, interactive
gizmo drag test or expanded R5.2/R5.3 closure is claimed. Shaders and their ABI
are unchanged. The agent harness supplies workflow and structural guardrails;
semantic reuse still requires the recorded review.

## Changed areas

- Render: deferred composition/frame mode, pass owners/settings, camera,
  target footprint and profiling contracts, RenderSystem facade.
- Tests: shared fake backend, RenderSystem/Camera/Profile tests, public-header
  compilation and its CMake source list.
- Agent workflow: AGENTS.md, placement command, implementation workflow and
  completion evidence.
- Documentation: Render design/map/roadmap/review, focused spec and this journal.
- Graphics/RHI, shaders, vendored code and the separate startup repair were not
  edited by this follow-up.
