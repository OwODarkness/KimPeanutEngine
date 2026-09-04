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
- [ ] **LO2 — Staged Runtime startup loop** — add the Runtime-owned startup
  transaction and keep presentation responsive through readiness and rollback.
  See [LO2 plan](.plan/LO2.md).
- [ ] **LO3 — Editor loading presentation** — present copied Runtime startup
  state and transition to normal Editor operation after commit. See
  [LO3 plan](.plan/LO3.md).

## Acceptance ledger

- [x] LO1 exposes coherent root and recursive Asset load observations without
  changing existing load behavior or ownership boundaries.
- [ ] LO2 reports complete startup readiness across Asset, CPU preparation,
  scene/GPU promotion, and level finalization.
- [ ] LO3 presents honest loading state and performs the loading-to-ready
  transition exactly once.
- [ ] The complete acceptance contract in the
  [spec](../../.spec/specs/asset-loading-progress.md) passes.

## Completion record

- [x] Add the implementation journal and link it here when work begins: see
  [Asset Loading Progress journal](../../.spec/journal/asset-loading-progress.md).
- [x] Update [asset_module.md](asset_module.md), the relevant module docs, and
  [docs/status.md](../status.md) with landed behavior and validation evidence.
