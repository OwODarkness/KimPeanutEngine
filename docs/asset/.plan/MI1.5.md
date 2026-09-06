# MI1.5 — Native Material Conversion

- Status: landed 2026-09-06
- Parent design: [MI1](MI1.md)
- Prerequisites: MI1.2 and MI1.3
- Unblocks: MI1.6

## Assignment

Convert imported material and image descriptions into deterministic native
Material products while preserving required glTF PBR semantics and sharing
identical output content.

## Deliverables

- Canonical Material serialization with fixed schema version, field/parameter
  order, numeric formatting, normalized references, and stable product hashes.
- Base color, metallic-roughness G/B, normal scale, occlusion R/strength,
  emissive, double-sided, and supported alpha behavior across Material schema,
  resolver, and shaders; unsupported required semantics fail precisely.
- Memory image decode for GLB buffer views and data URIs, plus content-addressed
  embedded-image storage and reuse.
- Tests for channel/color-space semantics, shared image identity, equivalent
  material deduplication, malformed images, and deterministic bytes.

## Boundaries

Do not split packed metallic-roughness textures by default, attach materials to
Mesh ownership, publish files or database roots, or silently edit an existing
hash-named product. Coordinate Model Material-reference bytes with MI1.4.

## Done when

- [x] Equivalent supported materials produce byte-identical `.material` data.
- [x] Generated references preserve source material-slot order.
- [x] Required converted materials preserve declared sampling
  semantics in focused Render tests.

## Landed implementation

`ConvertImportedMaterials` is a standalone Asset conversion function. It emits
canonical V2 material bytes and in-memory content-addressed embedded-image
products; it does not access AssetManager, Runtime, Render, Graphics, or the
archive database. ImageIO now provides memory decode and PNG encode seams.

Material V2 records alpha mode/cutoff and texture color-space/channel intent.
Render preserves packed metallic-roughness as one texture and selects G/B at
sample time. Emissive textures fail with `UnsupportedSemantics` because the
current G-buffer has no emissive attachment; this is an explicit limitation,
not a silent fallback.
