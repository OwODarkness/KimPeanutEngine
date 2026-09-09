# Asset Module TODO

**Status: active.** The architecture map is [PLANS.md](PLANS.md). Concrete
implementation decisions belong in the linked stage plans. Execution evidence
belongs in the corresponding `.spec/journal/` entry.

## Loading-progress roadmap

- [x] **LO1 — Asset load observation** — implement the Asset-owned,
  session-scoped observation contract and bounded snapshots. See
  [LO1 plan](.plan/LO1.md).

  Subtasks:

  - [x] Define the public observation values and session handle.
  - [x] Implement bounded session state and immutable snapshot publication.
  - [x] Integrate synchronous loads and recursive dependency correlation.
  - [x] Add phase, timing, size, disposition, and failure observations.
  - [x] Integrate asynchronous loads and session sealing/lifetime behavior.
  - [x] Add focused, integration, and concurrency coverage.
  - [x] Validate the Asset targets and record implementation evidence.
- [x] **LO2 — Staged Runtime startup and Editor promotion** — add the
  Runtime-owned startup transaction, keep presentation responsive, and activate
  future Editor workspace behavior on the game thread only after scene commit.
  See [LO2 plan](.plan/LO2.md).
- [x] **LO3 — Editor loading presentation** — present copied Runtime startup
  state, keep one presentation tick across modes, and transition exactly once
  to scene-aware Editor operation after commit. See
  [LO3 plan](.plan/LO3.md).

  Subtasks:

  - [x] Convert Runtime snapshots into a testable loading view model.
  - [x] Initialize ImGui once with only the loading tree.
  - [x] Draw determinate or indeterminate progress on every loading frame.
  - [x] Use one mode-aware Editor presentation tick for loading and main UI.
  - [x] Transactionally replace the loading tree with the existing main UI.
  - [x] Validate loading-first ordering, failure display, and both backends.

## Native texture products

- [x] Add database-free `TextureImporter` and `TextureCooker` stages for
  semantic mip generation, bounded portable RGBA products, and canonical
  `.texture` serialization.
- [x] Load native `.texture` products through a read-only runtime adapter and
  expose direct `cook-texture` tooling.
