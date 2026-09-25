# Render Module TODO

**Status: active.** This is the Render-level roadmap. Submodule implementation
details and stage checklists belong in the linked submodule documents.

## Submodule roadmaps

- [Material System](material_system/TODO.md) — render-owned material templates,
  instances, asset resolution, and frame-local bindings.
- [Deferred PBR](deferred_pbr/TODO.md) — attachment contract, G-buffer,
  lighting, shadows, environment, and presentation.
- [Render Capture](render_capture/TODO.md) — semantic capture requests,
  conversion views, readback, and screenshot export.
- [Render Scene](render_scene/TODO.md) — common scene-recording boundary and
  removal of legacy scene seams.

## Cross-cutting Render work

- [ ] **issue-9.7 — Sponza texture aliasing and frame throughput:** Stages 0–5,
  Stage 6.0 telemetry, Stage 6.1 effective shadow validity, Stage 6.2 stable
  bindings, Stage 6.3 recorder/packet reuse, and Stage 6.4 dirty-range OpenGL
  uploads are landed,
  including the unchanged editor viewport-size cache fix. The supplied profile
  attributes the remaining approximately
  30 FPS result to 28.12 ms of CPU command recording versus 10.98 ms on the
  GPU. Stage 6 targets ineffective shadow reuse, per-draw descriptor updates,
  redundant state work, section-packet copying, and OpenGL whole-arena uploads;
  its fixed-scenario record-time budget is 12.73 ms p95. Runtime Stage 6.0
  proof is now recorded for Vulkan Debug, Vulkan RelWithDebInfo, and OpenGL
  RelWithDebInfo; Stage 6.5 re-profiling and the larger performance gate remain
  open.
  The solved black-frame defect is out of
  scope. →
  [issue](issue/issue-9.7.md), [plan](.plan/issue-9.7.md),
  [review](.review/issue-9.7.md),
  [spec](../../.spec/specs/sponza-render-quality-performance.md),
  [journal](../../.spec/journal/2026-09-07-sponza-render-quality-performance.md),
  [Stage 6.0 evidence](../../.spec/journal/2026-09-17-render-r1-5-stage6-0-evidence.md)
- [x] **R1 — RenderSystem responsibility split:** R1.1–R1.5 code fixes are
  landed and the independent source findings are addressed. The comparator now
  bounds edge and structural differences, Runtime skips recoverable failed
  begins without presenting, and Editor UI initialization rolls back
  transactionally. Dual-backend orderly application-close evidence is recorded
  on 2026-09-17. →
  [R1 plan](.plan/R1.md),
  [R1.5 review](.review/R1.5.md),
  [R1.5 journal](../../.spec/journal/2026-09-04-render-system-r1-5.md)
- [x] **R1.1 — characterization and transactional lifecycle** landed
  2026-09-01: injected the existing `RenderBackend` factory, added explicit
  `Uninitialized`/`Ready`/`FrameActive`/`ShutDown` states, transactional
  initialization with diagnostics, reverse cleanup, and idempotent shutdown.
  `RenderSystemTest` records the current pass/frame, capture, resize, editor,
  rollback, and teardown behavior.
- [x] **R1.2 — deferred renderer and pass-owned state:** implementation is
  landed in the [stage design](.plan/R1.2.md) shape; all four findings in the
  [formal review](.review/R1.2.md) are addressed, and focused/full build,
  smoke, and fresh Vulkan/OpenGL capture evidence passed. The direct renderer
  owns named targets, pass-private handles, environment/shadow state, pass
  recording, and cleanup; R1.3 sequence unification and R1.4 ingestion cleanup
  remain separate stages. → [R1.2 spec](../../.spec/specs/render-system-r1-2.md), [R1.2 journal](../../.spec/journal/2026-09-02-render-system-r1-2.md)
- [x] **R1.3 — fixed pass declaration/execution unification:** implementation
  landed 2026-09-02; the [formal review](.review/R1.3.md) fixes canonical
  typed-ID role binding, moved-from cursor reuse, and stale module records.
  Focused/full build, CTest, dual-backend GraphicsSmoke, and fresh Runtime
  startup-level captures pass. The landed
  [stage design](.plan/R1.3.md) and [execution spec](../../.spec/specs/render-system-r1-3.md)
  now correspond to one immutable typed sequence, conditional diagnostic
  capture, and optional terminal editor composition. Render-graph and R1.4
  work remain out of scope. → [R1.3 journal](../../.spec/journal/2026-09-02-render-system-r1-3.md)
- [x] **R1.4 — ready asset ingestion and Render bootstrap removal:**
  source review findings are addressed: the catalog readiness/const contracts,
  ordinal role invariant, injectable preparation tests, and durable records are
  fixed. Full build and CTest pass, and all six required fixture captures were
  exported and inspected. R1.5 closed the comparator risk with bounded
  contour/area/edge metrics and synthetic rejection probes. → [R1.4 review](.review/R1.4.md),
  [R1.4 journal](../../.spec/journal/2026-09-02-render-system-r1-4.md)
