# R6 interactive path-tracing plan — 2026-09-29

User requested a concrete next-stage optimization plan for moving-camera low-SPP
denoising and a multi-light Sponza scene reported at 22 FPS. Deliverable is
documentation only. Baseline HEAD `dbb84c4461747bfd2a8db0e2c94a2233953c73c5`;
existing shader/scene-data edits were preserved.

Read current status, R4.7 GPU strategy and preview review, R5 ownership plan,
PT settings/pass/shader and tone-map filter. Camera data contributes to the
progressive history signature, movement resets its global sample count, and
current filtering has no motion reprojection/per-pixel rejection. Direct-light
code loops over scene lights at continuation hits. This identifies concrete
design gaps and a cost hypothesis, not measured attribution of today's 22 FPS.

Inspected retained Nsight TSV exports: three aggregate frame measurements,
about 34.3% RT Core, 24.8% SM, 40.8% L2 and 62.7% DRAM throughput. The regimes
file has no data rows. No new hardware trace, engine launch, build or performance
window was collected. Checked common recorder: general compute dispatch is a
contract gap for a potential NRD integration and is addressed explicitly in
the proposed plan.

Used engine-reference and modular-documentation skills. Retrieved gkNextEngine
main README and module map, plus NRD's primary documentation; attempted actual
renderer/pinned Sakura sample retrieval but web returned cache misses and direct
GitHub API requests failed TLS. Sakura conclusions use its existing pinned
source study. Do not claim verified Sakura denoiser or inspected gk renderer
implementation from those reads.

Created [R6 plan](../../docs/render/.plan/R6.md) with ordered profiling,
sampling, guide, temporal, spatial/library-selection, light-sampling and final
acceptance slices. Linked it from Render plans/roadmap and status. No source,
fixture or GPU ownership behavior changed. Open R5 correctness gates remain
prerequisites for affected lifetime changes. The plan makes no speedup promise.

Follow-up request explicitly asked which GPU costs dominate and whether SRAM
can help. Recomputed mean counters from the three frame columns: DRAM read
61.317%, write 1.372%, L2 sector hit rate 81.222%, compute-category register
launch stalls 17.615%, active warps 24.076%, sampled RT/L1TEX long-scoreboard
stalls 6.207/5.044% of peak elapsed. Added an evidence/confidence table and
separate register/live-state, cache-coherence and compute-filter tile
experiments to R6. The old trace cannot attribute the current 22-FPS fixture.
No new capture or native Nsight GUI inspection was performed; this is analysis
of retained exports. Registry confirms Nsight Graphics 2026.3.1 installed,
but the inspected exports do not provide per-pass or reliable per-shader
register data. The plan requires those captures in R6.0 before selecting a
production PT memory optimization.

User clarified that SRAM was an example, not an optimization requirement.
Updated the plan to make profiling the selection authority and memory examples
conditional. No SRAM implementation or memory-specific stage is required.
Documentation checks: local Markdown target files exist in all five changed
documents; `git diff --check` passed with LF/CRLF notices. Anchors and remote
URLs were not comprehensively checked. No source changes, builds or tests.
