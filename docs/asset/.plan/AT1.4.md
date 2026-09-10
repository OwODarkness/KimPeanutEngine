# AT1.4 — CPU Texture Compression Throughput

## Status

AT1.4 is complete. AT1.4.1's allocation-free `ReferenceV1` path, AT1.4.2's
settings/cache identity and metrics, AT1.4.3's pinned candidate integration,
and AT1.4.4's independent quality/determinism checks are landed. The candidate
fails the Sponza throughput gate, so `ReferenceV1` remains the accepted
production default and `rgbcx` is diagnostic-only.
The first live-buffer cleanup also landed: decoded pixels and compression
storage are moved/consumed in place, and native material cooking releases CPU
mip chains after serialization. This improves measured cook time but has not
yet changed 1-GiB admission.
AT1.3 is complete and supplies the bounded job-level concurrency,
memory admission, cancellation, deterministic assembly, and staged publication
that this stage must preserve.

## Problem statement

The post-AT1.3 RelWithDebInfo Sponza baseline averages about `62.50 s` for a
full import. Texture cooking still accounts for about `39.67 s` in the sampled
run, so BC3/BC4/BC5 encoding is now the largest actionable CPU stage. The
current encoder is scalar and performs avoidable dynamic-output operations; its
BC3 path also creates a temporary `std::vector` for every 4x4 block.

AT1.4 improves this stage without reopening the lifetime problem solved by
AT1.3. It does not retain all cooked Textures, create another unbounded queue,
or introduce a second general-purpose worker pool.

## Design boundary

Asset owns offline Texture preparation, semantic-to-format selection, block
encoding, and the bytes used to identify a cooked product. Render and Graphics
continue to consume the published format and payload; they do not select or
invoke an offline compressor.

This stage changes only BC3, BC4, and BC5 offline encoding. Portable RGBA8
output, mip-generation policy, color-space tags, Runtime decoding, archive
publication, and GPU object ownership remain unchanged.

## Invariants

1. `Color` and `Generic` remain BC3 sRGB, `PackedLinear` remains BC3 UNORM,
   `Opacity` remains BC4 UNORM, and `Normal` remains BC5 UNORM.
2. Non-multiple-of-four edges retain the current clamp-to-edge 4x4 sampling.
3. Blocks are emitted in deterministic mip-major, row-major order.
4. Encoder identity, revision, and quality policy participate in every cache
   key or settings hash whose product bytes they can change.
5. Third-party types and global state do not cross the Asset cooker boundary.
6. AT1.3 remains the only owner of job-level scheduling, byte-budget admission,
   completed-result backpressure, cancellation, and root-last publication.
7. A one-worker run and a many-worker run with the same settings produce the
   same product identities and bytes.
8. Headless CPU cooking remains the required path. GPU compression is outside
   AT1.4 and remains an evidence-gated later decision.

## Current hot path

`TextureCooker::CookPrepared` calls the in-house `ReferenceV1` encoder once per
mip. Disposable imported images transfer their pixel vector into preparation,
and compression consumes its owned prepared chain in place. Native material
profile cooking retains serialized bytes for publication but releases the
intermediate CPU mip chains as soon as serialization completes.
`CompressRgbaMip` gathers each clamped 4x4 block into stack RGBA storage, then:

- [landed in AT1.4.1] grew an output vector through `push_back` and `insert` even
  though the exact result size was known;
- [landed in AT1.4.1] created and destroyed an eight-byte alpha vector for every
  BC3 block;
- performs scalar endpoint and palette searches for every block; and
- processes mips sequentially within an AT1.3 Texture job.

The first two costs are removed without changing a single compressed byte.
That byte-equivalent cleanup is deliberately separated from selecting a new
encoder, so its performance effect and correctness are independently visible.

## Selected architecture

### One scheduling layer

AT1.4 uses AT1.3's independent Texture jobs to occupy the CPU. An encoder call
is synchronous and single-threaded within one admitted job. It may check
cancellation between block rows, but it must not create threads or enqueue work
onto an unrelated pool.

This avoids nested oversubscription when the default import already runs eight
Texture workers. Block-row parallelism may be reconsidered only after evidence
shows that a single very large Texture leaves cores idle, and then it must use a
cooperative scheduler with the same memory and cancellation ownership rather
than a new pool.

### Asset-owned encoder seam

Dispatch once per mip, not through a virtual call per 4x4 block. The internal
contract accepts:

