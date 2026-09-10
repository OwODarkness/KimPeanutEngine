# L2D4.1 RHI correction journal

## 2026-09-10

- Corrected OpenGL indexed draws to use the bound pipeline topology, the
  geometry view's index-buffer offset, UInt16/UInt32 element width, and the
  requested first-index and base-vertex arguments.
- Made invalid geometry, pipeline, and descriptor bindings observable and
  prevented rejected bindings from reusing stale mesh or geometry state.
- Made per-frame geometry slots non-bindable until written in the current
  frame; Vulkan resets the fence-safe slot after waiting, and OpenGL resets
  its synchronous slot at frame start.
- Fixed partial immutable OpenGL initialization so allocation does not read
  beyond the caller's initial payload. Removed the throwing validation path's
  incorrect `noexcept` declaration.
- Extended contract coverage for unwritten slots and lookup exceptions, and
  extended the two-backend smoke path with a six-frame, two-stream indexed
  quad using non-zero index offset, first-index, base-vertex, and triangle-strip
  semantics.

## Validation

- Full Debug build: passed; existing MSVC `/DEFAULTLIB` warnings remain.
- `GraphicsContractTest.exe`: 19/19 passed.
- `RenderPassScheduleTest.exe`: 100/100 passed.
- `Live2DCoreTest.exe`: 6/6 passed.
- `Live2DRenderContractTest.exe`: 7/7 passed.
- `GraphicsSmoke.exe`: exercised Vulkan and OpenGL and reached the corrected
  streaming path; exit remains 1 only at the existing D5 silhouette comparator
  (`raw=356, edge=293, structural=63, area_delta=120, bounds=match`).
- The `kp.ps1 test`/CTest wrapper currently reports no registered tests, so
  focused executables were run directly.
