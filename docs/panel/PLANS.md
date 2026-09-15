# Panel Module Plans

The panel is a dot-matrix display: a grid of addressable dots that shows text and
named patterns. It exists for emotion display — `OvO` and similar — where the
output should read as a physical LED panel rather than as a rendered label.

Current work is in [TODO.md](TODO.md). Concrete stage designs are under
[.plan/](.plan/). Encountered behaviour, validation output, and corrections live
in [`.spec/journal/`](../../.spec/journal/).

## Why this is not Live2D

Live2D is an *authored-deformation* runtime: `.moc3` topology is fixed at
authoring time, and there is no way to add a drawable at run time. A panel that
displays arbitrary text needs dynamic content, so expressing it as a Live2D model
would mean:

- a Cubism Studio re-export and reimport for every new emote;
- one drawable per glyph — a single 8x8 ASCII set is over a hundred, against 262
  for an entire character model;
- a bounded clip budget, since the mask atlas holds four channels of nine
  regions;
- Cubism Studio and a licence in the loop, to render text.

So the panel is its own module. **Neither depends on the other**: the panel names
no Live2D type and Live2D names no panel type. When one emote should drive both a
model expression and a panel pattern, the *host* composes two independent
commands; it is not an ownership relationship.

## Layering

| Library | Contents | Dependencies |
| --- | --- | --- |
| `Panel` | glyph, dot matrix, layout, product format, UTF-8, ASCII preview | **none** — standard library only |
| `PanelImport` | TrueType face → glyph product (tool-side) | `Panel`, vendored `stb_truetype` |
| `PanelRender` | submission planner and renderer | `Panel`, `Render` |
| `PanelModule` | standalone viewer host and its commands | `PanelRender`, Runtime |

`Panel` linking nothing is a load-bearing property, not an accident: it is what
lets the representation and layout layers be tested with no GPU, no window, and
no backend, so their contract tests run everywhere.

## The two layouts, and why they differ

Storage is **unit-first for the font and group-first for the panel**.

**The font is unit-first.** A `GlyphCell` is a fixed 16x16 dot matrix — sixteen
`std::uint16_t` rows plus an advance — so the glyph table is one contiguous,
trivially copyable array and lookup is an index rather than an allocation per
character. 16x16 is the floor at which complex hanzi stay legible.

**The panel is group-first.** A `DotMatrix` is one flat row-major dot buffer. It
is not a grid of character cells, and the reason is decisive: a halfwidth glyph
advances 8 dots while occupying 16 columns of glyph storage, so **character
boundaries never align with a fixed cell width**. Two ASCII characters share one
16-dot cell, and cell-aligned stamping is therefore impossible. Addressing by dot
offset into a flat row is the only layout in which a stamp is well defined.

### The byte-alignment invariant

Advances are only ever 8 or 16, so a layout cursor is always a multiple of 8.
That keeps every stamp byte-aligned and makes compositing a byte-wise `OR`
instead of a bit-shift. `DotMatrix::StampGlyph` **requires** a byte-aligned
offset and ignores a misaligned one rather than shifting it: nothing in the
layout produces a misaligned offset, and silently writing partial bits is the
failure the buffer exists to avoid.

The row stride is padded to whole 64-bit words so every row starts aligned and a
stamp cannot straddle a row boundary.

### Dots are a cache, never the truth

`Panel` holds the logical content — one string and one origin per row — and is
the source of truth. `DotMatrix` is a **disposable cache**, rebuilt from it.
Content changes go through `Panel`; the dots are never edited to change what the
panel says. That rule is what prevents a shorter replacement from leaving the
previous text behind, which is otherwise a whole class of bug.

`Rebuild` is a pure function of the content, so rebuilding twice is
byte-identical, and wiping the dot buffer and rebuilding it must reproduce the
same image. Effects belong *above* the rebuild, as transient overlays applied
after it — never persisted into the content.

### The origin's unit

`SetTextAt(row, column, text)` takes `column` in **halfwidth steps** — one 8-dot
character — not in dots. The unit is fixed by the byte-alignment invariant: raw
dots would let a caller pass an offset that `StampGlyph` ignores, rendering an
empty panel with no complaint. Every value of the chosen unit is representable,
so the API has no silent failure mode and needs no validation.

An origin is **not** a placement model. It is one run per row. Two independent
runs on one row cannot be expressed, and that stays deferred until something
needs it; a placement list is a natural generalisation from here rather than a
different design.

