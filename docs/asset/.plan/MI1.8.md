# MI1.8 — Tooling and End-to-End Validation

- Status: blocked by MI1.6 and MI1.7
- Parent design: [MI1](MI1.md)
- Prerequisites: MI1.6 and MI1.7

## Assignment

Expose the importer as an explicit standalone authoring workflow and prove the
complete pipeline on real formats and both graphics backends. The workflow must
remain usable with the engine application closed.

## Deliverables

- A CLI and/or Editor-subprocess front end for import, reimport, status,
  diagnostics, archive integrity/rebuild, and readable source-to-product
  inspection. The underlying importer must remain callable without opening the
  engine Runtime or `AssetManager`.
- Material promotion: copy an immutable generated Material to an authored path,
  record an explicit override, and produce a new Model reference without
  mutating the original product.
- Representative STL, OBJ, FBX, GLTF, and GLB end-to-end fixtures, including
  multiple materials, external dependencies, and embedded images.
- Focused tests, packaging checks, Vulkan/OpenGL smoke, captured SceneColor
  inspection, documentation/status updates, and completed journal evidence.

## Boundaries

Do not add runtime SQLite lookup, automatic archive garbage collection, shared
network-database operation, animation/skin import, or a new general-purpose
asset alias system. Those require separate designs.

## Done when

- [ ] A user can explicitly import and diagnose every supported source format.
- [ ] A headless/offline invocation creates the hash-named products while the
      engine application is closed.
- [ ] Repeat import is visibly `UpToDate`; changed input creates new immutable
  products while old shared products remain untouched.
- [ ] Representative native Models render correctly on Vulkan and OpenGL, with
  visual evidence recorded according to the project completion contract.
