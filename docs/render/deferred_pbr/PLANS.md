# Deferred PBR Renderer Plans

**Status:** active (D0–D7 validation/evidence landed; future shadow alternatives remain deferred)
**Parent module:** [Render module plans](../PLANS.md)
**Roadmap:** [Deferred PBR TODO](TODO.md)
**Execution spec:** [render-deferred-pbr](../../../.spec/specs/render-deferred-pbr.md)
**Journal:** [Render Deferred PBR](../../../.spec/journal/render-deferred-pbr.md)

This document defines the deferred-PBR submodule architecture and ownership
policy. It does not contain the concrete D0–D7 implementation designs; those
are split into the [stage plans](.plan/).

## Objective

Provide the first cross-backend opaque PBR renderer: value-only Gameplay light
sources, Render-owned immutable light snapshots, a stable G-buffer, typed
shadows, deferred HDR lighting, tone mapping, semantic debug capture, and
environment lighting. The renderer extends the existing Render → Graphics
boundary without reviving the deprecated backend-owned scene.

## Architecture

```text
Gameplay light/material source values
                ↓
Render source registries and frame snapshots
                ↓
RenderWorld + material policy + ordered pass schedule
                ↓
logical targets + PipelineDesc + FrameContext bindings
                ↓
Graphics/RHI allocation, synchronization, and API execution
```

The initial schedule is explicit and ordered:

```text
ShadowDepth → GBuffer → DeferredLighting → ToneMap → EditorComposite
```

Diagnostic capture is a Render conversion/readback path layered onto this
schedule. A render graph is intentionally deferred until measured dependency,
aliasing, or scheduling pressure justifies it.

## Ownership and lifetime

- Asset owns source identity, decoding, and CPU-side material/environment data.
- Resource converts CPU assets into render-ready artifacts and owns no GPU
  objects.
- Render owns light selection, material interpretation, pass dependencies,
  logical target policy, frame-local bindings, and immutable snapshots.
- Graphics/RHI owns GPU resources, attachment transitions, command encoding,
  synchronization, and safe retirement.
- Gameplay publishes copied authored values and retains only opaque source
  registration tokens. It never receives `LightHandle`, `ShadowHandle`,
  `TextureHandle`, targets, descriptors, or backend objects.

## Stable contracts

- Light and shadow data are typed, versioned, bounded, and copied at the frame
  boundary. Missing, stale, or incompatible shadow identities resolve to an
  explicit unshadowed state.
- Material and G-buffer policy stays in Render. Common Graphics descriptions
  expose formats, attachments, and handles but no Vulkan/OpenGL values.
- Frame targets are a render-private named set. Graphics allocates and retires
  the physical resources; Render does not become a transient graph allocator.
- The first renderer is single-sample. Sampled-MSAA resolve semantics are a
  separate contract and must not be implied by this module.
- Capture requests name semantic views such as `SceneColor`, `LinearDepth`,
  `WorldNormal`, `BaseColor`, `MaterialParams`, and `ShadowVisibility`. Render
  converts non-color sources to an owned debug target before Graphics readback.

## Stage plans

- [D0 — prerequisites](.plan/D0.md)
- [D1 — light and shadow ABI](.plan/D1.md)
- [D2 — attachment contract](.plan/D2.md)
- [D3 — Material V2 and G-buffer](.plan/D3.md)
- [D4 — directional shadows](.plan/D4.md)
- [D5 — deferred lighting and presentation](.plan/D5.md)
- [D6 — light and shadow expansion](.plan/D6.md)
- [D6.4 — bounded point-light shadows](.plan/D6.4.md)
- [D7 — evidence and handoff](.plan/D7.md)

## Non-goals

- No Asset-to-GPU shortcut, backend-specific material path, or native type leak.
- No transparent/decal/terrain/hair material graph in this renderer slice.
- No cascades, shadow atlases, clustered/forward+ lighting, or graph-driven
  aliasing without measured pressure and a separate design decision.
