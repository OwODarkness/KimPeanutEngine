# R4.7.8 GPU attribution — 2026-09-27

## Objective

Expose the existing engine-side GPU workload and timing context in the
Performance Profiler, then determine whether local tooling can collect the
hardware counters needed to choose the next path-tracing optimization.

## Implementation

Added a collapsed Advanced GPU section and extended profiler clipboard exports
with GPU timing freshness/percentiles, optional utilization, render graph and
PT settings, RT record counts, AS storage/build/update counts, scene-table
packing/upload/cache counts, and texture residency. Values come from the current
render profile and backend metrics. Unavailable utilization remains `N/A`.

## Validation

- `tools/kp.ps1 build KimPeanutEngine` — passed, Debug.
- `git diff --check` — passed.
- Hardware-counter tooling inspection found `nvidia-smi`, Nsight Compute
  2022.3, Nsight Systems 2022.4, and Nsight Graphics 2026.3.1.
- `nvidia-smi` reported a system-wide snapshot only; it was not used as
  per-process or render-pass attribution.
- Rebuilt and launched `build/RelWithDebInfo/KimPeanutEngine.exe` with Vulkan,
  `level/sponza.level`, and agent port 37373. Runtime `stats` reported Vulkan,
  RT/PT active, 1094×619, 4 samples/dispatch, eight bounces, and complete
  tracked texture residency (469,888,580 resident bytes; 81 dependencies).
- Opened Nsight GPU Trace against the running engine with 60-frame warm-up,
  three-frame limit, Ada Throughput Metrics, multi-pass metrics, and real-time
  shader profiling. No exported trace appeared while the Nsight window stayed
  open. This environment's CUA surface exposes browser tabs only, so it cannot
  operate the native Nsight UI to inspect or finish the session.

No engine run, visual capture, performance sample, runtime mutation, or shader
instrumentation was performed. The panel's read-only presentation does not
change the graphics/RHI ownership boundary.

## Outcome and follow-up

The profiler now gives a richer, copyable record for the external capture. The
required register/spill, occupancy, texture/cache throughput, traversal, and
ray-count evidence is still missing. R4.7.8 remains open until the native Nsight
session produces analyzable data or optional engine counters are added and
their instrumentation overhead is measured.

## Nsight CLI retry and successful trace

The local Nsight Graphics 2026.3.1 CLI was confirmed to support GPU Trace on
Ada. It launched the RelWithDebInfo Vulkan Sponza editor and established a
localhost profiling session after the firewall was closed. Collection then
failed before trace export with `GPU Performance Counters unavailable. Please
enable access to GPU performance counters.` Therefore, the earlier firewall
hypothesis is not supported: the local attach succeeded, and counter permission
was the blocker at that point. NVIDIA documents either running Nsight Graphics with
elevated privilege or enabling GPU-counter access in NVIDIA App/Control Panel.
Neither elevation nor a machine-wide permission change was performed at that
time. The user later enabled GPU-counter access in NVIDIA App and requested a
retry. The CLI then exported a report after a 12-second delay, with three
captured frames, Ada Throughput Metrics, multi-pass metrics, real-time shader
profiling, and clocks locked to base. Runtime stats separately verified the
same launch configuration as active Beauty PT at 4 SPP/eight bounces, with
1094x742 viewport and 469,885,796 resident texture bytes.

The three-frame means were 24.8% SM throughput, 34.3% RT Core, 40.8% L2 and
62.7% DRAM; DRAM reads were 61.3%, L1 texture sector hit rate 50.9%, and L2
sector hit rate 81.2%. RT Core long-scoreboard stalls averaged 6.2% of peak and
L1TEX stalls 5.0%. These counters point to memory traffic as the strongest
observed pressure but do not show saturation or prove a single bottleneck.
Ray counts, per-pass hardware attribution and per-shader register/spill totals
remain uncollected. The early, short trace with zero RT-Core counters is
superseded by the delayed shader-profiled report at
`build/NsightGPUTrace/KimPeanutEngine_2026_09_27_21_41_24.ngfx-gputrace`.

The report led to a matched check of the R4.7.10 static-BLAS
`PREFER_FAST_TRACE` candidate. The first launch attempt failed before sampling
because its command port could not bind; a retry with the normal viewport and a
fresh port worked. Three 120-warm-up/300-sample windows per variant measured
the candidate 5.2% slower for PT GPU p50 and 5.0% slower for total GPU p50 than
the no-preference baseline. The candidate was removed and the no-preference
RelWithDebInfo build passed. Exact values, missing clock/temperature telemetry
and remaining image/memory/lifetime gates are recorded in
`docs/render/.review/R4.7.10.md`; no broader acceptance claim is made.
