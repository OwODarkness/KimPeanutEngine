# 2026-09-09 — AssetTool AT1.0 baseline and attribution

**Status:** instrumentation landed; large-scene collection is partial because
the user supplied the completed Debug timing and asked not to wait for another
long run.

**Scope:** AT1.0 only. No product format, archive ownership, or import
algorithm was changed. The current serial path remains intentionally visible
so AT1.1 and AT1.2 can be measured against it.

## Instrumentation

- `ModelImportMetrics` is returned with every model import result and is
  printed by `KimPeanutAssetTool import|reimport`.
- Stage wall time covers cache probing, source decode, dependency hashing,
  texture cooking, product serialization/validation/hashing, staging,
  publication, and archive commit.
- Windows process CPU time and peak working set are sampled through the
  process APIs. CPU utilization is normalized by logical processor count.
- The baseline counts source images, requested bindings, logical cook keys,
  decodes, portable/block encoder calls, unique texture products, serialized
  texture bytes, product writes, source/product bytes read, and bytes written.
  The current path reports one active job and no memory budget.
- Existing behavior is captured rather than optimized: the current path
  reports the two staging writes per newly produced product and retains the
  decoded-image lifetime that AT1.1 will remove.

## Commands and evidence

Builds:

```text
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug --target KimPeanutAssetTool ModelImportServiceTest -- /m:2
cmake --build build --config RelWithDebInfo --target KimPeanutAssetTool -- /m:2
```

Fixed source and settings:

```text
source: model/sponza/main_sponza/NewSponza_Main_glTF_003.gltf
asset root: D:\C++Project\KimPeanutEngine\asset
profile: default ModelImportSettings, --compression portable
```

The user-observed completed Debug run reported:

```text
[1509.4s] done
decoded: 29 materials, 72 images
hashed: 74 dependencies
cooked: 144 native texture products
```

The live trace showed dependency hashing completed at approximately 71.4 s,
then serial texture cooking dominated the remaining run. Repeated packed
roughness/metalness bindings were visible in the progress stream.

The completed Cerberus Debug probe provided a finished metric-shape check in a
temporary ignored archive:

```text
total_seconds: 19.490446
texture_cook_seconds: 14.474232
peak_working_set_bytes: 344707072
requested_texture_bindings: 1
unique_cook_keys: 1
texture_decode_count: 1
texture_cook_count: 2
unique_texture_product_count: 2
product_write_count: 8
bytes_written: 57796422
```

## Validation and remaining evidence

- `ModelImportServiceTest`: 6/6 passed after adding cache and write-count
  assertions.
- `NativeMaterialTest`, `TextureImportTest`, and the related asset tests:
  16/16 passed.
- Debug and RelWithDebInfo AssetTool targets built successfully. The first
  non-elevated MSBuild attempt was blocked by Windows SDK access; the elevated
  retry passed.
- RelWithDebInfo Sponza cold, warm-filesystem, and no-op result metrics were
  not collected after the user requested that the long run not be awaited.
  The existing archive did not contain a valid Sponza cache, so a probe would
  have fallen back to a full import. AT1.1 should preserve this telemetry and
  repeat the complete Debug/RelWithDebInfo matrix before comparing speedups.
