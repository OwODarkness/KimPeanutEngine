# Render Graph R4.3 journal — 2026-09-19

## Investigation

The R3/R4.1 explicit frame binding table was the correct insertion point for
imported TLAS state. Adding a second physical-resource resolver would have
duplicated the ownership boundary. The current Vulkan and OpenGL backends do
not expose a complete native RT owner, so the effective capability gate must
remain false in runtime smoke.

## Implementation

- Added imported acceleration-structure logical handles and version validation.
- Extended portable graph usage and stage enums, transition intents, lifetimes,
  exports, and frame binding validation for AS resources.
- Added a `RenderFrameConditions::ray_query_shadow` variant. Its deferred
  lighting declaration imports `SceneTLAS` and reads it at ray-tracing stage;
  the ordinary variants do not contain the AS resource.
- Added `RenderBackend::GetActiveTopLevelAccelerationStructure()` as a borrowed
  Graphics-owned physical seam. Invalid handles select the existing fallback.
- Added focused graph and production-plan tests.

## Validation

- Focused build passed: `RenderGraphTest`, `RenderSystemTest`,
  `GraphicsContractTest`.
- Focused CTest passed: 46/46 tests in the `RenderGraph|RenderSystem|Graphics`
  selection.
- Runtime GraphicsSmoke and full Debug build remain the next validation gate.

## Remaining risk

The native Vulkan AS owner, feature enabling, TLAS population, and shader-side
ray query are deliberately not implemented in this slice. R4.4/R4.5 must not
claim runtime RT success until those paths are complete and measured.
