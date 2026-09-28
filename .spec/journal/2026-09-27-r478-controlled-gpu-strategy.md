# R4.7.8 controlled GPU strategy — 2026-09-27

## Objective and boundary

The user requested actual diagnostic measurements after the three-frame Nsight
trace, followed by a justified strategy, without implementing the final
optimization. Baseline commit was `9089df26325b5976b0ab2bc20bde46febd2ac9a2`;
the tree was initially clean. Existing R4.7.8 attribution and R4.7.11–13 define
the stage boundary. No new roadmap ID or level-specific policy was introduced.

Used reference-driven engineering and modular-documentation skills. Read AGENTS,
README, status, validation matrix, module/GPU plans, reviews, shader/graph/Editor
call paths and the reference index first. GitHub connector discovery returned
no tools; web repository discovery and actual nvpro/pbrt source inspection
succeeded. Read NVIDIA's Nsight architecture and RT live-state documentation.
References/applicability are in the GPU plan; no reference code was copied.

## Investigation performed

1. Parsed existing `build/NsightGPUTrace/BASE/GPUTRACE_FRAME.xls` as TSV.
   It contains mean compute-category register-allocation launch stalls of
   17.615% and active warp occupancy of 24.076% of peak. These are aggregate
   metrics, not per-shader registers/spills. `GPUTRACE_REGIMES.xls` has only a
   header and no per-pass rows. Native Nsight UI automation is unavailable on
   the current computer-use surface; CLI help exposed no offline shader/live-
   state export. No new hardware trace was collected.
2. Rebuilt RelWithDebInfo and launched Vulkan Sponza outside the sandbox on
   verified Default desktop, port 37481. Used Runtime JSON-lines stats, probe
   and capture commands; no backend object access or Editor console scraping.
3. Measured three 120-warm-up/300-sample windows each for baseline, no textures,
   fixed-transport reads/no reads, compact Beauty payload, Beauty specialization
   and primary re-tracing. The simple texture ablation changes BSDF and path
   survival. The controlled pair holds transport factors fixed and consumes
   sampled values through additive emission, which does not feed path sampling.
4. Found the default World Normal Debug Viewer requests raster diagnostic
   producers alongside PT. Temporarily changed its default to Scene Color and
   rebuilt. Measured Beauty, query visibility, repeated Beauty in the same
   session, and one-SPP preview. Stats verified `path_tracing` graph, no GBuffer
   or conversion timing, and active PT. No demand/lifecycle fix was retained.
5. Captured baseline, no-textures, compact/specialized, Scene Color-only
   Beauty/query and preview using `capture.screenshot` and completion polling.
   Inspected baseline/query/preview images. Broad lighting/shadows remain;
   preview softens detail and is still noisy. No equal-count numeric image
   comparison was performed; quality acceptance is open.
6. Restored three shader files and the Editor header byte-for-byte. Rebuilt
   RelWithDebInfo and measured three restored baseline windows. `actor.query`
   confirmed camera root/world position and (0,180,0) rotation. Earlier runtime
   rotation/FOV were not separately exported. Closed each investigation-owned
   engine normally through its GLFW window using WM_CLOSE.

## Conditions and evidence

1094x742; unchanged `level/sponza.level`; camera (10.80743,1.59222,0), authored
FOV 58.5 degrees, sun intensity 1000, environment intensity zero, eight bounces.
AS: 728,189,952 bytes; resident textures: 469,885,796 bytes/81 dependencies.
Historical 1094x619/786,098,304-AS-byte windows are not the comparator. Four SPP
except explicit one-SPP preview. Application-requested validation follows normal
RelWithDebInfo `NDEBUG` policy (disabled); implicit layers were not independently
enumerated per launch. This is performance evidence, not Debug correctness.

Two-second system-wide telemetry stayed P4, 1020/6001 MHz, approximately 58–61 C.
No builds/tests or hardware-profiler collection overlapped sampling. All windows,
including higher first Scene Color-only/restored windows, are retained. Results
are means of window p50/p95, not pooled percentiles or presentation FPS.

Canonical measurements/findings:
[R4.7.8 review](../../docs/render/.review/R4.7.8.md#controlled-gpu-investigation--2026-09-27).
Generated evidence under `save/diagnostics/r478-investigation/`: window JSON,
source/binary/fixture SHA-256 manifests, telemetry CSV, `summary.json`, selected
trace metrics, `restoration.json`, camera/actor records, variants/diffs and the
local `sample.py`, `variants.py`, `desktop.ps1` harness. These are not committed
production tools. Captures remain under
`save/screenshots/validation/r478-investigation-*.png`; logs remain in `save/logs/`.

## Decision

Select explicit raster diagnostic demand, then independent/capability-appropriate
inline visibility after correctness gates. Their measured configuration is about
20% below the initial diagnostic-active GPU median, still four SPP/eight bounces
and above 16.667 ms. Separate one-SPP preview measures 9.474 ms p50/10.150 ms p95
on this static fixture, with changed sample work/filtering. No combined query/
preview result exists because current probe modes are mutually exclusive.

Reject the compact payload, Beauty specialization and primary re-trace trials.
Material-fetch cost is established in a simplified transport workload, not an
accepted texture/material/LOD optimization. No claim of a purely bandwidth-bound
renderer, a guaranteed wavefront win, or unchanged-four-SPP 60 FPS.
The durable decision is in the
[GPU plan](../../docs/render/.plan/R4.7-gpu.md#measured-direction-after-the-gpu-trace).

## Validation and final state

- `tools/kp.ps1 build KimPeanutEngine -Configuration RelWithDebInfo` passed for
  initial, temporary Scene Color-default and restored original source. The
  temporary relink emitted a `LNK4098` MSVCRTD conflict warning; its cause was
  not investigated. No source/build error blocked the runs.
- Changed diagnostic GLSL passed `glslangValidator -V --target-env vulkan1.2`
  with outputs under `save/diagnostics/r478-investigation/`. Runtime logs confirm
  changed shader cache store/hit hashes; no cache deletion was needed.
- Runtime stats/capture polling succeeded; original baseline was remeasured.
  Byte comparison passed for all four restored source files. No engine is left
  running and no Render/Graphics/Editor implementation change remains.
- Debug validation, tests, Cornell/another scene, motion/edit, resize/toggle/fault
  checks and equal-sample errors were not run. The diagnostic/plan deliverable
  does not close R4.7 implementation acceptance or change ownership/lifetime.
- Link/anchor checks found no introduced issues across six documents; five
  pre-existing anchors in TODO/status were left outside this scope.
  `git diff --check` passed with line-ending notices. Generated reports, logs,
  binaries and captures remain uncommitted.
