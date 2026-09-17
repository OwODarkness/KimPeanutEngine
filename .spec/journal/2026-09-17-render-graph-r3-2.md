# Render Graph R3.2 Implementation Journal

## Scope

Implemented the R3.2 pure graph model/compiler after the R3.0 baseline and
Sakura reference study. Existing R1.5 profiling/capture evidence was
preserved; this stage does not alter runtime rendering.

## Changes

- Added `RenderGraphBuilder` and `CompiledRenderGraph` under Render.
- Added graph-scoped typed texture/buffer/pass handles and logical resource
  versions.
- Added imported/transient records, pass reads/writes, explicit dependencies,
  exports, side-effect roots, and optional disabled conditions.
- Added deterministic dependency compilation, declaration-order tie-breaking,
  reachability culling, diagnostics, and live first/last-use intervals.
- Added a standalone `RenderGraphTest` target with five tests covering ordering,
  culling/lifetimes, missing producers, cycles, conditional dependencies,
  cross-graph handles, and duplicate writes.
- Added the R3.2 spec and closed the R3.1 interface review for this scoped
  implementation boundary.

## Validation evidence

```text
cmake --build build --config Debug --target RenderGraphTest
  passed

ctest --test-dir build -C Debug -R "^RenderGraphTest\\." --output-on-failure
  5/5 passed

ctest --test-dir build -C Debug -R "^(FixedRenderPassSequenceTest|FixedRenderPassFrameTest)\\." --output-on-failure
  13/13 passed

cmake --build build --config Debug --target Render
  passed

git diff --check
  passed; Git reported expected LF/CRLF normalization warnings only
```

## Not performed by design

- No Vulkan/OpenGL runtime launch or capture was repeated: R3.2 is CPU-only
  and does not change the active renderer.
- No fixed-schedule migration, command recording, resource-state planning,
  transient allocation, or backend synchronization was attempted.

## Remaining risk

R3.3 must compile the current eight-pass declaration beside
`FixedRenderPassSequence` and prove order, conditions, external terminal
policy, outcomes, and resource edges before R3.4 changes execution.
