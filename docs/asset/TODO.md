# Asset Loading Progress TODO

**Status: active.** The architecture map is [PLANS.md](PLANS.md); the
cross-stage contract is the [Asset Loading Progress spec](../../.spec/specs/asset-loading-progress.md).
Concrete implementation decisions belong in the linked stage plans. Execution
evidence belongs in the corresponding `.spec/journal/` entry.

## Roadmap

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
- [ ] **LO2 — Staged Runtime startup and Editor promotion** — add the
  Runtime-owned startup transaction, keep presentation responsive, and activate
  future Editor workspace behavior on the game thread only after scene commit.
  See [LO2 plan](.plan/LO2.md).
- [ ] **LO3 — Editor loading presentation** — present copied Runtime startup
  state, keep one presentation tick across modes, and transition exactly once
  to scene-aware Editor operation after commit. See
  [LO3 plan](.plan/LO3.md).

  Subtasks:

  - [ ] Convert Runtime snapshots into a testable loading view model.
  - [ ] Initialize ImGui once with only the loading tree.
  - [ ] Draw determinate or indeterminate progress on every loading frame.
  - [ ] Use one mode-aware Editor presentation tick for loading and main UI.
  - [ ] Transactionally replace the loading tree with the existing main UI.
  - [ ] Validate loading-first ordering, failure display, and both backends.

## Acceptance ledger

- [x] LO1 exposes coherent root and recursive Asset load observations without
  changing existing load behavior or ownership boundaries.
- [ ] LO2 reports complete startup readiness across Asset, CPU preparation,
  scene/GPU promotion, and level finalization.
- [ ] LO3 visibly presents the progress screen first, then performs exactly one
  transition to the existing main Editor UI with no blank or mixed frame.
- [ ] The complete acceptance contract in the
  [spec](../../.spec/specs/asset-loading-progress.md) passes.

## Completion record

- [x] Add the implementation journal and link it here when work begins: see
  [Asset Loading Progress journal](../../.spec/journal/asset-loading-progress.md).
- [x] Update [asset_module.md](asset_module.md), the relevant module docs, and
  [docs/status.md](../status.md) with landed behavior and validation evidence.
