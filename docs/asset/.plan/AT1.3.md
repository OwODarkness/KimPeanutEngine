# AT1.3 — Bounded Parallel Cook/Write Pipeline

- Status: complete for the AT1.3 scope; implementation, runtime smoke,
  three-run Sponza evidence, closure-growth stress, and failure-matrix tests
  are landed
- Parent design: [AT1 — AssetTool Import Throughput and Memory](AT1.md)
- Parent spec: [AssetTool Import Performance](../../../.spec/specs/assettool-import-performance.md)
- Prerequisites: AT1.1 and AT1.2 landed; remaining AT1.0 attribution work is
  tracked independently of this bounded-pipeline stage

## Assignment

Execute the unique Texture jobs produced by AT1.2 concurrently, overlap their
CPU work with product staging, and make peak Texture-cook memory an explicit
budget rather than a function of the complete model Texture closure.

AT1.3 owns scheduling, memory admission, completion backpressure, staged-file
ownership, cancellation, deterministic result assembly, and pipeline metrics.
It does not change compression quality, native product formats, archive schema,
or Runtime loading.

## Current baseline

AT1.1 removed the unused decoded-image retention, product-buffer copies into
publication, full payload round-trip validation of fresh products, and the
second staging write. AT1.2 added a typed `TextureCookKey`, memoized shared
Material bindings, and shared prepared mips between portable and block
profiles.

The remaining path is serial:

```text
ConvertImportedMaterials
  for each first-seen TextureCookKey
    decode -> prepare mips -> portable cook -> BC cook
  retain every NativeImageProduct byte vector
ModelImportService
  serialize Model/Materials
  move all products into PendingProduct
  write and publish each product
```

The current code also has no reusable Core worker pool or bounded queue.
`TextureImporter` and `TextureCooker` are stateless, so job-local instances are
the narrow safe concurrency boundary. AT1.3 must use private AssetImport C++17
primitives rather than introducing a general engine task system.

## Scope and non-goals

In scope:

- a deterministic prebuilt Material/Texture cook plan;
- operation-scoped execution settings that do not affect product identity;
- a fixed Texture worker group;
- byte-budgeted admission and a bounded completion queue;
- staging completed products on the calling import thread while workers
  continue cooking;
- staged-product manifests with no retained payload bytes;
- cooperative cancellation and first-error propagation;
- progress and metrics aggregation on the calling thread; and
- focused concurrency, backpressure, memory, failure, and determinism tests.

Out of scope:

- SIMD or replacement BC encoders, which belong to AT1.4;
- the AT1.5 cache/integrity policy;
- per-mip native Texture streaming or a native format revision;
- parallel Assimp model decoding or Model V3 encoding;
- parallel archive publication or SQLite writes;
- a general Core thread pool, task graph, or coroutine framework;
- process-wide scheduling across separate AssetTool processes; and
- GPU compression.

## Required invariants

- `ModelImportSettings` contains only byte-affecting settings. Worker count,
  queue depth, memory budget, and cancellation do not participate in the
  settings hash or product identity.
- Job ordinals come from deterministic source Material/binding traversal and
  never from worker completion order.
- Each unique `TextureCookKey` is admitted and executed at most once.
- The complete decoded/prepared/encoded lifetime of an active job is charged to
  one memory reservation. The reservation transfers with its completion and is
  released only after staging destroys the payloads.
- The completion queue is bounded. Workers block instead of accumulating the
  complete cooked closure when storage is slower than cooking.
- Product bytes are structurally validated before staging, written once, and
  replaced in memory by a `StagedProduct` manifest.
- Progress callbacks and shared metric aggregation run only on the calling
  import thread.
- Worker exceptions never escape a thread entry point. The first failure is
  retained and rethrown on the calling thread after safe shutdown.
- No archive database row changes until every product has been staged and
  published successfully.
- Operation cleanup removes only its unique staging directory. Published
  immutable products and the previous source root are never rolled back or
  overwritten.

## Selected execution model

Use the thread that called `ModelImportService::Import` as the coordinator and
single staged-file writer. Start a fixed group of job-local Texture workers:

```text
calling import thread
  build ordered cook plan
  create operation staging root
  start N workers
  while jobs or completions remain
    pop one bounded completion
    validate and write its products once
    store result by stable job ordinal
    release its memory reservation
    report progress
  join workers
  resolve Material bindings by ordinal
  serialize/stage Materials and Model
  publish all staged manifests
  commit source root last

worker N
  claim next stable job ordinal
  acquire estimated-byte reservation
  decode -> prepare once -> cook profiles
  validate product structures
  push completion, transferring reservation
  repeat unless stopped
```

