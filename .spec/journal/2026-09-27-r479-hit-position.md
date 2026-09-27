# R4.7.9 closest-hit position reconstruction — 2026-09-27

## Investigation and implementation

Replaced barycentric interpolation of three transformed vertex positions with
`gl_WorldRayOriginEXT + gl_HitTEXT * gl_WorldRayDirectionEXT`. Index, UV and
normal reconstruction and payload layout are unchanged. The candidate is kept
in the working tree pending numerical edge-case validation.

## Evidence gathered

- Vulkan 1.2 closest-hit baseline and candidate both compiled with
  `glslangValidator`, optimized with `spirv-opt --O`, and passed `spirv-val`.
  Optimized IR: 602 to 560 instructions and 11,344 to 10,460 bytes. No hardware
  register/spill report is available.
- Visible RelWithDebInfo Vulkan Sponza runtime reported active path tracing,
  1094x619, 4 SPP, 8 bounces, fixed camera and complete texture residency.
- Three 120-warm-up/300-sample windows per shader produced mean PT GPU p50
  24.381 ms baseline and 23.640 ms candidate (-3.04%). Mean total GPU p50 was
  29.023 vs 28.315 ms (-2.44%); mean PT and total GPU p95 also decreased.
- A further baseline/candidate telemetry pair retained the same memory clock,
  temperature range and mostly P4 state. Exact windows, conditions and image
  comparison are in [R4.7.9 review](../../docs/render/.review/R4.7.9.md).
- Baseline/candidate Sponza captures at 1324/1336 accumulated samples were
  visually equivalent; RGB MAE 0.10/255, channel p95 1, maximum 21.
- Candidate Cornell startup failed before path tracing activated: zero RT
  geometry records/invalid TLAS followed by Vulkan command-buffer submission
  failure. No Cornell capture was produced.

## Remaining work

Test large-coordinate and grazing hits, mirrored/nonuniform transforms, and
thin-surface secondary-ray offsets; repeat Cornell capture once the fixture's
Vulkan submission failure is resolved. Vulkan validation-layer state and
R4.7.8 hardware attribution are also open. R4.7.9 remains incomplete.
## Independent review follow-up — 2026-09-27

Reviewed the uncommitted stage against 54283a6 without editing runtime/shader
code. Parsed all six saved timing windows and confirmed the mean-of-p50
improvements (3.04% PT, 2.44% total GPU), matching camera/SPP/depth/residency
and 120/300 windows. Viewed the baseline and near-matched candidate PNGs; no
obvious regression. No new MAE calculation or equal-sample capture was made.
Khronos builtin definitions support the world ray reconstruction semantics.
Correctness/precision and exact quality/provenance findings remain open in
[R4.7.9 review](../../docs/render/.review/R4.7.9.md). No build, tests or engine
launch performed; no hardware register-pressure claim. Stage remains provisional.

Review documentation validation: git diff --check passed with existing LF/CRLF notices; local review/journal links and whitespace checks passed.
