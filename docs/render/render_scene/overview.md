# Render Scene Overview

`RenderScene` is the current Render-owned scene-recording boundary. It owns
logical camera/view state and records draws using common Render/Graphics
contracts; it borrows resolved static resources and must not own their GPU
lifetime.

Its inputs come from `RenderSystem`/RenderWorld policy, while Graphics owns
native command buffers and transient GPU allocation. See the parent
[design](../design.md) and [lifecycle](../lifecycle.md); legacy scene detail is
retained in [render_module.md](../render_module.md).
