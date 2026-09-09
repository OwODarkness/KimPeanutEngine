# 2026-09-09 — AssetTool AT1.2 unique cook graph

**Status:** implemented and validated.

## Change

AT1.2 now memoizes material texture work with a typed `TextureCookKey`.
Repeated bindings, including metallic and roughness references to one packed
image, reuse the same decoded/prepared result. The key carries source
identity, source content hash, semantic, dimension and mip policy, compression
profile, and profile-variant selection. Model import dependency hashing fills
external image hashes; embedded payloads are hashed by the material converter.

`TextureCooker` now exposes a preparation step and a profile-specific cooking
step. Portable and block-compressed products consume the same converted mip
chain, while the existing `Cook` API remains equivalent for standalone callers.
Material slot order and content-addressed product paths remain driven by the
source material traversal and product hashes, not cache insertion order.

## Reference gate

- Godot `editor/import/resource_importer_texture.h/.cpp` at master revision
  `9552dfb6859a1aaba1e570b8e0ef5c599b830f19` separates import settings and
  platform/profile variants and supports threaded import. This supports
  keeping preparation independent from output profiles, but its importer
  database and editor ownership do not apply to KimPeanut's AssetImport-only
  converter.
- Piccolo `engine/source/runtime/resource/asset_manager/asset_manager.h/.cpp`
  at main revision `f5053707fed4d3f94d270a436fb0d3a8ae54e3e5` uses a simple
  offline JSON asset manager and does not provide a relevant cook memoization
  pattern. No source was copied.

## Validation

- `cmake --build build --config Debug --target NativeMaterialTest` passed.
- `ctest --test-dir build -C Debug -R
  "NativeMaterialTest|TextureImportTest|ModelImportServiceTest"`: 16/16
  passed.
- `cmake --build build --config Debug --target KimPeanutAssetTool` passed.
- Cold Debug Cerberus import with `--compression portable`:

```text
total_seconds: 15.574334
texture_cook_seconds: 10.687412
source_image_count: 1
requested_texture_bindings: 1
unique_cook_keys: 1
texture_decode_count: 1
texture_prepare_count: 1
texture_cook_count: 2
portable_encode_count: 1
block_encode_count: 1
unique_texture_product_count: 2
product_write_count: 4
bytes_written: 28898211
peak_working_set_bytes: 204894208
```

The prior AT1.1 Cerberus observation was 19.490446 s total and 14.474232 s
texture cooking. The comparison is useful as a Debug cold-import indicator,
but it is not a controlled benchmark because filesystem and process state can
vary between runs.

## Remaining risk

AT1.3 still owns bounded parallel scheduling, writer backpressure, and a
memory budget. The current import remains serial and reports no reservation.
