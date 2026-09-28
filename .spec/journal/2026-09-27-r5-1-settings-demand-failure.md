# R5.1 — Settings, Demand, and Failure Semantics (2026-09-27)

Plan: [R5.1 concrete design](../../docs/render/.plan/R5.1.md) ·
[R5 stage map](../../docs/render/.plan/R5.md) ·
Spec: [Render R5 execution spec](../specs/render-r5-render-configuration-graph-ownership.md)

## Task

Implement the source and CPU-contract portion of R5.1: copied path-tracing
settings, consumer-scoped diagnostic demand, and required-pass failure
propagation. Runtime capture validation was initially gated by the R5.0 raster
baseline; a 2026-09-28 follow-up is recorded below.

## Changes

- Added validated `PathTraceSettings` for PT enablement, hybrid query shadows,
  visibility method, SPP, continuation-bounce budget, reconstruction, output
  probe, and texture policy. `RenderSystem` validates and queues one copied
  snapshot under a mutex; `BeginFrame` consumes it at the frame boundary.
- Kept `PathTraceProbeMode` as a legacy adapter with explicit field mappings.
  Shader settings reuse the existing probe word and preserve the uniform layout.
  Requested/effective settings and fallback reasons are available in profiler
  command output.
- Added bounded Editor Viewer and Runtime Tooling debug-view consumers. The
  Viewer publishes only while active, releases when hidden, and releases on
  destruction. Pending captures remain their own demand and the active debug
  view is still supplied alongside the capture to preserve dependency union.
- Added explicit required/optional pass failure policy and a skipped-dependency
  outcome. Failed writes poison dependent outputs, while independent passes
  continue. Failed external composition is not recorded, and failed output is
  not accepted for capture readback. Optional capture failure leaves valid
  Scene Color composition available.
- Updated injected fullscreen-resource failure tests: a required lighting
  failure now makes `EndFrame()` report failure while still exercising owner
  cleanup/retry behavior.

## Architecture impact

- RenderSystem owns the cross-thread request snapshot. DeferredRenderer owns
  requested/effective settings and capability fallback resolution.
- Diagnostic views are scoped to two concrete consumers; capture ownership and
  readback lifetime remain in RenderCaptureService.
- The graph owns failure propagation and outcome reporting. Graphics/RHI
  resource ownership and common backend API were not changed.
- No backend-native type or Runtime-to-Editor dependency was introduced.

## Validation

- `.\tools\kp.ps1 build KimPeanutEngine` — PASS (Debug).
- `.\tools\kp.ps1 build RenderGraphTest` — PASS.
- `ctest --test-dir build -C Debug -R "^(RenderGraphTest|RenderGraphFrameTest|PathTraceSettingsTest)\." --output-on-failure` — PASS (27/27).
- `.\tools\kp.ps1 build RenderSystemTest` — PASS.
- `ctest --test-dir build -C Debug -R "^(RenderSystemLifecycleTest|RenderSystemEnvironmentTest)\." --output-on-failure` — PASS (16/16).
- `.\tools\kp.ps1 build RenderPassScheduleTest` — PASS.
- `ctest --test-dir build -C Debug -L render --output-on-failure` — PASS (162/162).
- No engine executable was launched in the sandbox.

## Open validation and limits

- Default Scene Color, explicit diagnostic views, and the two legacy probe
  settings transitions are captured and recorded in the 2026-09-28 follow-up.
  Scene Color capture plus a World Normal Viewer preview succeeded while the
  Viewer was open. Distinct simultaneous converted diagnostic outputs remain
  constrained by the shared `CaptureOutput`.
- Capture and Viewer can request together, and their pass/resource demands are
  retained, but the current renderer has one shared converted `CaptureOutput`.
  If their requested conversion views differ in the same frame, capture has
  output priority and the Viewer observes that shared target for that frame.
  Separate simultaneous outputs require a later target/pass design.
- A required graph failure suppresses the Render external-composite callback
  and rejects readback. Runtime skips the OpenGL swap when `EndFrame()` reports
  failure. Vulkan still submits and presents its acquired image to satisfy
  swapchain synchronization; safely suppressing that requires an acquired-image
  abandonment/present policy in Graphics. `EndFrame()` reports false so callers
  can observe the failed frame.
- Existing query/preview/denoise combinations and query-disabled portability
  have not received runtime visual or shader-quality validation.

## Runtime follow-up — 2026-09-28

- Built `KimPeanutEngine` in Debug and launched visible runtimes on the verified
  `Default` desktop. Vulkan Debug enables `VK_LAYER_KHRONOS_validation` under
  the device's non-`NDEBUG` policy. Captures and stats used Runtime commands.
- Captured Vulkan PT Beauty, then changed the legacy probe to `primary_albedo`.
  Runtime stats reported requested/effective output probe `0 -> 3`, active PT,
  and no fallback. A second transition to `ray_query_visibility` reported
  requested/effective visibility method `0 -> 1`, also active and without
  fallback. Screenshot paths are recorded in the R5.0 baseline journal.
- Fresh `world_normal` and `shadow_visibility` captures both exported when
  requested sequentially. An initial batch sent multiple screenshots before
  the first completed and hit the single-pending-capture limit; serial retries
  passed. `editor.panel.list` reported the Debug Viewer open and active, and the
  `engine_window` capture shows its World Normal preview alongside Beauty.
- The Vulkan/OpenGL RT-off Cornell pair is now the raster parity reference.
  The dark Sponza fallback was explained by zero authored IBL and lack of raster
  indirect bounce; it remains an unsuitable parity oracle, rather than evidence
  of cross-backend parity.

## Runtime follow-up — independent diagnostic outputs (2026-09-28)

- Added a separate Render-owned `DebugViewOutput` target and `DebugViewPass`.
  The Runtime Viewer samples that target; semantic screenshot readback continues
  using `CaptureOutput`. Their graph conditions, attachment transitions, and
  conversion views are independent, including when both are active in one frame.
- Expanded fixed-pass and GPU profiler slots for the Viewer conversion. The
  Editor profiler and Runtime stats report the new pass separately.
- `Debug` `KimPeanutEngine` build passed. `RenderGraphTest` passed 15/15 and
  `RenderGraphCompatibilityTest` passed 7/7, including plans for both outputs
  enabled together.
- On Vulkan Debug Sponza, `editor.panel.list` reported the Debug Viewer open and
  active on World Normal. `capture.screenshot` exported Base Color while that
  Viewer remained active. A following `engine_window` capture shows Scene Color
  in the main viewport and World Normal in the Viewer. Runtime stats confirmed
  PT active and the separate debug-view conversion pass executing. Captures:
  `save/screenshots/validation/r5-1-simultaneous-base-color.png` and
  `save/screenshots/validation/r5-1-simultaneous-engine-window.png`.
- The engine shut down cleanly after capture. No validation-layer diagnostics
  were observed. The R5.0 PT graph-outcome index and exit review remain open;
  this closes the remaining R5.1 runtime acceptance item.
