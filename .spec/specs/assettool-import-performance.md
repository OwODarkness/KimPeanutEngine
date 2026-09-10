# AssetTool Import Performance

- Status: proposed
- Owner: project team
- Parent TODO: [AssetTool import performance roadmap](../../docs/asset/TODO.md#assettool-import-performance-roadmap)
- Design authority: [AT1 plan](../../docs/asset/.plan/AT1.md)

## Objective

Reduce the reported approximately 1,200-second Sponza import and excessive
memory use by removing redundant Texture work, bounding live payloads, using
available CPU cores, and overlapping useful storage writes. Preserve
deterministic native products, immutable content-addressed publication, strict
collision handling, and the root-last archive transaction.

## Current state

The importer converts Material parameters serially and deduplicates Texture
products only after cooking. Portable and BC profiles repeat preprocessing.
Decoded images and all serialized Texture products remain alive until the
complete model is ready, product bytes are copied into a second publication
collection, and new staged products are written twice. BC encoding is scalar
and single-threaded. Routine cache reuse also performs a complete product
integrity scan.

The evidence and exact affected call paths are recorded in the
[AT1 problem statement](../../docs/asset/.plan/AT1.md#problem).

## Scope and non-goals

In scope:

- reproducible import-stage, byte, CPU, disk, and peak-memory telemetry;
- pre-cook Texture deduplication and shared mip preparation;
- removal of unused retained images, product copies, and duplicate writes;
- memory-budgeted CPU jobs and a bounded staged-product writer queue;
- measured SIMD/multithreaded BC compression;
- a lightweight ordinary no-op probe plus explicit full integrity audit; and
- real Sponza performance, deterministic-output, archive, and visual evidence.

Out of scope:

- moving import/cook work into Runtime, Editor, Render, or Graphics;
- changing Asset identity, Material ownership, or the native runtime loading
  boundary;
- general archive garbage collection or distributed build infrastructure;
- requiring a GPU or graphics context for ordinary/headless imports;
- changing the native Texture format before bounded per-Texture publication is
  measured; and
- AP1 runtime packaging, dependency scheduling, or mip residency.

## Invariants

- A fixed importer/encoder version and settings produce deterministic product
  bytes and identities independently of worker completion order.
- Each unique Texture cook key is decoded and prepared at most once per import.
- Parallel work is bounded by worker count and estimated bytes in flight.
- A product is staged once and published create-if-absent without overwrite.
- A concurrent destination is accepted only after strict collision validation.
- The existing source record remains visible until all new products are valid
  and published; the database root is committed last.
- Cancellation and failure clean only operation-owned staging paths.
- Runtime remains read-only and GPU resource ownership is unchanged.
- Full integrity auditing remains available even if routine no-op probing adopts
  an explicit trusted-metadata or verification-cache policy.

## Stages

1. **AT1.0:** reproduce and attribute cold, warm, and no-op Sponza imports in
   Debug and RelWithDebInfo.
2. **AT1.1:** remove unused decoded-image retention, product-buffer copies,
   redundant round-trip work, and double staging writes.
3. **AT1.2:** deduplicate Texture work before cooking and share preparation/mips
   between portable and BC profiles.
4. **[AT1.3](../../docs/asset/.plan/AT1.3.md):** add a bounded CPU and
   staged-writer pipeline with byte-budget backpressure, cancellation, and
   deterministic result assembly.
5. **AT1.4:** benchmark and adopt or implement an appropriate SIMD/
   multithreaded BC3/BC4/BC5 path.
6. **AT1.5:** separate lightweight unchanged-source probing from the explicit
   full archive integrity audit.
7. **AT1.6:** complete Sponza performance, determinism, transaction, integrity,
   and cross-backend validation, then decide whether GPU encoding is justified.

Stage interfaces and exit conditions are defined in the
[AT1 implementation sequence](../../docs/asset/.plan/AT1.md#implementation-sequence).

## Acceptance criteria

- [ ] The exact Sponza fixture, command, settings, hardware, build
  configuration, cache condition, and three-run measurements are recorded.
- [ ] Every requested Material binding resolves through a unique pre-cook key;
  shared and packed images are not redundantly decoded, mip-generated, or
  compressed.
- [ ] Portable and BC variants consume one compatible prepared mip closure.
- [ ] Each new product is written to staging exactly once and payload ownership
  is moved/released rather than copied into a closure-wide pending collection.
- [ ] Peak Texture-cook memory is bounded by configuration plus one documented
  oversized-job allowance and does not scale linearly with Texture count.
- [ ] One-worker and default-worker imports produce identical product identities
  and equivalent archive snapshots.
- [ ] Failure, cancellation, immutable collision, and concurrent-winner tests
  preserve the previous database root and unrelated/shared products.
- [ ] An unchanged import performs no foreign decode/cook/full Texture-closure
  scan, while `AssetTool integrity` still detects malformed or corrupt products.
- [ ] On the reference machine, RelWithDebInfo Sponza cold import is at most 120
  seconds and no-op import is at most 5 seconds, unless AT1.0 evidence supports
  a replacement budget explicitly approved by the user.
- [ ] Recooked Texture/Model products pass native codec/import/archive tests and
  Vulkan/OpenGL visual acceptance.
- [ ] GPU acceleration is adopted only if measured bounded CPU compression
  remains dominant and an optional deterministic headless fallback is proven.

## Validation plan

Follow [the project validation matrix](../../docs/validation_matrix.md).
Individual stages run focused AssetImport, native codec, archive, and AssetTool
tests. AT1.6 runs the complete Asset test set, deterministic one/default-worker
comparisons, real three-run Sponza measurements, archive integrity, and Vulkan/
OpenGL visual validation.

Performance evidence must include elapsed time by stage, requested versus unique
Texture jobs, bytes read/written, active jobs, peak reserved/working memory, CPU
utilization, storage throughput, output hashes, and cache condition. Debug and
RelWithDebInfo results must not be mixed.

## Risks and open questions

- Assimp's retained model document may remain a large non-Texture floor after
  Texture memory is bounded.
- Estimated job memory can undercount encoder or allocator scratch; telemetry
  must compare reservations with process peaks.
- Replacing the BC encoder may deliberately change content hashes and quality;
  its version and settings must participate in cache invalidation.
- Multiple writers may reduce rather than improve throughput on some storage;
  the bounded queue depth must be measured instead of assumed.
- A metadata-only cache probe requires a documented local archive trust model or
  a verification cache; it cannot silently remove corruption guarantees.
- GPU compression may improve future expensive formats but add initialization,
  transfer, driver, CI, and reproducibility costs.