## Attribute resolution

Resolution should match how fine an attribute genuinely varies.

| Attribute | Resolution | Why |
| --- | --- | --- |
| shape (on/off) | per dot, 1 bit | this *is* the display |
| colour | one uniform for the whole panel | colour varies per character at most, and per-dot would be 256x the data |
| gradients, shimmer | a shader function of position and time | procedural beats stored; uploading a colour buffer to make something shimmer is the signal to move it into the shader |

Per-character colour would need a character-resolution plane *and* a `Placement`
model, because halfwidth characters do not align to cells. It is deferred until a
consumer exists.

## Glyph production

A glyph product (`KPPNLGLY`) is baked from a TrueType face by
`KimPeanutAssetTool import-glyphs`. The product is derived from a licensed font,
so it lives in the git-ignored `asset/` tree and is never committed — the same
rule the Live2D fixtures follow.

Three rules in the bake are not obvious and each has cost a defect:

- **Bit order.** The rasteriser emits MSB-leftmost bytes; this module stores
  bit 0 as the leftmost dot, which is what makes the low byte hold the left eight
  columns. The bake converts. Getting it wrong mirrors every glyph, and mirrored
  hanzi still look plausible, so the tests pin the *stem side* of `E`, `L` and
  `J` rather than trusting a look.
- **Baseline.** One scale maps the em square onto the cell, and one baseline —
  derived from the face's ascent — places every glyph. Placement is by baseline,
  not by bounding-box top, which is what puts CJK and Latin on a shared baseline.
  A derived baseline outside the cell is **rejected**: Microsoft YaHei derives
  row 17 for a 16-row cell, and accepting it would bake a uniformly clipped set
  that still looks correct in aggregate.
- **Clipped ink is counted.** Ink outside the cell is dropped, so the bake reports
  how many glyphs lost ink. Without that count a bake that quietly loses
  descenders is indistinguishable from a correct one. Halfwidth glyphs are
  additionally forced to leave columns 8-15 clear, since a halfwidth glyph that
  uses them would bleed into the next character.

Note that CJK and Latin legitimately differ in height: a hanzi fills the em box
(about 14 of 16 dot rows) while a Latin capital sits at cap height (about 11).
That is how CJK typesetting works, and it is directly asserted rather than
tolerated. Latin is already at its maximum for a 16-row cell — its descender
reaches the last row — so making Latin larger would require a taller cell, not a
different scale.

## Rendering

The panel renders as **one quad sampling a dot-mask texture**. This is not only
the simplest option, it is the only one available: the generic submission
contract permits indexed, **non-instanced** draws only
([L2D4.3](../live2d/.plan/L2D4.3.md)), so a quad per character cell is out of
contract.

### The bezel is what makes it a matrix

Drawing the quad larger than the mask gives crisp blocks, but blocks alone are
not enough: **adjacent lit dots merge**. Five lit dots in a row rendered as one
solid bar, because the shader mixed the dot colour in wherever the mask was lit,
leaving no space between neighbours. The panel read as a low-resolution image
rather than as a display.

The fix is the **bezel**, and it is entirely a display concern — the
representation does not know gaps exist, and should not, or the gap would become
content that every glyph, effect, and test would have to carry. The fragment
shader takes the position *within* the current dot from `fract(uv * dotCount)`
and leaves a margin dark on every side. With it, the same five dots render as
five distinct cubes.

Two properties of that choice matter:

- **The gap is a fraction of a dot, not a pixel count**, so the bezel is
  resolution independent. The panel reads the same at any size or viewing
  distance, which is what a scene-placed panel will need.
- **It is a uniform**, not a constant, because the look is a look. `PanelDrawConstants`
  carries it in a `params` vec4 whose other lanes are reserved and must stay
  zero — packed as a vec4 rather than a bare float so std140 cannot introduce a
  padding member the two sides disagree about.

### Appearance is a parameter, and a colour has a space

The dot gap and the two colours are parameters of `PanelRenderPlanOptions`, and
they are reachable three ways: in code, through the `panel.set_appearance`
command, and — for a capture run — through `--panel-dot-color`. The launch option
exists because a standalone viewer has **no on-demand capture command**; its only
capture is at startup, so anything the image should show has to be set before
then. That is the same reason `--panel-text` exists.

