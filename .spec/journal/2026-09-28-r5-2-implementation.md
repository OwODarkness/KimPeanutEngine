# R5.2 implementation journal — 2026-09-28

- Stage: [R5.2](../../docs/render/.plan/R5.2.md)
- Spec: [Render R5 ownership](../specs/render-r5-render-configuration-graph-ownership.md)
- Status: implementation in progress; runtime and contract acceptance incomplete

## Changes

- Added Render-owned typed import roles, exact physical binding groups, group
  validation, per-member transitions and attachment-scope resolution in
  `render_graph_bindings.{h,cpp}`. Geometry bindings include each vertex and
  index buffer; scene instance and scratch groups come from Graphics-prepared
  common handles. Name-based virtual-buffer and SceneBLAS transition exceptions
  were removed.
- Added the common `RayTracingResourceOwner::PrepareBuildResources` bridge and
  Vulkan implementation for instance-input/scratch buffers. Build descriptions
  remain the command input; Graphics keeps native storage and buffers. Prepared
  tokens are cancelled on frame failure/finalization. RTA scratch graph writes
  are declared as `StorageWrite` versions.
- Added `RenderGraphExecutor`, which composes `RenderGraphFrame` and owns graph
  transitions, attachment and profiling brackets, record/reuse/fail disposition,
  external terminal transitions, transient leases/rollback, scoped pass lookup,
  and typed finalization outcomes. `DeferredRenderer` retains scene policy,
  pass dispatch, resource preparation, and path-tracing history progression.
- Added binding and executor unit-test sources and wired them into Render tests.

## Validation evidence

- `.\tools\kp.ps1 build KimPeanutEngine` — Debug build succeeded after the
  final source changes.
- `.\tools\kp.ps1 build RenderGraphTest`, `.\tools\kp.ps1 build
  RenderPassScheduleTest`, `.\tools\kp.ps1 build RenderSystemTest`, and
  `.\tools\kp.ps1 build GraphicsContractTest` — all targets built in
  serial Debug configuration.
- `ctest --test-dir build -C Debug -L "render|graphics" --output-on-failure` —
  203/203 tests passed. The first run exposed duplicate external-terminal
  invocation poisoning frame success; the exactly-once guard was fixed and the
  full labeled suite rerun successfully.
- Runtime smoke on the approved Default desktop with Vulkan Debug and
  `level/cornell_box.level` — `path_trace_active=true`; PT, tone-map, GBuffer,
  debug view, and Editor Composite outcomes executed; tracked texture residency
  complete; Runtime screenshot export succeeded to
  `save/screenshots/validation/r5-baseline-cornell-r52-final.png`. The latest
  run log contains no Vulkan validation, graph binding/transition, or executor
  errors.
- Sponza Vulkan Debug started and reached active PT, but the sampled plan had
  BLAS/TLAS build outcomes `not_in_plan`. This is not evidence that the new
  preparation bridge executed. It loaded 127 assets in about 46 seconds and
  showed 50 tracked textures still nonresident at the probe sample.

## Remaining acceptance

- Obtain a runtime frame whose selected plan includes BLAS and TLAS build
  passes, then verify prepared inputs/scratch and Vulkan validation while those
  callbacks execute.
- Add lease partial-failure, unsupported preparation provider, attachment
  balance, cached-shadow reuse,
  external ordering, optional capture and context-violation contracts.
- Verify Vulkan raster/PT, simultaneous Capture/Viewer, resize/reload, failed
  required recording, and OpenGL raster. Freeze and run matched image comparison
  and static-reuse checks before closing R5.2.
- The callback dispatch remains a `DeferredRenderer` responsibility. Pass
  texture lookups use `RenderGraphPassContext`; deeper migration of buffer/AS
  lookups and a full audit proving every pass consumes only context-declared
  resources remain for the gate.

## Review follow-up — 2026-09-28

User requested a risk review and a concrete way to close the unverified AS
bridge gate. Reviewed HEAD `fc4a08d6554ebf046d635d95f35a9819a4dc7b84` plus
the uncommitted implementation. Findings and remediation are canonical in
[R5.2 review](../../docs/render/.review/R5.2.md).