- [x] **R1.5 — facade hardening and R1 evidence:** implementation and review
  fixes landed 2026-09-04. Focused lifecycle/rollback tests, full Debug
  validation, dual-backend smoke, six inspected captures, and the durable
  Render/Graphics record updates are complete; orderly Vulkan/OpenGL
  application-close evidence was recorded on 2026-09-17. →
  [R1.5 plan](.plan/R1.5.md), [R1.5 review](.review/R1.5.md),
  [R1.5 journal](../../.spec/journal/2026-09-04-render-system-r1-5.md),
  [closeout evidence](../../.spec/journal/2026-09-17-render-r1-5-stage6-0-evidence.md)
- [x] Add direct RenderSystem orchestration tests for partial-init rollback,
  fixed pass order, conditional capture, resize, terminal editor composition,
  and reverse-order teardown before moving the corresponding code (R1.1).
- [ ] **R2 — adaptive render spatial index:** introduce one `RenderSpatialIndex`
  query boundary with `Flat` / `Bvh` / `Auto` strategies and workload
  metrics, so culling granularity and structure can change without touching
  pass policy. Acceptance: `Flat` and `Bvh` return identical handle sets for
  identical world state, an unchanged frame does no build work, and `stats`
  reports primitive/visible counts, build/refit/query time, visited nodes,
  tested primitives, and moved count. Status: **design only, not authorized for
  implementation** — the 2026-09-14 measurements show the current workload does
  not benefit, so the work waits for a scene that justifies it. →
  [R2 design](.plan/R2.md), [spatial-bvh spec](../../.spec/specs/spatial-bvh.md)
- [x] **R3 — render graph foundation (2026-09-18):** the graph keeps
  `DeferredRenderer` as policy owner, compiles logical pass/resource
  dependencies in Render, records through the common command seam, and leaves
  physical allocation and native synchronization in Graphics. The compiled plan
  is authoritative for pass order, resource state, and transients; R3.7's five
  extensions are closed as a gate with unlock criteria. →
  [R3 design](.plan/R3.md),
  [Render Graph plans](render_graph/PLANS.md),
  [Render Graph roadmap](render_graph/TODO.md),
  [closeout journal](../../.spec/journal/2026-09-18-render-graph-r3-closeout.md)
- [ ] **R4 — ray-tracing foundation and render-graph integration:** R4.0–R4.4
  established the selected ray-query shadow consumer, graph correctness,
  API-neutral RT contract, imported TLAS, and scheduled BLAS/TLAS work. R4.5
  integrates the Vulkan ray-query shader and still owes lifecycle/resource
  evidence. R4.6 is a separate Cornell Box path-tracing
  validation path: full RT-pipeline primary rays, hit reconstruction,
  rectangular area-light sampling, at least one diffuse bounce, progressive
  accumulation, and a same-contract comparison against the local
  `save/cornell_box_ref.jpeg` as a visual guide. Vulkan runtime dispatch and
  progressive samples are observed; visual framing/quality and full lifecycle
  evidence still block acceptance. → [R4 design](.plan/R4.md),
  [R4.6 spec](../../.spec/specs/render-r4-6-cornell-path-tracing.md),
  [R4.6 review](.review/R4.6.md),
  [Render Graph roadmap](render_graph/TODO.md)
- [ ] **R5 — post-R4.6 Render/Graphics decoupling (design only):** review the
  deferred renderer's mixed graph execution, pass recording, shadow/RT state,
  and lifetime ownership; repair enabled-conditional-pass failure semantics
  and typed physical binding coverage before extracting the graph executor and
  concrete pass families. Graphics contract narrowing and Vulkan RT owner
  decisions follow evidence, not file size. **Do not implement until R4.6 is
  accepted with real RT runtime and Cornell comparison evidence.** →
  [R5 review](.review/R5.md), [R5 plan](.plan/R5.md)
- [ ] Keep source registries, immutable snapshots, pass scheduling, and
  frame-local resource lifetime aligned across Render submodules.
- [ ] Add read-only Gameplay/editor snapshots before exposing mutable gameplay
  state to editor tools.
- [x] Revisit the explicit pass schedule only after measured dependency,
  aliasing, or scheduling pressure exists. The schedule is now compiled (R3,
  complete); the user-planned ray-tracing consumer unlocks the acceleration-
  structure row as the separate proposed R4 stage rather than reopening R3.7.
- [ ] Keep Render documentation links, validation evidence, and ownership
  boundaries current when a submodule lands work.

## Document ownership

This file tracks only Render-wide work. Use each submodule's `TODO.md` for
subtask status, `.plan/` for concrete stage design, and `.spec/journal/` for
dated implementation evidence.
