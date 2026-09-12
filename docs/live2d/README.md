# Live2D Module

The Live2D module is an optional CMake module for integrating the **Live2D
Cubism 5 SDK for Native R5 (`5-r.5`)**. It is enabled by default for engine
development. A local Cubism SDK path is therefore required unless the module
is explicitly disabled.

The module provides Cubism SDK integration, native Live2D asset products, and
the foundation for future model rendering.

Planning entry points:

- [module architecture](PLANS.md);
- [roadmap and acceptance ledger](TODO.md);
- [L2D4 concrete renderer plan](.plan/L2D4.md).

## SDK location

Keep the proprietary SDK outside the repository. Set `[SDK_PATH]` to the local
Cubism SDK root, for example:

```text
[SDK_PATH]
```

The CMake finder validates that the root contains:

```text
Core/include/Live2DCubismCore.h
Core/lib/windows/x86_64/143/Live2DCubismCore_MDd.lib
Framework/src
cubism-info.yml
```

The `cubism-info.yml` release must be exactly `5-r.5`. Do not commit Cubism
Core binaries, SDK sample content, or model data without checking the
applicable Live2D license and redistribution terms.

## Enable and build

Run these commands from the repository root in a **Developer PowerShell for
Visual Studio 2022**. `KPENGINE_ENABLE_LIVE2D` is already `ON` by default. Set
the SDK environment variable once for the current configure session:

```powershell
$env:KPENGINE_CUBISM_SDK_ROOT = "[SDK_PATH]"
```

CMake also accepts the legacy `CUBISM_SDK_ROOT` environment variable. An
explicit `-DKPENGINE_CUBISM_SDK_ROOT=...` cache value takes priority over both.
With the environment variable set, configure and build with:

```powershell
cmake -S . -B build-live2d `
  -G "Visual Studio 17 2022" `
  -DKPENGINE_LIVE2D_CRT=MD `
  -DKPENGINE_LIVE2D_MOC_PATH="[SDK_PATH]/Samples/Resources/Wanko/Wanko.moc3"

cmake --build build-live2d --config Debug --target KimPeanutEngine -- /m:2
```

If CMake Tools runs in a newly launched IDE, configure it after setting the
user environment variable or provide the cache entry in the IDE's configure
settings. Do not put the SDK path in a tracked preset or source file.

`KPENGINE_LIVE2D_CRT=MD` selects the SDK's `/MD` static Core library and
matches the engine's default MSVC runtime. Use `MT` only when the whole build
and the matching SDK Core library are intentionally configured for `/MT`.

The runtime preview is selected by `config/live2d.json`, not by a level:

```json
{
  "version": 1,
  "enabled": true,
  "preview_asset": "live2d/hiyori_pro/hiyori.live2d"
}
```

The standalone `Live2DViewerHost` loads that native `.live2d` product through
AssetManager and presents it in the dedicated Live2D Viewer. Live2D is no
longer registered into the scene `RenderSystem`; the level used for historical
fast validation is not part of the viewer path, and runtime does not import
`.model3.json` files or write temporary products.

The viewer presentation backdrop comes from the shared
`config/settings.json` `window_background_color` array. This is intentionally
separate from the editor/ImGui `background_color`. The window value is passed through
the API-neutral presentation submission contract, so OpenGL and Vulkan clear
their presentation attachments with the same configured color. If the shared
settings file is unavailable or malformed, the viewer falls back to the
historical Vulkan gray clear.

The standalone viewer uses the existing ImGui presentation infrastructure as a
viewer shell, not as the scene editor. Its central `Live2D Viewer` panel
displays the renderer-owned offscreen target, while the right side contains
`OutputLog` and `Performance Profiler` panels. The host does not construct
`Scene3DHost`, `RenderWorld`, `DeferredRenderer`, or scene editor components.
The Live2D product target is `RGBA8_SRGB` on both APIs. Live2D model textures
are sRGB, so the shaders work in linear space and the attachment format owns the
transfer function — neither backend's shaders encode. The host converts the
configured display-space clear to linear before rendering into the target, and
hardware re-encodes on store, so the configured value reaches the screen
identically on both backends without changing the deferred/PBR scene pipeline.

## Compare the two backends

The standalone viewer renders into its own offscreen target, so a capture of
that target is a comparable image — it carries no host UI. Ask for it with
`--capture-view live2d`, and use `--capture-alpha transparent` to clear the
target transparently so the product's own alpha and blend coverage survive into
the exported file:

```powershell
build/engine/editor/Debug/KimPeanutEngine.exe --mode live2d-viewer `
  --graphics-api opengl  --capture save/screenshots/validation/product-gl.png `
  --capture-view live2d
build/engine/editor/Debug/KimPeanutEngine.exe --mode live2d-viewer `
  --graphics-api vulkan  --capture save/screenshots/validation/product-vk.png `
  --capture-view live2d
python tools/compare_capture_regions.py `
  save/screenshots/validation/product-gl.png `
  save/screenshots/validation/product-vk.png
```

Add `--exit-after-capture` to have the viewer shut down on its own once the
capture resolves, instead of running until it is killed:

```powershell
build/engine/editor/Debug/KimPeanutEngine.exe --mode live2d-viewer `
  --graphics-api opengl  --capture save/screenshots/validation/product-gl.png `
  --capture-view live2d --exit-after-capture
