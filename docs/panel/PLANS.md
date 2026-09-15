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

The dot-to-pixel expansion is then free. The mask is one texel per dot, the
sampler is NEAREST, and drawing the quad larger than the mask *is* the chunky LED
look — no per-dot arithmetic and no geometry per dot. A full 512x256 panel is
128 KB as `R8_UNORM`, uploaded only when the content changes.

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