`panel.set_appearance` applies **only the fields the caller set**, so the gap can
change without restating the colours. A value the shader would clamp is rejected
rather than reported as applied: `dot_gap` is checked against the shader's own
limit, so success never means something other than what was asked. Colours are
`#RRGGBB` literals, and one parse function serves both the command and the launch
option, so the spellings `panel.report` produces are exactly the ones accepted.

**Colours are display space; the shader works in linear.** The panel's target is
sRGB and the hardware *encodes on store*, so a colour picked as a hex literal has
to be linearised before it reaches the shader or it comes out lighter than
requested. The planner does that conversion, which means callers — and the
command, and the launch option — all speak the display space a person picks in.
White and black are fixed points of the curve, which is why the default look is
unaffected. This is verified end to end rather than argued: a capture taken with
`--panel-dot-color "#FFB000"` decodes to a lit dot of exactly `(255, 176, 0)`.

One papercut worth knowing: the command's `dot_gap` is declared `Float`, and a
schema `Float` requires a JSON *double*. Writing `{"dot_gap": 0}` is refused
because the transport types a literal by its value; write `0.0`.

### The colour ramp spans the content, not the panel

`panel.set_appearance` can also run a colour ramp from `dot_color` to
`accent_color`, horizontally, vertically, or outward from the middle, optionally
animating. It needs no new data — a few parameters and some arithmetic in the
fragment shader. The ramp is a cosine rather than a linear interpolation, so it
loops without a seam when it animates and returns to the first colour at both
ends of its span.

**It spans the lit extent, not the panel.** The first implementation normalised
across the panel, and measuring the result made the flaw plain: for `OvO 中文` on
a 512-dot-wide panel the text occupies the left twelfth, so the colour moved only
from `(255, 106, 0)` to `(239, 133, 105)` across the whole line — a gradient that
is effectively invisible over the thing it is meant to colour. The extent is
therefore measured where the mask is measured, in `PanelRenderer::UploadPanel`,
and travels on the proxy beside the mask handle, because it describes the mask
rather than the appearance. With it, the same text ramps from `(254, 107, 18)`
through `(42, 227, 252)` at its centre and back to `(255, 106, 2)`.

A blank panel has no extent, so it keeps the whole panel rather than an inverted
or zero-width one, and the shader guards the division.

### Enough pixels per dot

A bezel is invisible if a dot is two pixels wide, which is what the first
capture was: a 1024x512 target for a 512x256 dot grid. A dot needs several pixels
before its gap is anything but a slightly dimmer pixel.

So **the render target is sized to the panel, not to the window**: panel dot
extent times a pixels-per-dot constant. A display device has its own resolution,
and tracking the window would make the dot size a function of how large the
window happens to be — and would have nothing to say when the panel is placed in
a scene. `--resize` still moves the window; it no longer changes what is
captured, so the panel host does not defer its capture on a resize.

A full 512x256 panel is 128 KB as `R8_UNORM`, uploaded only when the content
changes, and 4096x2048 at the current eight pixels per dot.

Geometry is static, unlike Live2D: four clip-space positions in one immutable
vertex buffer, six `UInt16` indices in another, and nothing uploaded per frame.
The panel's content lives entirely in the texture.

The planner never touches a `CommandRecorder` and never calls the backend. It
builds a `render::RenderSubmission`, validates it, and publishes nothing on
failure, so a caller can never record half a panel.

## GPU ownership and lifetime

The panel owns every handle it creates — pipeline, sampler, quad buffers, output
target, and dot mask — and counts them, so shutdown can assert zero. The backend
exposes no in-place texture upload, so a content change **recreates** the mask:
create the replacement, `WaitIdle`, then release the previous handle. Releases
run in reverse creation order.

## Hosting

`panel-viewer` is a standalone application mode: it owns its window, backend,
frame contexts, renderer, and glyph product, and drives its own render thread. A
host is necessary rather than convenient — the panel must not be loaded by the
Live2D viewer, because that would make one module depend on the other.

The mode is one of the hosts that own their window and backend, which
`Engine::IsStandaloneViewerMode` names once. That predicate exists because a new
viewer mode that missed one of the enumerated startup branches would compile and
then fail at run time; the panel mode hit exactly that on its first run.

## Boundaries to preserve

- `Panel` gains no dependency on Render, Graphics, Live2D, or Editor.
- `PanelRender` is the only part that knows a GPU exists.
- Render and Graphics name no panel type.
- The panel contributes generic submission work, exactly as Live2D does; Render
  knows neither producer.
