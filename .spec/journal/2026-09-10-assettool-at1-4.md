# AssetTool AT1.4 journal — 2026-09-10

## Scope

AT1.4 targets the remaining CPU cost in offline BC3/BC4/BC5 texture cooking
after AT1.3 bounded job-level scheduling. This entry records the allocation-free
reference path, versioned encoder seam, encoder-only metrics, cancellation
contract, and the first `rgbcx` bakeoff. It does not claim the complete AT1.4
quality or three-run runtime acceptance gates.

## Design decision

The implementation keeps the existing scalar encoder as the Asset-owned
`ReferenceV1` path and removes allocator work from its block loop:

- each mip allocates one exact-size output vector;
- BC4 and BC1 block encoders write to fixed destination pointers;
- BC3 writes alpha to bytes `[0, 8)` and color to `[8, 16)` of each block;
- block traversal, clamped edge gathering, endpoint search, palette choice, and
  byte order are unchanged.

This preserves AT1.3's one scheduling layer. No nested encoder pool was added.
The `rgbcx` candidate is integrated behind the versioned settings seam, but its
measured Sponza throughput fails the production gate, so it remains diagnostic
only and `ReferenceV1` remains the default.

## Reference gate

The local reference index had no compression-specific study, so the candidate
review used primary upstream sources. `rgbcx` exposes BC1/3/4/5 CPU encoders and
has thread-safe encode calls after one-time initialization, but its global
initialization contract must be isolated and guarded before AT1.3 workers use
it. DirectXTex confirms the required BC4 red-channel and BC5 red/green-channel
mapping, but its platform-shaped boundary is not adopted for this stage.

