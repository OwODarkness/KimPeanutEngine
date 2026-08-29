# Render Deferred PBR

- Status: proposed
- Owner: user + Codex
- Parent TODO: [Mesh Proxy After MP3](../../docs/world/mesh_proxy_TODO.md#after-mp3)
- Design: [Deferred PBR Renderer Plan](../../docs/render/deferred_pbr_plan.md)
- Execution ledger: [Deferred PBR TODO](../../docs/render/deferred_pbr_TODO.md)

## Objective

Implement a real cross-backend renderer in stages: extensible directional/point/spot light and shadow identities, a first directional shadow depth job, opaque G-buffer, metallic-roughness deferred lighting, and tone mapping, with supplied PBR assets as the first acceptance content.

## Current state

RenderSystem has proxy-derived opaque draws, material instances, frame-local bindings, an ordered scene pass, cross-API scene target, and screenshot capture. Graphics lacks the attachment/read contract for a G-buffer; Material Asset V1 is still unlit.

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
2. Attachment/pipeline validation and preserved unlit baseline.
3. Cross-backend attachment-capable targets with safe resize/retirement.
4. Material Asset V2 and opaque PBR G-buffer using supplied assets.
5. Extensible light/shadow ABI plus one directional depth shadow producer and consumer.
6. Type-switching deferred lighting, tone mapping, debug views, and screenshot evidence.
7. Enable point/spot lighting, then typed spot/point shadow jobs.
8. Render-graph decision only from demonstrated dependency pressure.

## Acceptance criteria

- [ ] Shared RenderWorld data produces equivalent Vulkan/OpenGL PBR captures.
- [ ] Shadow, G-buffer, HDR, and final targets have tested resize/teardown.
- [ ] No native type leak or deprecated OpenGL ownership pattern returns.
- [ ] Validation and visual evidence meet the linked design.

## Validation plan

At minimum: Level 2 `GraphicsContractTest` and `RenderPassScheduleTest`, then Level 3 `GraphicsSmoke` and screenshots on both APIs. Record commands, results, captures, and risk in `.spec/journal/render-deferred-pbr.md` when work begins, pauses, or completes.

## Risks and open questions

Attachment formats, tangent availability, directional-shadow fitting, and HDR tone-map policy are implementation gates. Resolve them from current source and targeted evidence, not the deprecated renderer.
