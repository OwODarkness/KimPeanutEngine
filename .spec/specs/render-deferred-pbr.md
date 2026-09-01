# Render Deferred PBR

- Status: active
- Owner: user + Codex
- Parent roadmap: [Deferred PBR Renderer Roadmap](../../docs/render/deferred_pbr/TODO.md)
- Design: [Deferred PBR Renderer Plans](../../docs/render/deferred_pbr/PLANS.md)
- D6 stage design: [Light and shadow expansion](../../docs/render/deferred_pbr/.plan/D6.md)
- D6.4 stage design: [Bounded point-light shadows](../../docs/render/deferred_pbr/.plan/D6.4.md)
- Journal: [Render Deferred PBR](../journal/render-deferred-pbr.md)

## Objective

Implement a real cross-backend renderer in stages: extensible directional/point/spot light and shadow identities, a first directional shadow depth job, opaque G-buffer, metallic-roughness deferred lighting, and tone mapping, with supplied PBR assets as the first acceptance content.

## Current state

The staged renderer now has the attachment-capable cross-backend target
contract, Material Asset V2 G-buffer, directional shadow producer/consumer,
deferred HDR lighting, runtime diagnostic capture, environment IBL, and
unshadowed point/spot lighting, the bounded one-spot `Spot2D` producer and
consumer, and a fixed six-face 2D-atlas `PointCube` producer/consumer.
Point-shadow runtime capture inspection, the fixed-atlas baseline profile, and
the D7 evidence handoff are complete. D6.4 provides typed point-depth/visibility
capture views; the temporary point-only fixture was restored to the normal
bootstrap lighting after capture.

## Scope and non-goals

The linked design is authoritative. This does not revive deprecated OpenGL or create a generic graph. Point/spot light and shadow ABI support is required from the first implementation; their shadow producers, cascades, and IBL wait for owned data paths and a later stage.

## Invariants

- Asset owns CPU asset identity/lifetime; Render does not load arbitrary files.
- Gameplay owns LightActor/component authoring state and publishes value-only
  light source updates. Render resolves them at the frame boundary into private
  LightWorld snapshots, then owns light selection, pass policy, material
  interpretation, and logical target-set lifetime.
- Graphics owns GPU allocation, synchronization, native translation, and safe destruction; common APIs expose no OpenGL/Vulkan values.
- Passes consume immutable RenderWorld snapshots and FrameContext bindings.

## Stages

1. [x] [Split backend scheduling from command recording](../../docs/graphics/command_recording_ownership_plan.md) (2026-08-29).
2. [x] Attachment/pipeline validation and preserved unlit baseline.
3. [x] Cross-backend attachment-capable targets with safe resize/retirement.
4. [x] Material Asset V2 and opaque PBR G-buffer using supplied assets.
5. [x] Extensible light/shadow ABI plus one directional depth shadow producer and consumer.
6. [x] Type-switching deferred lighting, tone mapping, debug views, screenshot evidence, and environment IBL.
7. [x] Unshadowed point/spot lighting; [x] one fixed-budget typed `Spot2D`
   producer/consumer; [x] one fixed-budget `PointCube` represented by a six-face
   2D depth atlas; [x] profile baseline before multiple punctual jobs or true
   cube targets.
8. [ ] Render-graph decision only from demonstrated dependency pressure.

## Acceptance criteria

- [x] Shared RenderWorld data produces equivalent Vulkan/OpenGL PBR captures.
- [x] Shadow, G-buffer, HDR, and final targets have tested resize/teardown.
- [x] No native type leak or deprecated OpenGL ownership pattern returns.
- [x] Validation and visual evidence meet the linked design.

## Validation plan

At minimum: Level 2 `GraphicsContractTest` and `RenderPassScheduleTest`, then Level 3 `GraphicsSmoke` and screenshots on both APIs. Record commands, results, captures, and risk in `.spec/journal/render-deferred-pbr.md` when work begins, pauses, or completes.

## Risks and open questions

The fixed six-face atlas is now the fixed-budget baseline for future
shadow-resource decisions. Numeric warm-runtime profile samples are recorded;
GPU timing remains unavailable until timestamp-query support exists. The
follow-up evidence closed the true-cube decision without implementation;
multiple punctual jobs and a render-graph decision remain intentionally
deferred.
The current cube texture path does not provide a correct cross-backend
cube-face target contract, so the first `PointCube` uses one fixed six-face 2D
depth atlas without changing the common recorder. Face orientation, atlas-tile
mapping, descriptor layout, face-edge filtering, and the six-pass cost require
explicit unit and runtime evidence. True cube resources, multiple punctual
maps, and render-graph scheduling remain later measured decisions.