This overlaps disk staging with later Texture CPU work without a dedicated I/O
thread. It also keeps filesystem errors, progress callbacks, and manifest
mutation on the existing calling thread.

One writer is intentional. Multiple writers can turn large sequential writes
into competing operations and do not help until measurement proves the storage
path is the bottleneck. AT1.3 records queue wait and write throughput so a later
change can be evidence-driven.

## Concrete contracts

Names below are the intended implementation seam; minor spelling may change
during implementation, but ownership and data flow must remain equivalent.

### Operation execution policy

Add execution-only configuration to `ModelImportRequest`, not
`ModelImportSettings`:

```cpp
struct ModelImportExecutionPolicy
{
    std::uint32_t texture_worker_count{};       // 0 = automatic
    std::uint64_t texture_memory_budget_bytes{}; // 0 = default budget
    std::uint32_t completion_queue_capacity{2};
    std::function<bool()> cancellation_requested;
};

struct ModelImportRequest
{
    // Existing roots, source, settings, and progress callback.
    ModelImportExecutionPolicy execution{};
};
```

Initial resolution policy:

- automatic workers: `clamp(logical_processors - 1, 1, 8)`;
- default Texture budget: 1,024 MiB;
- default completion capacity: two completed jobs; and
- explicit worker count and budget are validated before source decode.

These defaults are provisional. AT1.0/AT1.3 Sponza evidence may tune them
without changing output bytes. `KimPeanutAssetTool import|reimport` exposes:

```text
--jobs <count>
--memory-budget-mib <count>
--writer-queue-depth <count>
```

The importer adapter forwards the policy unchanged. `cook-texture` remains a
single-product path and does not use the model pipeline.

The budget is per `Import` operation. One AssetTool invocation imports one
source, so this bounds the user-facing case. Concurrent API calls have an
aggregate bound equal to the sum of their operation budgets; a future batch
import scheduler, not AT1.3, would own a process-wide budget.

### Deterministic conversion plan

Split material conversion internally into three phases:

```cpp
NativeMaterialCookPlan BuildNativeMaterialCookPlan(
    const ImportedModelDocument &, const NativeMaterialConversionSettings &);

TextureCookCompletion ExecuteTextureCookJob(
    const ImportedModelDocument &, const TextureCookJob &);

NativeMaterialConversionResult FinalizeNativeMaterials(
    NativeMaterialCookPlan, Span<const TextureCookOutcome>);
```

`NativeMaterialCookPlan` contains:

- Material drafts in source slot order;
- Texture parameter bindings containing a stable cook-job ordinal and sampled
  channel;
- unique `TextureCookJob` entries in first-request order; and
- no decoded pixels, mip data, or serialized Texture bytes.

Each job refers to an image by stable `document.images` index rather than a raw
pointer. The imported document remains immutable and alive until all workers
join. `FinalizeNativeMaterials` replaces job ordinals with portable/BC product
paths and serializes Materials in source order.

The existing `ConvertImportedMaterials` function remains as a serial
compatibility wrapper for focused callers and tests. `ModelImportService` uses
the planned/executed path so it can stage products incrementally.

### Job completion and staged manifest

```cpp
struct TextureCookCompletion
{
    std::size_t job_ordinal{};
    ImageReference reference;
    std::vector<NativeImageProduct> products; // normally portable + BC
    NativeMaterialConversionMetrics metrics;
    MemoryReservation reservation;
};

struct StagedProduct
{
    ProductRecord record;
    std::filesystem::path staged_path;
};
```

`MemoryReservation` is move-only. A worker cannot release it after enqueueing;
ownership moves into `TextureCookCompletion`. The coordinator releases it only
after all product byte vectors in that completion have been validated, written,
and destroyed.

The coordinator deduplicates by final product hash before staging. Distinct cook
keys may legally converge on identical output bytes. A duplicate completion
reuses the existing staged manifest and drops its duplicate bytes without a
second write.

Model and Material products also transition directly from a temporary byte
vector to `StagedProduct`. The old closure-wide `PendingProduct` collection is
removed. Publication accepts the staged path, expected size, and hash and does
not require product bytes to remain resident.

## Memory admission

### Metadata probe

Admission must happen before full image decode. Extend ImageIO with a bounded
metadata probe for file and memory inputs:

```cpp
struct ImageMetadata
{
    std::uint32_t width{};
    std::uint32_t height{};
    ImagePixelFormat decoded_format{ImagePixelFormat::Rgba8};
    std::uint64_t decoded_byte_count{};
};
```

