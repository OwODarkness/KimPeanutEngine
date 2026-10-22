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

### R7.2 implementation checkpoint — 2026-10-08

The GBuffer now writes a fifth `RGBA16F` attachment containing previous-minus-
current top-left UV displacement, positive view-space depth, and a validity bit.
The per-pass ABI carries previous submitted view/projection matrices, extent,
and jitter UVs; per-object data carries the previous submitted rigid transform
and validity. `motion_vectors` exposes the attachment through the semantic
capture path. UV motion removes each frame's jitter once. Current raster jitter
is deliberately zero while R7.3's temporal consumer is absent; R6 path tracing
does not use or advance raster history.

Camera history rejects first-use, resize, large view/projection discontinuities,
and PT-mode frames. Generational object identity rejects new/recycled handles;
removed objects disappear after an accepted frame. Transform and camera state
commit only after graph finalization and `RenderBackend::EndFrame` returns. A
failed graph or thrown submission leaves the last submitted transforms intact.

Validation: Debug `RenderPassScheduleTest` passed 102/102; Debug
`RenderSystemTest` passed 49/49; full Debug `KimPeanutEngine` build passed.
`glslc` compiled the GBuffer vertex/fragment and capture fragment shaders under
both Vulkan and OpenGL defines. Runtime capture was not performed. The asset
import wrapper does not register shader programs (`reimport
shader/pbr_gbuffer.shader` returned “no import provider matches the source
path”), so the active content archive was not updated by this attempt. The
visible outside-sandbox launch/capture and shader archive publication are still
required before R7.2 acceptance; R7.1 acceptance also remains open.

### R7.2 runtime acceptance follow-up — 2026-10-08

The first visible Vulkan run exposed three integration omissions that unit
contracts and shader compilation had not caught: the GBuffer pipeline still
declared four color formats for a five-color target; per-pass/per-object
uniforms used in the fragment stage were visible only to the vertex stage; and
`CaptureViewPass` omitted fragment sampler binding 13 for the motion target.
The GBuffer pipeline description now matches the fifth `RGBA16F` attachment
and makes bindings 0/1 visible to both stages. The capture pipeline now declares
binding 13. Focused pipeline contract tests guard these declarations.

Debug `RenderSystemTest` passed 51/51 and `RenderPassScheduleTest` passed
102/102. The Debug `KimPeanutEngine` target built successfully. `ctest -R
RenderPassScheduleTest` reported no registered tests in this build tree, so the
test executable was run directly and passed. The earlier Vulkan shader
compilation checks covered both API defines.

Visible Debug Runtime validation used `level/sponza.level` on Vulkan and
OpenGL, with `--disable-path-tracing` and the Runtime command transport. Both
reported `path_trace_active=false`, zero PT samples, fully resident textures,
and successfully exported `base_color`, `scene_color`, and `motion_vectors` at
1094x631. Vulkan reported `hybrid_ray_query` with ray-query shadows enabled;
its validation log contained zero pipeline/layout errors after the fixes.
OpenGL reported `deferred` and completed all three captures. Both visible
`GLFW30` windows were verified on the `Default` input desktop during their
respective runs. The static motion diagnostic is the expected neutral
zero-displacement color with the validity channel set.

Captures:

- `save/screenshots/validation/r7_2_sponza_base_color.png`
- `save/screenshots/validation/r7_2_sponza_scene_color.png`
- `save/screenshots/validation/r7_2_sponza_motion_vectors.png`
- `save/screenshots/validation/r7_2_sponza_opengl_base_color.png`
- `save/screenshots/validation/r7_2_sponza_opengl_scene_color.png`
- `save/screenshots/validation/r7_2_sponza_opengl_motion_vectors.png`

Correction to the earlier archive note: shader-program manifests and GLSL are
loaded from `asset/shader` and compiled/served through the runtime shader cache;
they are not inputs to the AssetTool's model/texture/audio import providers.
The failed `reimport shader/pbr_gbuffer.shader` attempt therefore reflects an
unsupported importer request, not a missing publication step. Runtime shader
logs on Vulkan confirmed the current GBuffer GLSL source/cache entries were
used. R7.2 is complete; R7.1's material/silhouette checks and the combined R7
acceptance remain open. Raster jitter remains zero until R7.3 consumes it.

