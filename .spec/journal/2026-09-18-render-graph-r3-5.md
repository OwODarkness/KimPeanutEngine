# Render Graph R3.5 Resource-State Journal

- Stage: [R3.5](../../docs/render/.plan/R3.md)
- Review: [R3.5 review](../../docs/render/.review/R3.5.md)
- Date: 2026-09-18

## Scope

Three slices: a portable usage vocabulary and compiled transition intents, a
common contract that Graphics consumes and both backends translate, and an
executor-owned attachment boundary.

## What landed

**R3.5a — vocabulary and intents.** A use carries a portable usage, an
attachment operation, and a whole-resource range. Compilation emits one intent
per resource whose required usage changes, naming the usage only and never the
state a resource is currently in, because the backend owns that. The production
declaration derives twelve intents, asserted by test. Compilation rejects a
usage that contradicts a use's access and an attachment operation on a usage
that is not an attachment.

This surfaced a real defect: `CaptureViewPass` declared a `SceneColor` read that
`RecordCaptureViewPass` never performs. It would have compiled a phantom
transition, so the declaration was corrected. Pass order is unchanged.

**R3.5b — the common contract.** `RequireRenderTargetUsage` names the state a
target's attachments must be in as a purpose, never a layout or stage. Vulkan
translates it against the frame context's single barrier emitter, resolving the
current state from its own tracked layouts and emitting a barrier only where
they differ. OpenGL accepts it and documents that sampled reads are ordered
implicitly. The renderer applies each pass's requirements before that pass
records, through the executor seam that already existed.

**R3.5c — the boundary moved.** The executor opens the target before a pass
records and closes it after, deriving the target from the pass's write use. The
26 scattered `BeginRecording`/`EndRecording` sites in the seven pass bodies are
gone.

## What did not

**The plan is not yet the authority for transitions.** Removing
`EndRendering`'s transition was attempted and reverted. With it gone, three
colour images were sampled while still in the attachment layout; the descriptor
mismatch escalated to a texture-handle failure and killed the render thread.
All twelve intents per frame were confirmed applied -- the plan covers the six
colour images it knows about -- so at least one consumer reads a render target
outside the planned passes and remains unidentified. Candidates not ruled out:
the editor viewport's debug-view path, the sRGB preview view, or an early-frame
draw.

The end-of-pass transition is retained as a state-checked safety net: it
guarantees readability for consumers the plan does not know about without
re-emitting a barrier the plan already placed. Net behaviour is unchanged.

## A constraint the plan did not anticipate

The attachment boundary cannot be owned purely by the executor while a pass is
allowed to skip itself entirely. A directional-shadow cache hit returns before
opening its target on purpose, because opening clears the depth map the cache
exists to preserve. Hoisting the boundary would have broken shadows on most
frames. The executor therefore decides that one skip before it opens anything.
Any future pass with a whole-pass skip needs the same treatment, which is a
runtime condition the compiled plan cannot express -- the question R3.4's review
deferred when it chose compiled variants over runtime conditions.

## Validation

Full gate per slice: complete build, complete suite, `GraphicsSmoke`, and both
API captures diffed against the previous build.

- Suite: 956/957 for every slice; the failure is the pre-existing
  `LevelLoaderTest` asset-content problem.
- `GraphicsSmoke`: passed on both APIs for every slice.
- Captures: pixel-identical on Vulkan and OpenGL for every slice. Confirmed
  across a re-run after one OpenGL capture showed a 35% difference that a repeat
  did not reproduce -- a reminder that a single capture here can be an outlier.
- Vulkan validation: clean during normal operation. A killed process reports
  teardown errors (a view destroyed while a descriptor set still references it)
  beginning after the profile completes and coinciding with the kill.

## Remaining risk

The uncovered consumer is the gate for making the plan authoritative and for
removing the safety net. Until it is identified, R3.5 delivers the contract and
the wiring without a behaviour change, and R3.6 transients would inherit the
same gap.

The teardown error signature matches one reported from a manual run, which died
mid-frame with the render thread failing. Those are different situations -- one
is a kill, the other is a fault -- but both destroy an image view a descriptor
set still references, so teardown ordering is worth a separate look.
