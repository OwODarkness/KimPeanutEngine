# Render R7 raster image quality planning checkpoint

- Status: complete for planning; runtime implementation not started.
- Date: 2026-10-03.
- Spec: [R7 contract](../specs/render-r7-raster-image-quality.md).
- Parent TODO: [R7 roadmap](../../docs/render/TODO.md#r7--raster-image-quality-r70-in-progress).

## What was done

Inspected current render roadmap/status, GBuffer/deferred-lighting/tone-map
shaders, graph declarations, fullscreen resources, camera/object uniforms, frame
targets, and PT history contracts. The baseline was HEAD
`fbc1599724de50dd86a6c0fb4c34274509771d54`, with unrelated README edits and
`resouce/example/tts.png` left intact.

Created R7 for SSAO → motion/history → TAA → height fog. R7.0 closes affected
correctness contracts and freezes measured budgets first. R7.5 accepts the
combined core and leaves reflection probes, SSR, and volumetric fog as separate
follow-up decisions. Existing R5/R6 progress was not relabeled.

The user's follow-up explicitly requires render-graph execution. The plan and
spec now require typed nodes/resources for every effect/filter/resolve, precise
attachment scopes, persistent history imports and writes, dependency pruning,
and graph-context recording. No independent post-process schedule is permitted.

Read actual Godot `4.4-stable` SSAO and forward-clustered shader source, plus
AMD's FSR2 integration manual. Adopted separate AO/filter stages and explicit
jitter/motion/fog conventions. Source was not copied; no new library was chosen.
The FSR2 reproject-header retrieval failed; temporal design relies on the
available manual and Godot motion source, not an asserted FSR2 source audit.

## Changed areas and impact

Documentation only: canonical R7 plan, execution spec, planning journal, Render
architecture/roadmap links, deferred-PBR roadmap link, and project-status summary.
No C++/shader ABI, dependency, runtime behavior, or GPU ownership changed.
The plan preserves Render policy / Graphics physical ownership and successful
submission as the boundary for committing temporal state.

## Validation

- Required level: Level 0.
- PowerShell local link/anchor validation: PASS, 27 new references across the
  seven R7-related files. New-document trailing-whitespace check: PASS.
- `git diff --check`: PASS. Git emitted normal LF/CRLF conversion notices.
- A broader link/anchor scan also reported five pre-existing anchor mismatches
  in the Render roadmap/project status. None was introduced by R7; they remain
  outside this task. The scoped check compared tracked links with HEAD.
- Build, tests, smoke, capture, and performance: NOT RUN; this request creates
  documentation and changes no generated artifact or executable behavior.

## Remaining risks at the planning checkpoint

R7 is planned, not runtime-validated. R5.2/R5.3 acceptance, the recorded
GraphicsSmoke descriptor failure, and R5.4 failed-frame layout issue remain
open. Quality/cost budgets require fresh R7.0 evidence. Subsequent implementation
must satisfy each stage exit and record real visual/lifecycle measurements.

## R7.0 execution checkpoint

### Fixture and Sponza raster captures

Added `asset/level/r7_raster_fixture.level` with the requested plane, convex
spheres, concave room corner, thin occluder, AO-textured rock, and rigid-object
motion reference. Added its three source materials and the machine-readable
viewport/camera/light/motion contract in `tools/validation/r7/raster_fixture.json`.
The fixture uses source OBJ compatibility paths so it does not depend on an
ignored local archive; startup logs mark that compatibility route transitional.
Its frozen lighting includes a directional key and an unshadowed point fill so
the disabled-feature capture has visible diffuse detail without an environment
product.

Added `asset/level/sponza_raster.level` and a neutral PBR override. The existing
Sponza logical product references were absent from the local archive. Importing
the three source models created native products locally, but runtime material
dependency resolution rejected their texture references. The baseline therefore
uses the direct source glTF models with the neutral override; it validates the
raster workload and graph but does not represent Sponza's authored materials.
Local imported products live only in ignored `content/.archive`.

Captured feature-off Vulkan frames under `save/screenshots/validation/`:

- `r7-sponza-raster-rt-off-vulkan.png`: both ray tracing and path tracing off;
  deferred raster shadow-map path, 9,547,143 triangles.
- `r7-sponza-raster-vulkan-1.png`: path tracing off, ray-query shadows on;
  hybrid ray-query path, same geometry and camera.
- `r7-raster-fixture-off-vulkan-baseline.png`: checked-in compact fixture,
  path tracing off, ray-query shadows on.

All runs used `build/RelWithDebInfo/KimPeanutEngine.exe`, Vulkan on an NVIDIA
RTX 4070 Laptop GPU, 1094×631 viewport, and `--vsync off`. Runtime stats, rather
than launch flags alone, confirmed the effective path-tracing and ray-query
modes. The fixture completed 120 warm-up frames and 300 GPU samples in each of
three fresh process runs:

| Run | GPU p50 / p95 (ms) | CPU p50 / p95 (ms) |
| --- | ---: | ---: |
| 1 | 1.329 / 1.540 | 2.211 / 2.870 |
| 2 | 1.637 / 3.113 | 2.872 / 5.208 |
| 3 | 1.810 / 3.163 | 3.224 / 5.188 |

Sponza produced 300-sample GPU p50/p95 of 2.833/2.925 ms with ray-query
shadows and 4.512/5.412 ms with ray tracing disabled. The matched conditions
and initial per-slice ceilings are recorded in the canonical R7 plan. They are
device-specific Vulkan acceptance budgets, not a frame-rate guarantee.

The first compact-fixture capture was under-lit. Adding the frozen point fill
made the floor texture, corner, occluder, and sphere readable in the canonical
baseline capture. The capture remains feature-off; no quality effect is active.

### Correctness gates and validation

- RelWithDebInfo `KimPeanutEngine` and `GraphicsSmoke` targets built serially.
- RelWithDebInfo `GraphicsSmoke.exe`: exit 0, six frames per API, but each
  backend logged rejected geometry views with invalid vertex stream handle,
  role, or offset. This is not a clean R5 binding/lifetime acceptance result.
- A prior Debug `RenderPassScheduleTest` run passed 95/95 and
  `GraphicsContractTest` passed 31/31; these are historical diagnostics only.
  No debugger was used, and all subsequent engine launches/measurements used
  RelWithDebInfo per the user's direction.
- R5.2/R5.3 acceptance and the R5.4 injected-frame editor-composite layout
  failure remain unresolved. Cornell and PBR showcase windows were not run.

R7.0 was initially left in progress pending broader R5 validation.

## User-directed R7.0 closure

On 2026-10-03 the user directed that R7.0 be marked done and that work move on
without the heavier validation sequence. The baseline milestone is closed based
on its recorded scenes, captures, fixture, and budgets. This does not claim that
R5.2/R5.3/R5.4 validation passed: their open correctness and lifetime cases
remain explicit risks for later acceptance. No Debug tool or Debug engine run
was used for this closure.

## R7.1 implementation checkpoint

Added graph-declared full-resolution SSAO estimate and edge-guided filter
passes, each writing a distinct Graphics-pooled R8 transient. The estimate
reconstructs world positions from GBuffer depth and evaluates a deterministic
6/12/24-tap neighborhood by Low/Medium/High quality; invalid/background/off-screen
taps are ignored and output is clamped. Deferred lighting now uses
`min(material AO, screen AO)` for ambient
diffuse while retaining authored AO on specular IBL. The path-tracing graph
does not schedule either SSAO pass. Disabling SSAO now compiles a graph variant
without the estimate/filter passes or their transients; deferred lighting uses
the material-AO attachment as a valid placeholder and gates AO sampling off.
`ScreenSpaceAoSettings` is accepted through RenderSystem and reports
requested/effective values, radius/bias/strength, and selected sample count
through runtime stats. Low/Medium/High presets use 6/12/24 estimate taps, with
Medium as the default.

Added semantic `screen_space_ao_raw` and `screen_space_ao_filtered` capture
views to `capture.screenshot`. The conversion pass samples the selected graph
transient; disabled SSAO yields a white AO diagnostic without requiring an AO
transient.

Validation: RelWithDebInfo `Render`, `RuntimeScreenshotCommand`, and `RuntimeLib`
targets build successfully.
No engine launch, Debug run, or tests were performed for this continuation.
Shader compiler and Vulkan/OpenGL image quality have not yet been checked;
raw/filtered AO capture views and stage acceptance remain open. The earlier
full-engine link attempt stopped with `LNK1168` because the executable was locked.

### Follow-up: Cornell source baseline attempt

The standard Cornell level fails at startup because the logical model product
`model/cornell-box/cornell-box` is absent from this checkout's content archive.
Added `asset/level/cornell_box_raster.level`, which uses the existing local OBJ
source and Cornell material overrides, plus `asset/level/pbr_showcase_raster.level`
and five scalar-only R7 PBR materials to provide a direct-source PBR scene
variant. The PBR variant has not been launched and these source models are
local ignored content, so these variants are not yet portable acceptance
fixtures.

The existing RelWithDebInfo engine loaded the direct-source Cornell variant on
Vulkan. Runtime `stats` returned `path_trace_active=false`,
`ray_query_shadows_active=true`, `render_graph_mode=hybrid_ray_query`, viewport
1094x631, and 32 triangles. This was one diagnostic stats sample, not a timing
window. The `capture.screenshot` request initially remained pending, then the
PNG appeared at `save/screenshots/validation/r7-cornell-box-raster-query-vulkan.png`.
The image shows the expected room, red/green walls, ceiling light, and occluder.
The matching RT-off capture is
`save/screenshots/validation/r7-cornell-box-raster-rt-off-vulkan.png`; its
runtime stats also reported PT and ray-query shadows off. Neither short Cornell
capture run accumulated a 300-sample timing summary.

The PBR showcase direct-source variant loaded on Vulkan in both modes and
captured `r7-pbr-showcase-raster-query-vulkan.png` and
`r7-pbr-showcase-raster-rt-off-vulkan.png`. Both runs used RelWithDebInfo,
1094×631, `--vsync off`, and 120 warm-up / 300 GPU timing samples. Runtime stats
confirmed PT off and query shadows on in the query run (111,246 triangles,
22,488,436 resident texture bytes, GPU p50/p95 1.903/3.298 ms); the RT-off run
confirmed query shadows off (GPU p50/p95 1.068/1.358 ms). The captures show the
bunny, teapot, gold sphere, Cerberus mesh, and floor with the authored scalar
PBR overrides.

Both visual runs exported PNGs through `capture.screenshot`. However, neither
the Cornell nor PBR process exposed a discoverable `GLFW30` window through
`GetProcess.MainWindowHandle` or window enumeration, and `GetForegroundWindow`
returned zero, although `OpenInputDesktop` reported `Default`. The captured
scene-color images are retained, but interactive desktop visibility was not
verified for these launches. These direct-source scene variants also depend on
checkout-local ignored model files; portability through a clean archive/import
is not established.

Per the user's instruction, the R5.2/R5.3/R5.4 Debug Vulkan validation gates
were not run in this continuation. A `tools/kp.ps1 -Help` probe unexpectedly
used the wrapper's default `validate` command and started Debug-config build
processes; they were stopped immediately after detection. No Debug build result
is used as validation evidence. The prior RelWithDebInfo GraphicsSmoke result
still has expected negative-probe logs and does not close R5 acceptance.
Affected R5 runtime correctness, Cornell timing windows, clean-checkout asset
portability, and visible-desktop window verification remain open. R7.0 is
closed as a baseline milestone at the user’s direction.

### R7.1 Vulkan runtime acceptance checkpoint

Built the full `KimPeanutEngine` target in RelWithDebInfo and launched the
Sponza raster fixture with path tracing disabled on Vulkan. Runtime stats
reported `path_trace_active=false`, `ray_query_shadows_active=true`, and a
1094x631 viewport. The Vulkan runtime loaded the estimate and filter shaders
and produced raw, filtered, and scene-color captures. Low, Medium, and High
reported 6, 12, and 24 taps when enabled. AO-off reported zero taps, skipped
both AO producers, and returned white for both AO diagnostics. The
enabled/disabled scene captures have RGB MAE 2.51/255, with 47.56% of pixels
changing; the AO difference is visible but subtle in this Sponza camera.

At High, a completed 120-frame warm-up plus 300-sample RelWithDebInfo summary
reported estimate p50/p95 0.211/0.215 ms, filter p50/p95 0.136/0.138 ms, and
combined AO cost 0.434 ms at 1094x631. The measured p95 components are within
the R7.0 1.0 ms combined budget. Timing is device- and fixture-specific. The
validated captures are under `save/screenshots/validation/` with the
`r7.1-sponza-vulkan-` prefix.

Two OpenGL RelWithDebInfo Sponza launches remained responsive but did not expose
the requested Runtime command port (the second bounded retry waited 30 seconds),
so no OpenGL stats or capture was obtained; both owned processes were stopped.
OpenGL image/lifecycle acceptance,
AO-textured material overlap, and focused planar-darkening/silhouette tolerance
checks remain open. AO-off and path-tracing-off behavior were checked only on
Vulkan. No debugger, Debug configuration, or test suite was run, per the user's
instruction to avoid debug tooling and to keep validation focused.