### R7.3 implementation checkpoint — 2026-10-11

Added a Render-owned raster temporal resolve before ToneMap. The graph imports
two persistent ping-pong targets (scene-linear RGBA16F history plus normal/depth
metadata) and writes the opposite target; only accepted submitted frames advance
the history index. Halton(2,3) jitter is applied to raster projection and removed
once from R7.2's motion UV. The resolve rejects invalid/out-of-bounds motion,
view-depth disagreement, and world-normal disagreement, and clips history to
the current 3×3 RGB neighborhood before blending. Sky/invalid GBuffer pixels
use current color. Tone mapping and editor composition remain downstream.
PT frames neither jitter nor use the raster history. Runtime settings,
requested/effective/history/reset stats, and a history-weight control are
available through `render.temporal_aa_settings`.

The new shader samples GBuffer motion and normal using the same screen UV
convention as the existing deferred-lighting and SSAO shaders. The initial
Vulkan-specific Y flip was removed after checking the common sampling contract.
The `.gitignore` now explicitly includes the new source shader manifest and GLSL
file; shader source assets are loaded from `asset/shader` by Runtime, not via
AssetTool import.

Validation: `cmake --build build --config Debug --target KimPeanutEngine`
passed. A visible Debug Vulkan run loaded `level/sponza.level`; Runtime stats
reported `hybrid_ray_query`, `pass.ray_tracing_path_trace.outcome=not_in_plan`,
full texture residency, `temporal_aa_effective=true`, valid history, and an
executed resolve at 1094×631. The resolve GPU sample was 0.07168 ms in Debug
(diagnostic only, not performance acceptance). TAA-on/off scene captures
exported, but the camera changed between commands, so they are not a matched
quality comparison. The visible `GLFW30` window was brought to foreground and
verified by handle/PID during runtime validation.

Known acceptance gaps: the GBuffer does not carry a stable object-ID channel,
so temporal reprojection cannot yet reject history by surface identity. No
deterministic motion/disocclusion sequence or offline supersampled reference
comparison has been recorded. Ghosting, edge-detail retention, and shimmer
tolerances are not frozen. OpenGL, RelWithDebInfo cost/memory, cuts/resize/PT
transition lifecycle, and unchanged PT output remain to validate; R7.3 is
implemented but not accepted.

Captures are under `save/screenshots/validation/` with the `r7_3-sponza-` prefix.

### R7.3 identity rejection follow-up — 2026-10-13

Added byte-exact renderable handle ID and generation outputs to the GBuffer.
The values are packed into RGBA8 UNORM byte channels, copied into two
additional ping-pong history attachments, and compared with `texelFetch` at
the reprojected prior pixel before history contributes. This distinguishes
recycled handles and avoids filtered/interpolated identity samples. The
per-object uniform ABI and Vulkan/OpenGL GLSL source declarations were updated
together; selection remains its own attachment and is still composed later.

Validation: the Debug `KimPeanutEngine`, `RenderPassScheduleTest`,
`RenderGraphTest`, and `RenderSystemTest` targets built successfully. The
render/graphics CTest labels passed 245/245 after updating graph/resource and
uniform-ABI expectations. Vulkan `glslc` compiled `pbr_gbuffer.vert`,
`pbr_gbuffer.frag` with the Vulkan define, and `temporal_aa.frag`; OpenGL-mode
`glslangValidator` checks also passed for those three shaders. The diff check
reported no whitespace errors.

A visible Debug Vulkan run loaded `level/sponza.level` and switched from path
tracing to raster hybrid ray-query mode through the Runtime command registry.
Stats confirmed `path_trace_active=false`, `render_graph_mode=hybrid_ray_query`,
full texture residency, `temporal_aa_effective=true`,
`temporal_aa_history_valid=true`, and an executed TAA resolve at 1094×631. The
resolve GPU sample was 0.099 ms p50 / 0.420 ms p95 in Debug (diagnostic only).
TAA-on and temporary TAA-off captures were exported, but their camera views
differ; they are not a matched quality comparison. The Vulkan `GLFW30` window
was verified on the `Default` desktop. Captures:

