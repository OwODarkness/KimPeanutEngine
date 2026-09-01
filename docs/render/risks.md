# Render Risks and Limits

This is the Render-wide risk register. Submodule-specific feature risks remain
in their plans and TODOs. The cleanup design for the risks below is
[R1 — RenderSystem Responsibility Split](.plan/R1.md).

R1.1 characterization landed 2026-09-01. Direct orchestration tests now cover
partial initialization rollback, the observed fixed target/pass order,
conditional SceneColor readback, resize safe-point waiting, editor terminal
composition, and teardown ordering. RS-3 and RS-6 remain relevant to the later
renderer extraction, but their pre-extraction lifecycle/test gaps are closed.

## Active architecture risks

| ID | Severity | Risk | Current evidence | Required control |
| --- | --- | --- | --- | --- |
| RS-1 | High | Pass declarations and pass execution can diverge. | `RenderPassSchedule` validates a declaration list, but `RenderSystem::BeginFrame()` separately calls the shadow, G-buffer, lighting, tone-map, and capture recorders. It does not execute the validated list. | One fixed sequence must pair resource declarations with the operation actually invoked, including conditional capture and the terminal editor pass. |
| RS-2 | High | `RenderSystem` is a change-amplification and ownership hotspot. | Roughly 2,400 implementation lines and 290 header lines cover bootstrap, loading, resource baking/cache, registries, camera, all deferred passes, capture, resize, editor composition, and teardown. | Keep it as a thin composition/frame facade; move present pass policy/state into a cohesive `DeferredRenderer` and request/cache transition into a focused ingestor. |
| RS-3 | High | Partial initialization and manual teardown can produce invalid lifetime combinations. | `Initialize()` returns `void`, exits after some collaborators are created, and uses `backend_` as a partial state sentinel. Shutdown manually retires many interdependent handles in one function. | Transactional initialization, explicit lifecycle state, reverse-order collaborator cleanup, idempotent shutdown, and failure-injection tests. |
| RS-4 | High | Render currently crosses the Asset-loading boundary and can stall a frame. | `ConsumeOne()` and bootstrap preparation call `AssetManager::LoadSync()` from Render; texture behavior is selected by comparing request paths with bootstrap environment configuration. | Asset owns disk loading/path validation. Render consumes ready AssetIDs and performs only render-resource resolution under a measured budget. GP7 removes bootstrap path interpretation. |
| RS-5 | Medium-high | Pass-private GPU state is stored on the global facade. | Fullscreen/shadow pipelines, samplers, environment bindings, active shadow frames, and record-success flags are `RenderSystem` members. Every new pass widens initialization, frame, and shutdown coupling. | The renderer implementation owns all pass-private state and retires it before backend teardown. Do not create generic wrappers until destruction dependencies are known. |
| RS-6 | Medium-high | The integration point lacks direct characterization tests. | Focused registries, worlds, material, capture, resource resolver, and schedule validators have tests, but no unit test constructs `RenderSystem` or proves its real pass order, partial-init rollback, resize boundary, or teardown sequence. | Inject the existing backend factory for tests and cover orchestration states before extraction. Keep dual-backend smoke/captures as runtime evidence. |
| RS-7 | Medium | The public facade mixes unrelated consumers and includes an opaque native editor escape hatch. | Gameplay takes source sinks, Runtime takes capture/bootstrap values, Editor takes a target view plus `GraphicsContext { type, void* }`. | Keep capability-oriented seams narrow. Audit `GraphicsContext` separately; do not widen it or let it leak into renderer policy during R1. |
| RS-8 | Medium | Bootstrap scene authoring keeps transitional scene/environment policy in Render. | `BootstrapSceneInfo`, bootstrap source transfer, default-camera policy, and environment-path matching are RenderSystem responsibilities. | Complete the linked [GP7 level-asset migration](../../.spec/specs/gameplay-level-asset.md); remove the special case rather than extracting it into a permanent service. |

## Feature and validation limits

- The explicit fixed schedule is still the chosen policy. Add graph machinery
  only after measured dependency, aliasing, pass-culling, or transient-lifetime
  pressure; R1 is not that evidence.
- Capture is a developer-debug path. Deterministic authored levels and image
  comparison are still needed for repeatable visual regression testing.
- Material and source resolution may be pending or failed. A proxy must never
  draw with invalid, private, or stale resource handles.
- Point shadows use the proven six-face 2D-atlas baseline. Cubemap/subresource,
  multiple-job, caching, and general-atlas work remain behind their documented
  evidence gates.
- Any backend-specific recording or presentation seam must remain behind common
  Render/Graphics capabilities and must not spread native values to Gameplay,
  Asset, or ordinary Render policy.

## Risk treatment order

1. Characterize lifecycle and real execution order before moving code.
2. Make initialization/teardown state explicit.
3. Extract the deferred renderer and its pass-owned state.
4. Unify fixed-pass declaration and execution.
5. Remove synchronous Asset/bootstrap path work from Render in coordination
   with GP7.
6. Reassess the native editor context seam separately.

Do not begin with a render graph, speculative renderer plugins, or a universal
context object. Those would increase surface area before the current ownership
risks are controlled.
