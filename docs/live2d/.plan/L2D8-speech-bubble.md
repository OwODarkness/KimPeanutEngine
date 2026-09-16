# L2D8 Companion — Speech Bubble Overlay

- Status: **landed** (2026-09-15)
- Parent: [PLANS.md](../PLANS.md) · [TODO.md](../TODO.md)
- Journal: [2026-09-15-live2d-l2d8.md](../../../.spec/journal/2026-09-15-live2d-l2d8.md)

## Goal

A manga-style speech bubble belonging to the character: a bubble that hugs its
text, carries a tail aimed at the model, pops in when its text is set, and appears
in the same image as the model.

## Why this is Live2D's

A speech bubble is a **character-presentation** element. It is positioned
relative to the model, points at it, and is shown in the model's image — so it
belongs beside the character system, and the dependency direction is the natural
one: a character system using a display, not a display knowing about characters.

The text itself is a panel's content, so the bubble **consumes the panel module**
and nothing consumes the bubble. The panel stays unaware of this module.

## The bubble is not dot-matrix, but its content is

An earlier attempt drew the bubble's shape into the same dot matrix as its text,
as a chunky pixel outline. That was the wrong shape: the bubble should read as
*drawn*, and only its content should read as a display.

So the bubble's outline is a **signed distance field** — a rounded body unioned
with a tail — and the dot matrix is the text's alone. That also removed a change
that had been planned and is no longer needed: because the fragment shape provides
transparency natively, there is no need to distinguish "paper" from "transparent"
per dot, and the mask stays one bit per dot with its stamping, merge modes, and
byte-alignment invariant untouched.

**Inside** the bubble, though, every dot is a cell — lit or not — with a bezel
between them. Without that the paper is a flat field and the text reads as ink on
a page rather than as a matrix of elements. It takes three colours to say it:
the paper, the lit dot, and the gap.

## Design

**The tail is a chain of circles.** A quadratic Bezier sampled into eight capsules
with tapering radii, rather than a triangle: a chain leaves the body along its
edge and comes to a point, which is what makes a tail read as drawn instead of
extruded. The shader evaluates it with the same distance function it already uses
for the body, and the chain starts on the body's edge so the union has no seam.

**Everything is in text-height units.** One unit is the height of the text the
bubble holds, so a layout is the same at any resolution and a radius stays a
circle rather than being stretched by the quad's aspect.

**The grid's pitch comes from the text's dot count**, so characters land inside
cells rather than across them. A grid of any other pitch would stretch every
character and make the bezel lopsided.

**The tail aims at the model, and only numbers cross.** `Live2DRenderer` publishes
where the model was fitted, in the same normalized space the placement is
expressed in; the host turns that into a direction and hands the bubble a
direction, not a Live2D type.

**The pop-in is carried by the placement matrix.** The shader always draws the
finished bubble and the transform does the growing, so the shape's proportions are
exact at every frame rather than being resampled smaller. The tail's root stays
fixed and the centre is pulled toward it, so the bubble grows out of the model
rather than out of its own middle.

**Placement is a matrix, not a viewport.** OpenGL measures a viewport's `y` from
the bottom and Vulkan from the top; they agree only for a full-extent viewport at
`y == 0`. A sub-rect placed by viewport would be vertically mirrored between the
two backends.

## Composition: inside the model's pass

The bubble's draws are **appended to the model's pass**, not given one of their
own. Both backends re-apply the target's `load_op` clear on every pass begin, so a
second pass into the same target erases the model — which validates cleanly and
produces an image containing only the bubble.

`Live2DRenderer::Record` therefore accepts generic extra draws and appends them to
the pass the planner pushes last, after the model's own so they paint over it.
Nothing about the bubble crosses into the renderer: it receives
`render::RenderSubmission` draws, which is the seam the module boundary rests on.

## Acceptance

- [x] The bubble's geometry is CPU-only and testable with no device, no shader,
      and no Cubism framework.
- [x] The placement keeps the bubble's proportions in any target aspect, and the
      pop-in keeps the tail's root fixed.
- [x] The bubble composites into the same image as the model, on both backends.
- [x] The grid's cells align with the text's dots.
- [x] The tail points at the model, derived from its fitted bounds as plain data.
- [x] The viewer without a bubble is byte-for-byte what it was.

## Validation

See the journal for commands and output. The decisive check is a capture
containing the model **and** the bubble: a capture containing only the bubble is
the failure mode this design exists to avoid, and it looks like success.

## Known limits

- **Not attached to an anchor on the model.** The placement is a fixed 2D
  position and scale; attaching it to a point on the model needs hit areas, which
  is L2D7's surface and does not exist yet.
- **One bubble at a time**, and no rotation.
- **The bubble's appearance is not settable at run time.** The panel's commands
  cover the panel; the bubble sets its own colours at startup.
- **`--panel-glyph-product` and `--panel-text` are accepted in both viewer
  modes**, because a bubble's content is a panel's content. The panel's own look
  options stay panel-only: a bubble has its own appearance, so a panel colour in
  this mode would be a mistake rather than a convenience.