- destination `TextureFormat`;
- source dimensions and tightly packed RGBA8 bytes;
- an exactly sized destination byte span;
- the selected bounded quality policy; and
- a cancellation query owned by the current cook job.

The contract guarantees either a complete valid mip payload or failure. It
does not expose `rgbcx`, SIMD ISA, backend, Render, or Graphics types.

Two implementations are kept during the stage:

- `ReferenceV1`: the allocation-free form of the current encoder, retaining
  byte-for-byte output for regression and comparison;
- `RgbcxV113`: a pinned `rgbcx` revision after its integration and acceptance
  gates pass.

The version-bearing identity is intentional: changing a compressor revision
can change product bytes and therefore creates a new encoder identity rather
than silently reusing old cache entries.

### Bounded public settings

Add stable settings equivalent to:

```cpp
enum class TextureBcEncoder : uint8_t
{
    ReferenceV1,
    RgbcxV113,
};

enum class TextureBcQuality : uint8_t
{
    Fast,
    Balanced,
};
```

Do not expose an arbitrary integer quality level. Candidate `rgbcx` mappings
are `Fast = 4` and `Balanced = 10`; AT1.4.2 freezes those mappings only after
the bakeoff. CLI diagnostics expose `--bc-encoder reference|rgbcx` and
`--bc-quality fast|balanced`. The accepted production default is changed to
`RgbcxV113/Balanced` only if all gates below pass; otherwise `ReferenceV1`
remains the default.

The encoder, bounded quality value, and fixed implementation revision must be
included in `TextureCookKey`, Model import settings bytes, and any direct
Texture-cook cache key. Existing profile, semantic, mip, and compression fields
remain part of those keys.

## Candidate decision

### Preferred candidate: rgbcx

`rgbcx` directly supports BC1/3/4/5 from RGBA input, has a small C++ integration
surface, does not bring its own thread pool, and is offered under public-domain
or MIT terms. Its one-time initialization mutates global state and is not
thread-safe, so Asset must pin the BC1 approximation mode and complete
initialization through `std::call_once` before AT1.3 workers encode blocks.
The mode must never change during the process lifetime.

Implementation pins an exact upstream commit, preserves its license notice,
adds a `KP::Rgbcx` CMake wrapper, and documents the provenance in
`third_party/README.md`. The user-approved vendor addition is complete; the
candidate promotion decision is closed with `ReferenceV1` as the default.

### Alternatives considered

- Intel ISPCTextureCompressor provides SIMD BC3/4/5 paths, but the upstream
  repository was archived in 2024 and adds the ISPC toolchain plus different
  whole-surface input assumptions. Keep it as a benchmark reference, not the
  first production dependency.
- AMD Compressonator supports the required formats and offers CPU/HPC/GPU
  routes, but its integration and scheduling surface are much larger. Use
  Compressonator Core as the fallback bakeoff candidate only if `rgbcx` misses
  quality or throughput gates.
- Microsoft DirectXTex supports CPU BC formats, but its platform types would
  add a Windows/DXGI-shaped boundary. Its documented GPU path targets BC6H and
  BC7, not the BC3/4/5 workload in this stage.

The selected first integration is therefore `rgbcx`, not a new handwritten
SIMD encoder. AT1.4 remains an evidence-driven CPU-compression stage rather than
promising that its implementation must use SIMD. The pinned `rgbcx` bakeoff is
recorded in the AT1.4 journal; it remains diagnostic-only after failing the
throughput promotion gate.

## Work breakdown

### AT1.4.0 — Freeze measurement and correctness fixtures

Status: complete; deterministic fixtures, independent decoded quality
measurements, and the three-run ReferenceV1 baseline are recorded. Literal
pre-change fixtures were not required for the accepted production decision.

- Record a three-run RelWithDebInfo Sponza baseline using the landed AT1.3
  defaults and the same machine/configuration used by its journal.
- Add encoder-only timing by format, block count, source megapixels, and
  megapixels per second. Keep the existing end-to-end TextureCook and import
  stage timings.
- Check in deterministic small fixtures for color, alpha gradient, opacity
  mask, tangent-space normal, packed linear channels, tiny mips, and dimensions
  not divisible by four.
- Capture current compressed bytes and decoded metrics from `ReferenceV1`.

Exit: performance attribution and fixture baselines can distinguish allocator
cleanup, encoder changes, and pipeline effects.

### AT1.4.1 — Make the reference encoder allocation-free

Status: landed. The exact-sized mip buffer and direct BC3/BC4/BC5 destination
writes preserve clamped edge behavior and deterministic output.

