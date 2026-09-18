# Render Graph R3 Stage Closeout Journal

- Stage: [R3](../../docs/render/.plan/R3.md)
- Roadmap: [Render Graph TODO](../../docs/render/render_graph/TODO.md)
- Architecture: [Render Graph plans](../../docs/render/render_graph/PLANS.md)
- Date: 2026-09-18

## Scope

R3 ran from a baseline gate to a compiled render graph, then closed R3.7 as a
gate rather than as work. This entry consolidates the completed-stage record so
the roadmap carries only live items and acceptance criteria. Per-slice evidence
stays in the journals linked below; nothing here replaces them.

## What landed, stage by stage

**R3.0 — baseline and design trigger.** Closed the R1 lifecycle evidence gap
and recorded the fixed-window Vulkan/OpenGL issue-9.7 baseline: the eight-pass
schedule, captures, pass outcomes, and CPU/GPU timings that later stages were
compared against. Evidence is in the
[R1.5/Stage 6.0 closeout](2026-09-17-render-r1-5-stage6-0-evidence.md).

**R3.1 — reference gate and design review.** Inspected the pinned Sakura
frontend, phase chain, executor, pools, deferred sample, and ray-query sample
from source, and recorded adopted, modified, and rejected patterns in the
[Sakura analysis](../../docs/render/render_graph/sakura_analysis.md). The formal
plan review closed the remaining interface decisions before R3.2 was authorized:
[R3.1 review](../../docs/render/.review/R3.1.md).

**R3.2 — pure graph model and compiler.** Typed logical texture and buffer
handles, resource versions, imported and exported resources, pass declarations,
stable topological ordering, reachability culling, diagnostics, and lifetime
intervals. Compilation performs no Graphics calls and is covered by
deterministic unit tests.
→ [R3.2 journal](2026-09-17-render-graph-r3-2.md)

**R3.3 — fixed-schedule compatibility proof.** Expressed the existing eight
passes through the graph while `FixedRenderPassSequence` remained the execution
oracle, and compared compiled order, conditions, external terminal policy,
outcomes, and required resource edges without changing GPU recording.
→ [R3.3 journal](2026-09-17-render-graph-r3-3.md)

**R3.4 — graph-directed raster execution and migration.** The existing
`DeferredRenderer::Record*Pass` operations execute from the compiled plan
against the current persistent targets, one graphics queue, and the existing
backend transitions; the second authored pass order, the fixed sequence, its
executor,
and the parity scaffolding were removed only after focused, full, smoke, and
visual parity evidence on both APIs. The same investigation found that the low
frame rate reported against the G-buffer was not the renderer: the game lane
paced itself with a sub-frame `Sleep()` that Windows rounds up to its ~15.6 ms
timer granularity, and lock-stepping handed that period to the render lane.
→ [R3.4a](2026-09-17-render-graph-r3-4a.md),
[R3.4b](2026-09-17-render-graph-r3-4b.md),
[R3.4c](2026-09-17-render-graph-r3-4c.md),
[R3.4 review](../../docs/render/.review/R3.4.md)

**R3.5 — portable resource-state plan.** Graph uses carry a portable usage, an
attachment operation, and a whole-resource range; compilation emits one intent
per resource whose required usage changes, naming the usage and never a native
layout. A common contract consumed by Graphics translates it — Vulkan through
the frame context's single barrier emitter, OpenGL as documented implicit
ordering — and the executor owns the attachment boundary.
→ [R3.5 journal](2026-09-18-render-graph-r3-5.md),
[R3.5 review](../../docs/render/.review/R3.5.md)

**R3.6 — transient resource ownership.** Graph-declared transients served by a
Graphics-owned frame-safe pool. Declared resources carry a caller-owned
description key, the graph plans each with the window it is needed for, and the
renderer takes every declared transient from the pool before the frame records.
`SceneHdr` is the first: its contents never survive the frame, so it left the
persistent target set. A release is stamped with the pending submission and the
resource is handed out only once that submission has completed; reuse is trimmed
on resize and reports identity churn rather than assuming stability. No
aliasing, as the stage requires. Follow-ups in the same line of work trimmed the
pool on resize and exposed identity churn to the profile.

**R3.7 — gated extensions.** Five extensions were listed as separately gated
and none was started: subresources, memory aliasing, async compute, parallel
recording, and acceleration structures. Each carries a recorded unlock criterion
in the [stage plan](../../docs/render/.plan/R3.md); the closure is accepted as
of 2026-09-18, so the roadmap owes nothing here until a future stage picks a row
and names its consumer. The tree facts behind the closure: the transient pool
holds one declared resource, the engine has no compute shaders, no ray-tracing
work is planned, and R3.4 measured recording as *not* the binding constraint.
Subresource tracking is the only row with a plausible candidate — the
point-shadow atlas is six faces in one target and `GBuffer` is tracked whole —
but whole-resource tracking there is coarse, not wrong, which is the case its
criterion asks for.

## Decisions closed before R3.2

All five were resolved before implementation began, and the roadmap no longer
carries them:

- Handle invalidation/generation and resource-version semantics.
  → [R3.1 review](../../docs/render/.review/R3.1.md)
- Cached topology variants versus one compiled graph with runtime conditions.
- Whether attachment begin/end moves in R3.4 or R3.5, and the minimum
  pass-context API.
- Which composite targets are tracked as one logical resource during migration,
  and when individual attachments become graph resources.
  → [R3.4 review](../../docs/render/.review/R3.4.md)
- The R3.2 no-regression boundary: compilation is CPU-only and does not claim a
  runtime performance improvement, with R3.0 as the migration oracle.

## Faults this stage found

Seeking the acceptance evidence surfaced defects that the migration itself did
not cause:

- `CaptureViewPass` declared a `SceneColor` read that `RecordCaptureViewPass`
  never performs, which would have compiled a phantom transition.
- The host's editor viewport samples `CaptureOutput` whenever a diagnostic view
  is active and no renderer pass reads it, so the terminal pass had to declare
  that read before the plan could be authoritative for resource state.
- A whole-pass skip cannot be owned purely by the executor: a directional-shadow
  cache hit returns before opening its target on purpose, because opening clears
  the depth map the cache exists to preserve.
- Streamed texture retirement destroyed descriptor sets and a texture before
  their last user was done, which could end the render thread.

→ [R3.5 journal](2026-09-18-render-graph-r3-5.md) for the first three
in full.

## Validation

Per slice: complete build, complete suite, `GraphicsSmoke`, and both-API
captures diffed against the previous build. The suite reported 956/957 for every
slice; the single failure is the pre-existing `LevelLoaderTest` asset-content
problem, unrelated to the graph. Captures were pixel-identical on Vulkan and
OpenGL. Parity is behavioural, not numeric: no R3.0 comparator is stored in the
repository, so no numeric match is claimed.

## Remaining risk and open criteria

Two acceptance criteria are deliberately unmet and stay open on the roadmap:
a callback's declared uses are not enforced against what it actually touches,
and no numeric R3.0 comparator exists. Both are policy debts rather than known
faults.

R3.7's unlock criteria are the only path back into the extensions it gated.
Any future measurement of this area should note that Vulkan runs with validation
layers enabled in non-`NDEBUG` builds, so its Debug figures are not
representative and a `RelWithDebInfo` measurement comes first.
