# Sponza frame-performance investigation

Date: 2026-09-26. Scope: review and diagnosis; no performance fix.

Reviewed HEAD `07ce893`, predecessor `9b91e16`, and the landed Stage 6 draw
state/descriptor/recorder optimizations. Normal Debug Vulkan Sponza with
`--disable-path-tracing` reproduced the user's slow frame rate. Collected
30 fresh Runtime `stats` snapshots in each successful comparison mode at
1094×619 with the same authored camera and 9,539,005 rendered triangles.

Median frame/Render CPU/G-buffer CPU times:

- Normal Debug: 36.449 / 33.697 / 19.742 ms, approximately 27 FPS.
- Temporarily validation-off Debug: 28.429 / 25.815 / 13.763 ms, about 35 FPS.
- Current RelWithDebInfo: 9.941 / 3.719 / 0.877 ms, approximately 101 FPS.

The slow sample is CPU-bound in G-buffer recording. Path tracing is inactive,
but ray-query shadows remain active. Earlier caches still report 95 material
resolutions, three descriptor allocations and zero pool creation. Full timing
tables, source locations, historical-baseline limits and prioritized findings
are in the canonical
[R4.6 frame review](../../docs/render/.review/R4.6.md#sponza-frame-performance-review--2026-09-26).
Local evidence uses `save/logs/sponza-frame-review-*` and validation captures;
generated artifacts are not intended for commits.

Normal and validation-off Debug captures succeeded and those processes closed
gracefully. A temporary RT-scene bypass failed before a steady sample with an
upload-queue wait error; exclude that experiment from performance comparisons.
The optimized sample succeeded, but its later screenshot remained pending
and close timed out, so only its owned process was terminated. No optimized
capture or teardown success is claimed.

Removed both temporary source modifications, confirmed no source diff, and
explicitly recompiled the restored device/renderer sources in normal Debug;
restored-file timestamps can otherwise make an incremental build skip them.
Current RelWithDebInfo also built successfully through
the normalized process launcher after raw MSBuild failed on duplicate PATH/Path.
The optimized link emitted the existing/default-library LNK4098 warning.
No concurrent builds or builds during sampling; no tests rerun for removed
diagnostic probes. No renderer policy, ownership, API or persistent source
change was applied. The exact historical 80–90 → 25 FPS change still requires
a matched prior-revision benchmark; current measurements prove the expensive
phase and the large Debug/performance-build difference.

## Debug-to-Debug follow-up

The user correctly challenged the earlier explanation: an optimized-versus-Debug
comparison cannot establish why an earlier Debug run was faster. Historical
status records identify OpenGL 83.6 FPS, and September 17 Vulkan runtime logs
also contain CPU p50 around 11–12 ms. Neither establishes compiler settings.

Built an isolated historical engine at `b35deba`, including the draw-state and
timer-resolution optimizations, with current Sponza fixture/assets. The ignored
export directory is named `debug-baseline-38bc` because it started from that
revision before applying the complete delta to `b35deba`. Both historical and
current generated renderer projects confirm `/Od`, `/RTC1`, `/MDd`. Used the
same MSVC 14.34 compiler, Vulkan validation and viewport/workload.

Normalized serial Debug builds passed. Parallel baseline attempts returned exit
1 without a retained source diagnostic; stopped their owned processes before
sampling and continued serially. The first baseline launch missed Assimp's DLL;
copying the current Debug DLL fixed that launch prerequisite. Baseline startup
then took 90.374 seconds, excluded from frame samples.

Forty Runtime stats snapshots per case, both restricted temporarily to confirmed
performance cores (logical 0–15), measured original/current respectively:
37.536/37.986 ms frame, 34.455/34.522 ms render CPU, 26.208/26.698 ms CPU recording,
20.260/20.136 ms G-buffer CPU, 8.519/9.256 ms total GPU. Both have 288 draws,
9,539,005 triangles and 1094×619. Current full RT-off stats confirm both RT and
ray queries disabled. These results do not reproduce a threefold code regression.

An unrestricted baseline sample varied up to 62.486 ms/frame; the subsequent
P-core probe measured 37.536 ms, with GPU clocks also changing. Forcing process
execution-speed throttling off measured 36.493 ms, not a recovery to 80–90 FPS.
Current OpenGL Debug/full RT-off measured 37.257 ms/frame. AC power and Balanced
policy were verified. Scheduling, clock and sampling conditions are confounders;
the sequential probes are not independent causal estimates.

All baseline/current Vulkan and current OpenGL registry captures succeeded and
owned processes closed gracefully. Restored temporary affinity/power overrides.
Artifacts use `save/logs/debug-regression-` and captures stay under
`save/screenshots/validation/`. No engine source, ownership or API change was
retained. Updated R4.6 review, status and roadmap; no unit tests are needed for
the retained documentation-only changes.

The user's suggestion of an earlier CPU-optimized build is consistent with the
current RelWithDebInfo result (~101 FPS) and today's similarly slow original
Debug source, but remains unverified without that binary's compiler settings.
Follow-up is to record actual build/optimization information in startup logs.

## RT-enabled current-build comparison — 2026-09-27

Rebuilt Debug and RelWithDebInfo serially using the normalized project wrapper,
then sampled current HEAD `07ce893` plus the existing uncommitted working-tree
changes. Both were Vulkan Sponza with the authored camera at
`(10.80743, 1.59222, 0)`, 1094×619, mailbox present mode, no camera movement,
and verified live `ray_tracing_enabled`, `path_tracing_enabled`, and
`path_trace_active` all true. Ray-query shadows were inactive. Each profiler
window completed 120 warm-up frames and 300 samples. Debug Vulkan validation
was enabled; RelWithDebInfo validation was disabled by `NDEBUG`.

| Metric | Debug | RelWithDebInfo |
| --- | ---: | ---: |
| CPU total p50 / p95 | 100.344 / 166.957 ms | 38.998 / 42.123 ms |
| Latest frame time | 108.080 ms | 37.539 ms |
| Latest total GPU time | 60.385 ms | 37.996 ms |
| Path-trace GPU p50 / p95 | Not retained | 34.261 / 37.187 ms |

Latest-frame FPS conversions are approximately 9.3 and 26.6 respectively, not
median FPS. The optimized path-trace pass dominates its measured GPU frame.
Debug per-pass GPU percentiles were not retained, preventing a matched pass
distribution comparison. Logs:
`save/logs/2026-09-27/KimPeanutEngineLog-2026.09.27-00.00.46.txt` and
`save/logs/2026-09-27/KimPeanutEngineLog-2026.09.27-00.04.28.txt`. A second
Debug launch failed Sponza model dependency resolution and was excluded.