- Pre-size each mip output to its exact block-compressed byte count.
- Change block encoders to write to fixed destination pointers/spans.
- Write BC3 alpha directly to bytes `[0, 8)` and color to `[8, 16)`; remove the
  per-block alpha vector and output `push_back`/`insert` operations.
- Preserve the current clamped block gather, endpoint search, palette choice,
  and byte order.
- Check cancellation between block rows without leaving a partial product
  publishable.

Exit: every reference fixture and representative Sponza product is
byte-identical to the pre-AT1.4 encoder, with zero dynamic allocations in the
per-block loop and no regression in encoder time.

### AT1.4.2 — Add the encoder contract and cache identity

Status: settings, identity, metrics, and cancellation slices landed. `TextureBcEncoder` and
`TextureBcQuality` are now part of Texture cook settings, model import settings
hashes, material texture cook keys, and AssetTool diagnostics. Encoder-only
block/source-megapixel/time/throughput metrics are propagated per BC3/4/5
format, and both encoders check cancellation between block rows. `ReferenceV1`
and `RgbcxV113` are integrated; the latter remains diagnostic-only because the
bakeoff fails the throughput gate.

- Introduce the internal per-mip encoder seam and dispatch once per mip.
- Add bounded encoder/quality settings and CLI diagnostics.
- Include the settings and versioned encoder identity in Texture and Model
  import keys/settings bytes.
- Extend import metrics with selected encoder, quality, BC format, blocks,
  encode seconds, and throughput.
- Add tests proving that changing encoder or quality changes the appropriate
  cook identity and that worker count does not.

Exit: `ReferenceV1` still produces the byte-equivalent baseline through the new
contract, and cache invalidation is explicit and tested.

### AT1.4.3 — Integrate the rgbcx candidate

Status: integrated behind `KP::Rgbcx` at pinned upstream commit
`f66c2e489b07138f2673a2fb3d27c1aa1d565c48`. Sponza evidence rejects it as the
production default: BC3 is substantially slower than `ReferenceV1` at both
Fast and Balanced mappings. The internal identity remains `RgbcxV113` per this
plan, while the vendored header provenance is documented as rgbcx v1.12.

- After vendor approval, pin the exact reviewed upstream commit, license, CMake
  wrapper target, and implementation translation unit.
- Initialize `rgbcx` once with the selected immutable BC1 mode before parallel
  cooks; test concurrent first-use and repeated initialization attempts.
- Map BC3 from RGBA, BC4 from R, and BC5 from R/G while preserving current
  semantic routing and clamped edges.
- Write directly into exactly sized mip output spans.
- Keep the third-party include private to Asset implementation.

Exit: all required formats and edge sizes encode correctly under focused
bounds/determinism tests, and concurrent AT1.3 jobs do not race global
initialization. The performance gate is not met; the candidate cannot become
the default.

### AT1.4.4 — Quality and determinism gate

Status: test-local BC1/BC3/BC4/BC5 decoding, semantic error metrics, and
concurrent candidate determinism coverage are landed. The current balanced
rgbcx fixture comparison passes the stated quality thresholds; it remains
diagnostic-only because AT1.4.5 throughput already rejects it.

- Decode test products with a test-local specification decoder independent of
  the selected encoder, then compare against prepared mip pixels.
- For BC3 color, report RGB PSNR and alpha RMSE separately. No fixture may lose
  more than `0.5 dB` RGB PSNR relative to `ReferenceV1`, and aggregate quality
  must not be worse.
- For BC3 packed-linear data, report per-channel RMSE; no channel may regress
  relative to `ReferenceV1` beyond measurement tolerance.
- For BC4 opacity, report RMSE, maximum absolute error, and classification
  disagreement at the engine's alpha cutoff.
- For BC5 normals, reconstruct Z and report mean and P95 angular error; neither
  may regress relative to `ReferenceV1` beyond measurement tolerance.
- Repeat products across runs, worker counts `1` and default, and supported
  build configurations. Bytes and identities must match for equal settings.

Exit: the candidate meets every semantic quality gate and produces identical
bytes independent of scheduling. A failed candidate remains selectable only
for diagnostics and cannot become the production default. The accepted result
is recorded in the AT1.4 journal.

### AT1.4.5 — Sponza and runtime acceptance

Status: complete; the production decision is closed with `ReferenceV1` as the
default and `rgbcx` retained for diagnostics after failing the throughput gate.

- Run at least three RelWithDebInfo Sponza imports for the reference and
  candidate with identical AT1.3 worker, queue, and memory settings. The
  ReferenceV1 three-run median is now recorded in the AT1.4 journal; the
  candidate is already rejected by the single-run throughput gate.
