# 2026-09-27 — R4.7 path-tracing performance review and plan

## Request and baseline

The user requested code review and an R4.7 optimization plan/TODO after the
matched RT-enabled Sponza comparison. Scope remained documentation only.
Reviewed HEAD `07ce893` plus existing uncommitted changes; preserved source,
build configuration, logs and earlier review records. The existing
[performance journal](2026-09-26-sponza-frame-performance.md) and
[matched R4.6 evidence](../../docs/render/.review/R4.6.md#matched-rt-enabled-debug-and-relwithdebinfo-runs--2026-09-27)
provided measurements. No new launch or performance run occurred.

## Investigation performed

Read repository/module contracts, status, Render plans/TODO, validation matrix,
completion evidence, local reference index and gkNext study. Applied modular
documentation, reference-driven engineering, engine-reference and C++ review
guidance. Inspected RT preparation/recording, shaders, common pipeline/binding
contracts, Vulkan descriptor/table allocation and retirement, AS build flags,
frame fence waiting and profile-window aggregation.

Confirmed repeated full-material visibility hits, four identical primary
traces, absent static trace-quality build preference, per-dispatch native
binding/table allocation and CPU scene extraction. Also recorded raster shadow
preparation and smaller hit-shader/profiling candidates. Static AS reuse and
Russian roulette already exist. CPU frame elapsed includes backend fence wait;
the measured GPU PT pass remains the principal demonstrated limit.

## Reference discovery and adaptation

GitHub MCP was unavailable, blocking the connector-specific discovery gate.
Web searches discovered NVIDIA's Vulkan tutorial and the indexed gkNext
repository. Some GitHub tree pages failed or exposed no source. Direct raw
NVIDIA `v2` reads succeeded for
[shader](https://raw.githubusercontent.com/nvpro-samples/vk_raytracing_tutorial_KHR/v2/raytrace_tutorial/05_shadow_miss/shaders/rtshadowmiss.slang)
and [pipeline source](https://raw.githubusercontent.com/nvpro-samples/vk_raytracing_tutorial_KHR/v2/raytrace_tutorial/05_shadow_miss/05_shadow_miss.cpp).
These support the separate visibility miss/SBT protocol; no code was copied.
The common shadow include was not retrieved; flags were checked against the
[maintainer tutorial](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/samples/05-shadow-miss/).
No newly verified gkNext source behavior or pinned external commit is claimed.

Read [NVIDIA RTX guidance](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/)
and [Khronos descriptor management](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html).
Adopt narrow traversal/shader recommendations and in-flight descriptor/buffer
reuse through existing ownership. Do not transfer external timing gains,
Vulkan-only application ownership or nested-recursion settings to this engine.

## Documentation produced

- [R4.7 review](../../docs/render/.review/R4.7.md): eight open source findings,
  priority, file/line evidence, existing optimizations and research limitations.
- [R4.7 plan](../../docs/render/.plan/R4.7.md): staged GPU/CPU design, ownership,
  invalidation/lifetime rules, optional quality budget and stop gates.
- [Acceptance spec](../specs/render-r4-7-path-tracing-performance.md): future
  matched measurements, runtime/visual checks and resource-lifetime coverage.
- Render architecture map/TODO and project status link the proposed stage.

## Validation and remaining work

Level 0 documentation validation completed:

- PowerShell local-link/heading-anchor check: **41 R4.7 links passed**, including
  inbound plan/review/spec links. New Markdown whitespace/conflict-marker
  checks passed.
- `git diff --check`: **exit 0** under the repository's normal line-ending
  configuration. A diagnostic override disabling `core.autocrlf` produced
  CRLF whitespace noise and was discarded; no line-ending cleanup was made.
- Inspected the new documents and architecture/TODO/status diffs. A broader
  link scan found two existing AP1.1/AP1.2 status anchors lacking their heading's
  double hyphen; those unrelated links were left unchanged.

Builds, unit tests, runtime captures,
shader compilation and benchmarks are intentionally not run in this review.
All speedups, CPU attribution per candidate and future visual/lifetime gates
remain unverified. Implementation and acceptance are not complete.

## R4.7.0 instrumentation start — 2026-09-27

The implementation baseline starts from HEAD `07ce893` with the pre-existing
working-tree edits shown by `git status --short`; those edits were preserved.
The R4.6 matched RelWithDebInfo Sponza data above is historical context, not a
fresh R4.7.0 baseline. No path-tracing shader or scene policy was changed.

`RenderProfileWindow` now caches its computed summary and invalidates it only
when warm-up/sample state changes. The Runtime profile reports the number of
fresh GPU pass timings and total-GPU p50/p95 over sampled snapshots. Vulkan
reports the CPU wall duration of its frame-slot fence wait, swapchain image
acquire call, and queue-present call, with p50/p95 wait/call values in the
existing command interface. These timings are separate observations inside
the existing frame total; callers must not add them to already inclusive
record/total timings. The present metric is CPU time inside `vkQueuePresentKHR`,
not a measurement of display scanout completion.

Validation: Debug and RelWithDebInfo engine builds both completed. The
RelWithDebInfo build emitted the existing `LNK4098` default-library conflict
warning. A first RelWithDebInfo Sponza launch loaded 127 assets in 4.200 s but
failed while waiting for the Vulkan upload queue, before a usable profile
window. The failure preceded the new `VkResult` diagnostic and therefore its
numeric result was not captured. An instrumented Debug retry and three
subsequent serial RelWithDebInfo runs did not reproduce it. The retry also
showed why process isolation matters: a Debug process I had asked Windows to
close remained alive and kept port 37373 bound; it was stopped before the
optimized windows. No queue failure or Vulkan validation error was found in the
successful logs. The first-frame “path tracing inactive” warning is transient:
the live stats later confirmed the scene had become PT-active. The precise
cause of the one-off upload wait failure remains unknown; the new error path
logs `VkResult` if it recurs.

### R4.7.0 Vulkan Sponza baseline and queue retry — 2026-09-27

Runs used the current working tree at HEAD `07ce893`, Vulkan, the checked-in
`level/sponza.level`, authored `main_camera` at `(10.80743, 1.59222, 0)` with
rotation `(0, 180, 0)`, 1094×619, mailbox presentation, and no camera movement.
Each successful optimized run reported 127 loaded assets, RT/PT enabled and
active, 120 warm-up frames and 300 samples. Actual-query counts below are the
latest returned completed pass-timing records; mode and sample-window state
were queried from Runtime stats rather than inferred from launch arguments.
The three windows were run serially on the RTX 4070 Laptop GPU, RelWithDebInfo
(`NDEBUG`, Vulkan validation off):

| Window | CPU total p50 / p95 ms | PT GPU p50 / p95 ms | Total GPU p50 / p95 ms | Fence wait p50 / p95 ms | Acquire p50 ms | Queue-present call p50 / p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 37.411 / 40.591 | 33.318 / 36.147 | 37.389 / 40.402 | 33.026 / 35.869 | 0.0047 | 0.249 / 0.498 |
| 2 | 38.513 / 43.275 | 34.486 / 38.728 | 38.664 / 43.168 | 33.969 / 38.489 | 0.0046 | 0.250 / 0.518 |
| 3 | 37.323 / 40.245 | 33.215 / 35.637 | 37.309 / 40.263 | 32.664 / 35.507 | 0.0050 | 0.245 / 0.502 |

The median of the three PT p50 values is **33.318 ms**; their observed range
is 33.215–34.486 ms. CPU total p50 is close to total GPU time, and its fence
wait p50 is 32.664–33.969 ms. This attributes most of the observed CPU-frame
wall time to waiting for the in-flight GPU frame, rather than CPU recording.
Fence/acquire/present values are nested wall-time observations within frame
timings, so do not add them to inclusive CPU phase totals. `vkQueuePresentKHR`
duration is CPU time in the call, not display completion. Total GPU is the sum
of available fresh pass queries for a sampled frame; the fresh query count is
exposed separately because conditional passes can be absent.

One Debug Vulkan validation run completed 120 warm-up and 300 sample frames,
reported PT active, and exported a 512×289 scene capture. Its CPU total p50/p95
was 21.446/41.175 ms; fence wait p50/p95 was 0.119/0.204 ms; PT GPU p50/p95
was 14.987/17.106 ms. These Debug measurements are correctness diagnostics,
not the performance baseline. Its log and the three optimized logs are:
`save/logs/2026-09-27/KimPeanutEngineLog-2026.09.27-09.01.02.txt`,
`...09.05.46.txt`, `...09.08.21.txt`, and `...09.10.23.txt`. No Vulkan
validation errors or upload-queue failures appear in these successful logs.
The Debug and first optimized visual captures are
`save/screenshots/validation/r47-debug-vulkan-sponza.png` and
`save/screenshots/validation/r47-relwithdebinfo-vulkan-sponza.png`.

Both configurations rebuilt successfully through `tools/kp.ps1`; no tests
were run. At this intermediate point R4.7.0 still needed RT
binding/pool/table allocation and upload bytes, build/retire counters,
scene/camera/light/environment/history metadata and texture-residency
completion. Those gaps and the later fully resident baseline are recorded
below; the isolated earlier upload failure is not considered root-caused.

## R4.7.0 resource attribution completion — 2026-09-27

Added owner-side per-frame counters for Vulkan RT descriptor pools/sets,
address-table buffer allocations and upload bytes, recorded BLAS/TLAS
builds/updates, and completed retirement of acceleration structures,
descriptor sets and temporary-buffer batches. The owner counters reset at the
frame boundary before fence retirement collection so that completed retirement
work remains visible in that frame's stats. Render reports scene-table records
written, geometry/instance/material/light records, camera, graph selection,
history reset, SPP/bounces, environment settings, and tracked texture residency.
The current scene-table path rewrites the full table, so `records_written` is
a truthful count but is not a value-diff dirty-record counter. No always-on
per-ray atomics were added.

Tracked texture residency excludes resources with no full-resolution streaming
loader. Its `incomplete_count` identifies supported streamed resources whose
full-resolution data is not active; the current texture API reports a null
result both while loading and when the asynchronous loader failed, so this
counter does not distinguish those outcomes.

Both `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` and
`tools/kp.ps1 -Configuration RelWithDebInfo build KimPeanutEngine` completed
after the final code changes. The RelWithDebInfo link emitted `LNK4098` for
`MSVCRTD`; the executable linked successfully. Debug Vulkan validation runtime
stats reported path tracing active, the expected 450 geometry/material, 3
instance and 1 light records, and complete tracked texture residency after
warm-up. No validation/error/upload-queue matches appeared in the closed
Debug log `save/logs/2026-09-27/KimPeanutEngineLog-2026.09.27-09.43.00.txt`.

A fully resident RelWithDebInfo Vulkan Sponza window (RTX 4070 Laptop,
`main_camera` at `(10.80743, 1.59222, 0)`, 1094×619, mailbox, 4 SPP, 8 bounces,
no movement, 120 warm-up + 300 samples, RT/PT active, tracked residency
complete) reported PT GPU p50/p95 33.852/37.229 ms, total GPU 37.942/41.456
ms, CPU total p50 37.928 ms and fence-wait p50 33.605 ms. Its per-frame owner
sample showed 904 scene-table records written, 65,536 upload bytes, one
descriptor set created/retired and one table buffer created, with zero AS
builds in steady state. The graph label was `capture` while `path_trace_active`
was true; consumers should use the explicit active-mode field, not infer PT
state from the graph label alone.

At that point, this fully resident window was not merged into the earlier
three-window median because those runs did not record residency state. The
following matched windows supersede that provisional baseline. The final
RelWithDebInfo scene capture was exported and inspected at
`save/screenshots/validation/r47-relwithdebinfo-vulkan-sponza-final.png`; it
matches the prior Sponza look. The optimized Vulkan Sponza engine remains open.

R4.7.0 profile attribution is implemented, closing review finding F8. At the
time this section was first written, the performance-baseline acceptance gate
was open pending two more fully resident windows; the following section records
those windows and closes that gate. The dedicated visibility-ray change in F1
is the next code optimization.

### Fully resident repeat windows

After confirming that each newly launched PID owned the loopback stats port,
three separate RelWithDebInfo Vulkan processes completed 120 warm-up and 300
sample frames with Sponza PT active and `textures_tracked_residency_complete`
true. All used `level/sponza.level`, `main_camera` `(10.80743, 1.59222, 0)`,
1094×619, mailbox, 4 SPP, 8 continuation bounces, and no movement. The GPU was
an RTX 4070 Laptop; validation is disabled in this performance configuration.

| Window | PT GPU p50 / p95 ms | Total GPU p50 / p95 ms | CPU total p50 ms | Fence wait p50 ms |
| --- | ---: | ---: | ---: | ---: |
| 1 | 33.852 / 37.229 | 37.942 / 41.456 | 37.928 | 33.605 |
| 2 | 34.548 / 37.225 | 38.627 / 41.462 | 38.467 | 33.889 |
| 3 | 34.729 / 47.155 | 38.896 / 51.937 | 38.932 | 34.290 |

The PT p50 median is **34.548 ms** (range 33.852–34.729); the p95 median is
37.229 ms (range 37.225–47.155). The 20% unchanged-quality p50 target is now
about **27.64 ms**. The third window has a clear p95 tail outlier; retain it in
the baseline rather than filtering it. The new telemetry reports capture graph
mode alongside PT-active, 450 geometry/material records, 3 instance records,
one light, 904 scene-table record writes, 65,536 address-table upload bytes,
one descriptor set and one address-table buffer created per sampled frame,
and zero steady-state AS builds. Table writes are not value-diff dirty counts.

The three optimized logs are
`save/logs/2026-09-27/KimPeanutEngineLog-2026.09.27-09.45.56.txt`,
`...09.50.55.txt`, and `...09.51.26.txt`; none contains a Vulkan validation,
error or upload-queue failure match. The Debug validation smoke log is
`...09.43.00.txt`, also without those matches. No tests were run. Both engine
builds passed after final changes; RelWithDebInfo retained the existing
`LNK4098` link warning. `R4.7.0` attribution and the residency-matched baseline
gate are complete; proceed with the planned F1 visibility-ray experiment.

## R4.7.1 — dedicated opaque visibility rays (2026-09-27)

**Change.** Added the optional API-neutral visibility-miss shader pointer to
`RayTracingPipelineDesc`, plus a distinct shader-program stage so the normal
surface miss shader remains independently addressable. Vulkan now builds one
or two miss groups and sizes/aligns the SBT miss region accordingly. The path
tracer has a scalar visibility payload; direct-light rays use opaque,
terminate-on-first-hit, and skip-closest-hit flags. The payload starts
occluded and only `ray_tracing_visibility.rmiss` writes visible on a miss.
Point/spot maximum distance, directional interval, and surface-origin bias are
unchanged. The new shader is explicitly allowed by the asset `.gitignore`.

**Validation.** `./tools/kp.ps1 -Configuration Debug build KimPeanutEngine`
passed. In Debug Vulkan, Sponza shader compilation and pipeline creation
succeeded and the SBT log showed a 128-byte miss region (two 64-byte records)
followed by the hit region; that run later failed with `VK_ERROR_DEVICE_LOST`
from upload-queue work before path-tracing scene records were available. A
separate Cornell run reached active PT in Runtime stats with 8 geometry
records, 1 instance, 8 material records, 1 light, and 9,200 samples. Its
`base_color` capture shows the scene, but `scene_color` capture was black; the
hidden `engine_window` capture was blank. That log also contains repeated
TextureManager invalid-handle and SamplerManager out-of-range errors. Therefore
the runtime proves shader load, pipeline/SBT construction, and dispatch
activity, but does not prove blocked/unblocked visibility or final PT image
quality. No tests were added or run.

**Status.** F1 is implemented but its visual/occlusion gate remains open. The
cause of the blank PT capture and runtime resource errors is undetermined; do
not attribute it to the visibility shader without a controlled capture.

## R4.7.2 — primary ray reuse and hit-shader reductions (2026-09-27)

**Implementation.** Ray generation now constructs and traces the fixed
pixel-center primary ray once per invocation, before the dispatch sample loop.
The deterministic primary emission/direct-light term is evaluated once and
reused by each independent continuation sample; each continuation retains its
own sample index for BSDF and roulette RNG. Primary misses and all diagnostic
probe modes still flow through the same progressive history average. The
unused `geometry` and `material` payload members were removed from raygen,
closest-hit and miss declarations and writes. Closest-hit now transforms
normals with the provided world-to-object matrix, transposed, and shares an
LOD-0 texture fetch only when metallic and roughness slots reference the same
texture; distinct slots keep independent fetches and channel selection.

For the fixed 1094×619 view, static source now has 677,186 primary traces per
dispatch instead of 2,708,744 (a reduction of 2,031,558 primary traces). This
is a ray-count result, not a GPU-time prediction. The primary direct/emissive
term is repeated once per continuation sample before division by SPP, so its
averaged weight is unchanged; secondary RNG still includes the per-sample
index, and the continuation loop keeps the same number of traced bounces as the
prior inclusive `0..maximum_bounces` loop.

**Validation.** Debug build passed before the final hit-shader reductions;
RelWithDebInfo build passed with all changes. A live RelWithDebInfo Vulkan
Cornell process loaded the changed closest-hit shader, created the two-entry
miss SBT, and reported path tracing active at 1094×619, 4 samples/dispatch,
8 bounces, complete tracked texture residency, and advancing samples. The
`scene_color` and `primary_albedo` exports are black while `base_color` is
visible, matching the pre-existing R4.7.1 capture defect. Cornell does not
produce the fixed profile window because its texture dependency count is zero;
its current path-trace GPU query is also reported as 0 ms. The matched Sponza
RelWithDebInfo run compiled/cached the modified closest-hit shader and created
the RT pipeline/SBT, but the Vulkan device was lost during upload-queue work
before scene records became available. No matched post-change performance
window could be collected.

**Repeated-log diagnosis.** When a level has no authored environment,
`UpdateEnvironment` clears `active_environment_`. The path-tracing binding set
still unconditionally binds its panorama at set 0/binding 4, while the black
fallback was only ensured in deferred-lighting preparation. Pure PT bypasses
that preparation and therefore submits default texture and sampler handles to
descriptor creation every frame. The path-tracing pass now ensures the cached
black environment fallback before creating its binding set. This addresses the
specific repeated invalid-handle lookup path. Short monitored Debug Cornell
and RelWithDebInfo Sponza launches after the fix produced zero texture- or
sampler-handle errors. The Cornell process exited before Runtime stats were
available; Sponza still hit `VK_ERROR_DEVICE_LOST` in upload-queue work seven
seconds after startup. No further Runtime was launched. This does not establish
the cause of the separate Sponza device loss. No tests were run.

**Upload synchronization follow-up.** Static review found that uploads may run
on asset worker threads while frame submission/presentation uses Vulkan queues,
and both upload helpers share command pools. Vulkan requires host external
synchronization for queue operations and command-pool allocation/free. A
device-owned mutex now serializes upload queue submit/wait, frame submit/present,
device-idle calls, and transient upload command-pool allocate/free operations.
The Debug engine build passes with the change. This is a concrete race
candidate for the startup device loss, but no runtime confirmation has been
collected. Keep the device-loss and image/performance gates open. No tests were
run. A guarded RelWithDebInfo Sponza launch exited with code 1 before producing
a new engine log; no process remains and the attempt yielded no runtime evidence.

**Status.** Source-level primary reuse and the three small hit-shader
reductions are implemented. The unchanged-quality image and performance gates
remain open: shader register pressure has not been measured, Sponza could not
reach its sampling window, and Cornell PT captures are black. See the [R4.7.2
runtime correction](../../docs/render/.review/R4.7.2.md).

### Subsequent runtime correction

The [focused investigation](2026-09-27-r472-runtime-failure-correction.md)
identified an incorrect two-miss record stride and a secondary null-recorder
crash during frame-exception cleanup. Corrected Debug Cornell and Sponza now
export valid Beauty captures; optimization acceptance remains open. This
supersedes the earlier unresolved-crash/black-image observations for the fixed
build without erasing historical run evidence.
