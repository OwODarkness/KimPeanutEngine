# 2026-09-27 — R4.7.2 runtime failure correction

## Investigation

User reported Cornell access violation `-1073741819`, initial zero RT records,
Sponza upload-queue device loss and unavailable Beauty/performance acceptance.
Reviewed HEAD `3fd9108` plus the existing uncommitted implementation. Applied
C++ coding, concise-comment and modular-documentation guidance. Read the
supplied `...11.40.24.txt` log, RT shaders/SBT builder, frame teardown and
prior progress records. No sub-agents, commits or broad refactor were used.

CDB from Windows Kits reproduced the pre-fix Cornell crash using the existing
RelWithDebInfo executable. Default symbol-server lookup stalled, so repeated
with local executable/PDB search. The captured stack shows a null command
recorder in `VulkanBackend::EndFrame`, re-entered by cleanup after an earlier
frame exception. Dump creation failed due to debugger path parsing; the stack
is retained in `save/diagnostics/r472/cornell-before-local-stack.txt`.

Separately, source and the SBT log establish a 128-byte miss stride despite
64-byte record packing. Miss index 1 selected the hit record rather than the
visibility miss. Checked the primary
[Vulkan miss addressing specification](https://docs.vulkan.org/spec/latest/chapters/raytracing.html).
Corrected stride/size via a checked backend-local region layout and regression
test; marked frame consumption before recorder destruction so cleanup cannot
re-enter submission. Kept earlier shader, fallback and queue work unchanged.

## Runtime checks

Initial corrected Debug build passed. Cornell Debug Vulkan reached active PT
with 8 geometry / 1 instance, 4 SPP / 8 bounces. Runtime capture exported a
nonblack Beauty image with indirect color bleed. Stats saved at frame 3654
show 14,596 samples. Log: `...11.46.27.txt`.

An attempted window-close took longer than expected; an overlapping preliminary
Sponza launch was aborted and excluded. The debugger attach command used to
inspect Cornell close incorrectly combined `.sympath` and subsequent commands;
it supplied no usable shutdown stack. No graceful shutdown result is claimed.

A separate Debug Sponza run, PID 13044, reached active PT with 450 geometry /
3 instances. Early stats/capture had incomplete texture residency. Later
Runtime stats show residency complete, frame 25,587 and 2,656 samples. Both
captures are retained; the later Beauty export was visually inspected. Log:
`...11.51.26.txt`. No matched validation/invalid-handle/device-loss errors were
found in the inspected corrected logs. CDB briefly attached during startup
and detached; this is correctness evidence, not a performance run. Both
fixtures used 1094×619 and authored cameras. Owned processes were stopped
after capture collection when `CloseMainWindow` did not terminate them.

Final-code build/test and documentation check results follow below. The
[formal review](../../docs/render/.review/R4.7.2.md) owns findings and limits;
the [R4.7 plan](../../docs/render/.plan/R4.7.md) remains the design source.

## Final validation

- `./tools/kp.ps1 -Configuration Debug build KimPeanutEngine`: passed after
  final checked-region helper and teardown changes.
- `./tools/kp.ps1 -Configuration Debug build GraphicsContractTest`: passed.
- `./tools/kp.ps1 -Configuration Debug test GraphicsContractTest`: CTest
  selected no tests. Ran
  `build/engine/test/unit/graphics/Debug/GraphicsContractTest.exe` directly:
  **30/30 passed**, including the new visibility-record regression.
- `./tools/kp.ps1 -Configuration RelWithDebInfo build KimPeanutEngine`: passed.
  Builds were serial; no compilation/tests overlapped performance sampling.

Corrected RelWithDebInfo Vulkan Sponza, PID 8732, then completed a fully
resident 120-warmup/300-sample window. Validation disabled; RTX 4070 Laptop,
mailbox, 1094×619, authored camera `(10.80743, 1.59222, 0)`, 4 SPP / 8 bounces,
PT active and ray-query shadows inactive. Environment intensity remains zero.
PT GPU p50/p95 **24.971408/26.610917 ms**; total GPU p50 **29.036896 ms**;
CPU elapsed p50 **28.88545 ms**. At inspection frame 21,476 the sample counter
was 3,772. Stats saved to `save/diagnostics/r472/sponza-rel-stats.json`.
Relative to the previously recorded 34.548 ms baseline median, this one-window
candidate is about **27.7% lower PT GPU p50**. Do not attribute this combined
working-tree result exclusively to primary reuse or treat it as repeated
performance acceptance.

Exported/polled/inspected
`save/screenshots/validation/r472-fix-rel-sponza-beauty.png`. Log
`save/logs/2026-09-27/KimPeanutEngineLog-2026.09.27-11.58.42.txt` shows miss
stride/size 64/128 and no inspected validation/device-loss/error matches.
Stopped this owned process after collection; no engines remain. No graceful
shutdown or forced device-fault recovery acceptance is claimed.

## RelWithDebInfo repeat window — 2026-09-27

Rebuilt the selected `RelWithDebInfo` configuration before measuring, then
launched PID 24084 with `--graphics-api vulkan --startup-level
level/sponza.level --agent-port 37373`. No build or test overlapped the sample;
no debugger was attached and the camera was not moved. The machine reported an
RTX 4070 Laptop GPU. Runtime stats confirmed Vulkan, PT active, ray-query
shadows inactive, 450 geometry/material records, 3 instances, one light,
1094×619, the authored camera `(10.80743, 1.59222, 0)`, 4 SPP, 8 bounces,
complete texture residency, 120 warm-up frames, and 300 collected samples.
Validation is disabled in this performance configuration.

The repeated window measured PT GPU p50/p95 **24.536640/27.120742 ms**,
total GPU p50/p95 **28.609024/31.544406 ms**, CPU total p50/p95
**28.665650/31.802480 ms**, and fence-wait p50 **24.987150 ms**. The first
corrected window measured PT p50 24.971408 ms. The two-window median is
24.7535 ms, 28.3% below the prior three-window baseline median of 34.548 ms;
two candidate windows still do not establish a stable repeated result.

Stats are saved at
`save/diagnostics/r472/sponza-rel-stats-repeat-2026-09-27.json`; the run log is
`save/logs/2026-09-27/KimPeanutEngineLog-2026.09.27-12.04.12.txt`. The log
reports miss stride/size 64/128 and contains no error, validation, or
device-loss matches. PID 24084 was still open after collection for the user's
inspection. The focused review records the aggregate and remaining gates.

## Changed files and boundary

- Vulkan `vulkan_acceleration_structure_owner.cpp`,
  `vulkan_ray_tracing_validation.h`: checked SBT region layout.
- Vulkan `vulkan_backend.cpp`: consume the frame before recorder release.
- `engine/test/unit/graphics/graphics_contract_test.cpp`: SBT index/bounds regression.
- Render `.review/R4.7.2.md`, parent `.review/R4.7.md`, `.plan/R4.7.md`,
  `TODO.md`, project `docs/status.md`, prior R4.7 journal and this journal:
  diagnosis, current stage state and evidence links.

No Render/RHI ownership or public common API changed. Existing uncommitted
shader, fallback, queue and other source changes were preserved. Performance
repeats, equal-sample quality equivalence, remaining probes/transforms/occlusion
and full lifecycle coverage remain open. This task resolves the reproduced
runtime blocker; it does not complete all R4.7.2 acceptance.

Final documentation validation: six local links in the new focused review and
journal resolve; `git diff --check` exits 0. Reviewed the source diff for
ownership/API changes and confirmed no engine/debugger processes remain.
The TODO's R4.7.2 historical journal anchor was corrected to its actual heading.
