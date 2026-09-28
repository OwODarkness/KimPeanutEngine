# R5 — Render configuration, graph execution, and ownership

- Status: active; R5.0 raster baseline established, PT graph outcomes/exit review open; R5.1 runtime acceptance partial
- Owner: Render
- Parent TODO: [Render R5 roadmap](../../docs/render/TODO.md)
- Design: [R5 stage plan](../../docs/render/.plan/R5.md)
- Source baseline: `9089df26325b5976b0ab2bc20bde46febd2ac9a`

## Objective

Decouple Render frame policy and graph execution from the growing
`DeferredRenderer` while preserving current raster/PT behavior, R4.7 CPU and
GPU reuse, common Graphics contracts, and safe resource retirement. R5.0 freezes
the observed source/data flow and establishes comparable baselines before any
runtime extraction.

## Current state

The source baseline is the exact HEAD above. At R5.0 start, `git status --short`
contained documentation-only edits for the R5 refresh and related R4.7.8
findings; no Render, Graphics, Editor, Runtime, shader, or build-system source
was modified. The historical R4.8 label has no corresponding plan, review, or
spec in `docs/` or `.spec/`. R4.7 evidence remains under its original IDs and is
not relabeled as accepted R4.8 work.

### Frozen behavior and ownership map

| Concern | Current owner/data flow | Frozen reuse/lifetime rule |
| --- | --- | --- |
| Requests and lifecycle | `RenderSystem` applies copied settings at frame boundaries, owns frame contexts, capture service, backend and renderer facade; Runtime supplies requests/callbacks. | Published metrics are completed-frame snapshots. Runtime does not depend on Editor. |
| Scene and material inputs | `RenderSceneCoordinator`, `RenderWorld`, and `MaterialSystem` produce immutable/revisioned CPU-side records; `DeferredRenderer` consumes them. | World/material revisions key scene-record reuse. Authored light signature is independent. Unchanged records avoid section/material walks, table packing, and address-patch generation. |
| Frame plans and graph | `DeferredRenderer` eagerly compiles the five-condition cross product into 32 plan slots. `RenderGraphFrame` borrows a compiled graph and owns per-frame outcomes/cursor/finalization state. | Keep all 32 cached plans initially. The frame object owns no GPU resources; the external Editor terminal remains ordered after renderer passes. |
| Physical frame resources | `DeferredRenderer` resolves logical graph imports and transient leases to common Graphics handles; Graphics pools/allocates physical resources. | History is a two-target ping-pong pair. Frame imports and transient leases are frame scoped. Existing string-keyed geometry/BLAS expansion remains a known R5.2 issue. |
| Raster passes | `DeferredRenderer` records shadow, GBuffer, deferred-lighting, tone-map, diagnostics, and capture work through the common backend/recorder. | Scene Color raster and diagnostic/capture dependencies stay distinct. RT-off currently records raster draws but its known Sponza capture is mostly black and is not a parity oracle. |
| RT preparation and tables | `DeferredRenderer` owns Render-side scene descriptors, revisions, record cache, pass policy and per-frame binding-cache keys. Graphics owns immutable GPU reference tables, descriptors, AS storage/builds, command submission and retirement. | Preserve frame-slot/history-parity binding reuse; static unchanged scene records and table uploads remain zero after warm-up. Vulkan native types stay below common Graphics. |
| History and filtering | `DeferredRenderer` owns PT history signature/sample progression and guide/filter targets; PT shader implements estimator, preview, denoise and visibility variants. | Scene/camera/extent/estimator changes reset history according to the signature. Filtering/output mode interactions are not fully characterized and remain open. |
| Debug demand | Editor Debug Viewer requests one `CaptureView` through `RenderSystem::SetDebugView`; graph conditions include diagnostic producers for that request. | Current single global request starts at World Normal in the editor and can keep raster diagnostics active with PT. It is not yet consumer-scoped or cancelled on hide/destruction. |
| Shutdown | `RenderSystem::Shutdown` delegates to owned-state/scene cleanup; `DeferredRenderer` releases its pass/history/binding wrappers and calls Graphics-owned destroy/retire paths. | Exact in-flight guarantees for Vulkan RT pipeline, SBT, descriptor and table cleanup remain an audit item; no lifetime defect is asserted without evidence. |

The five plan-condition bits are diagnostic capture, RT BLAS build, RT TLAS
build, ray-query shadows, and PT. The 32 variants are policy, not a measured
bottleneck. Current graph conditions are not sufficient to distinguish
inclusion from required execution: conditional producers are Optional and
dependent skip/failure propagation is an open correctness issue.

## Frozen measurement and comparison contract