- [ ] Add BCn/ASTC product variants after the common format contract and both
  backend upload paths support capability selection; retain the portable
  fallback. This is coordinated by
  [AP1.2](.plan/AP1.md#ap12--gpu-native-texture-compression).

## Startup performance roadmap

- [ ] **AP1 — Startup Asset Loading Performance** — reduce the measured
  317.488-second Debug Sponza Asset phase through single-verification native
  loading, compact cooked products, package locality, bounded dependency
  scheduling, and low-mip initial readiness. Keep Material and Texture as
  independent Asset identities even when their bytes share a package. See the
  [AP1 plan](.plan/AP1.md) and
  [execution spec](../../.spec/specs/asset-startup-loading-performance.md).

  Subtasks:

  - [ ] **AP1.0 — Baseline and attribution:** report exclusive load costs,
    bytes, slowest operations, build configuration, cache condition, and peak
    memory for the fixed Sponza startup.
  - [x] **AP1.1 — Product verification:** native Texture/Model loading now
    computes the content and embedded-digest hashes without a product-sized
    integrity clone, reuses those results for archive verification and decode,
    and retains strict corruption rejection. See the [AP1 journal](../../.spec/journal/2026-09-09-asset-ap1-0.md#ap11-product-verification).
  - [ ] **AP1.2 — Texture compression:** add capability-aware BC desktop
    products and portable fallbacks across Asset cook, common Graphics, Vulkan,
    and OpenGL; keep the Sponza desktop texture closure at or below 550 MB.
  - [ ] **AP1.3 — Model compaction:** characterize and implement a versioned
    locality-optimized, quantized/compressed native Model profile with measured
    quality and decode evidence.
  - [ ] **AP1.4 — Asset package:** build and mount a read-only dependency-closure
    package whose TOC preserves product identity, shared Texture deduplication,
    corruption bounds, and priority-ordered byte ranges.
  - [ ] **AP1.5 — Scheduling and mip readiness:** add shared in-flight loads,
    explicit loader concurrency policy, a bounded memory budget, low-mip scene
    commit, and observable background high-mip streaming.
  - [ ] **AP1.6 — Integration gate:** reach initial packaged Sponza scene commit
    within 5 seconds in RelWithDebInfo on the reference laptop and record full
    tests, Vulkan/OpenGL captures, latency, bytes, memory, and final-residency
    evidence.

## Asset extensibility

- [ ] **AX1 — extensible Asset types and polymorphic payloads:** complete the
  generic type/loader registration and keep runtime and offline importer
  ownership separate. The AX1.1 payload migration is landed; see the
  [AX1 plan](.plan/AX1.md).
  - [x] **AX1.0 — baseline characterization:** pinned built-in type values,
    packed-ID encoding, suffix routing, typed payload lifetime, dependency
    unload protection, owned-child rollback, and observation behavior. See the
    [AX1.0 journal](../../.spec/journal/2026-09-08-asset-ax1.md).
  - [x] **AX1.1 — polymorphic payload core:** replaced the top-level resource
    variant with `std::shared_ptr<IAssetPayload>`, migrated built-in resources
    and consumers, and preserved built-in identity/routing. See the
    [AX1 plan](.plan/AX1.md) and
    [AX1.1 journal](../../.spec/journal/2026-09-08-asset-ax1-1.md).
  - [x] **AX1.2 — generic type/loader registry:** added explicit descriptor
    registration and sealing, built-in loader adapters, custom type values in
    the reserved range, and payload/type validation before publication. See
    the [AX1 plan](.plan/AX1.md) and
    [AX1.2 journal](../../.spec/journal/2026-09-08-asset-ax1-2.md).
  - [x] **AX1.3 — offline importer registry:** added the database-free
    provider contract, explicit/longest-suffix selection, model/texture
    adapters, and AssetImport-owned texture publication. See the
    [AX1 plan](.plan/AX1.md) and
    [AX1.3 journal](../../.spec/journal/2026-09-08-asset-ax1-3.md).
  - [x] **AX1.4 — extension hardening:** added fake-module transaction tests,
    custom-type async observation routing, rollback/unload coverage, and the
    public registration contract. See the [AX1 plan](.plan/AX1.md) and
    [AX1.4 journal](../../.spec/journal/2026-09-08-asset-ax1-4.md).
- [ ] **Live2D consumer handoff:** verify that Live2D can register its payload,
  native loader, and offline importer through AX1 without an Asset-specific
  Live2D branch. See [L2D2](../live2d/.plan/L2D2.md).

## Model-import roadmap

- [ ] **MI1 — content-addressed native model import** — use a standalone
  offline importer, runnable while the engine application is closed, to
  convert foreign STL/OBJ/FBX/GLTF/GLB sources into immutable hash-named native
  `.model` and `.material` products. Use an Asset-owned SQLite database under
  `.archive` to index readable source/material names, dependencies, and product
  hashes; skip verified cache hits without decoding or writing, and keep
  Material references inside the native Model so runtime does not need SQLite
  for ordinary dependency resolution. Level model keys use a narrow read-only
  archive lookup. Ordinary runtime `LoadSync` remains read-only. See the
  [MI1 plan](.plan/MI1.md).

  Subtasks:

  - [x] [MI1.1 — characterize the existing loader contract](.plan/MI1.1.md).
  - [x] [MI1.2 — implement hashing and the SQLite archive core](.plan/MI1.2.md).
  - [x] [MI1.3 — extract the pure foreign-model decoder](.plan/MI1.3.md).
  - [x] [MI1.4 — implement native Model V1](.plan/MI1.4.md).
  - [x] [MI1.5 — implement native Material conversion](.plan/MI1.5.md).
  - [x] [MI1.6 — implement the transactional model importer](.plan/MI1.6.md).
  - [ ] [MI1.7 — migrate runtime loading to native Model](.plan/MI1.7.md).
    MI1.7-R1 build ownership, MI1.7-R2 native material selection, MI1.7-R3
    transactional Model-child registration, MI1.7-R5 archive product
    verification, MI1.7-R6 bounded foreign compatibility, MI1.7-R7 native
    Model integration, and MI1.7-R8 Level integration are landed;
    checked-in products and the remaining review risks stay open
    in the stage plan.
  - [ ] [MI1.8 — add tooling and end-to-end validation](.plan/MI1.8.md).

## Acceptance ledger

- [x] LO1 exposes coherent root and recursive Asset load observations without
  changing existing load behavior or ownership boundaries.
- [x] LO2 reports complete startup readiness across Asset, CPU preparation,
  scene/GPU promotion, and level finalization.
- [x] LO3 visibly presents the progress screen first, then performs exactly one
  transition to the existing main Editor UI with no blank or mixed frame.
- [x] The complete acceptance contract in the
  [spec](../../.spec/specs/asset-loading-progress.md) passes.
- [ ] AP1 satisfies the latency, byte-budget, integrity, package-equivalence,
  concurrency, lifetime, and cross-backend visual criteria in the
  [startup performance spec](../../.spec/specs/asset-startup-loading-performance.md).
- [ ] MI1 satisfies its
  [native-model-import acceptance criteria](.plan/MI1.md#acceptance-criteria).

## Completion record

- [x] Add the implementation journal and link it here when work begins: see
  [Asset Loading Progress journal](../../.spec/journal/asset-loading-progress.md).
- [x] Update [asset_module.md](asset_module.md), the relevant module docs, and
  [docs/status.md](../status.md) with landed behavior and validation evidence.
