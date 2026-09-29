# R6 interactive path-tracing implementation — 2026-09-29

- Status: partial — R6.1 implemented; one-light timing experiment retained for evaluation
- Date: 2026-09-29
- Source base: `dbb84c4461747bfd2a8db0e2c94a2233953c73c5` (`main`); working tree dirty
- GPU: NVIDIA GeForce RTX 4070 Laptop GPU; driver 595.71
- RelWithDebInfo executable SHA-256: `66540CA0FC00E001C6DF229DC1D34025203E84FE2C95F68FE7CB7BF2BEBAEE63`
- Ray-generation source SHA-256: `EF4E31DA0F93A846B98EE2EB6EF154F23897F061E61535FD247C23C36B23F734`
- Spec: [R6 implementation spec](../specs/render-r6-interactive-path-tracing.md)
- Parent TODO: [R6](../../docs/render/TODO.md)

## What was done

- Established a serial RelWithDebInfo Vulkan baseline on the checked-in Sponza
  with its directional sun and two point lights. Each profile reported active
  path tracing, three light records, eight continuation bounces, 4 SPP, a
  1094x631 viewport, and complete tracked texture residency.
- Added a per-successful-frame random sequence for the existing 1-SPP guided
  preview. Progressive Beauty still uses the progressive sample count, and
  camera motion does not reset the interactive random index.
- Removed tone-map pipeline/output settings from the scene-linear accumulation
  signature so exposure/output transfer changes do not discard valid history.
- Exercised Debug Vulkan `low_spp_preview`, captured the output, and closed the
  process through its GLFW window.
- Added an opt-in unbiased uniform one-light-per-hit mode. Every light remains
  eligible; the selected light's contribution is multiplied by the scene light
  count. The setting is serialized in Runtime performance stats and exposed as
  `single_light_preview`.
- Rebuilt the changed source in RelWithDebInfo and ran three serial visible
  Vulkan Sponza windows at 1094x631, 1 SPP, eight bounces, all three lights,
  full tracked texture residency, with 120 warm-up and 300 measured frames.
- Captured and inspected matched 420-frame static outputs for one-light and
  all-light 1-SPP. The one-light image has substantially more visible grain;
  neither is a denoised or accepted moving-camera result.
- Removed the earlier low-radiance point-light cutoff experiment because it
  dropped authored light energy and was not a valid equivalent-quality fix.

## What changed

- Behavior: guided preview gets fresh stochastic samples after camera/history
  resets; presentation-only changes no longer reset scene-linear history.
- Ownership: `PathTracingPass` owns and commits the random frame index; the
  shader consumes it only for guided preview. No module dependency or backend
  ownership changed.
- Important files: `path_tracing_pass.*`, `path_trace_history_progress.h`,
  `path_trace_history_signature.h`, `path_tracing_scene_data.h`,
  `ray_tracing_path_tracer.rgen`, and the focused RenderSystem test.

## Validation

- Required level: L3 — Render shader/uniform and accumulation behavior.
- `tools/kp.ps1 -Configuration RelWithDebInfo build KimPeanutEngine` — PASS
  for the baseline source before R6.1 edits.
- Three serial baseline profiles — PASS: total GPU p50/p95 values were
  48.00/52.00, 47.69/51.55, and 49.22/51.87 ms. The two retained exact PT
  percentile records are 42.94/46.48 and 44.36/47.10 ms. Median total GPU p50
  is 48.00 ms; the total GPU p95 range is 51.55–52.00 ms.