Use the controlled R4.7.8 Vulkan Sponza run as the current PT baseline where
conditions match: `level/sponza.level`, 1094x742, camera position
(10.80743, 1.59222, 0), fixture rotation (0,180,0), 58.5-degree authored FOV,
sun intensity 1000, environment intensity 0, four SPP/eight continuation
bounces, fully tracked residency (469,885,796 bytes, 81 dependencies),
728,189,952 AS bytes, 120 warm-up frames and 300 samples per window. It used
Vulkan RelWithDebInfo with the normal NDEBUG validation policy; this is timing
evidence, not Debug correctness evidence. The restored-source run has three
windows: PT GPU p50/p95 mean 28.545/32.296 ms, total GPU p50/p95 mean
33.356/37.186 ms, and CPU total p50 mean 33.236 ms. Scene Color-only 4-SPP
Beauty is a separate, diagnostic-demand-off baseline (six windows across two
labels): PT/total GPU p50 means 28.363/28.579 ms. Scene Color plus query
visibility is another separate setting: PT/total GPU p50 means
26.039/26.250 ms. These are not interchangeable baselines.

Use R4.7.5's current static-frame/cache evidence as the reuse baseline: Debug
Vulkan Cornell reports zero scene-table records packed/uploaded after warm-up,
cache hits on unchanged frames, and cache misses when the level reloads. Its
three-window RelWithDebInfo Sponza candidate reports 24.568 ms PT GPU p50,
28.610 ms total GPU p50, 28.258 ms CPU total p50, with 120 warm-up and 300
samples per window at 1094x619. It is a distinct camera/output-size/config
baseline from R4.7.8 and must only be compared under those matched conditions.

| Comparison dimension | R5 budget and rule |
| --- | --- |
| Repeated timing | At least three serial windows per variant; each uses 120 warm-up and 300 samples after residency/history stabilize. Report each window and the median of window p50/p95 values; never pool frames or call a latest snapshot a percentile. |
| Performance regression | For matched conditions, more than +5% median p50 in CPU total, total GPU, or the affected pass triggers investigation and blocks stage acceptance until explained or explicitly accepted. More than +10% p95 also blocks acceptance. A delta within observed window variation is inconclusive and requires more matched windows. No FPS target is introduced. |
| Static reuse | After warm-up on an unchanged resident scene: zero scene-table records packed/uploaded, zero address-table upload bytes, zero new RT descriptor sets/pools or address-table buffers, and zero BLAS/TLAS rebuild/update counts. Existing cache hit/miss counters and allocated bytes are recorded. One-time setup allocations are reported separately. |
| Graph correctness | Capture the selected plan/mode, included/executed/skipped/failed outcome for each relevant producer, and terminal handoff. Required producer failure must not produce a successful dependent output; stale history is not success. |
| Image parity | Same backend, fixture, camera, output dimensions, settings, and accumulated sample count. Keep representative baseline PNGs and use a defined image comparison before accepting extraction. No numeric image-error threshold is frozen yet because the current evidence does not include equal-count image-error analysis. |
| Backend correctness | Debug Vulkan validation is correctness evidence. OpenGL raster and Vulkan raster/PT are separate baselines. RelWithDebInfo is used for performance only; do not compare its validation policy to Debug as a compiler-only effect. |

## Scope and non-goals

R5 covers Render-owned settings resolution, consumer demand, graph execution,
pass-family ownership, common Graphics schemas/timing, and optional graph
extensions only when measured need exists. It preserves the existing PT
estimator, raw four-SPP Beauty, R4.7 scene/table/binding reuse, eager plan count,
OpenGL fallback contract, and common API boundary. It does not tune GPU shaders,
promise 60 FPS, declare open R4 acceptance gates closed, or expand the known
RuntimeLib/EditorLib dependency cycle.

## Invariants

- Asset/Resource preparation stays outside Render pass callbacks.
- Render owns policy, logical graph state and frame inputs; Graphics owns
  physical GPU allocation, native commands, synchronization and retirement.
- Common Render/Graphics APIs do not expose backend-native types.
- Compiled plans outlive frames; frame imports, callbacks and transient leases
  cannot retain frame-local pointers after completion.
- A required producer failure prevents dependent presentation/readback success.
- Editor terminal composition stays explicit and last; multiple consumers must
  not cancel one another when R5.1 introduces scoped demand.
- No new graph abstraction is added without a current consumer and measurable
  reason.

## Stages

1. **R5.0 — baseline freeze:** reconcile the R4.8 label, inventory ownership,
   variants, cache keys and lifetimes; complete affected raster/PT baselines and
   freeze comparison rules. Current PT/cache baselines above are usable. Fresh
   Vulkan/OpenGL raster evidence and the affected RT-off parity baseline remain
   established on the Cornell fixture. The 2026-09-27 mostly-black RT-off
   Sponza output used zero authored IBL, and raster adds no indirect bounce.
   Sponza now authors IBL at `0.35`; fresh Vulkan captures verify RT-off raster,
   PT-off with ray-query shadows active, and path tracing with the environment
   enabled. Cross-backend Sponza parity and PT graph outcomes remain open. Do
   not extract a path lacking a reproducible baseline.
