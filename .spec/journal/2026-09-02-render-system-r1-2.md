# RenderSystem R1.2 Deferred Renderer Extraction

- Status: complete
- Date: 2026-09-02
- Spec: [R1.2 execution spec](../specs/render-system-r1-2.md)
- Stage design: [R1.2 plan](../../docs/render/.plan/R1.2.md)
- Parent TODO: [Render TODO](../../docs/render/TODO.md)

## Implementation landed

- Added the concrete `DeferredRenderer` collaborator and wired it into the
  Render library.
- Moved named frame targets, pass schedule, shadow scheduling/recording,
  environment binding state, pass-private pipelines/samplers/fullscreen mesh,
  pass recording/preparation, capture conversion, resize handling, and cleanup
  out of `RenderSystem`.
- Kept `RenderSystem` responsible for lifecycle, backend and `FrameContext`
  bracketing, source registries/worlds, camera selection, resource ingestion,
  capture callback ownership, and editor composition.
- Added a value-only frame handoff and a non-retained shadow-validity callback.
- Extended the RenderSystem backend probe and tests to cover partial target
  initialization rollback and exactly-once target destruction before backend
  cleanup.
- Fixed fullscreen resource retry ownership: a successful mesh or sampler is
  retained when its pair fails, and both partial-failure directions are covered
  by injected normal-recording tests.
- Made deferred-lighting resource preparation idempotent once its pipeline and
  bindings are valid, avoiding repeated pipeline creation during normal frames.
- Extended the normal orchestration assertions to compare every pipeline, mesh,
  and sampler creation with exactly one destruction before backend cleanup.
- Removed the stale pass-only helper block and includes from `render_system.cpp`;
  removed the pre-R1.3 unconditional `normal_recording_completed` result field.

## Ownership notes

`DeferredRenderer` owns the GPU handles it creates and clears resolver-owned
environment bindings without destroying them. It does not own the backend,
resource pipeline, resource resolver, material system, worlds, registries,
capture service, or frame-context slots. The fixed manual pass order remains;
declaration/execution unification is still R1.3, and synchronous asset/resource
ingestion remains R1.4 debt.

## Validation

- `g++ -std=c++17 -fsyntax-only -Iengine/runtime -Iengine/runtime/core -Iengine/runtime/graphics/backend -Ibuild/engine/runtime/core/config engine/runtime/render/deferred_renderer.cpp engine/runtime/render/render_system.cpp` — PASS.
- `g++ -std=c++17 -fsyntax-only -Iengine/runtime -Iengine/runtime/core -Iengine/runtime/graphics/backend -Ibuild/engine/runtime/core/config -Ithird_party/googletest/googletest/include -Ithird_party/googletest/googlemock/include engine/test/unit/render/render_system_test.cpp` — PASS.
- `git diff --check` — PASS; Git emitted only existing ignore-file/line-ending warnings.
- `g++ -std=c++17 -Wall -Wextra -Wpedantic -fsyntax-only ...` on the affected
  renderer/facade sources and `g++ -std=c++17 -Wall -Wextra -fsyntax-only ...`
  on `render_system_test.cpp` — PASS; only pre-existing warnings were emitted.
- `cmake --build build --config Debug --target RenderSystemTest` — PASS.
- `build/engine/test/unit/render/Debug/RenderSystemTest.exe --gtest_color=no` — PASS, 12/12 tests.
- `cmake --build build --config Debug` — PASS.
- `ctest --test-dir build -C Debug --output-on-failure` — PASS, 239/239 tests.
- `build/engine/example/graphics/Debug/GraphicsSmoke.exe` — PASS, 3 frames/API across Vulkan and OpenGL.
- Fresh Vulkan/OpenGL PBR, point-shadow, and spot-shadow captures — exported successfully and inspected for SceneColor.
- `git diff --check` — PASS.

## Tooling limitation and follow-up

The `kp.ps1 test RenderSystemTest` wrapper reports no discovered tests, and
`kp.ps1 smoke` fails after its successful build because its empty argument
array is rejected. The direct binaries provide the corresponding evidence.
Remaining RS-1/RS-4 risks belong to the separate R1.3/R1.4 stages.
