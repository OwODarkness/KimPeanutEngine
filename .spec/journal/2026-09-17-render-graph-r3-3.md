# Render Graph R3.3 Compatibility Proof Journal

## Scope

Expressed the existing eight-pass fixed declaration as an SSA-style logical
resource chain and compared the compiled graph with `FixedRenderPassSequence`.
GPU recording, runtime execution, and resource ownership remain unchanged.

## Changes

- Extended graph pass metadata with renderer/external ownership and terminal
  policy validation.
- Added a canonical compatibility test that declares directional, spot, point,
  G-buffer, deferred-lighting, tone-map, capture, and external-composite
  passes through graph versions.
- Compared normal, external-terminal, and capture variants against fixed-frame
  outcomes and pass order.
- Verified every declared resource edge, read/write access, and SSA version
  transition, including the external terminal's final position.

## Validation evidence

```text
cmake --build build --config Debug --target RenderGraphTest
  passed

ctest --test-dir build -C Debug -R "^(RenderGraphTest\\.|RenderGraphCompatibilityTest\\.)" --output-on-failure
  6/6 passed

cmake --build build --config Debug --target Render
  passed
```

## Not performed by design

- No graph-directed command recording or runtime pass-order switch.
- No Vulkan/OpenGL capture or profiling rerun; the fixed renderer is unchanged.
- No physical resource binding, barriers, transient allocation, or aliasing.

## Remaining risk

R3.4 must execute the existing recording methods from the compiled plan while
retaining current persistent targets and backend-private transitions, then pass
focused/full/smoke/visual parity evidence on Vulkan and OpenGL before removing
the duplicate fixed execution path.
