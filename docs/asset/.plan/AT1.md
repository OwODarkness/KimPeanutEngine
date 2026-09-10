# AT1 — AssetTool Import Throughput and Memory

- Status: active; AT1.1 and AT1.2 landed, AT1.0 measurement remains open
- Parent roadmap: [AssetTool import performance](../TODO.md#assettool-import-performance-roadmap)
- Execution spec: [AssetTool Import Performance](../../../.spec/specs/assettool-import-performance.md)
- Related runtime work: [AP1 — Startup Asset Loading Performance](AP1.md)

## Problem

The initial Sponza model import was reported at approximately 1,200 seconds
with excessive process memory. AP1.2 and AP1.3 reduce the size and runtime cost
of native Texture and Model products, but the initial offline production path
performed redundant work and retained the complete cooked Texture closure
before publication. AT1.1 and AT1.2 have since removed the first copy/write
amplification and duplicate Texture preparation; bounded concurrent execution
remains AT1.3.

The observed import path has five independent forms of amplification:

1. `ConvertImportedMaterials` calls `PrepareImage` for each Material parameter.
   A packed metallic-roughness source is therefore cooked once for metallic and
   again for roughness, while images shared by multiple Materials are also
   recooked. Product-hash deduplication happens only after cooking.
2. Portable and block-compressed profiles independently repeat conversion and
   mip generation instead of consuming one prepared mip chain.
3. Each `NativeImageProduct` retains serialized bytes and an unused decoded
   image. Texture bytes are later copied into `PendingProduct`, so memory grows
   with the complete Texture closure rather than bounded active work.
4. New products are written into the operation staging tree once by the caller
   and again inside `PublishProduct` before the hard-link publication step.
5. The in-house BC3/BC4/BC5 encoder traverses blocks serially with scalar
   candidate searches. A Debug build magnifies this cost, but Release alone
   does not remove the redundant work, copies, or unbounded lifetime.

The no-op path is a separate cost center. `TryCacheHit` hashes recorded source
dependencies, then reads, hashes, and structurally deserializes every archived
product. This is a full integrity scan disguised as a routine cache probe.

## Design question

How should `KimPeanutAssetTool` keep all available CPU cores and useful disk
bandwidth busy while bounding memory, preserving deterministic native products,
and retaining the existing immutable publication and root-last SQLite
transaction?

## Boundaries and invariants

- Import and cooking remain offline Asset work. Runtime, Editor, Render, and
  Graphics are not dependencies of the importer.
- Assimp owns foreign decoding only. AssetImport owns cook scheduling,
  serialization, staging, hashing, publication, and failure cleanup.
- Texture identity is derived from source content, semantic cook settings, and
  the selected output profile. A shared result must never cross incompatible
  semantic or settings boundaries.
- Native products remain deterministic for a fixed importer/encoder version and
  settings, regardless of worker completion order.
- Product publication remains content-addressed, immutable, and
  create-if-absent. A concurrent winner is accepted only after the existing
  collision checks pass.
- The source/archive database update remains a short transaction performed only
  after every referenced product has been published successfully.
- Failure or cancellation must leave the prior source record unchanged. A
  staged file may be removed; an already-published immutable orphan may remain
  for later archive garbage collection.
- Parallelism is explicitly bounded by both worker count and estimated bytes in
  flight. `hardware_concurrency()` is not itself a memory policy.
- GPU acceleration is optional offline tooling. It must not introduce a Runtime
  graphics dependency or become required for deterministic headless builds.

## Chosen pipeline

```text
foreign Model decode and dependency discovery
  -> canonical unique TextureCookJob table
       key = source identity + semantic + dimensions/mips + profile settings
  -> bounded CPU workers
       decode one source
       prepare semantic pixels and mip chain once
       encode portable and BC variants from shared prepared mips
  -> bounded staged-product writer
       write once while the next jobs compute
       hash and validate
       release decoded/mip/encoded buffers
  -> small StagedProduct manifest
       ProductRecord + staging path + hash + size + logical bindings
  -> deterministic Material and Model serialization
  -> immutable create-if-absent publication
  -> root-last SQLite source transaction
```

The coordinator retains hashes, paths, dimensions, and bindings, not Texture
payloads. Material order and parameter order come from deterministic source
slots; worker completion order never determines serialized output.

### Unique cook jobs

Introduce a `TextureCookKey` containing at least:

- canonical external path plus current source content hash, or embedded-image
  identity plus content hash;
- `TextureSemantic`;
- maximum dimension and mip-level policy;
- portable/block profile selection and encoder version; and
- every quality option that can change bytes.

Build or memoize the table before expensive cooking. Material parameters refer
to shared job results. Metallic and roughness bindings for one packed image
share one `PackedLinear` job while preserving their distinct sampled channels.
Different semantics do not share prepared data unless an explicit future
conversion contract proves them byte-equivalent.

### Bounded memory scheduling

Each job estimates its peak reservation from decoded pixels, full mip closure,
portable output, block output, and encoder scratch. A memory semaphore admits a
job only when both a worker slot and its byte reservation are available. One
oversized job may run alone rather than deadlocking against a smaller budget.

The initial policy should expose explicit settings with conservative defaults:

- CPU worker count;
- writer queue depth; and
- maximum estimated Texture-cook bytes in flight.

The queue owns/moves completed buffers. It must apply backpressure; producers
cannot accumulate the complete result set while storage is slower than cooking.

### Staging and publication

AT1 initially keeps the existing native Texture format. A worker may serialize
one complete Texture product in memory, transfer ownership to the bounded
writer, and release it immediately after the single staging write. This caps
memory without requiring a format migration.

The writer emits a `StagedProduct` manifest instead of returning product bytes.
Publication consumes the existing staged path and never writes it again. Hash
and size checks are calculated during serialization/write where possible;
collision verification of an existing immutable destination remains strict.

Per-mip payload streaming is deferred until measurements show that the largest
single admitted Texture remains unacceptable. The current header, directory,
embedded digest, and final content hash may require header patching or a
sequential reread. That complexity is not required to stop closure-wide
retention.

### Cache policy

Routine `import` distinguishes source freshness from archive auditing:

- hash the recorded source dependency closure;
- validate database identity, canonical product metadata, existence, and size;
- reuse products without reading and deserializing their complete payloads when
  the immutable archive metadata and verification policy allow it; and
- keep `AssetTool integrity` as the explicit full byte/hash/structure audit.

If strict verification is required on every import, add a verification cache
keyed by stable file identity, size, modification metadata, and expected content
hash rather than silently weakening validation. The chosen trust policy must be
recorded before AT1.5 implementation.

## Implementation sequence

### AT1.0 — Reproducible import baseline and attribution

- Add elapsed time and byte counters for dependency probing, Assimp decode,
  image decode, semantic conversion, mip generation, portable serialization,
  BC encoding, validation, hashing, staging, publication, and database commit.
- Count unique source images, requested texture bindings, unique cook keys,
  cache hits, product writes, and bytes written.
- Record process peak working set, active jobs, reserved bytes, CPU utilization,
  and storage throughput using bounded diagnostics rather than per-block logs.
- Measure Debug and RelWithDebInfo separately with cold import, warm filesystem
  cache, and no-op reimport clearly labeled.
- Capture the exact Sponza command, settings, product counts, and hardware in the
  AT1 execution journal when implementation begins.

Exit: the reported 1,200-second run is reproduced or superseded by a comparable
baseline whose dominant stages and peak-live allocations are known.

### AT1.1 — Remove redundant lifetime, copies, and writes

- Remove the unused decoded image from `NativeImageProduct`.
- Move product buffers through conversion/publication interfaces instead of
  copying them into `PendingProduct`.
- Ensure each newly produced staged file is written exactly once.
- Replace unconditional full round-trip product deserialization during the
  fresh cook with bounded structural validation at the producing boundary where
  equivalent corruption coverage can be retained.
- Add counters/tests proving one staging write and no decoded-image retention.

Exit: a serial import produces byte-equivalent valid products with no
closure-wide decoded-image retention, no product-buffer copy into publication,
and exactly one staging write per new product.

### AT1.2 — Unique cook graph and shared preparation

- Define `TextureCookKey` and pre-cook/memoized result lookup.
- Share a packed metallic-roughness job across both Material bindings and share
  identical jobs across Materials.
- Decode, convert, and generate mips once per unique key; feed portable and BC
  encoders from the same prepared mip closure.
- Preserve stable Material slots, parameter ordering, profile references, and
  content-addressed output paths independently of job completion order.
- Cover external, embedded compressed, embedded raw, shared, and semantically
  distinct images.

Exit: instrumentation reports exactly one decode/preparation per unique cook
key and no duplicate packed-texture cook, with deterministic products across
repeated serial runs.

### [AT1.3 — Bounded parallel cook/write pipeline](AT1.3.md)

- Add a job-local or proven thread-safe Texture importer/cooker context.
- Add configurable CPU workers plus a byte-budget admission controller.
- Transfer completed products to a bounded writer queue so writing one product
  overlaps preparation/compression of later jobs.
- Retain only `StagedProduct` manifests after each write; release reservations
  and payload memory promptly.
- Propagate first failure/cancellation, drain or stop queues safely, clean only
  operation-owned staging paths, and keep the prior database root intact.
- Test worker counts of one and many, constrained budgets, an oversized single
  job, slow/failing writers, cancellation, and concurrent imports.

The concrete worker/coordinator interfaces, memory reservation transfer,
failure state machine, implementation slices, and validation matrix are owned
by the [AT1.3 stage contract](AT1.3.md).

Exit: peak Texture-cook memory is bounded by the configured budget plus one
documented oversized-job allowance and does not grow linearly with Texture
count. One-worker and multi-worker runs publish identical product identities.

### AT1.4 — CPU compression throughput

- Benchmark the existing scalar encoder against candidate SIMD/multithreaded
  implementations on representative color, normal, and packed textures.
- Prefer parallelism across independent Texture jobs; add block-row/tile
  parallelism only for a single large Texture when job-level concurrency cannot
  saturate the CPU within the memory budget.
- Put any adopted encoder behind the Asset-owned cook interface and include its
  version/quality policy in settings hashing.
- Measure encode time, quality, determinism, build/platform cost, and license or
  maintenance risk before selecting a dependency.

Exit: the selected CPU path materially reduces BC3/BC4/BC5 time, produces
deterministic valid blocks, passes visual/quality thresholds, and remains usable
in headless offline builds.

### AT1.5 — Fast no-op import and explicit integrity audit

- Separate the ordinary freshness probe from `integrity`'s complete archive
  scan without changing native Runtime verification.
- Define and test the archive trust or verification-cache policy.
- Report cache-probe dependency hashing, metadata checks, and any product bytes
  read so a regression cannot silently restore closure-wide scanning.
- Preserve detection of missing, truncated, replaced, colliding, and malformed
  products on the policy path responsible for each condition.

Exit: an unchanged Sponza import does not decode foreign inputs, cook products,
or read/deserialise the full Texture closure; the explicit integrity command
continues to detect product corruption.

### AT1.6 — Sponza integration and optional GPU decision gate

- Run at least three RelWithDebInfo cold-import and no-op samples on the same
  reference machine and settings; record median and range.
- Record output closure, unique cook count, bytes written, CPU occupancy, disk
  throughput, and peak memory alongside elapsed time.
- Compare one-worker and default-worker product hashes and archive snapshots.
- Run Asset importer, codec, collision, transaction, concurrency, and integrity
  tests plus Vulkan/OpenGL visual checks for the recooked products.
- Consider a GPU encoder only if the bounded SIMD CPU path remains the measured
  dominant cost. Record device setup, transfer/readback, determinism, fallback,
  CI, and cross-platform evidence before approving it.

Provisional performance target: on the same reference machine and Sponza
settings as the reported run, RelWithDebInfo cold import completes in at most
120 seconds and unchanged no-op import completes in at most 5 seconds. AT1.0
may revise these numbers only with recorded evidence and explicit user approval.

Exit: the provisional budgets or an explicitly approved replacement are met;
memory is bounded by policy rather than Texture count; output is deterministic;
and transaction, integrity, and cross-backend behavior remain valid.

## Reference discovery

- O3DE runs several independent Asset Builder jobs concurrently, gives each
  `ProcessJob` a temporary output directory, and lets Asset Processor move
  successful products into the cache and catalog. Its worker count is
  configurable to trade throughput for resource usage. Applicable: job-local
  output, small result metadata, and bounded job concurrency. Not adopted:
  O3DE's builder process/protocol and Asset Processor database architecture.
  Sources: [Asset Builders](https://www.docs.o3de.org/docs/user-guide/assets/pipeline/asset-builders/)
  and [Asset Processor configuration](https://www.docs.o3de.org/docs/user-guide/assets/asset-processor/configuration/).
- Intel's ISPC Texture Compressor demonstrates SIMD implementations for BC1,
  BC3, BC4, BC5, BC6H, BC7, and ASTC. Applicable: evidence that the current
  scalar BC path has substantial CPU optimization headroom. Not adopted by
  default: the repository is archived and would add a maintenance obligation.
  Source: [ISPC Texture Compressor](https://github.com/GameTechDev/ISPCTextureCompressor).
- Microsoft DirectXTex exposes maintained BC compression and parallel CPU
  compression facilities. Applicable: Windows benchmark candidate and evidence
  for an encoder adapter boundary. Not automatically adopted: KimPeanutEngine
  still needs a cross-platform/headless policy rather than making DirectXTex a
  universal contract. Source: [DirectXTex](https://github.com/microsoft/DirectXTex).
- AMD Compressonator offers CPU and GPU mip/compression paths. Applicable: an
  optional GPU feasibility comparison after CPU saturation. Not adopted in the
  base design: GPU setup, transfers, driver behavior, deterministic output, and
  CI fallback add costs that do not address the existing duplicate work or
  closure-wide retention. Source: [Compressonator](https://gpuopen.com/compressonator/).

## Rejected alternatives

- **Only add threads around the current loop.** It multiplies existing decoded
  images, mip closures, product copies, and encoder scratch, worsening peak
  memory before removing redundant work.
- **Use an unbounded producer/consumer queue.** It moves closure-wide retention
  from one vector into a queue and fails when storage is slower than cooking.
- **Make Material own Texture bytes.** Shared products would be duplicated and
  content-addressed Texture identity would be weakened.
- **Commit each Texture to SQLite as it finishes.** Partial source roots would
  become visible and rollback would no longer preserve the previous import.
- **Change the native Texture format before measuring per-job memory.** Writing
  and releasing complete per-Texture products already removes the dominant
  closure-wide lifetime with less compatibility risk.
- **Require a GPU encoder immediately.** It cannot eliminate redundant cooks,
  duplicated mips, copies, double writes, or expensive no-op validation and
  would make offline tooling dependent on device/driver availability.

## Validation requirements

Follow [the validation matrix](../../validation_matrix.md). Implementation
crosses AssetImport, native Texture serialization, the archive publication
transaction, and the authoring executable, so each stage requires focused Asset
tests and the final gate requires the complete Asset test set plus real Sponza
and Vulkan/OpenGL evidence.

Required evidence includes:

- deterministic outputs across one/default worker counts;
- shared/packed Texture deduplication and semantic-key separation;
- bounded-memory, backpressure, slow-writer, cancellation, and oversized-job
  cases;
- one-write publication, immutable collision, concurrent-winner, cleanup, and
  root-last transaction cases;
- fast cache-hit and explicit full-integrity corruption cases;
- Debug and RelWithDebInfo timings kept separate;
- three-run Sponza latency, peak memory, CPU, disk, and byte measurements; and
- Vulkan and OpenGL visual acceptance for recooked AP1.2 products.

Compilation alone cannot complete AT1 because its outcome is import throughput,
bounded memory, storage behavior, deterministic product identity, and valid
rendered output.

## Open questions

- What Texture-cook memory budget and default worker count fit the reference
  laptop after AT1.0 measures actual job peaks?
- Can semantic preparation be represented as one shared immutable mip view for
  both portable and BC encoders without another closure-sized copy?
- Should product hashing occur in the serializer, writer, or one combined sink
  without changing the current native digest contract?
- Which maintained cross-platform SIMD encoder meets BC3/BC4/BC5 quality and
  determinism requirements, or should the current encoder be vectorized?
- Does the immutable local archive permit metadata-only routine probes, or is a
  persistent verification cache required by the desired trust policy?
- After CPU optimization, is enough encode time left to justify an optional GPU
  backend for future BC6H, BC7, or ASTC profiles?
