# Render R4.1 — graph correctness prerequisites

## Objective

Close the correctness gap between the compiled render graph and frame
execution before adding the R4.2 Graphics ray-tracing contract. The graph must
preserve update lineage, resolve every active raster resource through an
explicit frame-local binding table, execute buffer state requirements through
the common command seam, and make required failures observable to the frame
and RenderSystem.

## Scope

- preserving and read-modify-write writes depend on the preceding version's
  producer;
- `Clear` is the explicit discard form and does not retain the prior producer
  dependency;
- texture and buffer transition intents are executed, with failures returned
  to pass/frame status;
- deferred raster resources are bound by exact graph handles in a frame-local
  table rather than by `RenderPassResource` ordinals;
- required recording failure reaches `DeferredRendererFrameResult` and the
  active graph frame while preserving the existing recoverable frame bracket;
  external binding/transition failure reaches `EndFrame`;
- focused graph lineage, buffer-intent, and execution tests.

## Invariants

1. A graph write creates a new version. A preserving write also consumes the
   producer of the previous version; a clear write explicitly discards it.
2. Render never names a native barrier or API object. Buffer requirements use
   the common `CommandRecorder` contract.
3. A required pass cannot succeed when any declared logical resource lacks a
   physical frame binding or when a backend rejects its state requirement.
4. The frame binding table is built after transient acquisition and is cleared
   after release. It is not persistent scene/material identity.
5. Vulkan owns buffer barriers and OpenGL accepts the same portable requirement
   through its implicit ordering model.

## Non-goals

R4.1 does not add acceleration-structure resources, RT pipelines, shader
stages, storage images, or graph-scheduled AS work. A graph buffer binding
provider for a future RT/imported-TLAS pass remains an R4.2/R4.3 concern.

## Acceptance

- [x] preserving/update write lineage is compiled into deterministic producer
  edges;
- [x] clear writes explicitly discard the previous version dependency;
- [x] missing physical bindings and failed texture/buffer requirements fail
  required recording/finalization;
- [x] active raster resources resolve through exact frame-local bindings;
- [x] buffer intents call `RequireBufferUsage` through the common seam;
- [x] focused tests and Vulkan/OpenGL smoke evidence pass.

## Follow-up

R4.2 may add the minimum effective capability and Graphics-owned acceleration
structure contracts. It must keep the binding table and failure propagation
rules intact when an imported TLAS is introduced.
