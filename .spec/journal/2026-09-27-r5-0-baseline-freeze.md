# R5.0 baseline freeze — 2026-09-27

Plan: [R5.0 concrete design](../../docs/render/.plan/R5.0.md) ·
[R5 stage map](../../docs/render/.plan/R5.md) ·
[execution contract](../specs/render-r5-render-configuration-graph-ownership.md)

## Objective and boundary

Execute R5.0 for the refreshed Render/Graphics decoupling plan: reconcile the
R4.8 label, freeze current source behavior and ownership, preserve available
baseline evidence, and define comparison rules before runtime extraction.
This turn changed documentation/specification only. GPU tuning and R5.1+
runtime implementation were not performed.

## Baseline investigation

- Exact source HEAD: `9089df26325b5976b0ab2bc20bde46febd2ac9a2`.
- At task start, `git status --short` showed existing documentation edits in
  the R5/R4.7 planning/review/status files and two new R4.7.8/R5 refresh journals.
  No Render, Graphics, Runtime, Editor, shader or build-system source changes
  were present. The existing edits were preserved and extended.
- Searched `docs/` and `.spec/` for Render R4.8 plans/reviews/specs; none were
  present. Recorded R4.8 as an unreconciled phase label, not an acceptance
  state. R4.7 evidence and IDs remain unchanged.
- Rechecked `RenderSystem`, `DeferredRenderer`, pass declaration, graph frame,
  Debug Viewer, profile snapshot and Vulkan RT ownership seams. The detailed
  ownership, reuse and lifetime table is in the
  [execution spec](../specs/render-r5-render-configuration-graph-ownership.md).
- Reused the controlled [R4.7.8 source/runtime review](../../docs/render/.review/R4.7.8.md)
  and its saved `save/diagnostics/r478-investigation/` manifests/windows. The
  restored-source Vulkan RelWithDebInfo Sponza measurements are three windows
  at 1094x742, 4 SPP/eight bounces, 120 warm-up and 300 samples each:
  PT GPU p50/p95 means 28.545/32.296 ms, total GPU p50/p95 means
  33.356/37.186 ms, CPU total p50 mean 33.236 ms. The exact source, executable,
  fixture and shader hashes are retained in
  `baseline-restored-manifest.json`.
- Kept Scene Color-only Beauty, Scene Color plus query visibility, and
  World-Normal diagnostic-active runs as distinct configurations. They cannot
  be merged into one PT comparator because they exercise different graph work
  and visibility policy.
- Reused R4.7.5 Debug Cornell cache/reload evidence and its distinct matched
  Sponza performance run. It reports static unchanged-frame zero table packing
  and upload, and confirms revision misses after reload/transform changes.
  Its 1094x619 results are explicitly not compared to the 1094x742 R4.7.8 run.
- The existing RT-off Sponza Runtime capture is mostly black. The R4.7.5 review
  also records raster draws but not a visually acceptable output. This is a
  blocker to using that fallback image as a parity oracle, not evidence that
  Vulkan/OpenGL raster parity passes. A fresh OpenGL capture is also absent.

## Changes

- Added the R5 execution spec with the owner/cache/lifetime table, known graph
  failure semantics, exact timing conditions, steady-state reuse invariants,
  preliminary regression budgets, completed/open R5.0 gates and R5 stage plan.
- Updated the canonical R5 plan, Render TODO and project status to mark R5.0
  active/in progress and link the execution spec. Runtime extraction is gated
  until missing affected-path baseline rows are established.
- No production source, shader, generated capture, binary or diagnostic data
  changed.

## Validation and limits

- Level 0 documentation/source-inventory review selected; no C++ source was
  changed. No tests/builds/runtime launches were run for this documentation
  turn. Existing runtime evidence is cited as historical evidence, not claimed
  as a new measurement.
- `git diff --check` — passed; Git emitted only existing LF/CRLF normalization
  notices for mixed-line-ending working files.