- `save/screenshots/validation/r7_3-sponza-taa-on-20261013.png`
- `save/screenshots/validation/r7_3-sponza-taa-off-20261013.png`
- `save/screenshots/validation/r7_3-sponza-final-taa-on-20261013.png`

One visible Debug OpenGL Sponza run also reported `render_graph_mode=deferred`,
full texture residency, effective TAA, valid history, and an executed resolve
(0.082 ms p50 / 0.086 ms p95). A `scene_color` capture from that run appeared
upright on inspection. The user reports that OpenGL output has repeatedly
appeared upside down; the captured scene target does not reproduce that report,
and the full engine-window/composited path was not captured. No more OpenGL
instances were launched after the report. The authored Sponza mesh transforms
are identity and its camera has yaw 180 degrees; the camera basis defines zero
yaw as +X-forward, so that yaw faces down the nave. The TAA resolve is scheduled
once before tone mapping; current and previous projections are independently
jittered from unjittered camera data.

The RelWithDebInfo `KimPeanutEngine` build passed with an existing MSVC
`LNK4098` default-library warning. Three uncontended Vulkan windows on commit
`699ac30` plus the uncommitted R7 worktree used `level/sponza.level`,
1094×631, path tracing disabled, hybrid ray-query shadows active, and full
texture residency. Each window completed 120 warm-up and 300 sample frames.
TAA resolve p50/p95 (ms): 0.094/0.132, 0.080/0.089, 0.081/0.088. Total GPU
p50/p95 (ms): 4.158/7.173, 3.036/6.788, 3.011/6.812. These satisfy the 1.0 ms
TAA and 8.5 ms combined p95 timing ceilings. The first run was excluded because
Debug runtimes were still open during part of its sample window.

Format-based R7-owned storage at this extent is estimated around 43.5 MiB:
two 24-byte-per-pixel temporal targets (31.6 MiB total), motion and identity
GBuffer attachments (10.5 MiB), and two full-resolution R8 AO transients
(1.3 MiB). Runtime stats do not expose target allocation bytes, so allocator
aliasing and the actual allocation total remain unmeasured; this estimate is
above the 32 MiB ceiling and needs reduction or direct allocator evidence.
Matched temporal-quality comparison, moving/disocclusion and history lifecycle
checks, unchanged PT output, and OpenGL upside-down reproduction remain open.
R7.3 is implemented but not accepted.


## R7.3 acceptance — 2026-10-22

### Objective, boundary, and starting state

Complete native-resolution raster TAA against the already frozen quality,
lifecycle, orientation, GPU, and memory gates. Starting revision was `699ac30`
plus extensive uncommitted R7.3 work and the separate Scene3D stable-startup
repair. Existing changes, including `engine.cpp`, were preserved. No commit was
requested or created. R7.1/R5/R6 acceptance and R7.4 fog remain separate.

Render retains temporal settings/history, graph scheduling, and resource
preparation. Graphics retains GPU allocation, native synchronization, and
submission-safe retirement. No dependency direction, common backend type,
public resource owner, PT estimator, or PT reconstruction algorithm changed.

### Diagnosis and completed changes

- Current HDR is reconstructed once into a stable output grid; history uses
  unjittered motion once. Current bilinear taps and Catmull-Rom history taps are
  edge-clamped. YCoCg neighborhood bounds avoid independent RGB clipping.
- A shader diagnostic showed about 10% normal rejection on a static view,
  concentrated in detailed fabric. The final resolve keeps the original 0.85
  normal-agreement threshold but accepts a matching normal in the same
  identity/depth-bounded 3×3 surface footprint. Depth remains compared with
  the surface's expected previous-view depth.
- Filter-only experiments plateaued just below the frozen 20% shimmer gate.
  Fine curtain detail exposed a native-versus-supersampled texture-band limit.
  Active TAA now selects a −0.5 mip bias for PBR texture sampling through the
  existing `history_params.w` uniform component. Raster without TAA and PT
  select zero. All eight Vulkan TAA-off captures remain byte-identical to the
  pre-correction baseline. Halton sequence, poses, settling frames, history
  weight (0.9), and numeric acceptance thresholds are unchanged.
