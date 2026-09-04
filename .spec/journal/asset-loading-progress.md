# Asset Loading Progress Screen

- Status: LO2 core implemented; LO3 and visual-gate hardening remain
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

- LO2 deterministic gate seams, close/failure matrix, and runtime visual
  evidence remain unverified.
- LO3 still needs to consume the copied startup snapshots for an honest loading
  screen.

## Remaining work

- Complete the LO2/LO3 runtime visual gate and implement the remaining
  [LO3](../../docs/asset/.plan/LO3.md) evidence under the active cross-stage
  spec.

## Documentation and follow-up

- Updated the Asset module design, LO1 plan status, roadmap, project spec
  status, and `docs/status.md` to record LO1 and the LO2 core lifecycle.

## LO2 core implementation — 2026-09-04

- Added the Runtime-owned `StartupCoordinator` with ordered startup phases,
  copied snapshots, revision waiters, first-failure preservation, and rollback.
- Split `RenderSystem` into presentation initialization and scene promotion;
  presentation frames can run before the prepared catalog and scene target
  exist, while the compatibility `Initialize` path remains intact.
- Changed Engine startup to initialize presentation first, observe and seal the
  startup Asset session, prepare CPU artifacts, promote Render, finalize the
  level, and promote the Editor workspace exactly once before `Ready`.
- Split EditorUI initialization from workspace construction. Loading frames use
  the existing ImGui context and backend; scene-dependent components are built
  only after commit.
- Added coordinator and staged RenderSystem tests. The full Debug build passed;
  focused Runtime/Render/Editor suites passed 16/16.

## LO2 remaining work

- Add deterministic gate seams proving multiple loading polls/frames during
  blocked Asset/CPU/promotion work, plus close/failure coverage at every stage.
- Add LO3 snapshot-driven loading presentation and Vulkan/OpenGL visual smoke
  evidence.

## LO2 risk corrections — 2026-09-04

- Added a startup access barrier so render-side cancellation cannot clear
  Runtime state until the main startup lane has quiesced; rollback ends the
  barrier before joining the render thread.
- Moved startup rollback ownership ahead of Editor attachment, Asset-session
  allocation, and render-thread creation. All terminal paths seal the retained
  Asset observation session.
- Made coordinator terminal states immutable to ordinary mutators and limited
  rollback to failed or cancelled startup transactions. Nested Asset revision
  changes are now observable by the coordinator waiter.
- Routed post-promotion loading frames through the fixed Editor composite pass.
- Added focused tests for the terminal contract, nested Asset wakeups, and the
  startup access barrier.

Validation after these corrections:

- `cmake --build build --config Debug --parallel 1` — PASS.
- Focused Render/Runtime/Editor startup suites — PASS, 23/23 tests.
- `ctest --test-dir build -C Debug --output-on-failure` — PASS, 281/281 tests.
- `git diff --check` — PASS, with only existing Git global-ignore and line
  ending warnings.

## LO3 loading/workspace presentation — 2026-09-04

- Added a pure `StartupSnapshot` to loading view-model conversion. It reports
  the Runtime stage, safe Asset display path, exact observed operation counts,
  determinate progress when Runtime supplies it, indeterminate activity when
  totals are unknown, and copied failure diagnostics.
- Added the loading component and loading-only component tree. It uses the
  existing ImGui context, WSI, and API renderer, and draws a centered loading
  panel with an animated progress bar before scene promotion.
- Made `EditorUI` render the active loading or workspace tree through one frame
  path. Workspace construction remains transactional; the loading tree is
  retired only after successful promotion.
- Added the startup-ready render handshake so the first workspace frame is
  drawn only after Runtime publishes `Ready`; no mixed loading/workspace frame
  is produced.

Validation:

- `cmake --build build --config Debug --parallel 1` — PASS.
- Focused Runtime/Render/Editor suites — PASS, 25/25 tests.
- `ctest --test-dir build -C Debug --output-on-failure` — PASS, 284/284 tests.

The deterministic hold and Vulkan/OpenGL loading/UI screenshot evidence remain
open. The Runtime command channel did successfully launch OpenGL and export a
scene-color capture, but that capture intentionally excludes the ImGui
composite; the available Computer Use surface did not expose a targetable
native engine window for UI capture in this session.

## Final engine-window capture — 2026-09-04

- Added `view=engine_window` to `capture.screenshot`. The request bypasses
  scene RenderTarget readback and is completed at the final presentation
  boundary on the render thread, so it includes the Editor/ImGui composite.
- Added a Window-owned Win32 GLFW capture path for the client area. Visible
  content uses `BitBlt`; `PrintWindow` remains a fallback for covered
  windows.
- The OpenGL runtime command exported the requested PNG successfully. In the
  current non-interactive validation desktop the native capture was visually
  blank even though the scene-color capture was valid; this environment result
  is recorded as a limitation rather than proof of UI pixels.
