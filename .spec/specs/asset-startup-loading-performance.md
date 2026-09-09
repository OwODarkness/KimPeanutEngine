# Asset Startup Loading Performance

- Status: proposed
- Owner: project team
- Parent TODO: [Asset startup performance roadmap](../../docs/asset/TODO.md#startup-performance-roadmap)
- Design authority: [AP1 plan](../../docs/asset/.plan/AP1.md)

## Objective

Turn the measured 317.488-second Sponza Asset startup phase into a compact,
verified, package-aware, progressively ready loading path. Initial scene
readiness must not require loading all full-resolution texture mips, and the
optimization must preserve Asset identity, dependency, rollback, and lifetime
contracts.

## Current state

The measured Debug run performs 195 operations with 74 cache hits and registers
126 Assets. Its render catalog contains 80 native textures with approximately
1.79 GB of source, decoded, and resident bytes. The three selected native Model
products contribute approximately another 486 MB. Native Texture and Model
runtime loads currently hash complete products three times, make a complete
integrity-input copy, copy decoded payloads, and resolve dependencies
synchronously under a shared-loader serialization policy.

Detailed evidence and calculations are owned by the
[AP1 stage design](../../docs/asset/.plan/AP1.md#observed-baseline).

## Scope and non-goals

In scope:

- reproducible phase, byte, and peak-memory telemetry;
- one authoritative runtime product verification and copy-bounded decoding;
- profile-specific GPU block-compressed Texture products;
- compact versioned native Model products;
- a read-only package/TOC layer that preserves independent product identity;
- bounded dependency scheduling with in-flight deduplication; and
- low-mip initial readiness followed by observable background streaming.

Out of scope:

- changing AssetID packing or built-in AssetType values;
- moving import/cook work into Runtime;
- making Material own Texture bytes;
- a general virtual filesystem unrelated to packaged Asset products;
- world/level streaming beyond the selected startup closure; and
- removing integrity validation without an explicit trusted-package policy.

## Invariants

- Offline import/cook owns product generation; native Runtime loading remains
  read-only.
- AssetManager owns Asset identity, dependency edges, cache publication,
  in-flight deduplication, payload lifetime, and unload protection.
- A package changes physical placement only; product identity and dependency
  semantics remain stable between loose and packaged paths.
- A parent Asset is not visible until its required dependency transaction can
  commit successfully.
- Loader concurrency is explicit and bounded; legacy non-thread-safe loaders
  remain serialized.
- Asset reports Asset work and residency facts. Runtime owns initial-scene and
  full-streaming readiness policy.
- Render/Graphics own GPU resources, partial mip usability, synchronization,
  upload, and fence-safe retirement.
- Unsupported compressed formats fail deterministically or select an explicitly
  cooked portable fallback; Runtime does not silently expand all textures.
- Progressive residency never exposes uninitialized mip levels to sampling.
- Existing malformed-product, digest, rollback, cache, and unload behavior is
  retained or deliberately versioned with equivalent safety.

## Stages

1. **AP1.0:** establish reproducible exclusive timing, byte, and memory
   attribution for Debug and RelWithDebInfo.
2. **AP1.1:** remove redundant product hashing and full integrity copies while
   preserving strict validation.
3. **AP1.2:** cook and upload capability-aware GPU block-compressed textures.
4. **AP1.3:** add a compact, measured native Model product profile.
5. **AP1.4:** package dependency closures into independently addressable,
   priority-ordered entries.
6. **AP1.5:** schedule dependency jobs with bounded concurrency and reach
   initial readiness from low mip ranges.
7. **AP1.6:** complete cross-backend performance, lifetime, corruption, and
   visual validation.

Stage interfaces, decisions, dependencies, and exit conditions are defined in
the [AP1 plan](../../docs/asset/.plan/AP1.md#implementation-sequence).

## Acceptance criteria

- [ ] A repeatable baseline reports Asset wall time, exclusive source/load
  costs, byte totals, slowest operations, build configuration, cache condition,
  and peak process memory.
- [ ] Runtime native Texture and Model loading performs no unused full-product
  hash and no full-product allocation solely to zero an embedded digest.
- [ ] Corrupt filename hashes, embedded digests where applicable, structures,
  ranges, and package entries still fail before Asset publication.
- [ ] The desktop compressed Sponza Texture closure is at most 550 MB, with
  semantic format selection and an explicit portable fallback.
- [ ] The compact Sponza Model closure is smaller and faster to decode than the
  approximately 486 MB baseline, with recorded quality bounds.
- [ ] Loose and packaged products resolve to equivalent Asset identities,
  dependency edges, payload semantics, and failure behavior.
- [ ] Shared Texture products occur once per resolved package/install chunk and
  are not duplicated per Material.
- [ ] Concurrent dependency branches share in-flight work, respect loader
  concurrency policy, preserve rollback, and remain within the recorded memory
  budget.
- [ ] Initial scene readiness uses only initialized resident mip ranges; larger
  mips stream afterward without sampling invalid data.
- [ ] On the current reference laptop, the packaged compressed Sponza fixture
  reaches initial scene commit within 5 seconds in RelWithDebInfo. Full
  resolution completion is measured and reported separately.
- [ ] Vulkan and OpenGL captures before and after full residency preserve
  material identity and show no unacceptable compression or mip transition
  artifacts.
- [ ] Focused tests, full required build/CTest, runtime smoke, performance
  samples, and visual evidence are recorded in a matching execution journal.

## Validation plan

Use the repository [validation matrix](../../docs/validation_matrix.md). AP1.0
and AP1.1 begin with focused Asset targets and tests. AP1.2 adds common Graphics
and Vulkan/OpenGL upload validation. AP1.4 and AP1.5 add Runtime startup,
package-equivalence, cancellation, and concurrency coverage. AP1.6 requires a
full Debug validation pass plus RelWithDebInfo performance runs and inspected
Vulkan/OpenGL Sponza captures.

Performance comparisons use at least three runs for each build configuration
and filesystem-cache condition. Reports must not mix Debug with
RelWithDebInfo, cold with warm, or initial scene commit with full streaming
completion.

## Risks and open questions

- Compressed Texture support crosses common RHI and both backends; format
  capability and fallback mistakes can become cross-platform data failures.
- Parallel loading can multiply transient memory unless products and copies are
  reduced first and the scheduler enforces a byte-aware budget.
- Partial mip readiness changes the current all-or-nothing Texture resource
  model and must preserve safe sampler/view updates.
- Model quantization needs measured error thresholds rather than a global
  assumption.
- Package trust and verification policy must distinguish development products,
  shipped immutable packages, and corrupted or untrusted input.
- Releasing CPU Texture pixels after GPU upload may be valuable but requires an
  audit of current CPU consumers and reload/device-loss expectations.
