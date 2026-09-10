# AssetTool AT1.6 — Sponza integration gate

## Scope

AT1.6 closes the measurable AssetTool performance work and runs the required
determinism, archive, integrity, and backend checks. The performance gate was
also used to address the AT1.5 no-op bottleneck exposed by the first Sponza
sample.

## Implementation — 2026-09-10

- Added `byte_size` and `last_write_time` to the in-memory
  `SourceDependencyRecord`.
- Added the auxiliary `source_dependency_metadata` SQLite table. It is created
  by read-write archives without changing the existing archive schema version;
  old read-only archives remain readable and use the hash fallback until they
  are rewritten.
- Model import records dependency metadata atomically with source replacement.
  The routine cache probe checks file size and write time first, reuses the
  recorded SHA-256 on a match, and hashes the file when metadata is absent or
  changed.
- Added a regression assertion that an unchanged import reads zero dependency
  bytes as well as zero product bytes.

This keeps the existing trust boundary: routine imports use the documented
local metadata cache policy, while `AssetTool integrity`, native runtime
loading, and strict `ProbeSource` still perform byte/hash verification.

## Sponza measurements

Fixture: `asset/model/sponza/main_sponza/NewSponza_Main_glTF_003.gltf`.

Configuration: RelWithDebInfo `KimPeanutAssetTool`, `--compression bc`,
`--bc-encoder reference`, `--bc-quality balanced`, `--jobs 8`,
`--memory-budget-mib 1024`, and `--writer-queue-depth 2`. Each cold sample used
a fresh archive root; the no-op sample immediately reused that archive.

| sample | cold seconds | no-op seconds | source bytes on no-op | product bytes on no-op |
| --- | ---: | ---: | ---: | ---: |
| 1 | 60.846607 | 0.033909 | 0 | 0 |
| 2 | 60.491406 | 0.035068 | 0 | 0 |
| 3 | 61.025464 | 0.031429 | 0 | 0 |
| median | 60.846607 | 0.033909 | 0 | 0 |

Every cold sample produced 72 unique cook keys, 144 Texture products, 29
Materials, 174 products, and the same Model hash:
`01fa768ff6984ecf7080afafebef02331018609e744166baec7c713ffca93257`.
Each wrote `2,107,134,699` bytes, admitted three active jobs, reserved
`1,019,215,872` bytes at peak, and reported approximately 749.6–749.7 MB peak
working set. The serial control completed in 123.411402 s and produced the same
174 product files; normalized product paths, lengths, and SHA-256 values
matched the default-worker archive exactly.

Before the metadata cache, the same unchanged Sponza reimport measured
8.44–8.59 s because it hashed the 2.17-GB dependency closure. AT1.6 reduces
that routine path to roughly 34 ms without reading product payloads.

## Validation

- `cmake --build build --config RelWithDebInfo --target KimPeanutAssetTool ModelArchiveDatabaseTest ModelImportServiceTest -- /m:2` — passed.
- `ctest --test-dir build -C RelWithDebInfo -R "ModelArchiveDatabaseTest|ModelImportServiceTest" --output-on-failure` — 15/15 passed after the cache implementation.
- Asset-focused RelWithDebInfo CTest matrix covering importer, archive, native
  products, collision/transaction, concurrency/failure, codecs, and mipmaps —
  121/121 passed.
- `AssetTool integrity` on the default-worker and serial Sponza archives —
  both passed.
- Product snapshot comparison — 174/174 files and total bytes matched exactly.
- `cmake --build build --config RelWithDebInfo --target GraphicsSmoke -- /m:2` — passed.
- `GraphicsSmoke --graphics-api vulkan` and `--graphics-api opengl` — both
  reached the D5 comparison but exited 1 with `D5 Vulkan/OpenGL silhouettes
  diverged`; this is a pre-existing Render validation failure, not an
  AssetTool failure. The fresh AT1.3 Sponza captures remain the available
  visual evidence for the unchanged native product path.

## Decision and remaining risk

The provisional AT1.6 performance budgets are met: cold import is below 120 s
and unchanged import is below 5 s by a wide margin. The bounded-memory and
deterministic archive gates also pass. GPU cooking is not adopted: the accepted
CPU path meets the end-to-end target, and the measured BC3/BC5 encode slices
are not sufficient evidence to justify GPU setup, transfer, determinism, and
CI complexity.

AT1.6 remains open in the roadmap only until the unrelated D5
Vulkan/OpenGL silhouette comparison is repaired and rerun. No Render changes
were made in this stage.