```

That flag is also what makes the shutdown path observable: without it the
process is terminated before `CleanupGpu` runs, so the reverse-order release
and its `Module GPU handles` leak count never execute. With it, the run ends
with a clean exit and the viewer logs a warning only if handles are still live.

`--capture-view`, `--capture-alpha`, and `--exit-after-capture` all require
`--capture` and are rejected outside `live2d-viewer` mode. The comparator
reports sha256, dimensions, and per-region statistics against the frozen L2D4.0
tolerances (2/255 for ordinary pixels, 4/255 for filtered edges), splitting
non-edge opaque, partial alpha, transparent background, and filtered edge so a
coordinate, gamma, blend, or mask-polarity disagreement cannot hide inside an
average.

Do not check in a captured model image — the model is licensed.

## Run the Live2D tests

```powershell
cmake --build build-live2d --config Debug --target Live2DCoreTest -- /m:2
ctest --test-dir build-live2d/engine/test -C Debug -R Live2D --output-on-failure
```

The tests verify repeated initialize/shutdown, allocator alignment, shutdown
protection while model leases are alive, independent parameter state for two
`CubismModel` instances, deterministic Hiyori `.model3.json` import, path
escape rejection, malformed product rejection, and ordinary AssetManager
texture dependency registration.

## Import a Live2D model offline

Build the asset tool, then import a `.model3.json` source closure with an
explicit `.live2d` output path:

```powershell
cmake --build build-live2d --config Debug --target KimPeanutAssetTool -- /m:2

build-live2d/engine/tool/asset/Debug/KimPeanutAssetTool.exe import-live2d `
  --source live2d/hiyori_pro/runtime/hiyori_pro_t11.model3.json `
  --output content/hiyori_pro.live2d `
  --asset-root asset
```

The command publishes the `.live2d` product and its native Texture products
under the output directory:

```text
content/
  hiyori_pro.live2d
  .archive/
    textures/
      <sha256>.texture
```

Re-running the same command is safe: existing products must have identical
bytes or the import fails with an immutable product collision. `--archive-root`
is optional and, when provided, must name the `.archive` directory beside the
requested `.live2d` output.

The repository preview configuration expects the Hiyori product at
`asset/live2d/hiyori_pro/hiyori.live2d`; generate it once with the same command
using that output path and its adjacent `.archive` directory.

## Native `.live2d` product format

`.live2d` is KimPeanutEngine's native binary product. It is not the authored
Cubism `.model3.json` file and it is not a ZIP/archive container. The offline
importer reads the authored source closure once, embeds the model bytes and
required metadata, cooks atlas images into native Texture products, and emits
deterministic `.live2d` bytes. Runtime loading reads only this product and its
native dependency closure.

A deployed product currently has this layout:

```text
content/
  hiyori.live2d
  .archive/
    textures/
      <sha256>.texture
```

The product stores texture references such as
`.archive/textures/<sha256>.texture`. They are resolved relative to the
directory containing the `.live2d` file. The referenced `.texture` files are
content-addressed native products; their bytes and hash are verified by the
ordinary Asset texture loader.

### Binary layout

All integer fields are unsigned 32-bit little-endian values. Variable-size
blobs and strings are prefixed by their unsigned 32-bit byte length. Strings
are UTF-8-like byte strings with no NUL terminator; paths use `/` separators.

| Order | Field | Meaning |
| --- | --- | --- |
| 1 | `magic[8]` | ASCII `KPL2DPRD` |
| 2 | `product_version` | 1 for the original layout; 2 adds typed motion, expression, and parameter-group sections |
| 3 | `model3_version` | Authored Cubism model schema version; current value: `3` |
| 4 | `texture_count` | Number of ordered native Texture references |
| 5 | `optional_chunk_count` | Number of named optional source chunks |
| 6 | `moc_bytes` | Length-prefixed embedded `.moc3` bytes |
| 7 | `textures[]` | Length-prefixed dependency path for each atlas |
| 8 | `optional_chunks[]` | V1-compatible named source chunks; V2 leaves authored motions and expressions in typed sections |

Conceptually, a decoded product looks like this:

```text
Live2DProductData {
  product_version: 1,
  model3_version: 3,
  moc_bytes: <embedded MOC3 bytes>,
  textures: [
    { path: ".archive/textures/<sha256-a>.texture" },
    { path: ".archive/textures/<sha256-b>.texture" }
  ],
  optional_chunks: [
    { name: "Physics", bytes: <embedded physics JSON> },
    { name: "Expressions/0", bytes: <embedded expression JSON> }
  ]
}
```

Product V2 keeps the V1 header/layout readable and appends three counts after optional_chunk_count: motions, expressions, and parameter groups. Motion records preserve group/index identity, fade-presence bits, exact motion JSON bytes, and optional sound bytes; expression records preserve authored names and exact expression JSON bytes; parameter groups preserve target/name/ID order. The importer validates this source closure before publishing native Texture products. The shared runtime payload contains immutable product data only. It does not
contain GPU objects, Cubism model instances, parameter state, motion state,
deformed vertices, or frame-local mask data. The current codec limits the
complete product to 512 MiB, each individual blob/string to 1 MiB, and each
collection to 4096 entries. Full Cubism MOC compatibility and required-feature
validation are not yet performed by the importer.

## Disable Live2D

To create a build tree without the module, explicitly set the option to `OFF`:

```powershell
cmake -S . -B build-live2d-off `
  -G "Visual Studio 17 2022" `
  -DKPENGINE_ENABLE_LIVE2D=OFF

cmake --build build-live2d-off --config Debug --target KimPeanutEngine -- /m:2
```

When disabled, no `Live2D` targets are added and no Cubism SDK path is needed.
When changing an existing build tree, rerun CMake with the desired option so
the cache is refreshed.
