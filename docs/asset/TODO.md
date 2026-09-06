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

## Model-import roadmap

- [ ] **MI1 — content-addressed native model import** — convert foreign
  STL/OBJ/FBX/GLTF/GLB sources into immutable hash-named native `.model` and
  `.material` products. Use an Asset-owned SQLite database under `.archive`
  to index readable source/material names, dependencies, and product hashes;
  skip verified cache hits without decoding or writing, and keep Material
  references inside the native Model so runtime does not need SQLite. Ordinary
  runtime `LoadSync` remains read-only. See the [MI1 plan](.plan/MI1.md).

  Subtasks:

  - [ ] Add stable hashing, dependency-closure fingerprints, a versioned SQLite
    archive repository, product integrity checks, and exact no-op decisions.
  - [ ] Extract a pure Assimp imported-model document and support STL, OBJ, FBX,
    GLTF, and GLB as import sources rather than native runtime formats.
  - [ ] Define deterministic native Model V1 serialization with ordered typed
    Material references and defensive loading.
  - [ ] Add canonical content-hashed Material conversion, embedded-image memory
    decode, deduplication, and glTF PBR semantics.
  - [ ] Stage and validate immutable products, then commit their metadata and
    per-source root in one short SQLite transaction with rollback on failure.
  - [ ] Dispatch `.model` through the native loader, migrate Levels/bootstrap,
    and preserve existing material overrides.
  - [ ] Add explicit import/reimport/status tooling, material promotion, focused
    filesystem tests, cross-backend smoke, and visual validation.

## Acceptance ledger

- [x] LO1 exposes coherent root and recursive Asset load observations without
  changing existing load behavior or ownership boundaries.
- [x] LO2 reports complete startup readiness across Asset, CPU preparation,
  scene/GPU promotion, and level finalization.
- [x] LO3 visibly presents the progress screen first, then performs exactly one
  transition to the existing main Editor UI with no blank or mixed frame.
- [x] The complete acceptance contract in the
  [spec](../../.spec/specs/asset-loading-progress.md) passes.
- [ ] MI1 satisfies its
  [native-model-import acceptance criteria](.plan/MI1.md#acceptance-criteria).

## Completion record

- [x] Add the implementation journal and link it here when work begins: see
  [Asset Loading Progress journal](../../.spec/journal/asset-loading-progress.md).
- [x] Update [asset_module.md](asset_module.md), the relevant module docs, and
  [docs/status.md](../status.md) with landed behavior and validation evidence.
