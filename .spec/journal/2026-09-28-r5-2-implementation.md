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