- `glslangValidator -V --target-env vulkan1.2 -S rgen ...` — PASS.
- `tools/kp.ps1 -Configuration Debug build RenderSystemTest` — PASS.
- `RenderSystemTest.exe` — PASS, 40/40 tests. CTest had no discovered test
  entry for this target, so the produced executable was run directly.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` — PASS, including
  the Runtime command and direct-light setting serialization.
- `tools/kp.ps1 -Configuration RelWithDebInfo build KimPeanutEngine` — PASS
  with the one-light experiment.
- Latest standalone ray-generation GLSL compile with
  `glslangValidator -V --target-env vulkan1.2 -S rgen ...` — PASS.
- Three one-light and three all-light windows (PT p50/p95; total GPU p50/p95;
  CPU p50/p95, ms):

  All six were serial RelWithDebInfo Vulkan Runtime launches on the checked-in
  Sponza, static camera, 1094x631, path tracing active, 1 SPP, eight bounces,
  three light records, and full tracked texture residency. Each mode used 120
  warm-up frames plus 300 measured frames. GPU is RTX 4070 Laptop, driver
  595.71; per-window GPU clock telemetry was not retained.

  | Window | PT | Total GPU | CPU |
  | --- | ---: | ---: | ---: |
  | 1 | 8.79 / 10.34 | 13.56 / 15.18 | 13.34 / 15.58 |
  | 2 | 9.64 / 10.77 | 14.14 / 15.50 | 14.05 / 15.43 |
  | 3 | 9.20 / 10.47 | 13.76 / 15.25 | 13.65 / 15.56 |
  | All-light 1 | 11.42 / 13.31 | 16.20 / 18.18 | 16.18 / 18.38 |
  | All-light 2 | 11.66 / 13.38 | 16.53 / 18.36 | 16.23 / 18.39 |
  | All-light 3 | 12.21 / 14.09 | 16.98 / 18.95 | 16.80 / 19.00 |

  All-light median p50/p95: PT 11.66/13.38 ms, total GPU 16.53/18.36 ms, CPU
  16.23/18.39 ms. One-light median p50/p95: PT 9.20/10.47 ms, total GPU
  13.76/15.25 ms, CPU 13.65/15.56 ms. One-light reduces total GPU p50/p95 by
  16.8%/16.9% in this matched static comparison. It also has visibly more
  variance in the captured image; keep the control opt-in pending reconstruction
  and motion-quality acceptance.
- The paired captures are `save/screenshots/validation/r6-uniform-one-light.png`
  and `save/screenshots/validation/r6-all-light-guided-control.png`.
- Latest Runtime log
  reports histories released with zero remaining and RT teardown at zero AS,
  pipelines, binding sets, address tables, and temporary build batches; no
  Vulkan validation VUID or device-loss message was found.
- Nsight Graphics 2026.3.1 GPU Trace CLI created
  `build/NsightGPUTrace/KimPeanutEngine_2026_09_29_14_22_32.ngfx-gputrace` and
  exported five-frame aggregate counters. It is rejected as R6.0 attribution:
  the activation snapshot was 1920x1080 with incomplete tracked texture
  residency rather than the matched 1094x631 resident fixture, and
  `GPUTRACE_REGIMES.xls` has no data rows. The trace also reported merged
  periodic samples. It does not identify shader-level PT cost. The Nsight CLI
  ended its launched target after capture; it did not leave an engine process
  running.
- Debug Vulkan Runtime reported path tracing active, 1 SPP, three light
  records, and successfully exported
  `save/screenshots/validation/r6-1spp-debug-vulkan.png`.
- Runtime log `save/logs/2026-09-29/KimPeanutEngineLog-2026.09.29-13.16.20.txt`
  contains no Vulkan VUID/validation or device-loss errors; shutdown reports
  two histories released, zero remaining, and zero RT objects in the teardown
  snapshot.
- A serial RelWithDebInfo rebuild after adding the independent Runtime settings
  command passed. A visible Vulkan Sponza run accepted
  `render.path_trace_settings` with 1 SPP, 8 bounces, raw Beauty, all-light
  sampling, and ray-pipeline visibility. The next `stats --json` response
  reported these values as both requested and effective, with path tracing
  active, three light records, complete tracked residency, and a 1094x631
  viewport. Its one-frame total GPU value was 15.06 ms; this is a smoke sample,
  not a performance window. The exported image is
  `save/screenshots/validation/r6-1-raw-one-spp-relwithdebinfo.png`.
- The first visible Runtime launch used `asset/level/sponza.level` and exited
  with code 1 because `--startup-level` requires an Asset-root-relative
  `level/*.level` path. Relaunching with `level/sponza.level` succeeded; this
  was a launch-argument correction, not a source or Vulkan failure. The GUI
  was closed through its process-owned `GLFW30` window.

## Adaptive SPP follow-up

- Added opt-in `adaptive_camera_motion` sampling. The policy compares per-frame
  camera translation and shortest-angle rotation against configurable
  thresholds, then waits for the configured number of stable submitted frames.
  While moving it selects the configured low-SPP reconstruction (default
  `variance_denoise`); once settled it selects the configured higher SPP in raw
  mode, preserving the existing progressive sample accumulator. Accumulated
  sub-threshold travel is retained so slow
  movement eventually crosses the threshold. Only successfully finalized path
  tracing frames advance policy state.
- Defaults for the opt-in policy are 1 moving SPP, 4 settled SPP, 8 stable
  frames, 0.02 world units translation, and 0.2 degrees rotation. Fixed
  sampling remains the default. Runtime settings and profiler stats expose the
  requested/effective policy, thresholds, effective samples per dispatch,
  camera sampling state, and accumulated sample count.
- Added unit coverage for threshold transitions, still-frame settling,
  sub-threshold travel accumulation, and yaw wraparound. Debug
  `RenderSystemTest` passed all 43 tests. Debug and RelWithDebInfo engine builds
  succeeded; RelWithDebInfo emitted the existing `LNK4098` CRT-library warning.
- On the visible RelWithDebInfo Vulkan Sponza Runtime (RTX 4070 Laptop,
  1094x631, three lights, complete tracked texture residency), the adaptive
  mode reported `stable_accumulating`, 4 SPP, and 10,072 accumulated samples.
  This confirms settled high-SPP history continues past the user's observed
  500-sample quality point. A replay exported
  `save/screenshots/validation/r6-adaptive-motion-final.png`. A 300-sample
  mixed adaptive window measured total GPU p50/p95 of 32.80/51.56 ms; it is not
  a 60-FPS acceptance result. The Runtime remains open in adaptive mode for the
  user to test.
- The deterministic fixture uses discrete transform keyframes; it does not
  model continuous mouse motion. Runtime stats are published from completed
  frames, so reading immediately after `actor.control` can show the prior
  camera pose. Unit tests verify the policy decision path; a reliable
  end-to-end moving-state replay and continuous-input comparison remain open.
- A replay rerun after adding pose synchronization timed out waiting for a
  fresh profile activation while the Runtime remained responsive and adaptive
  stats continued advancing. Do not count that rerun as validation.

## Remaining risks and unverified areas

- Uniform one-light preview improves static timing, but its capture is noisier
  than the all-light preview. It remains an opt-in experiment until temporal /
  spatial reconstruction demonstrates acceptable quality.
- R6.0 camera-motion replay and diagnostic ray counters remain open. The fresh
  Nsight capture is not valid for attribution, and a separate Shader Profiler
  capture with live/register/spill data remains outstanding. Historical
  aggregate counters do not attribute the shader bottleneck.
- R6.2-R6.7 remain open. In particular, R6.2/R6.3 persistent history and graph
  work is gated by the still-open R5.2/R5.3 ownership, runtime-failure, and
  lifecycle contracts documented in the R6 plan.
- R6.1 now has a Runtime control path for independently selecting estimator
  work, visibility, reconstruction, light sampling, and output probe. The raw
  1-SPP request is runtime-verified, but motion-sequence sample decorrelation
  and unchanged progressive Beauty are not yet runtime-verified.
- This evidence is specific to static Sponza on the checked-in three-light
  setup. It does not validate Cornell, object/camera motion, disocclusion,
  resizing/reload, light edits, or cross-backend behavior.

## Remaining work

Close the R5.2/R5.3 prerequisite contracts, then collect the R6.0 deterministic
camera replay and fresh Nsight captures. Only after that should R6.2-R6.4 add
submitted-transform guides and validated temporal reconstruction; rerun the
one/all-light comparison with matched build, replay, clocks, and image-quality
metrics before selecting the mode for final use.

## Adaptive SPP tiers

- The earlier adaptive runtime replay reached 10,072 accumulated samples but
  remained at 4 SPP. This confirmed that the previous settled policy did not
  reduce dispatch cost after the user's quality threshold.
- The policy now transitions while stationary from 4 SPP to 2 SPP when the
  accumulated count reaches 100, then from 2 SPP to 1 SPP at 200. Camera motion
  remains at its configured low SPP. Both thresholds and all phase rates are
  exposed through `render.path_trace_settings` and profiler settings.
- Path-tracing history identity excludes only the per-dispatch batch size;
  shader accumulation merges each batch using its actual sample count. This
  allows rate changes without discarding accumulated samples. Tests cover the
  96→100 and 198→200 boundaries and continued accumulation after the second
  transition.
- Debug `RenderSystemTest` and both Debug / RelWithDebInfo engine targets
  compile. The RelWithDebInfo linker reports the pre-existing `LNK4098`
  warning. Python syntax, fixture JSON, and `git diff --check` pass.
- On a fresh visible RelWithDebInfo Vulkan Sponza Runtime (RTX 4070 Laptop,
  1094x631, three authored lights, full tracked texture residency), the replay
  reported the configured thresholds as 100 / 200, selected
  `quality_maintaining_1spp`, and continued history to 551 accumulated samples.
  Its 120-warmup / 300-sample adaptive profile measured 16.62 / 17.84 ms total
  GPU p50 / p95. The profile meets the 60-FPS median but misses the p95 target.
  Screenshot: `save/screenshots/validation/r6-adaptive-motion-final.png`;
  machine-readable stats: `save/diagnostics/r6-adaptive-tier-validation.json`.
- The app remains open for interactive testing. One initial launch exited after
  `vkQueueWaitIdle(upload)` returned `VK_ERROR_DEVICE_LOST`; a clean relaunch
  initialized Vulkan and the full replay successfully. No further device-loss
  message appeared in the successful run's startup log.

## Motion denoising selection

- Adaptive camera motion now selects the configured
  `adaptive_moving_reconstruction` (default `guided_preview`, the stronger
  3x3 normal/depth-guided spatial filter); once camera motion settles, it returns to raw progressive
  accumulation. `guided_preview`, `variance_denoise`, and `raw` are selectable
  for the moving phase through `render.path_trace_settings`.
- `PathTraceSettings` now defaults to adaptive camera-motion sampling; fixed
  sampling remains selectable. The selection is a pure policy helper used by
  `DeferredRenderer`, with focused tests for moving/stable modes and the default.
- Visual testing exposed a shader branch-order bug: mode 2 satisfied the earlier
  `> 0.5` guided-preview condition, making the later `> 1.5` variance filter
  unreachable. The conditions are now ordered by mode (`> 1.5` first); `glslc`
  successfully compiled the corrected Vulkan fragment shader.
- Debug `RenderSystemTest` passed all 6 focused settings/adaptive tests, and the
  final RelWithDebInfo engine build succeeded with the pre-existing `LNK4098`
  warning. A live adaptive command reported policy 1, thresholds 100/200,
  moving state, 1 SPP and reconstruction mode 2; the captured moving frame was
  still visibly noisy. The user also reported a screen-locked noise pattern.
- Adaptive RNG mode is now constant for every adaptive SPP tier, so its packed
  shader mode cannot invalidate history at 200 samples. The independent random
  sequence advances by the committed dispatch size; 4-, 2-, and 1-SPP batches
  therefore get non-overlapping indices while camera history can reset on
  motion. The moving default changed to the stronger 3x3 normal/depth-guided
  filter (`guided_preview`); variance denoise remains configurable.
- Both changed shaders compile with Vulkan `glslc`; all 46 Debug
  `RenderSystemTest` cases pass after the tier-stability and moving-default
  updates. A fresh RelWithDebInfo GUI Runtime initialized and opened its command
  port, but Vulkan returned `VK_ERROR_DEVICE_LOST` on the first frame submit,
  including when path tracing was initially disabled. Visual validation of the
  new moving filter is blocked by that Runtime failure. This remains spatial
  filtering only; temporal reprojection is still an open R6 stage.

## Follow-up: reconstruction mapping and reported 72-sample reset

- Corrected the shader/CPU reconstruction mapping: CPU mode 1 is
  `guided_preview` (3x3), mode 2 is `variance_denoise` (5x5). The prior
  branch-order repair made both branches reachable but swapped their meaning.
  The Vulkan tone-map shader compiled successfully with `glslc`.
- Debug Vulkan Sponza completed and retired its initial BLAS/TLAS builds and
  continued rendering. After tracked texture residency completed, a stationary
  Runtime probe observed samples advancing from 529 to 753, reset reason
  `none`. Evidence: `save/diagnostics/r6-reset-probe.json` and
  `save/screenshots/validation/r6-fix-debug-sponza-settled.png`. This is settled
  accumulation evidence, not moving denoise acceptance or a performance result.
- There is no 72-sample threshold in the current policy (tiers are 100/200).
  The reported reset at 72 has not been reproduced. Startup observations with
  incomplete texture residency remained at low sample counts. Material texture
  indices and revisions participate in history invalidation; residency changes
  are a plausible explanation, not yet a captured causal event.
- Found a separate unnecessary invalidation: reconstruction bits 9/10 were
  hashed even though ray generation does not consume them. Moving guided output
  returning to settled raw output could discard valid accumulated radiance.
  History now masks only these output-filter bits; estimator, visibility,
  camera, scene, material, lighting, and pipeline changes remain invalidating.
  Added focused regression coverage and explicit reset categories in Runtime
  stats. Logs record the category and prior count for resets after >=16 samples,
  avoiding a per-frame log stream during camera motion. Render retains history
  ownership; RHI interfaces and GPU lifetime rules are unchanged.
- The rebuilt RelWithDebInfo Vulkan Sponza run still lost the device:
  `KimPeanutEngineLog-2026.09.29-17.55.03.txt` reports timestamp-query failure
  followed by `vkQueueSubmit` returning -4 at 17:55:08. Earlier GPU work may
  already have failed; this does not identify the offending shader/pass. No
  further identical optimized launches were attempted. Device-loss and moving
  denoising acceptance remain open.
- Read gkNextEngine source at commit
  `4ba5b7cd106c282e7ed166ff853aeea87b392680`: temporal reprojection and
  `Process.TemporalPostFilter.comp.slang`. Its implementation includes temporal
  rejection/clipping plus multiple spatial passes with local luminance variance,
  HDR compression, firefly suppression and normal/albedo guides. Our current
  single spatial filter does not provide that temporal reconstruction. No source
  was copied. Reference checkout stays in ignored `save/diagnostics/`.
- Follow-up validation: Debug engine, `RenderSystemTest`, and
  `RenderPassScheduleTest` builds succeeded. Direct test executables passed
  48/48 and 95/95 cases, respectively. `ctest -R RenderSystemTest` matched no
  registered cases; it is not counted as a test pass. One MSBuild attempt hit
  MSB6001 while launching CL; the targeted retry compiled successfully.
  `git diff --check` passed with line-ending notices.
- Resident Debug Sponza accumulated 136 -> 1361 over the 35-second probe with
  no reset. In fixed sampling, actual reconstruction modes 1 -> 0 -> 2 -> 0
  preserved sample history (37 -> 51, 53 -> 67, 69 -> 83, 85 -> 99), including
  crossing 72. Evidence: `r6-reset-cause-probe.json` and
  `r6-fixed-filter-history-probe.json` in `save/diagnostics/`. Adaptive settings
  were restored after the check. Screenshot:
  `save/screenshots/validation/r6-reset-fix-sponza.png`.
- These checks verify filter-switch history preservation, not the user's
  exact original reset event. Startup resets still require a captured category;
  a late log-format edit was absent from the earlier compiled object (the log
  printed literal placeholders). Forced that translation unit to rebuild before
  further diagnosis. No GPU/backend fix or candidate performance claim is made.

## Moving 1-SPP spatial reconstruction follow-up

- The user clarified that the main acceptance target is denoising during
  camera movement at 1 SPP. Replaced the guided preview's 3x3 arithmetic blur
  with a dense 7x7 binomial spatial kernel. It uses normal/depth rejection,
  luminance-compressed HDR filtering, guide-weighted local luminance variance
  from adjacent pixels, and a local upper-luminance clamp for fireflies. This
  adapts ideas from the pinned gkNextEngine source; it is not its temporal
  denoiser or a claim of equivalent quality. Preview filtering is biased and
  can blur surface texture or small bright details; raw accumulation is intact.
- No new GPU resources, shader bindings or RHI contracts were introduced.
  Filtering remains in tone-map for this bounded fix; separating reconstruction
  into measured graph passes remains part of R6.4. No SRAM optimization or
  performance improvement is claimed.
- `glslc --target-env=vulkan1.2` passed for both values of
  `KP_GRAPHICS_API_VULKAN`; macro 0 checks syntax of the alternate shader
  branch, not an OpenGL runtime. The unchanged C++ configuration is the rebuilt
  Debug engine with Vulkan validation enabled, Default desktop, Sponza,
  1094x631, full tracked texture residency, 8 continuation bounces and
  all-light sampling. The shader loads through the normal startup catalog.
- Continuous camera motion replay captured two raw and two guided outputs.
  Before/after Runtime snapshots report `moving`, dispatch SPP 1, and effective
  reconstruction 0 for raw / 1 for guided. The camera kept moving while capture
  export was pending. Evidence: `save/diagnostics/r6-moving-filter-check.json`.
  Representative actual capture paths are
  `save/screenshots/validation/r6-moving-raw-0-1.png` and
  `save/screenshots/validation/r6-moving-guided_preview-0.png` (raw requests
  received collision suffixes; older captures were preserved). The replay is
  wall-clock driven, so the poses are nearby, not an exact pixel-aligned metric.
  Visual inspection shows substantial speckle reduction, with coarse residual
  mottling and softer texture detail. No temporal stability acceptance is made.
- The moving default remains guided preview, returning to raw once settled.
  The Debug GUI remains running for interactive inspection. This does not close
  the RelWithDebInfo device-loss blocker or 60-FPS/motion-quality acceptance.
- A fresh startup probe and flushed log
  `KimPeanutEngineLog-2026.09.29-18.09.54.txt` identify the exact reported reset:
  `reason=materials_changed, previous_samples=72` at 18:09:58 and 18:10:00.
  Resets accompany asynchronously arriving textures and stop after residency;
  retaining those old fallback-material samples would be incorrect. This
  supersedes the earlier unconfirmed startup-reset hypothesis.
- Files changed in this follow-up: `asset/shader/tone_map.frag`,
  `engine/runtime/render/passes/path_tracing_pass.cpp` / `.h`,
  `engine/runtime/render/path_trace_settings.h`,
  `engine/runtime/render/path_trace_history_signature.h`,
  `engine/test/unit/render/render_system_test.cpp`, `docs/status.md`, and this
  journal. Earlier uncommitted R6 work was preserved. The inspected Debug GUI
  is open; the attempted foreground operation did not pass the GLFW focus
  check, so foreground focus is not claimed.
