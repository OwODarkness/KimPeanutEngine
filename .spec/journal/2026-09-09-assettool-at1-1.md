# 2026-09-09 — AssetTool AT1.1 remove amplification

**Status:** implemented and focused asset validation passed.

## Changes

- Removed `decoded_image` from `NativeImageProduct`; the conversion result
  retains only the cooked product identity, extension, and bytes.
- Changed texture publication to move cooked bytes into the conversion result,
  then moved Material and Texture product buffers into `PendingProduct`.
- Kept root-last archive publication and immutable collision handling, but
  staged and published each product in one loop so every new product is written
  once before hard-link publication.
- Added bounded `ValidateNativeModelProductStructure` and
  `ValidateNativeTextureProductStructure` checks. Fresh serialization uses
  these header/directory/digest checks without allocating decoded payloads;
  cache-hit and runtime paths retain full deserialization validation.

## Validation

```text
cmake --build build --config Debug --target KimPeanutAssetTool AssetUnitTest ModelImportServiceTest NativeMaterialTest NativeModelTest TextureImportTest
ctest --test-dir build -C Debug -R "NativeMaterialTest|NativeModelTest|ModelImportServiceTest|TextureImportTest" --output-on-failure
```

Results:

- Debug AssetTool and focused asset targets built successfully.
- 16/16 focused asset tests passed.
- `ModelImportServiceTest.PublishesProductsAndRepeatsAsVerifiedCacheHit`
  now proves `product_write_count == product_count` for a fresh serial import.
- Product bytes remain content-addressed and existing archive/collision tests
  continue to pass.

## Reference gate and remaining evidence

The local engine-reference index and Asset module plans were inspected. No
GitHub MCP read/search capability was exposed in this session, so no external
repository study was added; this stage is a local serialization/publication
optimization with unchanged Runtime ownership. A comparable post-change
Sponza Debug/RelWithDebInfo timing matrix remains AT1.6 evidence.
