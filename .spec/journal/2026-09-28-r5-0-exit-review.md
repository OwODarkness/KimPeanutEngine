# R5.0 exit review — 2026-09-28

Plan: [R5.0 baseline freeze](../../docs/render/.plan/R5.0.md) ·
Spec: [Render R5 execution spec](../specs/render-r5-render-configuration-graph-ownership.md)

## Decision

The R5.0 gate for R5.2 is closed. Current Debug Vulkan Cornell and Sponza PT
captures now have indexed Runtime pass outcomes under fully tracked resident
textures. The path-tracing producer, tone-map producer, active Editor Viewer,
and external composition all report `executed`; raster-only lighting/tone-map,
diagnostic capture conversion, and AS build passes report `not_in_plan` for
these steady PT frames. No pass reports `failed` or `skipped_dependency`.

R5.2 image comparisons are frozen to the same backend, fixture revision,
camera, viewport, effective settings, output, and exact `path_trace_samples`.
Compare decoded RGBA8 Scene Color: RGB mean absolute error must be at most
`0.002` in normalized display-space units, the 99th percentile channel error
must be at most `2/255`, and alpha must match exactly. This threshold is
informed by the existing Cornell Beauty trace-ray versus inline-query
consistency result (display-RGB MAE `0.00124`); that older result used different
sample counts, so it calibrates the limit but is not an R5.2 parity pass. Any
sample-count or fixture mismatch invalidates a comparison instead of widening
the tolerance.

## Frozen graph outcomes

Both captures used Debug Vulkan with validation enabled, `level/` fixture
defaults, 1094x631 viewport, complete tracked texture residency, path tracing
active, 4 SPP per dispatch, 8 continuation bounces, Beauty output, trace-ray
visibility, and no fallback reason. Cornell environment lighting was disabled;
Sponza used its current authored IBL intensity `0.35`. Both had the Debug
Viewer active on World Normal. Screenshot export succeeded through Runtime.

| Fixture | Samples at stats snapshot | Executed | Not in plan |
| --- | ---: | --- | --- |
| Cornell | 2,216 | Directional/spot/point shadows, G-buffer, PT, PT tone map, Debug View, Editor Composite | Deferred lighting, raster tone map, capture conversion, BLAS build, TLAS build |
| Sponza | 44 | Directional/spot/point shadows, G-buffer, PT, PT tone map, Debug View, Editor Composite | Deferred lighting, raster tone map, capture conversion, BLAS build, TLAS build |

There were no failed or dependency-skipped outcomes. The low Sponza sample
count is a deterministic capture point for later equal-count comparison, not a
converged quality image or performance sample. Capture and Viewer coexistence
remains covered separately by the R5.1 simultaneous-output Runtime evidence.

Artifacts:

- `save/screenshots/validation/r5-baseline-cornell-pt-1.png`
- `save/screenshots/validation/r5-baseline-sponza-pt.png`
- `save/diagnostics/r5-baseline/cornell-pt-stats.json`
- `save/diagnostics/r5-baseline/sponza-pt-stats.json`
- `save/diagnostics/r5-baseline/*-capture.json`

The image comparisons remain prospective: no R5.2 extraction has yet produced
a candidate image. The R5.0 review establishes exact fixture/settings/sample
matching and the numeric comparison limit before that movement.

## Validation

- `tools/kp.ps1 build KimPeanutEngine` — passed Debug.
- Runtime captures used the approved visible launch path on the verified
  `Default` desktop. The Runtime endpoint reported Vulkan, PT active, complete
  tracked residency, and successful screenshot export for both fixtures.
- `PerformanceStatsCommandProviderTest` — passed 2/2, including outcome text
  serialization. The focused Render graph suites passed 24/24.
- No RelWithDebInfo timing was collected; these are correctness baselines.
