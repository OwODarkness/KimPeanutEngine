# Render R1.5 and issue-9.7 Stage 6.0 Evidence

Date: 2026-09-17
Scope: R1.5 orderly application close and issue-9.7 Stage 6.0 runtime proof
Scenario: `level/performance_profile.level` → `level/sponza.level`,
`main_camera`, 1094×619, 120 warm-up frames, 300 measured frames, NVIDIA
GeForce RTX 4070 Laptop GPU.

## Changes made for evidence

The combined Runtime `stats --json` response now preserves the complete
per-pass telemetry already present in `RenderProfileSnapshot`:

- `pass.<name>.cpu_ms` for the current completed frame;
- `pass.<name>.gpu_ms` for the current completed frame;
- `pass.<name>.gpu_p50_ms` and `pass.<name>.gpu_p95_ms` for the fixed sample
  window;
- existing draw/section counts and CPU-subphase p50/p95 values remain present.

The provider regression test covers the new CPU and GPU percentile fields.

## Commands and results

The application was launched from the repository root with:

```powershell
build/Debug/KimPeanutEngine.exe --graphics-api <api> --startup-level level/performance_profile.level --agent-port 37373
build/RelWithDebInfo/KimPeanutEngine.exe --graphics-api <api> --startup-level level/performance_profile.level --agent-port 37373
```

For each run, the live loopback endpoint was queried with `stats --json`,
polled until `summary_complete=true`, and then issued:

```json
{"op":"execute","command":"capture.screenshot","arguments":{"path":"save/screenshots/validation/<capture>.png","view":"scene_color"}}
```

The window was closed with `Process.CloseMainWindow()`. Every close returned
`CloseRequested=True`, `Exited=True`, and `Remaining=False`; every captured
stderr file is zero bytes. No `KimPeanutEngine` process remains.

Focused validation:

```text
cmake --build build --config Debug --target PerformanceStatsCommandProviderTest KimPeanutEngine
PerformanceStatsCommandProviderTest.exe --gtest_color=no       1/1 passed
cmake --build build --config RelWithDebInfo --target KimPeanutEngine  passed
```

The build commands required a clean process environment with one normalized
`PATH`; the original inherited environment contained both `PATH` and `Path`,
which caused MSBuild to fail before compilation.

## Fixed-window profile summary

| Build / API | CPU total p50 / p95 ms | Present p50 / p95 ms | G-buffer GPU p50 / p95 ms | Total GPU current ms | Present mode |
| --- | ---: | ---: | ---: | ---: | --- |
| Debug / Vulkan | 4.156 / 6.184 | 0.305 / 0.501 | 0.451 / 0.732 | 1.141 | mailbox |
| Debug / OpenGL | 4.400 / 6.617 | 0.354 / 0.693 | 0.401 / 0.774 | 1.306 | vsync |
| RelWithDebInfo / Vulkan | 1.652 / 3.524 | 0.390 / 0.709 | 0.105 / 0.135 | 1.080 | mailbox |
| RelWithDebInfo / OpenGL | 2.182 / 2.979 | 0.414 / 0.620 | 0.147 / 0.431 | 1.339 | vsync |

All four runs reported 120 warm-up frames and 300 samples. The performance
configuration is the Stage 6.0 comparison configuration; Debug remains
correctness/validation evidence.

## Per-pass measurements — RelWithDebInfo terminal snapshots

CPU is the completed-frame pass time. GPU current is the completed-frame timer
value; GPU p50/p95 are retained fixed-window percentiles.

| Pass | Vulkan CPU ms | Vulkan GPU current / p50 / p95 ms | OpenGL CPU ms | OpenGL GPU current / p50 / p95 ms |
| --- | ---: | ---: | ---: | ---: |
| Directional shadow | 0.0436 | 0.0690 / 0.0250 / 0.0309 | 0.0860 | 0.1075 / 0.0522 / 0.1025 |
| Spot shadow | 0.0109 | 0.0225 / 0.0164 / 0.0225 | 0.0127 | 0.0328 / 0.0215 / 0.0317 |
| Point shadow | 0.0338 | 0.0143 / 0.0113 / 0.0143 | 0.0174 | 0.0532 / 0.0276 / 0.0462 |
| G-buffer | 0.0590 | 0.4291 / 0.1055 / 0.1352 | 0.0686 | 0.3994 / 0.1475 / 0.4312 |
| Deferred lighting | 0.0281 | 0.0942 / 0.0461 / 0.0594 | 0.0363 | 0.1260 / 0.0799 / 0.1188 |
| Tone map | 0.0109 | 0.0881 / 0.0655 / 0.0870 | 0.0161 | 0.1475 / 0.0952 / 0.1372 |
| Capture view | 0.0141 | 0.0287 / 0.0215 / 0.0276 | 0.0161 | 0.0594 / 0.0410 / 0.0553 |
| Editor composite | 0.4939 | 0.3338 / 0.2196 / 0.9747 | 0.4127 | 0.4137 / 0.2688 / 0.3912 |

The RelWithDebInfo runs also reported five draws/five sections, four emitted
pipeline binds from five requests, five emitted mesh binds from five requests,
five emitted resource-binding binds from five requests, three descriptor sets,
zero descriptor pools, and 102,994 triangles on both APIs.

## Captures

The same authored bunny composition was visible in both final performance
captures on inspection. API-specific PNG bytes differ, as expected from
backend encoding/timing paths; both are 1094×619 RGBA captures.

- [Vulkan RelWithDebInfo capture](../../save/screenshots/validation/r15-stage60-vulkan-relwithdebinfo-20260917.png) — SHA-256 `885E2ECBD174C1402FC12C907EE32F38C7C8D19402EBE6B74B49A706D35D53B2`
- [OpenGL RelWithDebInfo capture](../../save/screenshots/validation/r15-stage60-opengl-relwithdebinfo-20260917.png) — SHA-256 `FBD56C530672216D91CE19FF1208013458962F67B16B4FB4EEE93A27617049C5`

The corresponding Debug captures were also preserved:

- `save/screenshots/validation/r15-stage60-vulkan-profile-20260917.png`
- `save/screenshots/validation/r15-stage60-opengl-profile-20260917.png`

## Disposition

R1.5 orderly-close evidence is closed for Vulkan and OpenGL: startup reached
the live Editor/Render path, the profile completed, capture exported, and the
native window-close path terminated the application without a surviving
process or stderr output.

Issue-9.7 Stage 6.0 runtime proof is complete for the specified Vulkan Debug,
Vulkan performance, and OpenGL performance runs. Stage 6.5 re-profiling and
the larger render-graph architecture remain separate work; this journal does
not authorize either.