The stb implementation uses header inspection (`stbi_info` plus HDR detection)
without allocating the pixel payload. Raw embedded images already provide exact
dimensions. Probe failures use the existing malformed/decode error family.

### Reservation estimate

Use checked 64-bit arithmetic. A job estimate conservatively includes:

```text
source decoded bytes before resize
+ prepared semantic mip closure
+ portable serialized product upper bound
+ block product upper bound when emitted
+ encoder/serializer scratch allowance
+ fixed job bookkeeping
```

The estimate must account for both portable and BC byte vectors being present
in one completion. Actual sizes are recorded beside the estimate. If a later
phase discovers that it needs more than reserved, it must acquire the additional
bytes before that allocation. Underestimate incidents increment a diagnostic
counter; they must not silently exceed the budget and then attempt to account
for memory after the fact.

If one job estimate exceeds the complete budget, it may run only when no other
job owns a reservation. Metrics record the oversized allowance. Two oversized
jobs must never overlap.

The budget object uses `std::mutex` and `std::condition_variable` with FIFO
waiter order. Every wait predicate includes stop/cancellation state. Releasing a
reservation notifies all waiters so shutdown cannot strand a worker.

## Queue and worker state

Implement a private AssetImport bounded queue with these terminal states:

```text
Open -> Closing -> Closed
  |        |
  +------> Failed(first_exception)
```

- `Push` waits for capacity, budget ownership, or stop.
- `Pop` waits for a completion, terminal failure, or all workers finished.
- `CloseProducer` decrements the live-worker count; the final worker closes the
  producer side.
- `Fail` records only the first exception, requests stop, and wakes every wait.
- Queue destruction requires all worker threads already joined.

Use `std::thread`, `std::mutex`, `std::condition_variable`, atomics, and RAII
join guards available in C++17. Do not use `std::async`: its worker count is not
bounded by this policy and future destruction can block in surprising places.

Each worker constructs its own stateless `TextureImporter`/`TextureCooker`.
Workers read only immutable document/job data and write only a local
completion. No `NativeMaterialConversionMetrics`, product manifest, progress
callback, or Material object is mutated concurrently.

## Cancellation and failure sequence

Cancellation is cooperative:

1. check before claiming a job;
2. check while waiting for memory;
3. check after decode, preparation, and each profile cook;
4. check while waiting to enqueue; and
5. check before the coordinator stages a completion.

The current monolithic BC encode cannot stop mid-mip; AT1.4 may add block-row
cancellation points. AT1.3 guarantees no new job starts after cancellation and
that all threads are joined before returning.

On worker failure:

1. catch the exception in the thread entry point;
2. record the first `exception_ptr` and request stop;
3. wake budget and queue waiters;
4. let the coordinator destroy/drain unstaged completions without publishing;
5. join all workers;
6. allow `StagingCleanup` to remove the operation directory; and
7. rethrow as the existing precise import error, adding `Cancelled` only for an
   explicit cancellation request.

On staging failure, the coordinator performs the same stop/drain/join sequence.
No SQLite transaction has started, so the previous source record remains
unchanged. Products published only after the full staging phase remain absent;
if publication later fails, already-created immutable products retain the
existing orphan-safe behavior.

## Progress and metrics

Worker-local timing is returned in each completion and aggregated by the
coordinator. Existing `MetricTimer` and result counters are not mutated from
workers.

Add or preserve bounded metrics for:

- resolved worker count, budget, and queue capacity;
- active and peak-active jobs;
- current and peak reserved bytes;
- estimated versus actual bytes per bounded top-N jobs;
- oversized-job and estimate-correction counts;
- worker wait for memory;
- worker wait for completion-queue capacity;
- coordinator wait for completion;
- staging write time/bytes and effective throughput; and
- completed/total unique cook jobs.

`TextureCook` stage time remains pipeline wall time, not the sum of parallel job
durations. Optional summed CPU durations use separate fields. Progress callbacks
are emitted by the coordinator after each staged completion, preserving a
single-caller callback contract and monotonic `completed/total` counts.

## Expected file changes

- `engine/runtime/asset/model_import_service.h/.cpp` — execution policy,
  operation staging lifetime, coordinator loop, staged manifest publication,
  cancellation, and metrics.
- `engine/runtime/asset/native_material.h/.cpp` — deterministic plan/job/finalize
  seam, serial compatibility wrapper, worker-local cook result, and no retained
  Texture payloads in the service path.
- `engine/runtime/asset/asset_import_adapters.h/.cpp` — forward execution policy
  without mixing it into output settings.
