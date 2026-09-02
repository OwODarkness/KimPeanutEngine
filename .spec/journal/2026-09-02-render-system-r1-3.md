# RenderSystem R1.3 Journal

- Status: complete
- Spec: [render-system-r1-3](../specs/render-system-r1-3.md)
- Stage: [R1.3](../../docs/render/.plan/R1.3.md)

## 2026-09-02 — implementation

R1.2's `DeferredRenderer` ownership shape was reviewed before editing. Its
focused build/tests, dual-backend smoke, and deterministic captures remain
unverified, so R1.3 proceeds under an explicit predecessor waiver. The first
MSBuild failure is environmental and occurs while evaluating the Windows SDK
props file, before any project source is compiled:

```text
MSB4184: ToolLocationHelper.GetLatestSDKTargetPlatformVersion(Windows, 10.0)
access denied to C:\Users\17519\AppData\Local\Microsoft SDKs
```

The reference gate checked the local Sakura/gkNextEngine index and public
source examples for declaration/execution separation. The applicable result is
limited to a value-only declaration phase and an execution phase; Render R1.3
does not adopt render-graph compilation, resource aliasing, barriers, or
virtual pass objects.

Implementation changes:

- replaced mutable `RenderPassSchedule`/string declarations with
  `FixedRenderPassSequence`, typed IDs, owner/condition metadata, and static
  validation for identity, resource flow, conditional dominance, and terminal
  external ownership;
- added move-only `FixedRenderPassFrame` with ordered renderer execution,
  conditional capture skip, failure continuation, exactly-once external
  execution, and finalization outcomes;
- defined the canonical directional/spot/point shadow, G-buffer, deferred
  lighting, tone-map, capture, and editor entries in `DeferredRenderer`;
- moved lighting-binding creation into the deferred-lighting dispatch entry,
  removed repeated schedule checks, removed the unreachable G-buffer debug
  recorder, and converted live pass recorders to boolean outcomes;
- delegated editor execution and frame finalization through `DeferredRenderer`
  and removed the facade's duplicate once-per-frame flag;
- replaced `capture_view_recorded` with `capture_target_ready`, preserving the
  direct SceneColor readback path.

## Validation (initial environment probe)

Passed:

- `g++ -std=c++17 -fsyntax-only` for `render_pass.cpp`,
  `deferred_renderer.cpp`, and `render_system.cpp`;
- `g++ -std=c++17 -fsyntax-only` for the fixed-sequence and RenderSystem unit
  test translation units;
- static audit found no live `RenderPassSchedule`,
  `RecordGBufferDebugViewPass`, facade editor once flag, or per-recorder
  schedule validity check.

Initial blocked/unverified record:

- `cmake --build build --config Debug --target RenderPassScheduleTest` is
  blocked by the Windows SDK permission error above;
- focused executable tests, full Debug build, complete CTest, Vulkan/OpenGL
  `GraphicsSmoke`, runtime command captures, and deterministic visual inspection
  are unverified;
- The entries above preserve the initial environment limitation. The
  predecessor evidence was subsequently rerun successfully; the closure
  evidence is recorded below.

## Remaining R1.4 debt

Lazy shader loading/processing remains in the renderer pass preparation path.
R1.3 intentionally makes no Asset/Resource ingestion or backend contract
change.

## 2026-09-02 — review correction and closure

The formal review found that unique typed IDs could still be swapped between
canonical positions, and defaulted frame moves left the source cursor
executable. `FixedRenderPassSequence` now rejects IDs that do not match their
canonical ordinal. Sequence and frame move operations consume and invalidate
their sources; focused tests cover swapped IDs and moved-from renderer,
external, and finalization calls. The durable design, spec, TODO, risks, and
status records were updated to describe the landed sequence/cursor architecture
and remove the duplicated R1.2 roadmap entry.

Closure validation:

- `cmake --build build --config Debug --target RenderPassScheduleTest` — PASS.
- `build/engine/test/unit/render/Debug/RenderPassScheduleTest.exe` — PASS,
  73/73 tests, including moved-sequence invalidation.
- `cmake --build build --config Debug --target RenderSystemTest` — PASS.
- `build/engine/test/unit/render/Debug/RenderSystemTest.exe` — PASS, 12/12
  tests.
- `cmake --build build --config Debug` — PASS.
- `ctest --test-dir build -C Debug --output-on-failure` — PASS, 244/244 tests.
- `build/engine/example/graphics/Debug/GraphicsSmoke.exe` — PASS, three
  frames/API across Vulkan and OpenGL; SceneColor output was visually
  inspected.
- Fresh Runtime startup-level captures for PBR and directional/spot/point
  shadow fixtures — exported and inspected on Vulkan and OpenGL.
- `git diff --check` — PASS.

The `kp.ps1 smoke` wrapper still fails after its successful build because its
empty argument array is rejected; direct execution supplies the smoke
evidence. This is a tooling limitation, not an R1.3 source failure.
