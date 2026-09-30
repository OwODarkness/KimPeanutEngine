# Terrain PCG TP2 implementation journal

Date: 2026-09-29

## Completed

- Added a stateless seeded 2D Perlin primitive to Core Math. Terrain imports
  that primitive; the noise API contains no Terrain types.
- Added height-raster bilinear resampling in recipe world coordinates and
  deterministic Gaussian ridge/valley curve fields.
- Added domain warp, Nyquist-bounded ridged detail, height remapping and
  heightfield blending operators.
- Added slope in radians and height Laplacian curvature in inverse meters,
  computed with physical X/Z spacing and one-sided boundary stencils.
- Added vertical `MeshData` projection with highest-surface selection, datum-
  relative elevation offsets and an explicit no-hit elevation offset. This loses
  overhang and underside information.
- Added globally derived heightfield normals, meter-space positions, global UVs
  and upward-facing triangles for converting a field into CPU `MeshData`.
- Added deterministic D8 priority flood with perimeter outlets, optional
  authored-lake outlets, routing-only depression filling, downstream links and
  reverse-order cell accumulation.
- Added plateau, mountain-basin and coastal-plain JSON recipes and contracts for
  fixture samples, derivatives, projection, shared coarse/fine mesh samples,
  drainage acyclicity/conservation and operator repeatability.
- Removed “CPU-only” from the CMake feature description. CPU is the reference
  evaluator; this option does not preclude a future Graphics/RHI compute path.

## Validation

- `cmake -S . -B build -G "Visual Studio 17 2022" -DKPENGINE_ENABLE_TERRAIN=ON` —
  passed.
- `tools/kp.ps1 build TerrainGenerationTest` — passed.
- `tools/kp.ps1 test -l terrain` — 14/14 passed.
- `cmake -S . -B build -G "Visual Studio 17 2022" -DKPENGINE_ENABLE_TERRAIN=OFF`
  and `tools/kp.ps1 build ModuleBootstrap` — passed, confirming the optional
  module-disabled runtime still composes.

## TP2 shared-renderer viewer

- Added the `terrain-viewer` application mode and a TerrainViewer host registered
  only when `KPENGINE_ENABLE_TERRAIN` is enabled.
- The host evaluates the checked-in mountain-basin recipe over a 129x129
  preview domain, builds `MeshData`, registers a transient Mesh Asset,
  clones the standard PBR error material with a terrain-green base color, and
  registers that Material Asset with the shader dependencies intact.
- The preview scene uses Runtime's `GameplayWorld`, the existing static mesh,
  camera and directional light Actor factories, and MeshComponent. The host
  exposes mesh/material roots; Runtime prepares these alongside Render built-ins
  without requiring a Level. Renderer promotion and frame recording remain the
  existing Runtime/RenderSystem path. No second renderer or TerrainCore/Render
  dependency was added.
- Terrain mode skips normal Level instantiation and Editor UI setup. Runtime
  ticks the preview GameplayWorld. The host destroys partial Actors and releases
  generated Asset registrations during failure/teardown.
- Module host registration is contributed by each module through the generic
  static registration registry. `ModuleBootstrap` has no Terrain, Panel, or
  Live2D includes or factory logic; CMake composes enabled contribution objects.

## Validation and remaining acceptance

- `tools/kp.ps1 build TerrainGenerationTest` — passed. Terrain CPU suite was
  previously run after the TP2 algorithm changes: 14/14 passed.
- `tools/kp.ps1 build ModuleBootstrap` with `KPENGINE_ENABLE_TERRAIN=ON` — passed.
- `tools/kp.ps1 build KimPeanutEngine` Debug — passed.
- After the generic registration change, `tools/kp.ps1 build KimPeanutEngine`
  Debug — passed. Terrain Viewer launched with Vulkan and returned Runtime
  `stats` successfully, confirming the Terrain contribution is linked and runs.
- A follow-up `capture.screenshot` request remained pending during this
  registration smoke run, so no new image was claimed from this run. The
  earlier inspected Vulkan capture remains the visual evidence for the same
  viewer scene path.
- Debug Vulkan `terrain-viewer` started on the interactive desktop. Runtime
  `capture.screenshot` returned success; inspected `terrain-tp2-vulkan-final.png`
  shows the green terrain mesh.
- Added a Terrain-specific UI layer under `engine/module/terrain/editor/`. It
  reuses the EditorUI presentation backend, main editor's dock host, draggable
  splitters, existing Output Log component, and GPU Performance Profiler. The
  compact initial layout puts Terrain View center, Heightmap Debug and
  Performance Profiler at right, and Log/Terrain Controls in bottom tabs; the
  Controls content is intentionally empty.
- Added a generic compact viewer layout preset to the shared editor layout
  model. It defines the default panel regions while retaining the same dock
  placement and splitter interaction used by the main editor.
- The `scene_color` terrain capture confirms the shared scene target contains
  the generated terrain but does not include EditorUI composition. The later
  `engine_window` capture returned the current desktop rather than the engine
  window, so this run does not provide visual evidence for the newly docked UI.
- `tools/kp.ps1 build KimPeanutEngine` — passed in Debug after the docked viewer
  changes.
- Launched the Debug Vulkan `terrain-viewer`; Runtime `stats` reports
  `pass.editor_composite.outcome=executed`, `imgui_ms=0.767`, and the normal
  terrain scene draw. The Runtime engine-window screenshot path still captures
  the desktop, so panel appearance and pointer-driven docking were not verified
  from a window-targeted image.
- OpenGL `terrain-viewer` starts; Runtime `stats` reports OpenGL, executed
  G-buffer/deferred-lighting passes, one section and mesh draw calls. However,
  `scene_color`, `engine_window`, `base_color`, `world_normal` and
  `material_params` captures are black. Redirected process stdout/stderr were
  empty, and the repository's latest file log was stale Vulkan output. The
  Runtime command surface reports pass metrics but has no OpenGL backend debug
  message query, so the failed visual path could not be localized from these
  commands at that time. This finding was resolved by the OpenGL follow-up
  below.
- No unit tests were added or rerun for the viewer integration. Runtime visual
  captures are under `save/screenshots/validation/` and are not source assets.

## OpenGL follow-up

- OpenGL Cornell Box rendered correctly, isolating the black Terrain capture to
  Terrain's sectionless generated mesh draw path.
- The shared draw recorder uses `DrawIndexed()` to submit a complete mesh when
  it has no explicit sections. Vulkan retained the total index count; OpenGL
  retained only per-section counts, so the implicit draw resolved to zero.
- OpenGL mesh resources now retain total index count and use it for sectionless
  draws. Debug `KimPeanutEngine` build passed. The rebuilt OpenGL Terrain viewer
  produced a visible `scene_color` capture; Runtime stats reported the G-buffer
  and deferred-lighting passes executed, one G-buffer section, and 32,768
  triangles. The OpenGL Cornell capture was also visible.
- The Terrain viewer's `base_color` capture request was rejected by the capture
  service; scene-color capture provides the visual acceptance evidence. No
  tests were run as part of this backend fix.
