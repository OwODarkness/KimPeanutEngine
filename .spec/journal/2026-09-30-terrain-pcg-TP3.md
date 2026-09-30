# Terrain PCG TP3 authoring and bake

- Status: partial
- Date: 2026-09-30
- Spec: [Terrain PCG authoring and bake](../specs/terrain-pcg-authoring.md)
- Parent TODO: [TP3](../../docs/terrain/TODO.md#tp3--authoring-workspace-preview-and-bake-vertical-slice)

## What was done

- Continued the TP3 authoring and bake path in the Terrain Viewer: seeded
  regeneration, cancel/pause/step/resume controls, intermediate-field selection,
  per-node timing/bytes/range/finite/hash diagnostics and native bake.
- Added Runtime agent commands `terrain.cancel` and
  `terrain.execution_control`; existing regenerate and bake commands remain.
- Kept the UI's seed, amplitude and frequency synchronized with the active
  recipe after command-driven changes.
- Changed the fallback environment texel to black, matching the requested
  Terrain viewport background.
- Mapped the regeneration-time access violation through the matching Debug PDB
  to `RuntimeScreenshotService::RequestScreenshot`: the persistent Runtime
  screenshot service retained a reference to a `RenderCaptureService` that
  catalog promotion destroyed. RuntimeScreenshotService now resolves the
  current service for each request and returns `CaptureUnavailable` if it is
  absent. Added a regression test that swaps the resolved service between
  requests.
- Baked `model/terrain/generated/default` and loaded it in fresh ordinary
  Scene3D startup with path tracing disabled on Vulkan and OpenGL.

## What changed

- Architecture or behavior: Terrain continues to use the existing Gameplay
  Actor/MeshComponent, prepared Render catalog and shared RenderSystem. Runtime
  screenshot export remains outside Render but resolves its currently owned
  capture service instead of holding a borrowed lifetime across catalog swaps.
- Important files/modules: `engine/module/terrain/**`, Runtime screenshot
  service/global context, shared Render environment fallback, screenshot test,
  Terrain roadmap/spec and this journal.
- Public API or ownership changes: RuntimeScreenshotService adds a resolver
  constructor while preserving the reference constructor for stable test and
  caller-owned services. RenderSystem remains owner of RenderCaptureService.

## Validation

- Required level: L3
- Command: `tools/kp.ps1 build KimPeanutEngine` — PASS, Debug.
- Command: `tools/kp.ps1 build RuntimeScreenshotServiceTest` — PASS.
- Command: `tools/kp.ps1 test RuntimeScreenshotServiceTest` — PASS, 7/7,
  including resolution of a replacement capture service.
- Command: `tools/kp.ps1 test -l terrain` — PASS, 15/15 CPU contracts,
  including one-node-at-a-time pause/step control.
- Command: `tools/kp.ps1 test NativeMaterialTest` — PASS, 7/7 (including
  content-archive shader path resolution).
- Runtime: Debug Vulkan Terrain Viewer regenerated seed 321 and successfully
  captured `engine_window` after the catalog swap. Screenshot:
  `save/screenshots/validation/terrain-tp3-black-background.png`.
- Runtime: Debug OpenGL Terrain Viewer regenerated seeds 778 and 909; stats
  reported OpenGL, deferred raster, 32,258 triangles and three draw calls.
  Cancellation retained the prior preview. Screenshots:
  `terrain-tp3-opengl-cancel.png`, `terrain-tp3-opengl-replaced.png`, and
  `terrain-tp3-opengl-final.png` under `save/screenshots/validation/`.
- Runtime: Vulkan normal Scene3D loaded the baked fixture with path tracing
  disabled and showed 32,258 triangles / four draw calls. OpenGL normal Scene3D
  loaded the same logical model, showed 32,258 triangles / four draw calls, and
  exported visible scene-color, world-normal and base-color images. OpenGL
  resize produced 1024x768 diagnostic captures. The Vulkan/OpenGL baked startup
  images are `terrain-tp3-baked-scene3d-raster.png` and
  `terrain-tp3-baked-scene3d-opengl.png`.
- Runtime: a negative-amplitude request was rejected before evaluation. The
  scene-color captures before and after had identical SHA-256
  `0174d77a1b729fe739551787e29296bcef8b85b43b07e4b741da2417de196a5a`.
- Runtime: held a SQLite exclusive transaction while rebaking the existing
  deterministic recipe. `TerrainBaker` failed at archive source commit with
  `database is locked`; the prior source `settings_hash`, `package_hash` and
  status were unchanged. A fresh Vulkan Scene3D startup then loaded the prior
  logical bake in strict deferred raster mode with path tracing, ray tracing and
  ray-query shadows disabled (32,258 triangles / four draws). Screenshot:
  `terrain-tp3-vulkan-raster-after-archive-failure.png`.
- Runtime: three consecutive valid Vulkan replacements (seeds 1201-1203) each
  completed and exported a screenshot; final stats remained at 32,258 triangles
  and three draws with path tracing and ray tracing disabled. These runs passed
  with Debug Vulkan validation enabled, but do not replace a high-count
  resource/leak stress test.
- Runtime: Debug Vulkan enables `VK_LAYER_KHRONOS_validation` through the
  non-`NDEBUG` device path. Terrain preview replacement and teardown completed
  without a reported validation error in the inspected run.
- Command: `git diff --check` — PASS; Git emitted only existing LF-to-CRLF
  working-copy notices.

## Remaining risks and unverified areas

- Invalid generation controls and archive-commit failure were exercised, with
  prior preview/source preservation verified. Catalog-promotion failure was
  not injected, so catalog rollback remains unverified.
- Three consecutive Vulkan swaps and an OpenGL replacement/cancel succeeded.
  Higher-count resource-retirement/leak stress was not run.
- Chunk-edge validation is deferred until TP7 introduces chunk products.
- No RelWithDebInfo performance sample was taken; Debug frame timings are
  diagnostic only.

## Remaining work

- Add bounded prepared-catalog promotion fault injection and verify preview
  rollback.
- Run higher-count replace/cancel/resize/close and resource-retirement stress
  under Vulkan validation and OpenGL, then close TP3 acceptance.

## Documentation and follow-up

- Updated Terrain TODO, PLANS, AGENTS, project status and the implementation
  spec to distinguish accepted TP3 paths from outstanding fault injection.
- No commit was created; the shared worktree contains unrelated pre-existing
  Live2D changes and the user's sectionless Vulkan mesh fix.
