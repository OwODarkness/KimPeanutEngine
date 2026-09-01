# RenderSystem R1.1 Characterization and Transactional Lifecycle

- Status: complete
- Date: 2026-09-01
- Spec: [R1 stage design](../../docs/render/.plan/R1.md)
- Parent TODO: [Render TODO](../../docs/render/TODO.md)

## What was done

- Added a factory seam that injects the existing `RenderBackend` contract into
  `RenderSystem` tests.
- Added explicit lifecycle states and diagnostic initialization results.
- Made initialization transactional and consolidated partial/full cleanup into
  one idempotent teardown path.
- Added direct orchestration tests for the current frame, capture, resize,
  editor-composite, rollback, and teardown behavior.

## What changed

- Architecture or behavior: `RenderSystem` now transitions through
  `Uninitialized`, `Ready`, `FrameActive`, and terminal `ShutDown`. Invalid
  frame operations are rejected deterministically. A failed initialization
  releases all collaborators and returns to `Uninitialized`.
- Important files/modules: `engine/runtime/render/render_system.*`,
  `renderer_frame_targets.*`, `runtime_global_context.cpp`, and the new
  `RenderSystemTest` target.
- Public API or ownership changes: the existing RenderBackend factory is an
  optional test seam; RenderSystem owns lifecycle result/state reporting and
  cleanup ordering. No new RHI or backend abstraction was added.

## Validation

- Required level: Level 2
- Command: `cmake --build build --config Debug --target RenderSystemTest`
- Result: PASS
- Evidence: target compiled and linked.
- Command: `ctest --test-dir build -C Debug -R "(RenderSystemLifecycleTest|RenderPassScheduleTest)" --output-on-failure`
- Result: PASS
- Evidence: 11/11 tests passed.
- Command: `cmake --build build --config Debug --target RuntimeLib RenderPassScheduleTest`
- Result: PASS
- Evidence: RuntimeLib and existing render schedule test target built.
- Command: `cmake --build build --config Debug`
- Result: PASS
- Evidence: full Debug build completed for the engine, runtime, editor, smoke,
  and unit-test targets.
- Command: `ctest --test-dir build -C Debug --output-on-failure`
- Result: PASS
- Evidence: 191/191 tests passed.

## Remaining risks and unverified areas

- The later R1.2 renderer extraction, R1.3 declaration/execution unification,
  and R1.4 Asset/bootstrap boundary work are not part of this checkpoint.
- Vulkan/OpenGL visual output was not changed intentionally and was not rerun
  for this lifecycle-only slice.
- GraphicsSmoke and visual capture comparison were not rerun for this
  lifecycle-only slice; those remain required when renderer ownership moves in
  later R1 stages.

## Remaining work

- Proceed with R1.2 only after preserving this characterization baseline.
- GP7 level asset/bootstrap migration remains a separate user-owned task.

## Documentation and follow-up

- Updated `docs/status.md`, `docs/render/TODO.md`,
  `docs/render/.plan/R1.md`, `docs/render/lifecycle.md`, and
  `docs/render/risks.md`.