- [rgbcx/bc7enc](https://github.com/richgel999/bc7enc)
- [rgbcx implementation](https://github.com/richgel999/bc7enc_rdo/blob/master/rgbcx.h)
- [DirectXTex](https://github.com/microsoft/DirectXTex)

## Files changed

- `engine/runtime/asset/texture_importer.cpp` — allocation-free `ReferenceV1`
  BC3/BC4/BC5 output path, `rgbcx` dispatch, encoder-only metrics, and per-mip
  cancellation checks.
- `engine/runtime/asset/texture_importer.h` and
  `engine/runtime/asset/model_import_service.h/.cpp` — versioned encoder and
  quality settings plus import diagnostics.
- `engine/runtime/asset/native_material.cpp` — texture cook-key identity.
- `third_party/rgbcx/`, `third_party/CMakeLists.txt`, and
  `engine/runtime/asset/CMakeLists.txt` — pinned private candidate wrapper.
- `engine/tool/asset/asset_tool_main.cpp` — encoder/quality CLI selection.
- `engine/test/unit/asset/texture_import_test.cpp` — deterministic 5x3 odd-edge
  coverage, an independent BC1/BC3/BC4/BC5 decoder, semantic quality metrics,
  and concurrent rgbcx initialization/encoding coverage.
- `engine/test/unit/asset/model_import_service_test.cpp` — settings-driven cache
  invalidation coverage.
- `docs/asset/.plan/AT1.4.md`, `docs/asset/TODO.md`, and `docs/status.md` —
  landed-slice status and remaining gates.

## Validation

- `cmake --build build --config Debug --target TextureImportTest` — passed.
- `ctest --test-dir build -C Debug -R TextureImportTest --output-on-failure` —
  5/5 passed.
- `cmake --build build --config RelWithDebInfo --target TextureImportTest ModelImportServiceTest` — passed.
- `ctest --test-dir build -C RelWithDebInfo -R "TextureImportTest|ModelImportServiceTest" --output-on-failure` — 16/16 passed.

## AT1.4.2 continuation

The versioned settings groundwork is now landed. `TextureCookSettings` carries
`ReferenceV1`/`RgbcxV113` encoder identity and `Fast`/`Balanced` quality. The
values participate in native Model settings hashes and material Texture cook
keys, so changing a potentially byte-changing compressor policy cannot reuse a
product under the old identity. AssetTool accepts `--bc-encoder
reference|rgbcx` and `--bc-quality fast|balanced`, and import diagnostics report
the selected values. Encoder-only BC3/4/5 block, source-megapixel, elapsed, and
throughput metrics are propagated to import diagnostics. Both encoder paths
check cancellation before cooking and between block rows.

The pinned candidate is `rgbcx` upstream commit
`f66c2e489b07138f2673a2fb3d27c1aa1d565c48`, wrapped as `KP::Rgbcx`; the vendored
header identifies itself as rgbcx v1.12. The internal `RgbcxV113` identity is
retained to match the concrete AT1.4 plan, while the source revision is recorded
explicitly to prevent provenance ambiguity.

Additional validation:

- `cmake --build build --config Debug --target TextureImportTest ModelImportServiceTest KimPeanutAssetTool` — passed.
- `ctest --test-dir build -C Debug -R "TextureImportTest|ModelImportServiceTest" --output-on-failure` — 18/18 passed, including cache invalidation when BC quality changes.
- `cmake --build build --config Debug` — passed after `rgbcx` integration and
  encoder metrics/cancellation changes.
- `ctest --test-dir build -C Debug --output-on-failure` — 456/456 passed.
- `cmake --build build --config RelWithDebInfo --target TextureImportTest NativeMaterialTest ModelImportServiceTest KimPeanutAssetTool` — passed.
- `ctest --test-dir build -C RelWithDebInfo -R "TextureImportTest|NativeMaterialTest|ModelImportServiceTest" --output-on-failure` — 26/26 passed.
- `cmake --build build --config RelWithDebInfo --target TextureImportTest` — passed.
- `ctest --test-dir build -C RelWithDebInfo -R TextureImportTest --output-on-failure` — 10/10 passed, including the independent BC decoder quality gate and concurrent rgbcx initialization test.

## Sponza bakeoff evidence

These are fresh single-run RelWithDebInfo imports on the same machine and
configuration: 8 workers, completion queue capacity 2, and a 1 GiB texture
memory budget. They are attribution evidence, not the required three-run
median.

| encoder / quality | total seconds | texture cook seconds | BC3 encode seconds | BC3 MP/s | BC5 encode seconds | peak working set |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ReferenceV1 / Balanced | 61.751765 | 39.705348 | 4.742191 | 57.785 | 2.219669 | 749,416,448 B |
| RgbcxV113 / Fast | 67.225812 | 45.372087 | 23.724073 | 11.551 | 0.705584 | 749,862,912 B |
| RgbcxV113 / Balanced | 72.602293 | 50.790789 | 37.871546 | 7.236 | 0.703075 | 749,342,720 B |

All three runs produced 174 products, 144 texture products, 17,126,823 BC3
blocks, and 8,039,121 BC5 blocks. `rgbcx` improves BC5 but loses substantially
on the dominant BC3 workload. Neither candidate quality meets the AT1.4.5
texture-cook or total-import thresholds, and the production default remains
`ReferenceV1`.

## Remaining work

- AT1.4.0 still needs literal pre-change compressed-byte/decoded-quality
  fixtures; the fresh three-run ReferenceV1 baseline is now recorded below.
- AT1.4.2 metrics and the cancellation-aware per-mip seam are landed.
- AT1.4.3 integration is landed, but the pinned candidate fails the Sponza
  throughput gate and must not become the default.
- AT1.4.4 independent BC decoders, semantic quality metrics, and concurrent
  determinism coverage are landed; the current balanced fixture comparison
  passes its thresholds.
- AT1.4.5 still needs the required three-run comparison and runtime visual
  acceptance; the current single-run bakeoff already rejects the candidate on
  performance.

## Memory-admission control measurement

A fresh `ReferenceV1/Balanced` control import with the same 8 workers and
queue depth, but a 4 GiB texture memory budget, completed in 43.749626 s with
21.927433 s in TextureCook and `peak_active_jobs = 8`. This demonstrates that
the worker pool can exploit more parallelism, but peak working set rose to
1,473,421,312 B and peak reserved bytes to 3,397,386,240 B. The mandated 1 GiB
run remains bounded at approximately 749 MB and admits three active jobs.
Lowering the estimator alone would therefore weaken the memory contract; a
future performance slice must reduce per-job live storage or explicitly revise
the memory budget with new acceptance evidence.

## AT1.4.0 / AT1.4.5 ReferenceV1 three-run baseline

Three fresh RelWithDebInfo imports used the same Sponza source and AT1.3
execution policy as the bakeoff: 8 texture workers, completion queue depth 2,
and a 1 GiB texture memory budget. Each run used a new archive root and
`ReferenceV1/Balanced`.

| run | total seconds | texture cook seconds | BC3 encode seconds | BC5 encode seconds | peak working set |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 62.648796 | 40.732158 | 4.812097 | 2.211852 | 749,457,408 B |
| 2 | 62.367591 | 40.381529 | 4.821298 | 2.202306 | 749,281,280 B |
| 3 | 62.563106 | 40.501032 | 4.975703 | 2.211766 | 749,326,336 B |
| median | 62.563106 | 40.501032 | 4.821298 | 2.211766 | 749,326,336 B |

All runs published 174 products and 144 textures, with 17,126,823 BC3 blocks
and 8,039,121 BC5 blocks. The model identity was
`01fa768ff6984ecf7080afafebef02331018609e744166baec7c713ffca93257` in every
run. The median peak working set is within the AT1.3 memory observation.
These results establish the current reference median but do not satisfy the
provisional AT1.4.5 target of 30 s TextureCook or 55 s total import; the
allocation cleanup is byte-compatible and does not by itself meet that target.

## Decode and cook lifetime cleanup

The first memory-lifetime slice now transfers ownership through the hot path:

- `TextureImporter::Import` moves decoded file pixels into `ImportedTexture`.
- `TextureCooker::Prepare(ImportedTexture&&)` moves RGBA8 pixels into the
  prepared mip chain instead of copying them.
- BC compression consumes its owned prepared data and no longer clones the
  complete mip chain before replacing each level.
- Native material profile cooking releases CPU mip chains after serialization
  and scopes the prepared chain to the cooking operation, retaining only
  serialized product bytes for publication.

The affected RelWithDebInfo build and 28 texture/material/model-import tests
passed. A fresh Sponza run with the same 8-worker, queue-depth-2, 1-GiB,
ReferenceV1/Balanced policy measured 60.653783 s total and 38.527467 s in
TextureCook, with 749,658,112 B peak working set and three peak active jobs.
Compared with the current three-run median, this is 1.909323 s faster overall
and 1.973565 s faster in TextureCook. Peak RSS and admission did not change
materially, so the 1-GiB estimator remains unchanged pending direct evidence
that a lower reservation is safe.
