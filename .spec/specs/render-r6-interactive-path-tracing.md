# R6 interactive path tracing

- Status: active
- Owner: Codex with user review
- Parent TODO: [R6 in Render TODO](../../docs/render/TODO.md)
- Design: [R6 plan](../../docs/render/.plan/R6.md)

## Objective

Deliver a measured, moving-camera path-tracing mode that uses low-SPP sampling
and temporal/spatial reconstruction while preserving progressive Beauty as a
reference. Improve the reported multi-light Sponza performance without silently
removing authored light, material, bounce, or visibility work.

## Current state

The checked-in Sponza level has a directional sun and two shadow-casting point
lights. Three serial RelWithDebInfo Vulkan profiles at 1094x631, 4 SPP and eight
bounces measured total GPU p50 values of 48.00, 47.69, and 49.22 ms; full
texture residency was confirmed. The 1-SPP guided preview was exercised on
Debug Vulkan with all three lights. R6.1 source now gives that preview an
independent random frame sequence; no reprojection or temporal history exists
yet. Three matched windows of opt-in uniform-one-light 1-SPP preview measure
median total-GPU p50/p95 of 13.76/15.25 ms, 16.8%/16.9% below all-light 1-SPP
controls with all three light records and eight bounces. Its captured image is
noisier than the all-light control. Keep this diagnostic/preview-only until
reconstruction passes the motion-quality checks. Prior Debug Vulkan history
reports zero retained RT objects at shutdown.

## Scope and non-goals

Implement stages R6.0 through R6.7 in the order defined by the design plan:
profile and replay; establish separate sampling modes; add guides and motion;
reproject and validate per-pixel history; compare native filtering with NRD;
measure light/visibility and remaining GPU changes; complete visual, runtime,
performance, and lifetime acceptance.

Do not select an optimization from the historical whole-frame Nsight counters
alone. Do not trade away light energy, authored materials, visibility, or
bounces without an explicit separately accepted preset. SRAM use is not a
requirement.

## Invariants

- Asset, Render, Graphics, and backend ownership follows `AGENTS.md`.
- Progressive Beauty retains sample-count seeding and resets on camera changes.
- Interactive random sequences advance only after successful submitted PT work.
- Failed frames do not rotate history or commit previous-frame transforms.
- Common Render interfaces contain no Vulkan/OpenGL implementation types.
- Runtime visual checks use the visible Default desktop, Runtime commands, and
  captures under `save/screenshots/validation/`.

## Stages

1. R6.0: capture a matched current multi-light replay, exact profiler context,
   and separate Nsight GPU Trace/Shader Profiler evidence.
2. R6.1: explicit one-SPP interactive sampling, independent frame seeds, and
   preserved progressive Beauty.
3. R6.2: primary guides, view-space depth, motion vectors, and submitted-frame
   transform history.
4. R6.3: per-pixel reprojection, disocclusion rejection, moments/confidence,
   failure-safe history commits, and camera-cut handling.
5. R6.4: measure native spatial reconstruction and compare a pinned NRD path.
6. R6.5-R6.6: retain only measured light, traversal, material, register, or
   resolution changes that pass equal-time quality checks.
7. R6.7: close visual quality, three-window performance, Debug Vulkan
   validation, resize/reload/failure, and teardown acceptance.

## Acceptance criteria

- [ ] Reproducible static and camera-motion replay with current light setup.
- [ ] Evidence-ranked current GPU cost and separate named Nsight captures.
- [ ] Interactive per-frame noise changes during camera motion; progressive
      Beauty behavior remains available and stable.
- [ ] Guides and reprojection pass camera motion, object motion, disocclusion,
      camera cut, scene edit, resize, and failed-frame cases.
- [ ] Reconstructed output improves motion-sequence error over same-resolution
      1-SPP raw without persistent trails or lighting-energy drift.
- [ ] Any retained performance optimization improves repeated total-frame
      timing beyond run-to-run variation at accepted quality.
- [ ] Runtime evidence, validation logs, screenshots, and resource teardown
      satisfy the R6 plan's R6.7 gate.

## Validation plan

Use the validation matrix's Level 3 runtime path for shader, Render, graph, or
resource-lifetime changes, plus the focused render/graphics contracts. Run
serial RelWithDebInfo profiles for performance and rebuilt Debug Vulkan with
validation for correctness. Run the broad matrix when common Graphics APIs,
handles, or CMake boundaries change.

## Risks and open questions

- The current static baseline does not include a deterministic camera-motion
  replay or fresh Nsight shader attribution.
- The one-SPP preview is visibly noisy and cannot satisfy the motion-quality
  target without reconstruction.
- R5.2/R5.3 owner/lifetime acceptance and the injected-frame editor-composite
  layout issue remain prerequisites for affected history/graph changes.
- This task does not yet establish whether light visibility, traversal,
  material reads, or register pressure dominates the current multi-light GPU
  workload.
