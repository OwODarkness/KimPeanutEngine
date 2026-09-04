# Asset Loading Progress Screen

- Status: partial
- Date: 2026-09-04
- Spec: [Asset Loading Progress](../specs/asset-loading-progress.md)
- Parent TODO: [Asset Loading Progress TODO](../../docs/asset/TODO.md)

## What was done

- Implemented LO1 Asset-owned load observation.
- Added opt-in session handles, operation identity, recursive parent
  correlation, bounded snapshots, timing, supported payload-size accounting,
  cache/dedup dispositions, failure diagnostics, and async sealing behavior.
- Preserved unobserved load behavior, Asset ownership, dependency semantics,
  and the existing loader/state lock order.
- Corrected review findings for source-phase publication, async dispatch
  failure completion, relative-path diagnostics, trailing-slash normalization,
  and reentrant-safe observation clocks.

## What changed

- Architecture or behavior: `AssetManager` now offers observed sync/async load
  overloads backed by shared session state; snapshots contain exact aggregates
  and bounded copied detail.
- Important files/modules: `engine/runtime/asset/asset_manager.*`,
  `asset_load_observation.*`, Asset CMake wiring, and AssetUnitTest coverage.
- Public API or ownership changes: added `AssetLoadSession` and value-only
  observation types. No Asset payload or GPU ownership changed.

## Validation

- Required level: L4
- Command: `cmake --build build --config Debug`
- Result: PASS
- Evidence: full Debug build completed successfully.
- Command: `ctest --test-dir build -C Debug --output-on-failure`
- Result: PASS
- Evidence: 275/275 tests passed.
- Command: `ctest --test-dir build -C Debug -R AssetLoadObservation --output-on-failure`
- Result: PASS
- Evidence: 10/10 LO1 observation tests passed, including recursive, cache-hit,
  exception, concurrency, async-seal, bounded-state, decoded-size, reentrant
  clock, and diagnostic-path cases.
- Command: `.\tools\kp.ps1 test AssetLoadObservation`
- Result: PASS (documented focused command)
- Evidence: The wrapper now uses the actual CTest regex for the LO1 tests;
  `AssetUnitTest` is the build target, not a discovered CTest name.

## Remaining risks and unverified areas

- LO2 and LO3 are not implemented; Runtime startup and Editor presentation do
  not yet consume these snapshots.
- Runtime visual loading-screen evidence is therefore not applicable to LO1.

## Remaining work

- Implement [LO2](../../docs/asset/.plan/LO2.md) and
  [LO3](../../docs/asset/.plan/LO3.md) under the active cross-stage spec.

## Documentation and follow-up

- Updated the Asset module design, LO1 plan status, roadmap, project spec
  status, and `docs/status.md` to record LO1 as landed while LO2/LO3 remain
  planned.
