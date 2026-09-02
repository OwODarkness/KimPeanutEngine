# Render Risks and Limits

This is the Render-wide risk register. Submodule-specific feature risks remain
in their plans and TODOs. The cleanup design for the risks below is
[R1 — RenderSystem Responsibility Split](.plan/R1.md).

R1.1 characterization landed 2026-09-01. The focused
[R1.2 extraction plan](.plan/R1.2.md) started implementation 2026-09-02. Direct
orchestration tests now cover
partial initialization rollback, the observed fixed target/pass order,
conditional SceneColor readback, resize safe-point waiting, editor terminal
composition, and teardown ordering. The extraction, ownership probes, full
build, dual-backend smoke, and fresh visual evidence are landed; remaining
risks are tracked by the later R1.3/R1.4 stages.

R1.3 implementation landed 2026-09-02 under the predecessor evidence waiver.
The immutable typed sequence and frame cursor now have canonical-ID and
moved-from-cursor regression coverage; focused/full build, CTest, and
dual-backend smoke pass. Fresh Runtime startup-level captures were also
exported and inspected.

## Active architecture risks

| ID | Severity | Risk | Current evidence | Required control |
| --- | --- | --- | --- | --- |
| RS-1 | High | Pass declarations and pass execution can diverge. | R1.3 uses one immutable eight-entry typed sequence and a per-frame cursor for validation and invocation; canonical ordinal validation rejects swapped IDs, and focused tests, full CTest, dual-backend smoke, and fresh startup-level captures pass. | Preserve the closed sequence and cursor assertions through R1.4; do not add dynamic pass registration or a second recorder order. |
| RS-2 | High | `RenderSystem` is a change-amplification and ownership hotspot. | R1.2 moved the deferred pass implementation and pass-private state into `DeferredRenderer`; request/cache transition remains in the facade until R1.4. | Keep the facade as composition/frame owner; keep resource ingestion separate in R1.4. |
| RS-3 | High | Partial initialization and manual teardown can produce invalid lifetime combinations. | `DeferredRenderer::Initialize()` rolls back its partial target set; cleanup retires renderer-owned state before resolver/backend cleanup. Focused ownership probes and dual-backend smoke pass. | Preserve the transactional initialization and ordered cleanup assertions in later Render stages. |
| RS-4 | High | Render currently crosses the Asset-loading boundary and can stall a frame. | `ConsumeOne()` and lazy pass preparation call `AssetManager::LoadSync()` from Render. GP7 has removed bootstrap scene/path interpretation, but the remaining synchronous work is still live. | Asset owns disk loading/path validation. R1.4 makes Render consume ready AssetIDs/artifacts and perform only render-resource resolution under a measured budget. |
| RS-5 | Medium-high | Pass-private GPU state is stored on the global facade. | R1.2 moved fullscreen/shadow pipelines, samplers, environment bindings, active shadow frames, target state, and recording methods into `DeferredRenderer`. | Keep pass-specific state inside the renderer and retire it before resolver/backend teardown. |
| RS-6 | Medium-high | Renderer extraction can regress behavior that unit-level pass tests do not observe. | R1.2 preserves the orchestration test source, adds renderer-owned pipeline/mesh/sampler cleanup assertions, injects both fullscreen partial-creation failures, and passed focused execution, full CTest, dual-backend smoke, and fresh startup-level captures. | Keep deterministic captures and lifecycle/ownership assertions as regression coverage for later Render stages. |
| RS-7 | Medium | The public facade mixes unrelated consumers and includes an opaque native editor escape hatch. | Gameplay takes source sinks, Runtime takes capture/resource values, and Editor takes a target view plus `GraphicsContext { type, void* }`. | Keep capability-oriented seams narrow. Audit `GraphicsContext` separately; do not widen it or let it leak into renderer policy during R1. |
| RS-8 | Resolved by GP7 | Bootstrap scene authoring kept transitional scene/environment policy in Render. | GP7 removed `BootstrapSceneInfo`, bootstrap source transfer, environment-path matching, and hard-coded startup Actors. Level assets now publish typed sources. | Do not recreate the deleted bootstrap special case during R1.2 or R1.4. |

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
5. Remove the remaining synchronous Asset/resource work from Render; keep the
   completed GP7 bootstrap special cases deleted.
6. Reassess the native editor context seam separately.

Do not begin with a render graph, speculative renderer plugins, or a universal
context object. Those would increase surface area before the current ownership
risks are controlled.