2. **R5.1 — settings, demand and failure:** independent copied settings and
   legacy mappings; scoped viewer/capture demand; required-producer failure and
   dependent skips.
3. **R5.2 — graph execution:** typed one-to-many physical bindings, Render-side
   executor, transient rollback, transitions and terminal ordering.
4. **R5.3 — pass owners:** move one cohesive family at a time with cache/history
   invalidation and init/cleanup parity.
5. **R5.4 — common Graphics contracts:** preserve shader ABI while narrowing
   uniform/timing schemas; decide RT retirement/split based on actual safety.
6. **R5.5 — optional extensions:** only with measured demand; a documented
   no-change decision is acceptable.
7. **R5.6 — closure:** validation matrix, cross-backend visual/lifecycle checks,
   and matched RelWithDebInfo checks against the frozen budgets.

## Acceptance criteria

- [x] Historical R4.8 is reconciled without inventing R4.8 acceptance or
  relabeling R4.7 evidence.
- [x] Current source owners, reuse keys, frame lifetimes, known graph failure
  semantics and 32-plan policy are recorded.
- [x] Existing Vulkan Sponza PT and static reuse measurements are recorded with
  exact conditions and known limitations.
- [x] Repeated timing, cache/reuse, graph outcome and image parity comparison
  rules are defined.
- [x] Fresh Vulkan/OpenGL raster captures and a usable RT-off raster parity
  baseline are recorded on Cornell; the dark Sponza fallback is diagnosed and
  retained as non-parity evidence.
- [ ] Cornell/Sponza representative current PT captures and graph outcomes are
  indexed under the frozen conditions.
- [ ] R5.0 exit is reviewed against the remaining PT graph-outcome rows before
  R5.2 extraction; R5.1 source implementation proceeded earlier by user
  direction while the raster baseline was open.
- [x] R5.1 adds validated copied settings applied at a frame boundary, explicit
  legacy probe mappings, requested/effective stats, active Editor Viewer demand,
  and required-pass dependency propagation. Focused CPU contracts cover setting
  validation/mapping, consumer release, and failed producer outcomes.
- [x] R5.1 Vulkan Beauty/Primary-Albedo runtime captures and legacy probe
  transitions are verified through requested/effective stats.
- [x] R5.1 explicit diagnostic-view captures and Scene Color capture alongside
  the active World Normal Viewer are verified through Runtime.

## Validation plan

R5.0 and R5.1 implementation contracts were built in Debug and validated with
the focused RenderGraphTest and RenderSystemTest contracts. On 2026-09-28,
Runtime screenshots verified Vulkan/OpenGL Cornell RT-off raster and Vulkan
Sponza Beauty/Primary-Albedo output. Legacy probe transitions were verified
through requested/effective stats. World Normal and shadow-visibility captures
succeeded. The Debug Viewer was active while Scene Color captures ran; the
engine-window capture shows the Beauty viewport and World Normal preview
together. Distinct simultaneous converted diagnostic outputs, indexed PT graph
outcomes, and the R5.0 exit review remain open. Runtime validation uses Runtime
commands and checked-in fixtures on the normal `Default` desktop, with captures under
`save/screenshots/validation/`.

## Risks and open questions

- The 2026-09-27 RT-off Sponza capture is mostly black because that fixture
  authored zero environment intensity and rasterization does not add indirect
  bounce. The fixture now uses `0.35`; updated Vulkan raster, ray-query, and PT
  captures show environment contribution. Use Cornell for cross-backend raster
  parity until the Sponza comparison is re-established across backends.
- Scene Color capture and World Normal Viewer output succeeded together.
  Distinct simultaneous converted diagnostic views still share one
  `CaptureOutput` and remain unverified as an independent-output combination.
- Existing R4.7.8 Sponza timing conditions are suitable for PT policy work but
  do not stand in for every capture/debug graph variant.
- No equal-sample image-error method or tolerance has been validated.
- Vulkan pipeline/SBT/table/descriptor retirement under in-flight work still
  needs a source and runtime audit before any Graphics owner split.
- Combined query/preview/denoise settings and query-disabled RT portability are
  not evidenced by current measurements.


## Baseline correction — 2026-09-28 opaque surface parity

The [surface-parity correction](../journal/2026-09-28-sponza-pt-surface-parity.md)
excludes alpha-blended sections from the opaque RT scene, matching raster's
existing policy, and preserves original section material indices. Current
fully resident Sponza has 446 RT geometry records versus 450 before this
correction. The older timing windows remain historical comparators; capture a
new matched baseline before performance acceptance of subsequent R5 changes.
The scalar-texture/LOD and environment changes also remain part of the current
working tree. This correction does not close R5 or transparent/cutout quality
gates.