Correction to the interpretation of smoke evidence: Cornell's final saved
probe is frame 1745; `not_in_plan` there does not establish that startup never
selected build passes. Source requests builds only for unbuilt/changed AS
state. Exported zero build counters also need executable/publication
reconciliation; they do not substitute for retained first-build evidence.
The 17:05:23 Cornell log records repeated TLAS preparation failures in an
intermediate run, and the 16:48/17:02 logs include image-layout validation
messages. Their exact binary revisions were not established by this review;
they must not be attributed automatically to the final successful smoke or
silently omitted from the investigation history.

Five open findings cover AS build/abort/retirement evidence, direct buffer/AS
access outside the pass context, external failure erasure on duplicate calls,
ignored context violations, and prepared-token/descriptor identity. The
review specifies retained first-build events and, if needed, a proposed safe
one-shot frame-boundary rebuild diagnostic. No such command was implemented.

Only review documentation, the R5.2 plan's evidence wording, roadmap links
and the status pointer changed. No production code, ownership, fixture,
build, test or runtime behavior changed. Builds/tests/runtime were not rerun
for this documentation-only review; historical 203/203 results remain
attributed to the preceding implementation. R5.2 remains in progress.

Documentation validation: `git diff --check` passed with existing LF/CRLF
notices. PowerShell checked local Markdown link target files in the five
changed documents; all exist. Anchor content was not independently validated.

## Review remediation — 2026-09-28

Implemented source corrections for R5.2 review findings F2–F5:

- Renderer BLAS/TLAS callbacks now use the pass context recorder and require
  declared, correctly-intended AS/buffer groups for targets, geometry, BLAS
  references, instance inputs, and scratch. PT and ray-query AS descriptors use
  the declared `SceneTlas`. PT address-table vertex/index buffers are now
  explicit `SceneGeometry` graph reads and are checked against that group.
- Context lookups record undeclared handle/role or wrong-access requests. The
  executor rejects a callback that ignored such a violation and retains a
  diagnostic naming the pass and key.
- External cursor callbacks now return success, so a failed transition records
  `Failed` and required failure. Duplicate calls cannot clear the original
  failure; a rejected duplicate after success does not poison frame success.
- Vulkan prepared batches own copies of build mode, geometry and instance
  descriptors. Build checks the caller descriptors and exposed instance/scratch
  lists against the token snapshot before recording, then records from the
  owned snapshot. Cancelling a never-recorded batch immediately destroys its
  temporary buffers; recorded batches remain queued for submission retirement.
- Added a frame-cursor regression for external recording failure.

Validation on this remediation: `.\tools\kp.ps1 build RenderGraphTest` and
`.\tools\kp.ps1 build Render` succeeded in Debug. A direct CMake build first
failed before compilation because MSBuild received case-colliding `PATH` and
`Path` environment keys; the project wrapper normalized the environment and
then succeeded. `git diff --check` passed, with only Git's existing LF/CRLF
conversion notices. Tests were compiled but not executed in this remediation.

Runtime follow-up used the approved visible Default-desktop workflow. The first
Sponza attempt exposed two incorrect access requests (Deferred Lighting's
`SceneHdr` is a write, and Ray Tracing Tone Map does not consume GBuffer); both
were corrected. A rebuilt Sponza process then reached fully resident PT at
frame 21415, with GBuffer, PT, Ray Tracing Tone Map, and Editor Composite
executed; screenshots exported to `save/screenshots/validation/` and the
retained snapshot is `save/diagnostics/sponza-albedo/r52-review-stats.json`.
Its build outcomes were already `not_in_plan`, so it is not cold-build
acceptance. The process outlived the first GLFW close request, delaying a
fresh-process attempt; it exited before this turn ended.

F1 remains open: this turn did not add the bounded per-build event history or
produce retained cold BLAS+TLAS, TLAS-update, partial-failure, abort, submitted-
serial and retirement runtime evidence. R5.2 must stay in progress until that
evidence and its failure-injection checks are captured.

## Retained AS lifecycle diagnostics and runtime follow-up — 2026-09-28

Added a fixed-capacity Graphics-owned AS build event history, copied into the
published render profile and exposed as scalar `stats --json` fields. Each
record captures frame/token/target identity, build mode and counts, a hashed
physical-handle signature, prepare/record/cancel/submit/retire stage, failure
reason, recorded command count, serials, and whether temporary buffers were
reclaimed immediately. No native Vulkan object or address is exposed to Render.
The graph remains the source of pass outcomes; frame numbers correlate those
outcomes with the Graphics history.

Fresh Debug Vulkan launch on the approved Default desktop with
`level/cornell_box.level` established:

