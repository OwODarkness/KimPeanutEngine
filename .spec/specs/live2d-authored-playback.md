# Live2D Authored Playback

- Status: proposed; implementation has not started
- Parent roadmap: [Live2D Module Roadmap](../../docs/live2d/TODO.md)
- Architecture: [Live2D Module Plans](../../docs/live2d/PLANS.md)
- Concrete design: [L2D6](../../docs/live2d/.plan/L2D6.md)
- Journal: create a dated L2D6 journal when L2D6.0 execution begins

## Objective

Extend the native Live2D product and per-character model instance so callers
can deterministically play authored motions and expressions with priority,
cross-fade, cancellation, completion, and value-owned events. Preserve the
Asset/runtime/render ownership boundaries established by L2D1–L2D5.

## Scope

L2D6 includes typed Product V2 motion/expression/parameter-group data,
transactional offline import, per-instance SDK clip construction, one primary
motion channel, one logical expression channel, caller-supplied time, and one
canonical parameter-update transaction.

It excludes automatic idle/emotion selection, physics/pose/blink/breath/gaze,
audio/lip sync/TTS, Gameplay/Editor integration, arbitrary blend graphs,
programmatic looping/seeking/time scaling, and Render/Graphics changes.

## Invariants

- Asset core remains unaware of Live2D; the module owns its importer, product,
  loader, and playback adapter.
- `Live2DModelResource` is immutable shared data. Parsed SDK motions,
  expressions, managers, callbacks, clocks, priorities, tokens, and events are
  owned independently by each `Live2DModelInstance`.
- Public product/playback contracts expose no Cubism or native backend type.
- Runtime reads native Product V1 or V2 and never loads source files; authored
  playback consumes typed V2 data only and never reinterprets optional chunks.
- Time is finite, non-negative, and supplied by the caller. Playback does not
  read a clock, choose randomly, or silently clamp time.
- Motion checkpointing, expressions, future secondary behavior, final model
  update, and snapshot extraction have one documented order.
- Accepted playback events are emitted exactly once from bounded storage; user
  code is never invoked from an SDK callback, and user-event source identity is
  not fabricated when R5 omits it.
- Playback mutates model parameters only and has no Render, Graphics, Editor,
  Audio, Gameplay, or TTS dependency.
- Existing V1 products and no-playback model updates remain supported.

## Stages

1. [L2D6.0 — Playback contract freeze](../../docs/live2d/.plan/L2D6.0.md).
2. [L2D6.1 — Typed animation product and import](../../docs/live2d/.plan/L2D6.1.md).
3. [L2D6.2 — Instance-local clip library](../../docs/live2d/.plan/L2D6.2.md).
4. [L2D6.3 — Deterministic playback transaction](../../docs/live2d/.plan/L2D6.3.md).
5. [L2D6.4 — Playback hardening and L2D7 handoff](../../docs/live2d/.plan/L2D6.4.md).

Stages execute in order. A red exit gate blocks the next stage; implementation
must not merge product migration, SDK ownership, and behavior semantics into
one unreviewable change.

## Acceptance

L2D6 is complete only when every acceptance item in the
[concrete plan](../../docs/live2d/.plan/L2D6.md#acceptance-criteria) is green and
the implementation journal contains product compatibility, malformed-input,
two-instance isolation, priority/fade/expression numeric, event, lifecycle,
SDK-off, and standalone-viewer evidence.

Compilation alone cannot close mutable runtime/lifecycle behavior. Any missing
proprietary SDK or fixture evidence remains an explicit blocker rather than a
compile-only pass.

## Validation

Use the target ladder in the concrete plan. At minimum, build and run the
Live2D core/asset/import suites, build the main runtime, run all Live2D CTest
cases, and execute the standalone viewer long enough to cross one authored
motion's completion and shutdown boundary.

Escalate to broader validation if Product V2 changes a common Asset interface,
if module target dependencies change outside Live2D, or if the final diff
touches Render/Graphics despite the stage boundary.

## Risks

- Product V2 must preserve typed model3 identity without creating an Asset-core
  special case.
- Parsed Cubism motion objects contain mutable state and cannot be cached in
  the shared payload.
- Priority replacement and queue fade-out produce overlapping internal entries
  that require bounded capacity and explicit terminal classification.
- Direct parameter writes and later L2D7/L2D9 contributors can become
  order-dependent unless the update transaction remains the sole authority.
- Cubism/model fixture licensing may limit CI and official-probe evidence.