- Direct unfiltered current accumulation, longer history (0.95), a global
  looser normal threshold, Blackman-Harris reconstruction, and motion-based
  history attenuation did not meet or improve the quality gate. These
  experiments and the temporary rejection shader were removed.
- Temporal targets/pipelines now prepare before `BeginFrame` recording. Failed
  preparation propagates before frame acquisition. Allocation failure also
  cancels pending history, so a later commit cannot validate released targets.
- A real PT-enabled resize reproduced destruction of AS scratch referenced by
  the recording command buffer. Moving temporal preparation alone did not fix
  it: Editor UI resize also invokes `WaitIdle` during recording. Vulkan's idle
  collection previously called `RetireSubmitted(last_submitted_serial)` on
  active-frame scratch. It now does so only outside an active frame; real
  submission still assigns the serial in `EndFrame`. The final Debug Vulkan
  runs have no VUIDs and close with zero RT objects/temporary build batches.
- AO/shadow-policy changes invalidate TAA history; unchanged settings retain
  it. Owner tests cover shader replacement rejection/publication, pending
  cancellation, failed allocation, submitted-only rotation, resize retirement,
  and idempotent cleanup. Resized frames prohibit a mid-frame idle call.
- GraphicsSmoke had drifted from the current shaders: a four-attachment
  GBuffer, missing AO binding 15, and missing tone-map options/guide bindings
  4/5. It now borrows the current `RendererFrameTargets` GBuffer and cleans it
  through that owner, with the existing shader descriptors supplied. No
  production shader was weakened for the smoke fixture.
- The checked-in validator now bypasses Windows monitor tracking limits only
  for oversized reference windows, restores the original client extent,
  records source hashes, checks minimize/restore, can collect three timing
  windows, and exits nonzero on any failed acceptance check. Captures still
  use Runtime `capture.screenshot` plus polling, never backend inspection.

The engine-reference workflow inspected the clean local gkNextEngine source
at `4ba5b7cd106c282e7ed166ff853aeea87b392680`,
`assets/shaders/Process.NativeTemporalReproject.comp.slang`, for stable
reconstruction coordinates, Catmull-Rom history, and YCoCg bounds. Its temporal
upscaling, lock state, and moments were not adopted. No source was copied.

### Changed files in this completion turn

- `asset/shader/temporal_aa.frag`, `asset/shader/pbr_gbuffer.frag`.
- `engine/runtime/render/deferred_renderer.cpp` and `.h`.
- `engine/runtime/render/render_system.cpp`.
- `engine/runtime/render/passes/deferred_lighting_pass.cpp`.
- `engine/runtime/render/passes/temporal_aa_pass.cpp`.
- `engine/runtime/graphics/backend/vulkan/vulkan_backend.cpp`.
- `engine/example/graphics/rhi_example.cpp`.
- `engine/test/support/fake_render_backend.h`.
- `engine/test/unit/render/render_system_test.cpp`.
- `tools/validation/r7/taa_fixture.json`, `tools/validation/r7/taa_validation.py`.
- `docs/render/.plan/R7.md`, `docs/render/.review/R7.3.md`,
  `docs/render/TODO.md`, `docs/status.md`, `docs/command/built_in_commands.md`.
- `.spec/specs/render-r7-raster-image-quality.md` and this journal.

### Correctness and quality validation

Validation level: **Level 4 plus visible cross-backend runtime acceptance**,
because the existing R7.3 working tree spans shared shader/RHI/history contracts.
Builds were serial. Visual launches and localhost command clients ran through
the approved outside-sandbox path. Debug Vulkan validation was left enabled.
The owned `GLFW30` windows were verified visible and foreground on `Default`.

Relevant commands actually run (logs retain intermediate attempts):

