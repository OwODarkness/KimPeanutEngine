# D5 Silhouette Comparator Follow-up

## 2026-09-10 — topology-based cross-backend comparison

The D5 Vulkan/OpenGL smoke images had matching overall bounds and visible
geometry, but the final-color threshold classified dark shadowed rock/floor
pixels as background. That produced `raw=356`, `edge=293`,
`structural=63`, and `area_delta=120` even though the two silhouettes had the
same connected topology.

`engine/example/graphics/rhi_example.cpp` now builds each silhouette mask by
thresholding bright pixels, flood-filling only border-connected background,
and treating enclosed dark regions as model area. This keeps the existing
translation, missing-thin-feature, and bounded-edge synthetic policy probes;
it changes only the source mask used by the comparison.

Validation:

- `cmake --build build --config Debug --target GraphicsSmoke -- /m:1` — passed.
- `build/engine/example/graphics/Debug/GraphicsSmoke.exe` — passed for six
  frames per API, including the D5 Vulkan/OpenGL gate.

The smoke still prints pre-existing Vulkan validation diagnostics for a
depth-only sample path and intentional streaming-geometry rejection probes;
they do not fail the executable or belong to this comparator correction.