- `engine/runtime/image_io/image_io.h/.cpp` and the stb codec — allocation-free
  dimension/HDR metadata probes used by memory admission.
- `engine/tool/asset/asset_tool_main.cpp` — validated `--jobs`,
  `--memory-budget-mib`, and `--writer-queue-depth` options plus metrics output.
- `engine/test/unit/asset/native_material_test.cpp` — plan ordering, job sharing,
  and serial/concurrent equivalence.
- `engine/test/unit/asset/model_import_service_test.cpp` — bounded execution,
  staging, failure, cancellation, publication, and determinism.
- ImageIO tests — metadata probe agreement with decoded images and malformed
  input rejection.
- AT1 TODO/status and a new AT1.3 journal after implementation evidence exists.

Avoid a new source file unless the private queue/budget implementation makes
`model_import_service.cpp` materially harder to review. If extracted, keep it
inside AssetImport rather than Core.

## Implementation slices

### AT1.3.0 — Freeze serial equivalence and execution policy

- Add one-worker/default-policy equivalence fixtures before concurrency.
- Add `ModelImportExecutionPolicy`, CLI parsing, validation, and metric echo.
- Prove execution-only settings do not change `SettingsHash` or product hashes.

Exit: `--jobs 1` retains the AT1.2 output and transaction behavior.

### AT1.3.1 — Build the deterministic cook plan

- Separate Material discovery/templates, unique job construction, job
  execution, and Material finalization.
- Preserve current first-request cook-key order and Material parameter order.
- Keep `ConvertImportedMaterials` as a serial wrapper.

Exit: plan + serial execution is byte-identical to AT1.2 and contains no decoded
or serialized Texture payloads before execution.

### AT1.3.2 — Add metadata probing and memory reservations

- Add ImageIO metadata probes and checked job estimates.
- Implement the move-only reservation and oversized-job rule.
- Add wait, peak, correction, and allowance metrics.

Exit: synthetic mixed-size jobs cannot exceed the configured reservation policy,
and malformed/overflow dimensions fail before allocation.

### AT1.3.3 — Add bounded workers and completion queue

- Start the fixed worker group and execute each stable job ordinal once.
- Return worker-local results/timings through the bounded queue.
- Resolve results into pre-sized ordinal slots and aggregate only on the caller.

Exit: worker counts one through the tested maximum produce identical Texture and
Material hashes; queue depth one demonstrably applies backpressure.

### AT1.3.4 — Stage while cooking and remove payload closure

- Create a process/call-unique `operation_root` before Texture execution; a
  source stem plus service-local sequence is not sufficient across processes.
- Make the caller stage each completion while other workers continue.
- Replace service-path `NativeImageProduct`/`PendingProduct` retention with
  `StagedProduct` manifests.
- Stage Model and Material byte vectors immediately after their validation.
- Publish from staged paths only after all products are staged.

Exit: each unique new product has exactly one staging write, and after each
completion the coordinator retains only manifest/reference metadata.

### AT1.3.5 — Cancellation and failure hardening

- Implement first-error, stop, wake, drain, join, and rethrow behavior.
- Add explicit cancellation observation to the typed import request path.
- Cover decode/cook failure, blocked producer, blocked memory waiter, staging
  failure, cancellation, collision, and concurrent winner.

Exit: no test hangs, no thread escapes, operation staging is removed, and the
prior source database root remains unchanged in every pre-commit failure.

### AT1.3.6 — Sponza evidence and acceptance

- Run one-worker and default-worker RelWithDebInfo cold imports three times.
- Run a constrained-memory/queue-depth-one import to prove backpressure.
- Compare complete product hash sets and archive source snapshots.
- Record total/stage time, CPU occupancy, disk throughput, job counts, queue
  waits, reservation estimate/actual/peak, and process peak working set.
- Run the required Asset tests and Vulkan/OpenGL visual checks.

Exit: the acceptance contract below is satisfied and the results are appended
to a factual AT1.3 journal.

## Acceptance criteria

- [x] `--jobs 1` remains byte- and archive-equivalent to the AT1.2 serial path.
- [x] Default workers cook every unique job exactly once and produce the same
  product hash set as one worker.
- [x] Peak reserved Texture bytes do not exceed the configured budget except
  for one explicitly reported oversized job running alone.
- [x] Peak Texture-cook memory does not grow linearly when equivalent jobs are
  repeated to enlarge the source Material/Texture closure.
- [x] Queue depth one blocks producers under an injected slow writer without
  increasing queued completions beyond one.