- Relative Markdown file-link check across the seven touched R5/Graphics
  documents — passed. No C++ tests or builds were run because this turn changed
  only documentation and the validation matrix selects Level 0.
- R5.0 is **not closed**: fresh Vulkan/OpenGL raster captures, a diagnosed and
  usable RT-off parity image, and indexed current Cornell/Sponza PT graph
  outcomes remain open. No R5.1 source path should move until its affected
  baseline is reproducible.

## Ownership and risk

No ownership or API boundary changed. The freeze preserves Render policy and
logical graph ownership, Graphics physical resource/command/synchronization/
retirement ownership, Editor/Runtime copied requests, 32 eager variants, and
R4.7 cache keys. Vulkan pipeline/SBT/table/descriptor retirement under
in-flight work remains an audit question; this journal does not assert a
confirmed lifetime defect.

## Runtime raster and settings follow-up — 2026-09-28

- Rebuilt the current Debug engine with `tools/kp.ps1 build KimPeanutEngine`
  (PASS). Launched each runtime visibly on the verified `Default` desktop and
  closed it through its `GLFW30` window. Debug Vulkan validation is enabled by
  the `#ifndef NDEBUG` Vulkan-device policy. The build used HEAD
  `9089df26325b5976b0ab2bc20bde46febd2ac9a2` plus the pre-existing dirty R5.1
  working-tree changes shown in `git status --short`; this follow-up changed no
  C++ or shader source.
- Captured fresh `scene_color` through the Runtime command endpoint for both
  APIs, using `level/cornell_box.level`, `--disable-ray-tracing`, and the
  1094x742 viewport. Both reports show RT/PT/path-trace inactive; all 32 Cornell
  triangles render. The paired references are
  `save/screenshots/validation/r50-vulkan-cornell-raster.png` and
  `save/screenshots/validation/r50-opengl-cornell-raster.png`. Visual review
  confirms matching framing and direct-light response, with a shadow-edge
  difference retained for later image-parity analysis.
- Also captured Sponza raster on Vulkan with path tracing disabled and on both
  APIs with all RT disabled. Base Color showed intact geometry and authored
  materials. The scene has zero environment/IBL intensity and the raster path
  has no indirect bounce; its near-black shadowed hall and direct-sun patch are
  therefore a poor raster parity reference. This explains the old dark capture
  without changing the Sponza fixture or claiming that image is a renderer
  parity pass.
- With Vulkan RT/PT active on the same Sponza fixture, captured Beauty before
  settings changes and Primary Albedo after `render.path_trace_probe
  mode=primary_albedo`. Runtime stats changed requested/effective output probe
  from 0 to 3. A subsequent `ray_query_visibility` transition changed requested
  and effective visibility method from 0 to 1, with path tracing active and no
  fallback reason. The captured images are
  `r51-vulkan-pt-beauty.png` and `r51-vulkan-pt-primary-albedo.png` under the
  validation screenshot directory. This verifies the legacy-adapter transition
  path; it does not validate every independent settings combination.
- Captured `world_normal` and `shadow_visibility` one at a time; both exported
  successfully. The first attempt had queued three screenshots concurrently,
  so two were rejected by the single-pending-capture limit; those failures were
  harness contention, not graph rejection.
- `editor.panel.list` reported Debug Viewer open and active. Runtime Scene Color
  screenshots succeeded while it was open, and
  `r51-vulkan-engine-window-viewer.png` shows the Beauty viewport and World
  Normal preview together.
- R5.0 now has a usable RT-off cross-backend raster reference on Cornell, and
  fresh Vulkan/OpenGL raster evidence is recorded. R5.0 remains open for
  indexed Cornell/Sponza PT graph outcomes and the formal exit review. R5.1
  Runtime screenshot, diagnostic-view and Scene Color-plus-Viewer checks pass;
  independent settings combinations and distinct simultaneous diagnostic
  outputs remain open.
