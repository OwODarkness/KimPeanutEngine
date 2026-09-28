# R4.7 — Path-tracing performance acceptance

**Status: implementation in progress; R4.7.0 attribution/baseline complete, R4.7.11–R4.7.13 prototypes measured, later-stage correctness and quality acceptance open.**

- Objective: reduce scene-independent PT CPU/GPU cost while preserving R4.6
  Sponza/Cornell indirect lighting, settings and resource-lifetime semantics.
- Latest execution authorization: GPU attribution through temporary controlled
  diagnostics and an evidence-based strategy, with no retained optimization
  implementation. The [selected direction](../../docs/render/.plan/R4.7-gpu.md#measured-direction-after-the-gpu-trace)
  follows measured diagnostic-demand and visibility-policy gains; interactive
  sample reduction remains a separate quality decision. Earlier R4.7.5
  edit/residency and non-capture graph coverage remains open.
- Design and stages: [R4.7 plan](../../docs/render/.plan/R4.7.md).
- GPU strategy: [R4.7.8–R4.7.13](../../docs/render/.plan/R4.7-gpu.md).
  R4.7.11 inline ray-query, R4.7.12 ray-cone texture LOD and R4.7.13 guided
  one-SPP preview prototypes have measured Sponza results. R4.7.13 also has an
  opt-in accumulated-Beauty spatial denoiser; it retains the separate motion,
  scene-coverage and quality gate recorded in its review.
- Findings: [R4.7 review](../../docs/render/.review/R4.7.md).
- Acceptance ledger: [Render TODO](../../docs/render/TODO.md).
- Baseline evidence: [matched R4.6 runs](../../docs/render/.review/R4.6.md#matched-rt-enabled-debug-and-relwithdebinfo-runs--2026-09-27).
- Execution record: [journal](../journal/2026-09-27-render-r4-7-performance-plan.md).

## Scope and ownership

Stages R4.7.0–R4.7.7 cover attribution, visibility shader/SBT, primary/hit
reuse, static AS preferences, persistent Graphics tables/bindings, revision
caches, graph preparation, optional budget and final validation. Render owns
policy/data identity; Graphics owns GPU objects, native addresses and safe
submission retirement. No level-specific path, asset-loader redesign or R5
decomposition is required. Implementation remains bounded by the user-authorized
stage and the open gates below.

## Required evidence for future implementation

1. Freeze baseline/candidate source and binary configuration. Record GPU/driver,
   validation/layers, resolution, camera, presentation mode, lights/environment,
   SPP/bounces, probe/capture state and active passes. Use RelWithDebInfo for
   performance and Debug with validation for correctness. Never compare an
   RT-off run against an RT-on candidate as optimization evidence.
2. After residency and history/reset behavior settle, warm at least 120 frames
   and collect 300 fresh samples per window; repeat at least three matched
   windows. Report per-window p50/p95 and variation; do not relabel latest-frame
   snapshots as percentiles. Include CPU work/wait attribution, GPU PT/total,
   allocations/uploads and AS builds. Keep ray-counter/vendor-profiler runs
   separate from final timing runs.
3. Demonstrate each unchanged-quality change at full resolution, 4 SPP and
   eight continuation bounces. The provisional PT p50 target is in the plan;
   the baseline must be rerun before treating its absolute value as binding.
   A change below run variation is inconclusive. Investigate raster CPU/GPU
   regressions over 5% before acceptance rather than hiding them in PT results.
4. Capture Cornell and Sponza, plus a reordered/material-overridden general
   scene, through the Runtime command registry using `capture.screenshot` and
   `poll`. Save unique PNGs under `save/screenshots/validation/`. Compare
   equal accumulated samples for unchanged-quality work and equal wall time
   separately for budget settings. Inspect indirect-light reach, shadows,
   textures, metallic/rough surfaces, environment-zero lighting and probe modes.
   Internet reference PNGs remain qualitative, not exact-match targets.
5. Validate camera/light/material/transform/visibility edits, texture residency,
   insertion/removal/reorder, nonuniform/mirrored transforms, resize, scene
   replacement, shader reload, RT/PT toggles and deferred capture/debug views.
   Relevant changes reset history; dispatch failure never commits samples.
6. Demonstrate bounded GPU resources across frames in flight, static long runs
   and repeated resize/toggle/unload. No in-flight descriptor/table overwrite,
   dangling buffer addresses, validation lifetime errors or steady device idle.
   After static warm-up, unchanged scene tables/binding storage are reused.
7. Use `tools/kp.ps1` for serial affected builds/tests per the
   [validation matrix](../../docs/validation_matrix.md). Common RT contracts
   need Graphics contract tests; pass gating needs Render schedule tests;
   shader/backend changes need Vulkan runtime captures. Retain OpenGL/raster
   fallback coverage. Compilation alone cannot close shader/lifetime findings.

## Acceptance status

The R4.7.3 AS-policy result is preliminary: the TLAS candidate has one matched
window, and the static-BLAS candidate ended before steady sampling. No AS policy
is retained. R4.7.4 persistent-table reuse and Debug lifecycle gates pass. Its
inherited exclusive mesh-buffer upload path now uses the graphics queue family;
the reason for the earlier device-loss events remains unproven. Three matched
RelWithDebInfo Sponza windows completed 120 warm-up plus 300 samples each.
Median PT GPU p50 is 25.173 ms versus 23.974 ms at the R4.7.3 baseline (+5.0%),
so no speedup is claimed. Broader quality/equal-sample and remaining lifecycle
gates remain open. R4.7.5 then added scene/material revisions, static record
reuse, and graph-keyed shadow preparation; its three-window medians improved
relative to R4.7.4, but run-to-run causality and the remaining revision/graph
matrix are not closed. See the
[R4.7 findings](../../docs/render/.review/R4.7.md) and
[R4.7.5 evidence](../../docs/render/.review/R4.7.5.md).