```powershell
cmake --build build --config Debug
cmake --build build --config Debug --target KimPeanutEngine RenderSystemTest RenderGraphTest RenderPassScheduleTest GraphicsContractTest
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Debug -L 'render|graphics' --output-on-failure
build/engine/test/unit/render/Debug/RenderSystemTest.exe
build/engine/example/graphics/Debug/GraphicsSmoke.exe
cmake --build build --config RelWithDebInfo --target KimPeanutEngine
python tools/validation/r7/taa_validation.py --port 37522 --pid 16088 --output save/diagnostics/r7_3-20261022/mip-vulkan.json --prefix r7_3-20261022-mip-vulkan --quality
python tools/validation/r7/taa_validation.py --port 37522 --pid 18276 --output save/diagnostics/r7_3-20261022/final-opengl.json --prefix r7_3-20261022-final-opengl --quality
python tools/validation/r7/taa_validation.py --port 37522 --pid 18276 --output save/diagnostics/r7_3-20261022/final-opengl-lifecycle.json --prefix r7_3-20261022-final-opengl-lifecycle
python tools/validation/r7/taa_validation.py --port 37522 --pid 28064 --output save/diagnostics/r7_3-20261022/final-vulkan-Debug-lifecycle.json --prefix r7_3-20261022-final-vulkan-Debug-lifecycle
python tools/validation/r7/taa_validation.py --port 37522 --pid 16300 --output save/diagnostics/r7_3-20261022/final-vulkan-performance.json --prefix r7_3-20261022-final-vulkan-performance --performance
```

The initial sandbox client hit WinError 10013; the approved external client
worked. Initial full-suite sandbox failures included access-denied fixture
publication and localhost tests. The external 1147-case run had 14 failures.
Five Render failures were caused by test callers skipping the new pre-frame
preparation or asserting the old lighting-only failure phase; the callers were
corrected, the 55-case direct RenderSystem run passed. With the final pre-acquisition
allocation-failure/recovery regression added, all 251 Render/Graphics CTest
cases pass. Rechecking the 14 failures leaves nine unrelated failures:

- `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures`.
- `AssetCatalogProviderTest.MissingArchivePublishesAValidPartialLiveSnapshot`.
- `AssetCatalogProviderTest.UnreadableArchiveFileIsReportedWithoutThrowing`.
- `AssetCatalogProviderTest.LiveAssetsAreCopiedWithoutPayloadDataCrossingTheContract`.
- `AssetCatalogProviderTest.DestroyingTheProviderLeavesTheManagerUsable`.
- `NativeModelLoaderTest.ConcurrentLoadsShareModelAndUnloadOwnedMesh`.
- `NativeModelRuntimeIntegrationTest.LoadsMultiMaterialGraphThroughAssetManager`.
- `AssetImportRegistryTest.ExecuteReturnsProviderIdentityAndLateRegistrationIsRejected`.
- `TerrainMeshTest.CoarseAndFineSamplingShareWorldPositionsAndGlobalNormals`.

These include archive/fixture identity/layout expectations and a terrain index
count mismatch (96 versus expected 24); they are not reported as passed. No
unrelated Asset/Terrain implementation or tests were modified. The first smoke
attempt failed with stale shader descriptors/attachment count and a dark rock;
exception teardown also produced VUIDs. The updated smoke passes six frames on
each API with **no Vulkan VUIDs**. Its intentional invalid-geometry negative
probes still log rejected handles. One intermediate smoke build attempted to
call private `BuildDesc`; the final fixture instead borrows the public owned
GBuffer view, without exposing a new API.

Quality fixture: canonical textured `level/sponza.level`; camera position
`[10.80743, 1.59222, 0]`, rotation `[0, 180, 0]`; eight frozen subpixel strafe
poses, 16 settling frames, native 1094×631 and 2188×1262 reference reduced
offline in linear display color. Full tracked residency is required. SSAO and
ray-query shadows are disabled only for isolated quality comparisons and are
restored for combined acceptance. Exposure/lights/materials are fixed.

| Metric | Vulkan off | Vulkan on | OpenGL off | OpenGL on |
| --- | --- | --- | --- | --- |
| Static MAE | 0.01828484 | 0.01311880 | 0.01824616 | 0.01283605 |
| Pose-sequence error shimmer | 0.02064201 | 0.01333151 | 0.02060229 | 0.01332609 |
| Edge-gradient error | 0.04535845 | 0.02983643 | 0.04525672 | 0.02985866 |

