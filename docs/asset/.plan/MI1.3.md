# MI1.3 — Pure Foreign-Model Decoder

- Status: landed 2026-09-06
- Parent design: [MI1](MI1.md)
- Prerequisites: MI1.1
- Unblocks: MI1.4, MI1.5, and MI1.6

## Assignment

Extract Assimp parsing into a pure decoder that returns the parent plan's
`ImportedModelDocument`. Support STL, OBJ, FBX, GLTF, and GLB at the explicit
import boundary while preserving the MI1.1 geometry and material contract.

## Deliverables

- Imported mesh, section/material-slot topology, material/image descriptions,
  source dependency records, and structured source-scoped diagnostics.
- Correct static node-transform baking and inverse-transpose normal handling.
- Discovery of external and embedded source dependencies needed by later hash
  and image conversion stages.
- Focused decoder tests using the MI1.1 fixtures, including STL default-material
  behavior and malformed/missing dependency failures.

## Boundaries

The decoder returns CPU values only. It must not allocate Asset IDs, register
assets, write files, query SQLite, choose output names, create GPU objects, or
silently approximate unsupported source semantics.

## Done when

- [x] Every supported foreign extension produces a deterministic imported
  document or a stable diagnostic.
- [x] Decoder execution has no Asset-cache or archive side effects.
- [x] Existing characterized geometry and section behavior remains covered.
