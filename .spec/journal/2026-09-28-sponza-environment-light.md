# Sponza environment-light recovery — 2026-09-28

## Change

Changed `asset/level/sponza.level` environment `ibl_intensity` from `0.0` to
`0.35`. Zero intensity suppressed both raster and path-traced environment light
for this fixture. Also removed the deferred-lighting gate that disabled raster
IBL whenever hybrid ray-query shadows were selected. Shadow visibility and
environment illumination are independent inputs to deferred shading.

Render continues to own the environment binding and intensity policy. Raster
uses the existing split-sum IBL artifacts; path tracing uses the panorama on
ray misses. No Graphics/RHI ownership changed.

## Validation and remaining work

- Source inspection confirms the active environment bindings and intensity are
  consumed by `deferred_lighting.frag` and
  `ray_tracing_path_tracer.rmiss`.
- `.\tools\kp.ps1 -Configuration Debug build KimPeanutEngine` passed. Raw
  CMake initially failed because its MSBuild child received duplicate `PATH`
  and `Path` environment keys; the project wrapper normalized that environment.
- Runtime capture `save/screenshots/validation/r50-vulkan-sponza-ibl-recovered-raster.png`
  passed on Debug Vulkan with all ray tracing disabled. Visual inspection shows
  environment-lit material response in the shadowed hall.
- Runtime capture `save/screenshots/validation/r50-vulkan-sponza-ibl-recovered-ray-query.png`
  passed with PT disabled and `ray_query_shadows_active: true`; the scene still
  shows the environment contribution on the deferred ray-query pipeline.
- Runtime capture `save/screenshots/validation/r50-vulkan-sponza-ibl-recovered-pathtrace.png`
  passed with `path_trace_active: true`,
  `path_trace_environment_enabled: true`, and intensity `0.35` in `stats`.
- The 2026-09-27 R5.0 Sponza captures still describe the old zero-intensity
  fixture and must not be presented as the updated result. Cross-backend
  Sponza parity remains open.
