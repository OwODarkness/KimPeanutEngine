# Engine Host Mode Roadmap

**Status: MODE1.5 landed.** Detailed ownership and migration rules are in
[MODE1](.plan/MODE1.md). This roadmap covers only the first two hosts.

- [x] **MODE1.1 — host contracts:** define `ApplicationMode`, the neutral
  `IApplicationHost` lifecycle, and host-provider registration without
  changing the default scene behavior. The shared service view remains a
  later extraction from the existing RuntimeContext. →
  [MODE1.1 journal](../../.spec/journal/2026-09-11-engine-mode1-1.md)
- [x] **MODE1.2 — 3DSceneHost isolation:** add the Runtime-owned scene host
  lifecycle shell and make module composition mode-aware. `scene3d` keeps the
  existing Gameplay/RenderWorld/DeferredRenderer/editor path, while Live2D
  module and viewer-editor registration are excluded from that mode. The
  deeper RuntimeContext member extraction remains a later mechanical step. →
  [MODE1.2 journal](../../.spec/journal/2026-09-11-engine-mode1-2.md)
- [x] **MODE1.3 — Live2DViewerHost:** move Live2D asset/runtime/rendering and
  viewer presentation behind a standalone host with no RenderWorld or
  DeferredRenderer construction. → [MODE1.3 journal](../../.spec/journal/2026-09-11-engine-mode1-3.md)
- [x] **MODE1.4 — startup selection and viewer evidence:** add the validated
  launch policy for `scene3d` and `live2d-viewer`, reject incompatible
  scene-only options in viewer mode, and provide a standalone one-shot viewer
  capture path. → [MODE1.4 journal](../../.spec/journal/2026-09-11-engine-mode1-4.md)
- [x] **MODE1.5 — extension retirement:** remove the temporary Live2D
  registration seam from the scene `RenderSystem`; the standalone viewer now
  owns the renderer directly. → [MODE1.5 journal](../../.spec/journal/2026-09-11-engine-mode1-5.md)

## Acceptance ledger

- [x] Scene mode produces the existing deferred/PBR output and existing
  gameplay/editor smoke remains valid.
- [x] Viewer mode creates no `RenderWorld`, `DeferredRenderer`, level scene,
  scene viewport, outliner, inspector, or scene gizmo.
- [x] Viewer mode renders Hiyori on OpenGL and Vulkan, supports resize/capture,
  and shuts down without live GPU or Cubism objects. OpenGL and Vulkan startup
  capture evidence is landed in MODE1.4.
- [x] `RenderSystem` has no Live2D include, semantic, or registration branch
  after MODE1.5.
- [x] Shared services remain API-neutral and no host depends on backend-native
  types.
