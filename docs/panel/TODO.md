# Panel Module Roadmap

Architecture and design decisions live in [PLANS.md](PLANS.md). Concrete stage
designs are under [.plan/](.plan/). This file is the roadmap and acceptance
ledger; execution evidence belongs in [`.spec/journal/`](../../.spec/journal/).

## Stages

| Stage | Deliverable | State |
| --- | --- | --- |
| P0 | Dot-matrix representation: `GlyphCell`, `DotMatrix`, `Panel`, UTF-8 | landed |
| P1 | Glyph product and bake: `KPPNLGLY`, `import-glyphs` | landed |
| [P2](.plan/P2.md) | Offscreen render and cross-backend capture | landed |
| P3 | Placement model, effects, pattern commands | proposed |
| P4 | World-space placement of the panel in a scene | proposed |

## P0 — representation

Acceptance, all landed:

- [x] `GlyphCell` is a fixed 16x16 cell plus an advance, trivially copyable.
- [x] `DotMatrix` is a flat buffer with a padded row stride and byte-aligned
      stamping, with `Replace`/`Or`/`Xor`/`AndNot` as an explicit parameter.
- [x] `Panel` is the source of truth; `Rebuild` is pure and byte-identical for
      unchanged content, and clears dots left by a longer line.
- [x] Halfwidth glyphs advance 8 while occupying 16 columns of storage; the
      halfwidth bleed invariant is rejected rather than tolerated.
- [x] UTF-8 decoding resynchronises on malformed input instead of abandoning the
      string.
- [x] `Panel` links nothing.

## P1 — glyph production

Acceptance, all landed:

- [x] A TrueType face bakes into a `KPPNLGLY` product with an explicit
      little-endian layout.
- [x] The product round-trips, and rejects a foreign magic, an unknown version, a
      truncated body, and trailing data.
- [x] The bake converts the rasteriser's MSB-leftmost bytes to this module's
      bit-0-leftmost layout, asserted through the stem side of `E`, `L`, and `J`.
- [x] CJK and Latin share one baseline; a derived baseline outside the cell is
      rejected rather than silently clipping.
- [x] Ink clipped by the cell is counted and reported; the shipped default
      reports zero.
- [x] Baking 40,928 glyphs takes about 1.6 s and produces 1.4 MB.

## P2 — render

Acceptance, all landed:

- [x] `R8_UNORM` textures with a CPU payload validate, so a single-channel mask
      can be uploaded.
- [x] `PanelRenderPlanner` builds one target pass with one quad draw, publishes
      nothing on failure, and never touches a `CommandRecorder`.
- [x] The renderer owns every handle it creates, reports zero after cleanup, and
      recreates — rather than updates — the dot mask, releasing the previous one
      only after `WaitIdle`.
- [x] `panel-viewer` is a standalone host that renders offscreen and captures a
      PNG on both backends.
- [x] OpenGL and Vulkan captures of identical content are **byte-identical**,
      for both `OvO` and `OvO 中文`.
- [x] The capture decodes back to the expected dot pattern, upright and
      unmirrored.

## P3 — placement, effects, and patterns

Not yet designed. The open questions, recorded so they are not rediscovered:

- **Positioning.** `SetTextAt` places one run per row. Two independent runs on
  one row needs a `Placement` list, which also brings the per-character colour
  plane with it. Neither has a consumer yet, so neither is built.
- **Effects.** Marquee, blink, glitch, and wave are `f(DotMatrix, t)` applied
  after `Rebuild`, never persisted into the content. A scrolling marquee is an
  animated origin, not a rewrite of the logical text.
- **Named patterns.** `OvO` should be data — a pattern table in the product or
  config — not a code path, and not a Live2D parameter.
- **Colour.** Per-character colour needs both a character-resolution plane and
  the `Placement` model. Per-panel colour is already a uniform.

## P4 — world placement

The panel currently renders offscreen and is captured, deliberately. Placing it
as a floating screen in a scene needs a camera and a transform, neither of which
the panel has. This is the "floating screen" the module exists to become, and it
is a scene-integration problem rather than a panel problem.

## Open decisions

- **Panel extent.** `kPanelColumns` and `kPanelRows` are 32x16. Nothing depends
  on them being fixed, but nothing has needed otherwise either.
- **`--capture-view` spelling.** The parser accepts `live2d` for the product
  view, which is now a misnomer for a panel run. A neutral spelling would be
  better; it has not been added because it would touch Live2D's documented
  surface too.
- **Glyph resolution.** A hand-tuned bitmap CJK font (Unifont, Zpix) would be
  crisper than outline rasterisation at 16x16. The product format does not
  change if the source does.
