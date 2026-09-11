# Live2D concrete renderer preview — 2026-09-11

## Scope

Connected the SDK-free Live2D snapshot/planner work to a concrete
renderer-owned offscreen preview so the checked-in Hiyori fixture can be seen
through the generic Render submission path.

## Landed

- Added `IRenderExtension` registration/recording and capture-target
  resolution without adding Live2D semantics to generic submission types.
- Added renderer-owned Hiyori GPU resources, shader programs, normal/additive/
  multiplicative pipeline variants, a packed mask target, and editor preview
  consumption.
- Added `config/live2d.json` as the module-owned preview selector and kept
  `asset/level/live2d_test.level` as a camera-only fixture for fast runtime
  validation. Runtime now consumes the offline native `.live2d` product
  through AssetManager; it no longer imports `.model3.json` or writes a
  temporary product closure.
- OpenGL pipelines now apply blend factors, blend operations, enable state,
  and color-write masks on every bind. Existing non-blended pipelines
  explicitly disable blending, preventing state leakage into the PBR path.
- Vulkan sampler creation now applies the common mipmap mode, and file-backed
  images declare transfer-destination usage before upload.

## Validation

- `cmake --build build --config Debug --target KimPeanutEngine`: passed.
- `Live2DRenderPlannerTest`: 3/3 passed.
- `Live2DMaskAtlasPlannerTest`: 4/4 passed.
- OpenGL `level/live2d_test.level` runtime capture: Hiyori rendered correctly
  with transparent character background.
- Vulkan runtime capture: model geometry is submitted, but sampled texture
  colors remain corrupted; this is an open backend investigation, not claimed
  as a passing cross-backend result.

## Remaining risk

The renderer is a concrete preview path, not yet the final render-graph-based
viewer architecture. Vulkan texture sampling parity and the complete L2D4.5
lifetime/resize/cross-backend hardening matrix remain open.
