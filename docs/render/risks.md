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

R1.4 landed in the [ready-asset ingestion plan](.plan/R1.4.md). Runtime now
publishes a typed startup catalog before Render initialization; the catalog and
preparation review findings are addressed, focused/full Debug validation passes,
and six fresh fixture captures were exported and inspected. Direct
dual-backend smoke still launches both APIs but its strict silhouette
comparator rejects a small cross-backend edge difference; that follow-up is
separate from the resolved ingestion-boundary risk.

R1.5 code fixes landed 2026-09-04 in the [facade-hardening plan](.plan/R1.5.md).
It
introduced the address-stable scene coordinator, typed Graphics-owned
presentation bridge, value-only viewport/metrics access, and frame-open unwind.
The outstanding R1.4 comparator is superseded by a reviewed policy with
separate edge/structural budgets, contour bounds, and synthetic rejection
probes. R1 remains open only until orderly application-close evidence is
captured; the journal records the evidence gap and the intentionally retained
Graphics-internal context helper.

## Active architecture risks

| ID | Severity | Risk | Current evidence | Required control |
| --- | --- | --- | --- | --- |
| RS-1 | High | Pass declarations and pass execution can diverge. | R1.3 uses one immutable eight-entry typed sequence and a per-frame cursor for validation and invocation; canonical ordinal validation rejects swapped IDs, and focused tests, full CTest, dual-backend smoke, and fresh startup-level captures pass. | Preserve the closed sequence and cursor assertions through R1.4; do not add dynamic pass registration or a second recorder order. |
| RS-2 | High | `RenderSystem` is a change-amplification and ownership hotspot. | R1.2 moved deferred passes into `DeferredRenderer`, R1.3 moved ordering into the fixed sequence, R1.4 removed the request/cache surface, and R1.5 moved source registries/worlds/camera/material resolution into `RenderSceneCoordinator`. | Preserve the coordinator boundary while keeping the facade as composition/frame owner. |
| RS-3 | High | Partial initialization and manual teardown can produce invalid lifetime combinations. | `DeferredRenderer::Initialize()` rolls back its partial target set; cleanup retires renderer-owned state before resolver/backend cleanup. Focused ownership probes and dual-backend smoke pass. | Preserve the transactional initialization and ordered cleanup assertions in later Render stages. |
| RS-4 | Resolved by R1.4 | Render crossed the Asset/Resource preparation boundary and could stall a frame. | Runtime prepares and freezes the selected-API-validated catalog before Render initialization; Render has no path loading, AssetManager lookup, or Resource processing calls, and transaction tests cover failure publication. | Preserve the catalog boundary; future streaming must introduce a real ready-payload producer before changing Render. |
| RS-5 | Medium-high | Pass-private GPU state is stored on the global facade. | R1.2 moved fullscreen/shadow pipelines, samplers, environment bindings, active shadow frames, target state, and recording methods into `DeferredRenderer`. | Keep pass-specific state inside the renderer and retire it before resolver/backend teardown. |
| RS-6 | Medium-high | Renderer extraction can regress behavior that unit-level pass tests do not observe. | R1.2 preserves the orchestration test source, adds renderer-owned pipeline/mesh/sampler cleanup assertions, injects both fullscreen partial-creation failures, and passed focused execution, full CTest, dual-backend smoke, and fresh startup-level captures. | Keep deterministic captures and lifecycle/ownership assertions as regression coverage for later Render stages. |
| RS-7 | Resolved by R1.5 | The public facade mixed unrelated consumers and included an opaque native editor escape hatch. | Gameplay still takes stable source sinks; Editor now receives only `IEditorPresentationBridge` and a value `RenderTargetView`; `Tick`, `PostInitialize`, full-target access, and public `GraphicsContext` access are removed. | Keep API-specific downcasts inside Editor adapters and retain the bridge/view lifetime rules. |
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
6. Harden the facade and replace the native editor context seam through R1.5.
7. Close R1 only after focused, full, cross-backend, Editor lifecycle, and
   visual evidence—including the R1.4 comparator disposition—is recorded.

R1 source risks are addressed on 2026-09-04. Future work should treat the
coordinator, typed bridge, and value-view contracts as the baseline; the
RuntimeLib ↔ EditorLib cycle, Graphics-internal context helpers, and orderly
close-path evidence remain separate/open items.

Do not begin with a render graph, speculative renderer plugins, or a universal
context object. Those would increase surface area before the current ownership
risks are controlled.
