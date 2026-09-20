# Render Graph R4.5 journal — 2026-09-20

## Scope

R4.5 connects the scheduled BLAS/TLAS graph work to deferred directional
lighting through a Vulkan ray-query shader variant. The authored shadow-map
path remains the fallback; the ray-query validation variant temporarily emits
a black background so direct-light visibility is easier to inspect without a
skybox.

## Implementation

- Added an API-neutral acceleration-structure resource binding. Vulkan maps it
  to `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR` and resolves the native
  handle only inside the descriptor manager; OpenGL rejects the binding and
  retains the raster fallback.
- Added the `ray_query` deferred-lighting shader variant. It uses
  `GL_EXT_ray_query` for directional and point-light hard-shadow visibility;
  spot lights retain the existing shadow-map path. The ray-query variant is
  selected only after a valid active TLAS exists, so the first/fallback frame
  cannot bind a null acceleration structure.
- Kept separate bound and ray-query deferred-lighting pipelines. This avoids
  changing a live pipeline layout after the first frame has already selected
  the fallback descriptor set.
- Ray-query output continues through the existing deferred-lighting,
  tone-map, and capture path; no parallel compositor or manual pass order was
  added.

## Validation

- `glslc --target-env=vulkan1.2 -fshader-stage=frag -DKP_RAY_QUERY=1
  asset/shader/deferred_lighting.frag` passed after selecting GLSL 4.60,
  which is required by the installed Vulkan shader toolchain for
  `GL_EXT_ray_query`.
- Vulkan live startup with `level/sponza.level` and the command transport
  reached a mature frame, returned performance stats, and exported
  `save/screenshots/validation/r4-5-2026-09-20-vulkan-ray-query.png`.
  The capture has no skybox/background and shows the lit Sponza geometry.
- The same run exported
  `save/screenshots/validation/r4-5-2026-09-20-vulkan-world-normal.png`; the
  compact measured sample is preserved in
  `save/logs/r4-5-2026-09-20-vulkan-stats.json`. The Vulkan startup log also
  records successful cache hits for both the bound and `ray_query`
  `deferred_lighting.frag` variants on the RTX 4070.
- Vulkan mature-frame measurements: deferred lighting CPU/GPU
  `0.1248/0.245760 ms`, G-buffer `6.0730/2.692096 ms`, tone-map
  `0.0365/0.041984 ms`, and total GPU `3.664896 ms`. The profiler summary
  completed 120 warmup frames and 300 samples.
- OpenGL startup with the same fixture reached the command port, returned
  stats, and exported
  `save/screenshots/validation/r4-5-2026-09-20-opengl-fallback.png`.
  Its mature-frame measurements were deferred lighting `0.0922/0.133120 ms`,
  G-buffer `5.2920/3.770368 ms`, tone-map `0.0172/0.058368 ms`, and total GPU
  `4.179968 ms`; the fallback path remained operational.
  Its compact sample is preserved in `save/logs/r4-5-2026-09-20-opengl-stats.json`.

## Remaining risks

- The current runtime stats schema does not expose named BLAS/TLAS build,
  scratch-memory, or AS-memory counters, so those remain follow-up evidence.
- Moving-instance, resize, failed-build, orderly-close, and retirement checks
  still need dedicated R4.5 runs before the full stage can be closed.
- The Vulkan ray-query capture is runtime evidence for the selected path, but
  a controlled same-frame shadow differential against the raster fallback is
  still desirable for final visual sign-off.

## Cornell-box validation follow-up

- Published the checked-in `model/cornell-box/cornell-box.obj` through the
  normal AssetTool path so `level/cornell_box.level` resolves from the archive.
- Adjusted the fixture camera to `[0.0, 2.65, 5.5]` with a 65-degree field of
  view so the complete room and both boxes are visible in the capture.
- Vulkan runtime selection was confirmed in the startup log as
  `Deferred lighting shadow path: ray_query`.
- The full-room capture is
  `save/screenshots/validation/r4-5-2026-09-20-cornell-ray-query-full.png`.

## Cornell point-light correction

- The first same-camera Cornell comparison was byte-identical. Investigation
  found that the fixture uses a point light while the initial ray-query shader
  only handled directional lights; this was a coverage bug, not evidence that
  ray tracing had no visual effect.
- Added point-light ray-query visibility with a finite light-distance bound,
  then repeated the comparison with the same camera and IBL-disabled setup.
- Shadow-map fallback:
  `save/screenshots/validation/r4-5-2026-09-20-cornell-shadow-map-before.png`.
- Ray-query point-light path:
  `save/screenshots/validation/r4-5-2026-09-20-cornell-ray-query-point.png`.
- Comparison visualization:
  `save/screenshots/validation/r4-5-2026-09-20-cornell-ray-comparison.png`.
- The controlled comparison reports 1,046 of 677,186 pixels changed (0.15%),
  with maximum channel delta 231 and mean absolute delta 0.0948. Changes are
  localized around the ceiling light and box/floor shadow edges. The runtime
  log records `Deferred lighting shadow path: ray_query`.