- Cold build frame 3: BLAS and TLAS each recorded one build; BLAS covered eight
  geometries and sixteen geometry buffers; TLAS covered one instance input.
  Both events reached `retired` at submission/completion serial 4, with no
  failure and no immediate reclamation. The saved stats snapshot is
  `save/diagnostics/r5-2-cold-cornell-stats-initial.json`; the exported PT image
  is `save/screenshots/validation/r5-2-cornell-cold-vulkan.png`.
- After a runtime-only Cornell actor transform, frame 5108 recorded a TLAS-only
  `update` (one instance; no BLAS event) and retired at serial 5109. The actor
  was restored to its original transform. Snapshot:
  `save/diagnostics/r5-2-cold-cornell-stats-transform.json`.
- A separate Debug OpenGL Cornell launch reported `deferred` mode, GBuffer and
  deferred lighting `executed`, ray tracing `not_in_plan`, and external
  composite `executed`. Capture succeeded at
  `save/screenshots/validation/r5-2-cornell-opengl.png`; snapshot:
  `save/diagnostics/r5-2-cornell-opengl-stats.json`.
- New typed executor tests cover an ignored undeclared lookup and a failed
  external transition followed by a rejected duplicate call. Both pass.

The instrumentation and these Cornell runs closed the cold-build, TLAS-update,
submission-serial and temporary-buffer-retirement observation gates for that
fixture. At the time of this entry, Sponza had not been relaunched because
`asset/level/sponza.level` has unrelated pre-existing edits. A later section
records Sponza runtime checks that read the current fixture without editing it.
Partial preparation/recording failure, abort before submission, unchanged-frame
allocation reuse, Capture/Viewer demand and matched image-error comparison
remain open. R5.2 stays in progress.

Validation for this follow-up: both new cases passed through
`ctest --test-dir build -C Debug -R
"RenderGraphExecutorTest\\.(IgnoredUndeclaredLookupFailsTypedPassRecording|ExternalTransitionFailureStaysFailedAfterDuplicateRejection)"
--output-on-failure`. Final `.\tools\kp.ps1 build` succeeded and
`.\tools\kp.ps1 test` passed 1009/1009 tests.

## Additional Sponza and required-failure runtime checks — 2026-09-28

Read the existing locally modified `asset/level/sponza.level` without editing
it. A fresh Vulkan PT run recorded BLAS and TLAS builds on frame 9955 and
retired both at serial 9956; the sample still had 54 tracked textures
nonresident. The screenshot was exported to
`save/screenshots/validation/r5-2-sponza-vulkan-current.png`.

A separate Vulkan launch with `--disable-path-tracing` reached
`hybrid_ray_query`, with ray-query shadows active and GBuffer, deferred
lighting and Editor Composite executed. Its cold BLAS/TLAS records retired at
serial 21367. After `window.resize` reached 1280x720 and `level.reload`
succeeded, the published snapshot reported zero incomplete tracked textures;
the scene viewport was 722x389 after Editor layout. Capture succeeded at
`save/screenshots/validation/r5-2-sponza-vulkan-raster-query-resized.png`.
Snapshot: `save/diagnostics/r5-2-sponza-vulkan-raster-resize-reload.json`.

The existing `render.path_trace_fail_next` command was exercised on Cornell.
Its command returned success, and the process log at
`save/logs/2026-09-28/KimPeanutEngineLog-2026.09.28-19.22.05.txt` records the
zero-width RT dispatch rejection followed by Render graph frame-finalization
failure at 19:22:42. A bounded stats poll did not retain the brief failed
frame outcome. A later snapshot at frame 6107 showed PT, tone-map and external
composite executed again, with 24404 accumulated samples and no history reset:
`save/diagnostics/r5-2-cornell-required-failure-recovery.json`.

An additional Cornell Vulkan launch with `--disable-path-tracing` reported
`hybrid_ray_query`, ray-query shadows active, GBuffer/deferred lighting/
Editor Composite executed, and the path-trace pass `not_in_plan`. Capture
succeeded to `save/screenshots/validation/r5-2-cornell-vulkan-raster.png`;
snapshot: `save/diagnostics/r5-2-cornell-vulkan-raster-stats.json`.

Remaining R5.2 gaps: retain the required-failure frame's exact pass outcomes
and sample state; inject partial AS preparation failure, recording failure
after some BLAS commands, and abort before submission; verify unchanged-frame
preparation reuse; exercise Capture/Viewer demand; and freeze/run matched
image-error comparison. No performance claim is made.
