# Render Graph TODO

**Status: R3 complete and closed. R4.2 is complete; R4.3 is next.** The compiled plan
schedules passes, owns resource state, and declares its transients; R3.7's five
extensions are closed as a gate with recorded unlock criteria. Completed-stage
detail moved to the R3 closeout journal:
[2026-09-18-render-graph-r3-closeout.md](../../../.spec/journal/2026-09-18-render-graph-r3-closeout.md).
Architecture: [PLANS.md](PLANS.md). Completed migration:
[R3](../.plan/R3.md). Next proposed stage: [R4](../.plan/R4.md).

## Roadmap

- [x] **R3.0 — baseline and design trigger (2026-09-17):** the fixed-window
  Vulkan/OpenGL issue-9.7 baseline recorded the eight-pass schedule, captures,
  pass outcomes, and CPU/GPU timings used as the migration oracle.
- [x] **R3.1 — reference gate and design review:** the pinned Sakura source
  was inspected and the adopt/modify/reject decisions were recorded before
  implementation was authorized. Pre-R3.2 interface decisions closed here. →
  [analysis](sakura_analysis.md), [review](../.review/R3.1.md)
- [x] **R3.2 — pure graph model and compiler:** typed handles, versions,
  imports/exports, pass declarations, deterministic ordering, reachability
  culling, diagnostics, and lifetime intervals; compilation performs no Graphics
  calls. → [journal](../../../.spec/journal/2026-09-17-render-graph-r3-2.md)
- [x] **R3.3 — fixed-schedule compatibility proof:** the eight passes were
  expressed through the graph against the fixed-sequence oracle without changing
  GPU recording. →
  [journal](../../../.spec/journal/2026-09-17-render-graph-r3-3.md)
- [x] **R3.4 — graph-directed raster execution:** passes record from the
  compiled plan, and the fixed sequence, its executor, and the parity
  scaffolding were removed after full parity evidence on both APIs. →
  [review](../.review/R3.4.md),
  [R3.4c journal](../../../.spec/journal/2026-09-17-render-graph-r3-4c.md)
- [x] **R3.5 — portable resource-state plan:** uses carry a portable usage,
  attachment operation, and whole-resource range; compilation emits intents that
  Graphics translates, and the plan is authoritative for resource state. →
  [review](../.review/R3.5.md),
  [journal](../../../.spec/journal/2026-09-18-render-graph-r3-5.md)
- [x] **R3.6 — transient resource ownership:** `SceneHdr` is declared by the
  graph and served by a Graphics-owned pool with serial-quarantined reuse,
  trimmed on resize, reporting identity churn. No aliasing.
- [x] **R3.7 — measured extensions — closed as a gate (2026-09-18).** None
  of the five extensions has a consumer, so building any would be the
  speculative abstraction the stage exists to forbid. Each has an unlock
  criterion in the [R3 plan](../.plan/R3.md); a future stage picks one, names
  its consumer, and plans it as its own work rather than resuming R3.7. Closing
  this box accepts the gate decision; it does not mean the extensions were
  built.
- [x] **R4.0 — ray-tracing consumer and clean-baseline gate (2026-09-19):**
  selected ray-query directional hard-shadow visibility with the existing
  directional shadow-map fallback, made Vulkan validation errors fail
  `GraphicsSmoke`, and recorded the Vulkan/OpenGL raster baseline. The
  attachment-scope work remains a validated raster-baseline change; no RT
  contract was added. → [spec](../../../.spec/specs/render-r4-ray-query-shadow.md),
  [review](../.review/R4.0.md),
  [journal](../../../.spec/journal/2026-09-19-render-graph-r4-0.md)
- [x] **R4.1 — graph correctness prerequisites (2026-09-19):** model
  preserving/update writes as dependencies on prior versions, propagate
  transition/binding failures, replace raster resource ordinals with explicit
  frame bindings, and execute buffer requirements. →
  [spec](../../../.spec/specs/render-r4-1-graph-correctness.md),
  [review](../.review/R4.1.md),
  [journal](../../../.spec/journal/2026-09-19-render-graph-r4-1.md)
- [x] **R4.2 — common Graphics RT contract (2026-09-19):** add API-neutral
  generational AS/RT-pipeline handles, validated build/pipeline/binding/dispatch
  descriptors, effective capability gates, and optional recorder operations
  without enabling a partial backend. →
  [spec](../../../.spec/specs/render-r4-2-graphics-rt-contract.md),
  [review](../.review/R4.2.md),
  [journal](../../../.spec/journal/2026-09-19-render-graph-r4-2.md)
- [ ] **R4.3 — graph RT vocabulary and imported-TLAS path:** add the graph AS
  and storage-image vocabulary, wire a Graphics-owned TLAS into one selected RT
  pass, and preserve the explicit fallback when the complete capability set is
  unavailable.
- [ ] **R4.4–R4.5 — scheduled builds and evidence:** move BLAS/TLAS build or
  update work into graph passes only after the imported-TLAS slice is proven;
  integrate its output with composite/tone-map/capture and validate lifecycle,
  fallback, captures, metrics, and Vulkan correctness. →
  [R4 plan](../.plan/R4.md)

## Acceptance ledger

- [x] One declaration is authoritative for pass identity, resource flow, and
  graph execution; manual duplicate ordering is removed after migration.
- [x] The current directional/spot/point shadow, G-buffer, deferred-lighting,
  tone-map, conditional capture, and external Editor behavior is preserved.
- [x] Graph compilation rejects cycles, missing producers, invalid handles,
  ambiguous writes, incompatible usage, and unsafe conditional dependencies
  with useful diagnostics.
- [x] Independent passes use declaration order as a deterministic tie-break.
- [x] Imported resources are never destroyed by the graph; transient physical
  resources remain Graphics-owned and retire only after submitted work is safe.
- [x] Common graph and Graphics contracts contain no Vulkan/OpenGL types.
- [ ] Graph callbacks record only through the common command seam and cannot
  access resources they did not declare. Recording goes through the common
  recorder, but a callback's declared uses are not enforced against what it
  touches; the declaration is authored, not policed.
- [x] Compilation is cached while topology/descriptions are unchanged, and
  graph build/compile/execute CPU costs are observable.
- [ ] Raster output and pass metrics match the R3.0 baseline on Vulkan and
  OpenGL within the reviewed comparator/performance policy. Parity is
  behavioural -- every slice captured pixel-identical on both APIs -- and no
  numeric R3.0 comparator exists in the repository, so this is not a numeric
  match and does not claim one.
- [x] Resize, failed begin, required-pass failure, optional capture, orderly
  close, and repeated shutdown preserve current lifecycle behavior.
- [x] R3.7's five extensions are recorded with unlock criteria and were not
  started without a consumer. This is the accepted outcome of the stage, not a
  deferred item.

## Document ownership

This file tracks only the render graph roadmap. Architecture lives in
[PLANS.md](PLANS.md), concrete stage design in [.plan/](../.plan/), formal
reviews in [.review/](../.review/), and dated implementation evidence in
[.spec/journal/](../../../.spec/journal/).

The two unchecked ledger items above are deliberately unmet acceptance criteria,
not unfinished stages. Unlock criteria for the gated extensions stay in the
[R3 plan](../.plan/R3.md); reference engines informed these decisions, but they
do not define this API.
