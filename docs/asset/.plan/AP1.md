# AP1 — Startup Asset Loading Performance

- Status: active
- Parent roadmap: [Asset Module TODO](../TODO.md#startup-performance-roadmap)
- Execution spec: [Asset Startup Loading Performance](../../../.spec/specs/asset-startup-loading-performance.md)
- Architecture map: [Asset Module Plans](../PLANS.md)

## Objective

Reduce the Sponza startup Asset phase from 317.488 seconds to a bounded,
measured loading path without weakening Asset identity, dependency semantics,
transactional registration, or native-product integrity. The long-term path
must also reduce the amount of data required before the first useful frame,
instead of relying only on faster execution of the current 2+ GiB preload.

## Concrete design question

How should KimPeanutEngine load a large initial-scene dependency closure with
one integrity decision per product, compact GPU-oriented cooked data, useful
physical locality, bounded concurrency, and progressive mip readiness while
preserving independent Asset identities and the existing
Asset -> Resource -> Render -> RHI ownership boundaries?

## Observed baseline

The 2026-09-09 Debug Sponza run is the current measured incident baseline:

| Observation | Value | Evidence |
| --- | ---: | --- |
| Asset-phase wall time | 317,488.018 ms | Runtime completion log |
| Load operations | 195 | Runtime completion log |
| Cache hits | 74 | Runtime completion log |
| Operations that reached source loading | 121 | Derived from operations minus cache hits |
| Registered Assets | 126 | Runtime completion log |
| Texture products in the render profile | 80 | Render profile |
| Texture source bytes | 1,789,607,680 | Render profile |
| Texture decoded bytes | 1,789,569,600 | Render profile |
| Texture resident bytes | 1,789,569,644 | Render profile |
| Three selected native Model products | approximately 486 MB | Read-only archive query |

The authoritative log is
[`logs/2026-09-09/KimPeanutEngineLog-2026.09.09-10.57.25.txt`](../../../logs/2026-09-09/KimPeanutEngineLog-2026.09.09-10.57.25.txt).
The archive contains 81 texture products of 22,370,096 bytes each. The active
scene consumes 80 of them. That size is consistent with an uncompressed 4096²
RGBA8 texture and its complete mip chain.

The run used `build/Debug/KimPeanutEngine.exe`. Debug magnifies the cost of the
hand-written scalar SHA-256 and scalar native Model parser, but it does not
remove the architectural problem: startup reads and retains more than 2 GiB
before Resource/GPU preparation begins.

## Root-cause analysis

### 1. Native products are hashed and copied repeatedly

`NativeTextureLoader` and `NativeModelLoader` each read the complete product
into a `std::vector<std::byte>`, call `VerifyArchiveProduct`, and then call the
native deserializer.

For every native Texture and Model, the current runtime path performs:

1. a complete SHA-256 in `VerifyArchiveProduct` to compare the bytes with the
   content-addressed filename;
2. an integrity-input allocation and complete copy, followed by another
   SHA-256 to validate the embedded digest; and
3. a third complete SHA-256 to populate `product_hash` after deserialization.

The texture loader then copies every mip payload from the product buffer into
separate retained vectors. The Model parser similarly allocates and populates
runtime vertex, index, and section arrays while the complete file and integrity
copy are still live.

For the measured closure, the three SHA passes account for approximately
6.8 GB of byte hashing before allocator, parser, and payload-copy costs. The
embedded-integrity helpers add another approximately 2.28 GB of full-product
copying. The third hash result is not consumed by the current runtime Texture
or Model loaders.

### 2. Cooked textures are portable but not GPU-compressed

The current native Texture profile stores RGBA8 or RGBA16F mip payloads. It
avoids runtime source-image decoding and generates correct semantic mips, but
it does not reduce the byte count presented to storage, CPU memory, staging,
or GPU residency. Eighty 4K RGBA8 chains therefore contribute approximately
1.79 GB to the startup closure.

### 3. Native Model products are GPU-unfriendly in size

The three selected Model products total approximately 486 MB. Native Model V1
stores a float-heavy interleaved vertex representation and fixed-width index
data, then reconstructs fields vertex by vertex. The cook pipeline does not yet
quantize attributes, select narrower index widths, optimize locality, or encode
vertex/index streams for fast decompression.

### 4. Dependency execution is serial

`AssetManager::LoadSyncInternal` resolves dependency requests in a loop with a
recursive synchronous call. Loader callbacks are protected by one global
`load_mutex_` because several registered loader instances are shared and are
not declared thread-safe. `LoadAsync` moves the same work to another thread but
does not increase throughput.

This serialization is not the first fix: parallelizing the present multi-GiB,
copy-heavy path could increase peak memory and contention. Product size and
copy/hash amplification must be reduced before bounded parallel execution.

### 5. Startup requires full-resolution residency

Every mip of every hard dependency is loaded before Asset readiness. For a
4096² chain, the 512²-and-smaller tail is approximately 1/64 of the complete
chain. Loading only that tail initially would reduce the measured texture
startup payload from approximately 1.79 GB to approximately 28 MB, then allow
larger mips to stream after scene commit.

### 6. File packing is useful but not the primary cause

The material products are tiny compared with their textures. Embedding texture
bytes in each Material would create duplicate storage for shared textures,
couple material identity to physical layout, and obstruct independent mip
streaming and eviction.

The useful form of the proposal is a package with a table of contents. Model,
Material, Texture, and mip-range entries retain independent content identities
and dependency edges but are physically ordered by startup priority. This
reduces file-open/seek overhead and creates sequential read ranges without
turning a Material into the owner of Texture bytes.

## Ownership and lifecycle boundaries

| Owner | AP1 responsibility |
| --- | --- |
| Asset import/cook | Produce deterministic platform/profile products, semantic compressed textures, compact geometry, dependency metadata, and package entries. |
| Asset package layer | Map product identity to validated offset/size/format ranges; it does not create `AssetID`s or own GPU resources. |
| AssetManager | Deduplicate in-flight and completed loads, schedule dependency work, commit Asset identity and dependency edges, and expose Asset-stage observations. |
| Runtime | Choose the startup closure and readiness budget; distinguish initial scene readiness from background streaming completion. |
| Resource | Convert or expose CPU artifacts needed for GPU upload without taking Asset identity ownership. |
| Render/Graphics | Create full GPU resources, upload resident mip ranges, clamp sampling to ready levels, and retire resources only after submitted work is safe. |

The native runtime remains read-only. Cooking, compression, package creation,
and package-index publication are offline operations and must not migrate into
`AssetManager::LoadSync`.

## Chosen design

### One authoritative runtime integrity decision

Introduce a verified-product read result containing the bytes or byte view,
the expected product identity, and the already-computed content hash. A native
deserializer accepting this result performs structural validation but does not
rehash the complete product.

For existing product versions, preserve compatibility while eliminating the
temporary integrity copy by feeding the hash incrementally as:

```text
bytes before embedded digest -> zero digest bytes -> bytes after digest
```

The near-term compatibility path may still compute the filename and embedded
digest hashes in one read pass, but it must not calculate an unused third hash
or clone the product. A later native product version should have one
authoritative integrity scheme. A mounted package may validate a signed or
content-addressed package once and supply trusted entry metadata according to
an explicit policy; development loose-product verification remains strict.

### Profile-specific Texture products

Extend the API-neutral `TextureFormat` contract and both backends before the
cooker emits compressed payloads. The desktop profile should prefer:

| Semantic | Preferred product format | Fallback |
| --- | --- | --- |
| Base color / emissive | BC7 sRGB | RGBA8 sRGB |
| Normal | BC5 UNORM or SNORM according to shader convention | RGBA8 linear |
| Metallic-roughness / packed linear | BC5 or BC7 according to used channels | RGBA8 linear |
| Opacity or single-channel mask | BC4 when the sampling contract permits | RGBA8 linear |
| HDR environment | BC6H where supported and quality-validated | RGBA16F |

BC7 and BC5 use 8 bits per pixel rather than RGBA8's 32 bits. If all measured
textures used an 8-bpp block format, the 1.79 GB closure would become roughly
447 MB before any initial-mip streaming.

The cooker records the selected format and the Runtime rejects unsupported
products deterministically or selects an explicitly published portable
fallback. Runtime never silently decompresses the whole texture to RGBA8 just
to preserve compatibility.

### Compact native geometry

Add a versioned Model product profile that can independently encode vertex and
index chunks. The cook pipeline should first optimize index/vertex locality,
then select representations such as narrower indices, quantized UVs, and
octahedral normal/tangent encodings. A fast bounded decoder writes directly to
the final CPU artifact or staging representation without per-element heap
allocation.

Lossy representation decisions require fixture-specific error budgets and
visual validation. The compression codec itself must remain versioned and
deterministic; third-party code, if adopted, remains behind an Asset-owned
format contract.

### Asset package with independent entries

Add a package product such as:

```text
Sponza.assetpack
  header + version + package identity
  table of contents
    product identity, AssetType, offset, stored size, decoded size
    codec/TextureFormat, mip or chunk range, integrity metadata
  startup-priority data
    Level and Model metadata
    Materials
    low-resolution mip tails
  background-priority data
    high-resolution mip ranges
    optional/non-startup products
```

Package layout is physical, not semantic. The existing Asset path/product
identity remains the cache and dependency key. Shared textures occur once in a
package or shared install chunk. Package mounting produces a read-only lookup;
it does not register every contained Asset.

Already GPU-compressed textures should not receive an additional general
compression layer. Geometry chunks may use a fast codec when measured decode
time plus I/O is better than raw reads. Entries and high-priority ranges should
be aligned for the selected filesystem/I/O strategy.

### Priority-aware dependency execution and mip residency

Replace recursive execution with an internal dependency job graph only after
the product path is compact enough to keep peak memory bounded. Preserve the
public `LoadSync` behavior by waiting on the root job; introduce an in-flight
path table so concurrent parents share one underlying load and receive one
Asset identity.

Loader descriptors need an explicit concurrency policy or a factory that
creates job-local loader state. Native Texture/Model parsing can become
parallel-safe independently; shared Assimp, miniaudio, or legacy loaders remain
serialized until proven otherwise. Registration remains a short AssetManager
transaction after required dependencies reach their promised state.

Texture readiness becomes range-aware:

1. schedule Level, Model metadata, Materials, and low-resolution mip tails;
2. create the full GPU texture allocation with only ready mip ranges usable;
3. clamp the sampler/view to the resident range;
4. commit initial scene readiness;
5. stream larger mips by priority and publish monotonic residency changes; and
6. retire or replace upload resources under existing RHI fence rules.

## Implementation sequence

### AP1.0 — Reproducible baseline and cost attribution

- Extend the existing startup completion diagnostic with the already-collected
  cache, loader wait, source load, registration, source-byte, and decoded-byte
  totals.
- Retain a bounded top-N list by exclusive source-load cost or add a separate
  diagnostic query; do not enlarge normal startup snapshots without a bound.
- Record Debug and RelWithDebInfo samples separately and never compare mixed
  filesystem-cache conditions.
- Record peak process memory and the selected product closure by type.

Exit: the 317.488-second run can be reproduced or superseded by a documented
same-level baseline that identifies the dominant product types and phases.

### AP1.1 — Single-verification, copy-bounded native loading

- Compute the content hash and digest-with-zeroed-range hash while traversing
  the loaded product, without allocating a second product-sized buffer.
- Pass the computed content hash through archive verification and pass both
  verified hashes into native Texture and Model structural decoding.
- Remove the unused post-deserialization Texture and Model hash pass and the
  old `BuildIntegrityInput` full-product copies.
- Keep malformed, truncated, digest-mismatch, and filename-mismatch rejection.
- Cover the zeroed-range hash contract, invalid ranges, and the precomputed
  archive-verification path in Asset unit tests.

Landed 2026-09-09. Native runtime products now perform the required content
and embedded-digest checks from one source-buffer traversal. The loader still
owns one product-sized read buffer for structural decoding; AP1.1 does not yet
introduce memory mapping, package ranges, or allocator counters.

Reference comparison used for this slice: Distill's packfile reader keeps
immutable mapped/buffer-backed bytes alive for decoding, while O3DE and Godot
document chunked file hashing. KimPeanutEngine retains its current owned
`std::vector<std::byte>` boundary and adopts only the relevant property—the
hash path does not clone the product or reread it for each validation decision.

Exit: a runtime product is read once, has at most one authoritative full-byte
verification on the selected policy path, and is not cloned solely for hashing.

### AP1.2 — GPU-native Texture compression

- Extend common Texture formats and Vulkan/OpenGL mappings.
- Add cooker profile/capability selection and deterministic fallback products.
- Add semantic BC format tests, mip-size validation, and backend upload tests.
- Re-cook Sponza and measure product, CPU payload, staging, and GPU residency.

Exit: the Sponza texture closure is at most 550 MB for the desktop compressed
profile, with portable fallback behavior and no unacceptable captured visual
regression.

### AP1.3 — Compact native Model profile

- Characterize vertex/index counts, index-width opportunities, and acceptable
  attribute errors before choosing encodings.
- Add a versioned compact product and bounded fast decode path.
- Preserve bounds, sections, material slots, and deterministic product hashes.
- Measure product bytes, decode time, retained bytes, and render correctness.

Exit: the three Sponza Model products are materially smaller than the current
approximately 486 MB baseline and decode faster in RelWithDebInfo; the exact
byte target is set from AP1.3 characterization rather than guessed in advance.

### AP1.4 — Scene/install Asset package

- Define the package header, TOC, entry/range integrity, alignment, versioning,
  and corruption bounds.
- Add offline package construction from a resolved dependency closure.
- Mount packages read-only and resolve existing product identities to ranges.
- Order initial metadata and low mips ahead of background ranges.
- Prove shared Texture deduplication and missing/corrupt-entry failure.

Exit: packaged and loose-product paths produce equivalent Asset identities and
dependency graphs, while the packaged path uses bounded file opens and
contiguous priority reads.

### AP1.5 — Bounded dependency scheduler and initial-mip readiness

- Introduce shared in-flight work keyed by canonical product identity.
- Add per-loader concurrency policy and a bounded worker budget.
- Schedule independent dependency branches without exposing partial parent
  Assets.
- Add low-mip readiness, post-commit high-mip streaming, and cancellation.
- Bound transient memory and preserve GPU-safe upload/retirement.

Exit: the selected level reaches a visually usable committed scene without
waiting for full-resolution texture residency, while final closure completion
remains observable and deterministic.

### AP1.6 — End-to-end performance and compatibility gate

- Run the fixed Sponza scenario in Debug and RelWithDebInfo.
- Validate loose and packaged native products, cache hits, corruption,
  cancellation, and repeated concurrent dependencies.
- Capture Vulkan and OpenGL output before and after residency completion.
- Record startup latency, total streaming completion, byte counts, peak memory,
  and remaining platform limitations in the execution journal.

Exit: on the current reference laptop, RelWithDebInfo reaches initial scene
commit in at most 5 seconds for the packaged compressed Sponza fixture, with no
Asset ownership regression and with full-resolution completion reported
separately. If measurement shows the hardware or fixture cannot meet this
budget, revise the budget only with recorded evidence and user approval.

## Reference discovery gate

The plan uses the following directly inspected open-source evidence:

- Godot's PCK layer maps independent resource paths to package offsets and
  sizes. Applicable: one physical package can preserve independent logical
  resource identity. Not adopted: Godot's path hashing, MD5 format, encryption,
  or delta semantics. Source:
  [`file_access_pack.cpp`](https://github.com/godotengine/godot/blob/master/core/io/file_access_pack.cpp).
- bgfx defines API-neutral BC/ETC/ASTC Texture formats and maps them separately
  in Vulkan and OpenGL backends. Applicable: block compression belongs in the
  common format contract with capability-aware backend translation. Not
  adopted: the complete bgfx renderer contract. Sources:
  [`bimg.h`](https://github.com/bkaradzic/bimg/blob/master/include/bimg/bimg.h),
  [`renderer_vk.cpp`](https://github.com/bkaradzic/bgfx/blob/master/src/renderer_vk.cpp),
  [`renderer_gl.cpp`](https://github.com/bkaradzic/bgfx/blob/master/src/renderer_gl.cpp).
- meshoptimizer exposes versioned vertex/index codecs and recommends locality
  optimization and quantization before encoding. Applicable: compact,
  deterministic GPU-oriented Model chunks and caller-owned decode output. Not
  adopted until AP1.3 measures quality, dependency, and build costs. Source:
  [`meshoptimizer.h`](https://github.com/zeux/meshoptimizer/blob/master/src/meshoptimizer.h).

These patterns support package indirection, GPU-native texture products, and
compact geometry independently. None supports embedding every Texture directly
inside its Material as the primary Asset ownership model.

## Rejected alternatives

- **Embed complete Texture products in each Material.** This duplicates shared
  data, damages content-addressed identity, and obstructs mip streaming.
- **Add a package before reducing product size and hash amplification.** It
  improves locality but retains the dominant multi-GiB CPU work.
- **Use `LoadAsync` unchanged.** The current shared loader mutex still
  serializes callbacks and future destruction can block.
- **Remove integrity validation globally.** Development loose products and
  untrusted/corrupt package entries still require an explicit verification
  policy.
- **Parallelize every loader immediately.** Existing loaders do not share one
  thread-safety contract, and present transient allocations can multiply peak
  memory.
- **Treat Release performance as the entire fix.** It is a required comparison,
  but it does not reduce product bytes, CPU/GPU residency, or first-frame
  dependency requirements.

## Validation requirements

Follow [`docs/validation_matrix.md`](../../validation_matrix.md). Individual
stages use the smallest sufficient target, but AP1 as a whole crosses Asset,
Runtime, Resource, Render, common Graphics, and both backends and therefore
requires level-4 plus runtime/visual evidence.

Required evidence includes:

- native Texture/Model codec and corruption tests;
- Asset deduplication, rollback, dependency, unload, and observation tests;
- common Graphics format and backend upload tests;
- Runtime startup transaction tests;
- packaged/loose equivalence tests;
- Vulkan and OpenGL Sponza startup and inspected captures;
- three-run latency samples per build configuration and cache condition;
- source, decoded, resident, uploaded, and streamed byte counts; and
- peak process memory and bounded concurrency evidence.

Compilation alone cannot complete AP1 because it changes runtime loading,
resource lifetime, GPU format handling, and visible progressive residency.

## Open questions

- Whether desktop products should be backend-specific BC payloads or a KTX2/
  Basis intermediate transcoded according to runtime capabilities.
- Whether package trust is established per package, per entry, or through a
  signed manifest plus optional development verification.
- Whether low-mip-first ranges fit a revised `.texture` format or should use a
  package-only range table over unchanged product payloads.
- Which Model attributes can be quantized without violating current shader and
  CPU consumer assumptions.
- Whether Texture CPU payloads can be released after safe GPU upload, and which
  current consumers require retained pixels.
- The worker count and memory budget for the reference laptop.
