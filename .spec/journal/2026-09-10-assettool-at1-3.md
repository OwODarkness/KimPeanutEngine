# 2026-09-10 — AssetTool AT1.3 bounded cook pipeline

**Status:** complete for AT1.3; implementation, CPU pipeline evidence,
cross-backend runtime visual evidence, closure-growth stress, and the complete
injected-failure matrix are recorded. AT1.6 remains the later integration gate.

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
- Added a coordinator-only test hook and a three-texture fixture proving that
  queue depth one blocks a worker behind a deliberately slow completion
  consumer without allowing the queue to exceed one entry.
- Added coverage for monotonic stage-local progress, coordinator-thread-only
  callbacks, and the absence of Texture entries in the published source
  manifest.
- Rebuilt `KimPeanutAssetTool` in Debug and RelWithDebInfo with elevated
  MSBuild access after the non-elevated SDK metadata lookup was denied.
- `ctest --test-dir build -C Debug --output-on-failure -R
  "ImageIOUnitTest|NativeMaterialTest|TextureImportTest|ModelImportServiceTest"`:
  17/17 passed before the backpressure hook was added.
- `ctest --test-dir build -C Debug --output-on-failure -R
  "ModelImportServiceTest"`: 8/8 passed with the backpressure test.
- Full `ctest --test-dir build -C Debug --output-on-failure`: 450/450 tests
  passed after the scheduler change.

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

## Sponza observation

The rebuilt RelWithDebInfo AssetTool imported
`asset/model/sponza/main_sponza/NewSponza_Main_glTF_003.gltf` into isolated
temporary archives. All runs produced the same model hash, 29 Materials, 144
Texture products, 72 unique cook jobs, and 174 total products.

| Policy | Total | Texture cook | Workers | Queue | Budget | Peak reserved | Peak working set |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `--jobs 1 --writer-queue-depth 1` | 131.913208 s | 107.711235 s | 1 | 1 | 1 GiB | 679,477,248 B | 749,391,872 B |
| default | 61.884811 s | 39.665596 s | 8 | 2 | 1 GiB | 1,019,215,872 B | 749,551,616 B |
| `--jobs 8 --memory-budget-mib 256 --writer-queue-depth 1` | 131.496776 s | 109.060851 s | 8 | 1 | 256 MiB | 339,738,624 B* | 749,469,696 B |

The default policy is 2.13× faster than one worker, a 53.1% wall-time
reduction. The 1 GiB policy admitted three active jobs at peak; the 256 MiB
policy admitted one oversized job at a time, reported all 72 oversized jobs,
and accumulated 729.23 s of memory wait without exceeding the one-job
oversized allowance. This demonstrates bounded admission and queue-depth-one
backpressure. `*` is intentionally above the configured budget because the
job was reported oversized and ran alone.

The required three-run cold samples were then repeated in fresh isolated
archives. The default-worker totals were 61.884811 s, 63.565133 s, and
62.035308 s (mean 62.495084 s); the one-worker totals were 131.913208 s,
130.786900 s, and 129.990728 s (mean 130.896945 s). Every run produced the
same 72 unique cook jobs, 144 Texture products, and 174 total products. The
three-run mean is a 2.095× speedup and 52.26% wall-time reduction.

## Runtime visual observation

The checked-in `level/sponza.level` was launched through the Runtime command
transport after its 127-asset startup completed. Vulkan startup asset loading
took 61.136 s and OpenGL took 61.072 s; both rendered 81 textures, 283 draws,
and the same Sponza material layout. Fresh `scene_color` captures were exported
and visually inspected:

- [Vulkan retry](../../save/screenshots/validation/at1-3-sponza-vulkan-rerun.png)
- [OpenGL](../../save/screenshots/validation/at1-3-sponza-opengl.png)

The first Vulkan capture was taken before an extra post-readiness delay and was
discarded; the retry matches the OpenGL corridor capture. This validates the
runtime/backend path against the current checked-in archive products. The
Runtime startup is separate from AssetTool cook time and does not replace the
required repeated cold-import measurements.

## Remaining risk

AT1.4 remains the place for replacing or optimizing the BC encoder. The
AT1.6 integration gate still owns the final end-to-end budget and isolated
archive recook wiring.

## Closure-growth and failure-matrix evidence

- `ModelImportServiceTest.RepeatedEquivalentTextureClosureDoesNotGrowCookReservation`
  imports a 96-material closure that references one equivalent Texture cook
  key repeatedly. It confirms one unique job, one estimated/peak reservation,
  96 requested bindings, zero residual reservation, and successful material
  publication.
- `ModelImportServiceTest.FailureMatrixJoinsBlockedWorkersAndPreservesPublication`
  covers a worker decode/cook failure, cancellation while a producer is blocked
  on a depth-one completion queue, cancellation while a worker waits for the
  memory budget, immediate cancellation, and staging-root failure. Each path
  joins workers, removes operation-owned staging, preserves the prior source
  package/model, and retains a user-owned staging sentinel when applicable.
- The worker-failure case exposed and fixed a stop-state classification bug:
  internal first-error shutdown is no longer reported as `Cancelled`; explicit
  cancellation is tracked separately and worker exceptions are rethrown on the
  coordinator thread.
- `cmake --build build --config Debug --target ModelImportServiceTest` — PASS.
- `ctest --test-dir build -C Debug -R ModelImportServiceTest
  --output-on-failure` — PASS, 11/11 tests.
- The two new closure/failure tests also passed five consecutive
  `--repeat until-fail:5` iterations, with no hang or residual staging state.
