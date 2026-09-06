# MI1.8 — Tooling and End-to-End Validation

- Status: standalone authoring tool, Material promotion, and checked-in Level
  native-package migration landed; cross-backend capture evidence remains
- Parent design: [MI1](MI1.md)
- Prerequisites: MI1.6 and MI1.7

## Assignment

Expose the importer as an explicit standalone authoring workflow and prove the
complete pipeline on real formats and both graphics backends. The workflow must
remain usable with the engine application closed.

## Deliverables

- [x] A standalone `KimPeanutAssetTool` provides import/reimport, source
  status, diagnostics, archive integrity, and readable source-to-product
  inspection. The underlying importer remains callable without opening Runtime
  or `AssetManager`.
- [x] Material promotion copies an immutable generated Material to an authored
  path, rebases its material-relative shader/texture references, records an
  explicit slot override, and leaves the original product unchanged.
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
