# Render Graph TODO

**Status: R3 complete — the compiled plan schedules passes, owns resource state,
and declares its transients. R3.7 closed as a gate.** Architecture:
[PLANS.md](PLANS.md). Concrete migration: [R3](../.plan/R3.md).

## Roadmap

- [x] **R3.0 — baseline and design trigger:** close the R1 lifecycle evidence
  gap and record the fixed-window Vulkan/OpenGL issue-9.7 baseline. Preserve the
  current eight-pass schedule, captures, pass outcomes, and CPU/GPU timings as
  the migration oracle. The baseline evidence is recorded in the current
  2026-09-17 R1.5/Stage 6.0 closeout work.
- [x] **R3.1 — reference gate and design review:** inspect Sakura Engine's
  render-graph builder/compiler/executor, transient pools, command callbacks,
  and deferred sample from actual source. Compare a small number of other
  primary references only where they resolve a concrete uncertainty. Update
  this architecture with adopted, modified, and rejected patterns; perform a
  formal R3 plan review before implementation.
  - [x] Inspect the pinned Sakura frontend, phase chain, executor, pools,
    deferred sample, and ray-query sample from actual source.
  - [x] Record source-backed adopted, modified, and rejected patterns in the
    [Sakura analysis](sakura_analysis.md), and apply the resulting architecture
    refinements to `PLANS.md`.
  - [x] Perform the formal R3 plan review and close the remaining interface
    decisions before authorizing R3.2. → [R3.1 review](../.review/R3.1.md)
- [x] **R3.2 — pure graph model and compiler:** implement typed logical texture
  and buffer handles, resource versions, imported/exported resources, pass
  declarations, stable topological ordering, reachability culling, diagnostics,
  and lifetime intervals. Compilation performs no Graphics calls and is covered
  by deterministic unit tests.
- [x] **R3.3 — fixed-schedule compatibility proof:** express the existing eight
  passes through the graph while retaining `FixedRenderPassSequence` as the
  execution oracle. Compare compiled order, conditions, external terminal
  policy, outcomes, and required resource edges without changing GPU recording.
  → [R3.3 journal](../../../.spec/journal/2026-09-17-render-graph-r3-3.md)
- [ ] **R3.4 — graph-directed raster execution:** execute existing
  `DeferredRenderer::Record*Pass` operations from the compiled plan using the
  current persistent render targets, one graphics queue, and existing backend
  target transitions. Remove the second authored pass order only after focused,
  full, smoke, and visual parity evidence passes on Vulkan and OpenGL.
  → [R3.4 review](../.review/R3.4.md)
  - [x] R3.4a graph execution frame and caller-owned pass key; no runtime
    wiring. → [journal](../../../.spec/journal/2026-09-17-render-graph-r3-4a.md)
  - [x] R3.4b single authored declaration and per-frame dual-path parity; the
    fixed path stays authoritative.
    → [journal](../../../.spec/journal/2026-09-17-render-graph-r3-4b.md)
  - [x] R3.4c switched scheduling to the compiled plan, removed the fixed path
    and its executor, and passed the full gate on both APIs.
    → [journal](../../../.spec/journal/2026-09-17-render-graph-r3-4c.md)
- [x] **R3.5 — portable resource-state plan:** graph uses carry a portable
  usage, an attachment operation, and a whole-resource range; compilation emits
  one intent per resource whose required usage changes, naming the usage and
  never a native layout. A common contract consumed by Graphics translates it
  (Vulkan through the frame context's single barrier emitter, OpenGL as
  documented implicit ordering), and the executor now owns the attachment
  boundary. The plan is now **authoritative**: the backend's end-of-pass
  transition is removed, so a target nothing reads keeps its attachment layout.
  Reaching that required finding the consumer the plan did not declare -- the
  host's editor viewport samples `CaptureOutput` whenever a diagnostic view is
  active, and no renderer pass reads it, so the terminal pass now declares that
  read. The imported-resource handoff remains deferred: no import path exists to
  consume it. → [R3.5 review](../.review/R3.5.md),
  [journal](../../../.spec/journal/2026-09-18-render-graph-r3-5.md)
- [x] **R3.6 — transient resource ownership:** graph-declared transients and a
  Graphics-owned frame-safe pool. Declared resources carry a caller-owned
  description key, the graph plans each with the window it is needed for, and
  the renderer takes every declared transient from the pool before the frame
  records. `SceneHdr` is the first: its contents never survive the frame, so it
  left the persistent target set. Reuse is serial-quarantined, trimmed on
  resize, and reports identity churn rather than assuming stability. No
  aliasing, as the stage requires.
- [ ] **R3.7 — measured extensions — closed as a gate, not started.** None of
  the five extensions has a consumer, so building any would be the speculative
  abstraction the stage exists to forbid. Each has a recorded unlock criterion
  in the [R3 plan](../.plan/R3.md); a future stage picks one, names its
  consumer, and plans it as its own work rather than resuming R3.7.

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
  The pool stamps a release with the pending submission and only hands the
  resource out once that submission has completed.
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
  close, and repeated shutdown preserve current lifecycle behavior. Seeking this
  evidence found and fixed a real fault: streamed texture retirement destroyed
  descriptor sets and a texture before their last user was done, which could end
  the render thread.

## Decisions required before R3.2

- [x] Finalize handle invalidation/generation and resource-version semantics.
  → [R3.1 review](../.review/R3.1.md)
- [x] Decide whether normal/capture/editor topology uses cached variants or one
  compiled graph with runtime conditions.
- [x] Define the minimum pass-context API and whether attachment begin/end moves
  in R3.4 or R3.5.
- [x] Define which current composite targets are tracked as one logical resource
  during migration and when individual attachments become graph resources.
  → [R3.4 review](../.review/R3.4.md)
- [x] Set the R3.2 no-regression boundary: compilation is CPU-only and does
  not claim runtime performance improvement; R3.0 remains the migration oracle.
- [x] Create [.spec/specs/render-graph.md](../../../.spec/specs/render-graph.md).

R3.4 execution/parity remains the next gate. Reference engines inform the
decision; they do not define this API.
