# Render Graph R3.4a Graph Execution Frame Journal

- Stage: [R3.4](../../docs/render/.plan/R3.md)
- Review: [R3.4 review](../../docs/render/.review/R3.4.md)
- Date: 2026-09-17

## Scope

Added the graph-side execution frame and a caller-owned pass key that lets a
compiled plan carry the renderer's typed pass identity. Nothing in
`DeferredRenderer` changed: the fixed frame remains the only live scheduler, so
this slice is non-behavioral at runtime.

## Changes

- `RenderGraphPassDesc` carries an optional caller-owned `user_key`, propagated
  to the compiled pass. Authors who need to dispatch or query by pass set it.
- Compilation rejects two enabled passes that share a key, since only enabled
  passes reach an executor and the collision would make dispatch ambiguous. A
  disabled pass may reuse an enabled key because it is culled.
- Added `RenderGraphFrame`, the graph-side counterpart of
  `FixedRenderPassFrame`: renderer sweep in compiled order stopping at the
  external terminal, external terminal exactly once and never prematurely,
  finalize validation, per-key outcome query, and the required-failure latch.
- `GetOutcome` reports `NotInPlan` for a key absent from the compiled plan. That
  is how a culled conditional pass reports its state, and it keeps the frame
  from inventing an outcome for a pass it never saw.
- The fixed oracle always requires one external terminal; the frame treats it as
  optional, so a graph that ends after its renderer passes finalizes normally.
  An unrequested terminal finalizes as `SkippedExternal`, matching the oracle.

## Validation evidence

```text
cmake --build build --config Debug --target RenderGraphTest
  passed

ctest --test-dir build -C Debug -R "RenderGraph" --output-on-failure
  16/16 passed (6 RenderGraphTest, 1 compatibility, 9 RenderGraphFrameTest)

cmake --build build --config Debug --target Render RenderPassScheduleTest
  passed

ctest --test-dir build -C Debug -R "FixedRenderPass" --output-on-failure
  13/13 passed

git diff --check
  clean apart from repository line-ending normalization warnings
```

The compatibility proof now also asserts that every compiled pass carries the
matching `FixedRenderPassId` as its key, so the ordinal that indexes the profile
arrays and the backend GPU-profile slots survives graph compilation.

## Not performed by design

- No change to `DeferredRenderer`, command recording, persistent targets, or
  backend transitions.
- No resource, barrier, transient, or subresource work.
- No Vulkan/OpenGL smoke or capture rerun; the live frame path is untouched.

## Remaining risk

R3.4b must promote the canonical declaration into Render so the graph and
`FixedRenderPassSequence` derive from one authored source, then run both frames
per frame and compare visited order, outcomes, required-failure, and capture
readiness. The compiled variant for a culled conditional pass reports
`NotInPlan`, while the oracle reports `SkippedCondition` for the same pass, so
the comparator must map those two states explicitly rather than assuming equal
enums.
