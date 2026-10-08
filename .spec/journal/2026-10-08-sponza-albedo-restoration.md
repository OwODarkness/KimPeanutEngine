# Sponza authored-material restoration — 2026-10-08

## Objective and boundary

Restore the normal `level/sponza.level` scene with authored albedo and prevent
rendering-feature work from accepting a gray replacement as scene compatibility
evidence. Asset owns reference validation and offline products; Render and GPU
ownership, dependency directions, shader interfaces, and SSAO implementation
remain unchanged.

## Diagnosis

- Starting state: HEAD `80c3eac`; only `README.md` was modified before this task.
  That unrelated edit was preserved.
- The latest prior successful launch selected `sponza_raster.level`, whose
  foreign glTF models use a single gray material without textures.
- The preceding canonical-level launch failed with `logical model is not
  present in the archive`. Runtime selects `content/.archive` when its database
  exists; that archive held six other sources and no Sponza source records.
  Sponza's three ready sources remained in `asset/.archive`.
- `GetRuntimeArchiveDirectory` was introduced in `470b8ec` on 2026-09-14,
  before SSAO commit `919334f`. The SSAO journal explicitly records using the
  gray source-model fixture after native dependency resolution failed.
- `ResolveOwnedAssetPath` allowed only paths below `asset/`, rejecting native
  texture references from generated materials in `content/.archive/materials`.
  Source glTF documents retain 25, four, and one albedo-texture bindings for
  the main model, curtains, and ivy respectively.

## Changes

- `engine/runtime/asset/utility.h`: permit a generated content-archive material
  to reference a native `.texture` directly in that same archive's `textures`
  directory. Absolute references and arbitrary paths outside `asset/` remain
  rejected. Native product integrity validation remains downstream.
- `engine/test/unit/asset/native_material_test.cpp`: regression coverage for
  portable/BC references and rejection of archive escapes, unrelated folders,
  non-native extensions, nested texture paths, and authored-material access.
- `docs/validation_matrix.md`: canonical textured Sponza, settled residency,
  albedo/final captures, and comparison with the previous working scene are
  required alongside rendering-feature fixtures. Gray fixtures are supplemental.
- `.gitignore`: exclude generated native model/material/texture products and
  this checkout's Sponza import metadata.
- Reimported the three sources into the active local archive. This updates the
  tracked `content/.archive/archive.sqlite3` locally; products and metadata are
  generated checkout-local artifacts, not a portable packaged scene. Existing
  audio records and the legacy archive were preserved. No commit was made.

## Commands and evidence

```powershell
cmake --build build --config Debug --target KimPeanutEngine NativeMaterialTest
ctest --test-dir build -C Debug -R NativeMaterialTest --output-on-failure
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The targeted build and full Debug build passed. NativeMaterialTest passed 9/9.
The full suite ran 1,133 tests, initially failing 71 and skipping six optional
Live2D renderer tests. The first failure was a local HTTP-response assertion;
file-publication tests also failed under sandbox restrictions. Repeating only
the 71 failures outside the sandbox cleared 58; 13 still fail. The complete
suite is therefore **not green**, despite the passing material regressions.

```powershell
ctest --test-dir build -C Debug --rerun-failed --output-on-failure
```

Remaining failures are outside the edited material path implementation:

- One checked-in-level test: `pbr_showcase.level` is missing its logical rock
  product from the active archive.
- Four `AssetCatalogProviderTest` cases and two native-Model cases assume
  the legacy archive, while Runtime selects the content archive.
- One `AssetImportRegistryTest` case does not receive its expected provider
  result.
- Three `RenderGraphCompatibilityTest` cases retain incompatible resource,
  transition, or transient expectations; one capture test expects EngineWindow
  enum position 12 while SSAO views moved it to 14.
- One `TerrainMeshTest` coarse/fine normal comparison fails.

These failures were recorded without changing their fixtures or unrelated
subsystems to hide the failures. Logs are
`save/logs/sponza-albedo-full-debug-ctest-20261008.log` and
`save/logs/sponza-albedo-failed-tests-external-20261008.log`.
`git diff --check` passed.

For each Sponza source, the offline import command was:

```powershell
build/engine/tool/asset/RelWithDebInfo/KimPeanutAssetTool.exe import --source <source.gltf> --asset-root asset --archive-root content/.archive --compression bc --bc-encoder reference --bc-quality balanced --jobs 8 --memory-budget-mib 1024 --writer-queue-depth 2
```

The first sandboxed import reached publication and failed with `Access is
denied`; an approved outside-sandbox retry published all three imports. This was
an execution-environment failure, not a decoder or material source failure.

Both runtime launches used the rebuilt Debug executable outside the sandbox,
Vulkan with normal Debug validation, the canonical Sponza camera and lights,
and a 1094×631 viewport. The initial raster launch used
`--disable-path-tracing`; Runtime confirmed active SSAO, ray tracing enabled,
and complete tracked texture residency before the final raster captures.
The engine was then restarted with its normal options:

```powershell
build/Debug/KimPeanutEngine.exe --graphics-api vulkan --startup-level level/sponza.level --agent-port 37373
```

Runtime confirmed active/available path tracing, 764 accumulated samples, and
complete texture residency. The process's `GLFW30` window was visible and
foreground on the `Default` desktop. The engine remains open for the user.

Runtime `capture.screenshot` followed by request polling produced:

- `save/screenshots/validation/sponza-albedo-restored-vulkan-20261008.png`
- `save/screenshots/validation/sponza-textured-ssao-vulkan-20261008.png`
- `save/screenshots/validation/sponza-textured-pathtrace-vulkan-20261008.png`
- `save/screenshots/validation/sponza-textured-window-vulkan-20261008.png`

Inspection confirms colored curtains, green ivy, stone albedo/detail, and
textured raster and path-traced output. Stats and capture responses are under
`save/diagnostics/sponza-albedo-20261008/`; build/import/runtime/test logs are
under `save/logs/sponza-albedo-*20261008*`.

## Limitations

- OpenGL was not launched for this Asset reference change.
- No performance or broad GPU lifetime acceptance is claimed.
- Native scene products still require offline import on another checkout;
  packaging remains outside this repair.
- Full-suite failures and skipped tests must be distinguished from the passing
  material regressions and verified visible Sponza runtime.