- Require candidate median TextureCook time at or below `30 s` from the
  approximately `39.67 s` sampled AT1.3 value, and median total import at or
  below `55 s` from the approximately `62.50 s` three-run baseline.
- Require encoder-only throughput of at least `2x` the allocation-free
  `ReferenceV1` path for the Sponza format mix.
- Require peak working set to remain within `5%` of the AT1.3 approximately
  `749.55 MB` observation under the same memory policy; reserved-byte
  accounting and oversized-job behavior must remain valid.
- Run focused and full unit suites, injected cancellation/failure coverage,
  archive-integrity checks, and Vulkan/OpenGL Sponza visual acceptance.
- Compare one-worker and default-worker products and manifests.

The current 4 GiB control is diagnostic only: it reaches 21.93 s TextureCook
with eight active jobs but peaks at 1.47 GiB, so it cannot replace the 1 GiB
acceptance policy. AT1.4 leaves that policy unchanged.

Exit: all required reference-path correctness, memory, determinism, failure,
and visual gates pass. Candidate promotion is conditional; when throughput or
quality fails, retaining the allocation-free reference default is the accepted
terminal outcome for AT1.4.

## Expected change surface

- `engine/runtime/asset/texture_importer.h/.cpp`: settings, encoder seam,
  reference cleanup, and selected backend dispatch.
- `engine/runtime/asset/native_material.cpp`: Texture cook identity.
- `engine/runtime/asset/model_import_service.cpp`: Model settings identity and
  metrics propagation.
- `engine/tool/asset/asset_tool_main.cpp` or the current AssetTool option owner:
  CLI selection and diagnostics.
- `engine/runtime/asset/CMakeLists.txt` and `third_party/`: private wrapper and
  pinned dependency, only after approval.
- Asset unit tests for format correctness, byte equivalence, quality,
  determinism, concurrency, cancellation, and cache invalidation.
- The AT1 spec/TODO/status and an AT1.4 execution journal after work lands.

No Runtime Render or Graphics interface change is expected.

## Validation commands

Use the validation matrix to select the wrapper targets. At minimum the landed
stage records:

```powershell
cmake --build build --config RelWithDebInfo --target TextureImportTest ModelImportServiceTest
ctest --test-dir build -C RelWithDebInfo -R "TextureImportTest|ModelImportServiceTest" --output-on-failure
.\tools\kp.ps1 test
```

It also records the exact AssetTool Sponza commands/configuration, three-run
raw timings, peak working set/reservations, worker count, queue capacity,
quality metrics, product comparisons, failure injection, and Vulkan/OpenGL
capture evidence in `.spec/journal/`.

## Risks and controls

- **Silent cache aliasing:** versioned encoder and quality values are hashed
  before a new backend can become default.
- **Nested oversubscription:** encoders cannot own threads; AT1.3 owns
  concurrency.
- **Global initialization race:** immutable `rgbcx` initialization is guarded
  by `std::call_once` and exercised concurrently.
- **Packed/normal quality loss:** semantic metrics are gates, not only visual
  spot checks or aggregate RGB PSNR.
- **Memory regression:** exact destination sizing and the existing AT1.3
  reservation lifecycle are retained and measured.
- **Dependency maintenance:** pin one reviewed commit, keep a private wrapper,
  preserve `ReferenceV1`, and record license/source provenance.
- **Benchmark optimism:** compare medians on identical inputs/configuration and
  report encoder-only plus end-to-end time.

## Reference evidence

- [rgbcx/bc7enc](https://github.com/richgel999/bc7enc) — compact BC1/3/4/5 CPU
  encoder candidate and current upstream context.
- [rgbcx implementation and initialization contract](https://github.com/richgel999/bc7enc_rdo/blob/master/rgbcx.h)
  — encoder API, quality levels, licensing, and one-time global initialization.
- [Intel ISPCTextureCompressor](https://github.com/GameTechDev/ISPCTextureCompressor)
  — archived SIMD comparison point with BC3/4/5 support.
- [AMD Compressonator](https://github.com/GPUOpen-Tools/compressonator) — broader
  CPU/HPC/GPU fallback candidate.
- [Microsoft DirectXTex](https://github.com/microsoft/DirectXTex) and
  [Texconv documentation](https://github.com/microsoft/DirectXTex/wiki/Texconv)
  — CPU BC support and the BC6H/BC7 scope of its documented GPU route.
