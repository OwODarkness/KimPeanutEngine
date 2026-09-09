# 2026-09-10 — AssetTool AT1.3 bounded cook pipeline

**Status:** implementation landed; target build/runtime evidence is pending an
environment repair.

## Change

AT1.3 now executes the AT1.2 unique texture cook plan through a bounded
`std::thread` worker pipeline. Workers reserve an estimated decoded working
set from a configurable budget, cook immutable document inputs, and transfer
move-only reservations with completions through a bounded queue. The import
coordinator is the only thread that invokes progress callbacks, stages texture
products, assembles materials, and publishes immutable products.

Texture metadata is probed before decode for external and embedded encoded
images. Model/material payloads are staged once and released after the write;
texture payloads are staged once as completions arrive. The archive source row
is still replaced last, and operation staging is removed on cancellation,
conversion failure, or publication failure.

The AssetTool import command exposes `--jobs`, `--memory-budget-mib`, and
`--writer-queue-depth`. Execution policy is deliberately excluded from the
settings hash, so serial and parallel policies produce the same content
addressed products. Metrics now expose resolved policy, queue/memory pressure,
estimates, corrections, completion progress, and coordinator waits.

## Reference gate

- O3DE `Code/Tools/AssetProcessor/native/utilities/Builder.h` at development
  revision `80f47141642496748de7313c6c475a0b564ea060` was inspected for
  request/result ownership, terminal job outcomes, and cancellation handling.
  Its job/result boundary applies; its editor process architecture does not.
- NVIDIA Texture Tools `src/nvtt/TaskDispatcher.h` and
  `src/nvtt/OutputOptions.h` at master revision
  `aeddd65f81d36d8cb7b169b469ef25156666077e` were inspected for sequential /
  parallel dispatch and caller-owned output sinks. The implementation keeps
  KimPeanut's C++17 worker boundary and coordinator-owned staging instead of
  copying either framework.

## Validation

- MSVC `/Zs /std:c++17 /EHsc /W4` syntax checks passed for all changed
  translation units and `model_import_service_test.cpp`.
- `git diff --check` passed.
- Added a model-import regression test covering serial/parallel product
  equivalence, policy metrics, queue depth, completion count, released
  reservations, cancellation, staging cleanup, and preservation of the
  previous source row.
- `cmake --build build --config Debug --target ModelImportServiceTest` was
  attempted but blocked before compilation by MSBuild's
  `GetLatestSDKTargetPlatformVersion(Windows, 10.0)` access to
  `C:\Users\17519\AppData\Local\Microsoft SDKs`.

## Cerberus observation

The rebuilt Debug AssetTool imported the local
`asset/model/cerberus/Cerberus_LP.FBX` into isolated temporary archives.
Both policies produced the same model, material, and two texture filenames.

| Policy | Total | Texture cook | Workers | Queue | Peak reserved | Peak working set |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `--jobs 1 --writer-queue-depth 1` | 15.742587 s | 12.931747 s | 1 | 1 | 339,738,624 B | 205,451,264 B |
| default | 15.245878 s | 12.547623 s | 8 | 2 | 339,738,624 B | 205,451,264 B |

The source has only one unique texture cook job, so this is equivalence and
pipeline telemetry evidence rather than a meaningful parallel speedup test.
The default run was approximately 0.50 s faster, within the noise expected for
one job. A warm reimport completed in 1.594876 s with zero texture cooking,
zero product writes, and a cache hit. The warm result currently reports zero
textures because the archive snapshot links only root Model/Material products;
the two texture files remain present and unchanged in the archive. This is a
pre-existing transitive-product reporting limitation, not a recook.

## Remaining risk

The real unit-test binary and Sponza/Cerberus runtime observations still need
to be run after the Windows SDK access issue is fixed. AT1.4 remains the place
for replacing or optimizing the BC encoder.