- [x] The calling thread stages every unique new product exactly once while at
  least one worker can continue CPU work.
- [x] No Texture payload bytes remain in the final publication manifest.
- [x] Progress is monotonic and callbacks are never invoked concurrently.
- [x] Cancellation and each injected failure join all workers, remove only the
  operation staging tree, and preserve the previous source record.
- [x] Concurrent-destination collision checks and create-if-absent behavior are
  unchanged.
- [x] RelWithDebInfo Sponza results show bounded memory and a material speedup
  over `--jobs 1`; AT1.3 does not claim the final 120-second AT1.6 budget unless
  the measurement actually meets it.
- [x] Current checked-in Sponza products pass the Vulkan and OpenGL runtime
  visual smoke captures; direct isolated-archive recook wiring remains an
  AT1.6 evidence concern.

## Validation commands

Use the standard wrapper where its target mapping is available; otherwise use
the equivalent focused CMake/CTest commands:

```powershell
cmake --build build --config Debug --target ImageIOUnitTest
cmake --build build --config Debug --target NativeMaterialTest
cmake --build build --config Debug --target ModelImportServiceTest
cmake --build build --config Debug --target KimPeanutAssetTool
ctest --test-dir build -C Debug --output-on-failure -R "ImageIO|NativeMaterialTest|TextureImportTest|ModelImportServiceTest"
cmake --build build --config RelWithDebInfo --target KimPeanutAssetTool
```

The exact ImageIO target/test name must be confirmed from CMake before running;
do not claim a guessed target. Sponza commands, cache conditions, hardware, and
three-run results belong in the execution journal. Compilation alone cannot
complete AT1.3.

## Reference fit

- O3DE Asset Builders allow multiple `ProcessJob` operations, write products to
  job-local temporary directories, and return product metadata for later cache/
  catalog publication. Applicable: job-local CPU work, temporary products, and
  small completion manifests. KimPeanutEngine keeps its simpler in-process
  threads and root-last SQLite transaction rather than adopting O3DE's process
  protocol. See [O3DE Asset Builders](https://www.docs.o3de.org/docs/user-guide/assets/pipeline/asset-builders/).
- O3DE Asset Processor exposes a maximum concurrent job count because throughput
  and machine resource use must be balanced. Applicable: an explicit worker
  policy. KimPeanutEngine adds a byte budget because Texture dimensions, not job
  count alone, dominate its peak memory. See [O3DE Asset Processor configuration](https://www.docs.o3de.org/docs/user-guide/assets/asset-processor/configuration/).
- NVIDIA Texture Tools' archived output-handler design demonstrates emitting
  compressed data to a caller-owned sink instead of requiring an importer to
  retain the complete result closure. Applicable: sink ownership and early
  release. Not adopted: its discontinued library, CUDA backend, format choices,
  or exact callbacks. See [NVIDIA Texture Tools](https://github.com/castano/nvidia-texture-tools).

No reference is used as a framework template. The selected design follows the
local immutable-product, operation staging, and root-last database boundaries.

## Rejected designs

- **One `std::async` per Texture.** Worker count and memory are not bounded, and
  future lifetime can introduce blocking shutdown behavior.
- **A dedicated writer plus a coordinator thread.** The caller can perform the
  one required write while workers continue; another thread adds synchronization
  without establishing a measured benefit.
- **Release reservations when cooking finishes.** Completed payloads would then
  sit uncharged in the queue and memory could exceed the declared budget.
- **Bound only queue item count.** Two 8K HDR completions and two small mask
  completions do not represent comparable memory.
- **Publish each Texture immediately from workers.** It mixes cooking with
  filesystem collision policy, allows concurrent callbacks/manifest mutation,
  and complicates deterministic failure handling.
- **Serialize Materials as jobs finish.** Completion order would leak into
  authored ordering and makes shared-result failure propagation harder.
- **Move the scheduler into Core.** There is no second proven consumer or stable
  general task contract; AT1.3 needs a private bounded pipeline first.

## Open decisions for AT1.3.0

- Confirm whether 1,024 MiB is the appropriate default budget on the reference
  laptop or whether it should be derived conservatively from physical memory.
- Confirm the maximum automatic worker cap after the RelWithDebInfo baseline;
  eight is the initial ceiling, not an assumed optimum.
- Decide whether explicit cancellation is supplied only through the typed API
  in AT1.3 or also connected to platform Ctrl+C handling in AssetTool.
- Confirm whether file metadata probing belongs in ImageIO's public codec
  contract or can remain an AssetImport-private stb-backed helper without
  leaking stb types.
