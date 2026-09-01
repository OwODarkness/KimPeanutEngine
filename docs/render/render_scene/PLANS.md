# Render Scene Plans

**Status:** active
**Parent module:** [Render module plans](../PLANS.md)
**Overview:** [Render Scene overview](overview.md)

This submodule defines the Render-owned scene-recording boundary. Concrete
stage design lives in [`.plan/`](.plan/); current work lives in [TODO](TODO.md).

## Architecture

`RenderScene` consumes Render policy, immutable scene snapshots, resolved
common resource handles, and `FrameContext` bindings. It records through the
API-neutral command contract. It does not own Asset identity, persistent GPU
resources, native command buffers, or backend-specific scene state.

```text
RenderSystem / RenderWorld policy
        ↓
RenderScene + FrameContext + common handles
        ↓
CommandRecorder contract
        ↓
Vulkan/OpenGL backend execution
```

Graphics owns native command buffers, API translation, synchronization, and
GPU lifetime. Legacy scene content must not return to the RHI backend.

## Stage plans

- [S1 — common scene-recording boundary](.plan/S1.md)