All gates pass: shimmer improves 35.42% / 35.32% (required ≥20%), with lower
static and edge-gradient errors. Both on/off composited windows are upright;
the earlier OpenGL report does not reproduce. Rigid Ivy move/return captures
show no full-scene trails. Cut, resize, minimize/restore, reload, disabled
bypass, and history recovery pass. Vulkan PT excludes TAA/jitter, and toggling
TAA during active PT preserves its increasing accumulated samples.

### Performance, memory, and provenance

Revision/state: `699ac30` plus the uncommitted working tree, with source hashes
in the result files and supplementary final provenance. Rebuilt
RelWithDebInfo Vulkan, normal configuration validation state; correctness uses
separate Debug validation-on runs. RTX 4070 Laptop GPU, driver 617.42.
Canonical fixture/camera as above, viewport 1094×631, PT inactive, hybrid
ray-query shadows active, Medium SSAO, full texture residency. No build, test,
or other owned engine ran during sampling. Each independent window resets the
profiler and completes 120 warm-up plus 300 sample frames.

| Window | TAA p50 / p95 ms | Total GPU p50 / p95 ms |
| --- | --- | --- |
| 1 | 0.096256 / 0.099328 | 3.152480 / 3.215283 |
| 2 | 0.099328 / 0.102400 | 3.306976 / 3.339662 |
| 3 | 0.099328 / 0.101376 | 3.291152 / 3.325155 |

All windows pass 1.0 ms TAA / 8.5 ms combined p95 ceilings. These are current
budget checks, not a matched speedup claim against historical runs. GPU
telemetry before startup was idle (48°C, 210 MHz graphics/405 MHz memory);
after sampling it was 58°C, 1875/8001 MHz. Per-frame clocks were not measured.

Active R7 logical payload is 31,754,444 bytes (30.28 MiB). Measured Vulkan
allocator ranges total 32,440,320 bytes (30.94 MiB) in all three windows,
including both histories, motion/identity GBuffer additions, and AO targets.
This is allocation-range accounting, not total device heap reservation or total
engine VRAM. OpenGL native allocation measurement remains unavailable and is
left absent. The earlier 43.5 MiB estimate describes obsolete attachments.

### Evidence index and completion limits

- Results: `save/diagnostics/r7_3-20261022/mip-vulkan.json`,
  `final-opengl.json`, `final-opengl-lifecycle.json`,
  `final-vulkan-Debug-lifecycle.json`, `final-vulkan-performance.json`,
  `final-provenance.json`, and `final-visible.json`.
- Captures under `save/screenshots/validation/`: prefixes
  `r7_3-20261022-mip-vulkan-*`, `r7_3-20261022-final-opengl-*`,
  `r7_3-20261022-final-opengl-lifecycle-*`, and
  `r7_3-20261022-final-vulkan-performance-*`. Each includes semantic and
  composited-window PNGs; quality runs retain all eight off/on/reference poses.
- Build/test logs under `save/logs/`: `r7_3-20261022-final-full-debug-build.log`,
  `final-debug-build.log`, `final-rel-v2-build.log`, `final-focused-v3-tests.log`,
  `full-tests-external.log`, `full-failure-recheck.log`,
  `graphics-smoke.log` (first failure), `graphics-smoke-v2.log` (passing).
  All names after the first share the `r7_3-20261022-` prefix.
- Runtime diagnostics: `save/diagnostics/r7_3-20261022/final-vulkan-Debug.stdout.log`,
  `performance-vulkan-RelWithDebInfo.stdout.log`, `performance-before.csv`,
  `performance-after.csv`. Debug Vulkan closes with zero AS/pipelines/bindings/
  address tables/temporary build batches. Captures from other tasks are retained.

R7.3 is accepted within the frozen native-resolution fixture and supported
rigid-motion contract. The pose-sequence metric is not a guarantee for every
scene/deforming surface. OpenGL allocator bytes, the nine unrelated broad-suite
failures, and R5/R6/R7.1/R7.4/R7.5 acceptance are not claimed complete. No
required R7.3 gate was skipped. The final RelWithDebInfo Vulkan editor remains
visible on `Default` for user inspection, with raster TAA and query shadows active.
