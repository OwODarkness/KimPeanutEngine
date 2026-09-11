# Engine Host Mode Roadmap

**Status: MODE1.1 landed.** Detailed ownership and migration rules are in
[MODE1](.plan/MODE1.md). This roadmap covers only the first two hosts.

- [x] **MODE1.1 — host contracts:** define `ApplicationMode`, the neutral
  `IApplicationHost` lifecycle, and host-provider registration without
  changing the default scene behavior. The shared service view remains a
  later extraction from the existing RuntimeContext. →
  [MODE1.1 journal](../../.spec/journal/2026-09-11-engine-mode1-1.md)
- [ ] **MODE1.2 — 3DSceneHost extraction:** make the existing Gameplay,
  RenderSystem, RenderWorld, DeferredRenderer, and scene-editor attachment
  explicit members of the scene host.
- [ ] **MODE1.3 — Live2DViewerHost:** move Live2D asset/runtime/rendering and
  viewer presentation behind a standalone host with no RenderWorld or
  DeferredRenderer construction.
- [ ] **MODE1.4 — startup selection:** add the remaining validated
  launch/configuration policy for `scene3d` and `live2d-viewer`; reject
  incompatible scene-only options in viewer mode.
- [ ] **MODE1.5 — extension retirement:** remove Live2D registration from the
  scene `RenderSystem` after viewer-mode capture and shutdown evidence passes.

## Acceptance ledger

- [ ] Scene mode produces the existing deferred/PBR output and existing
  gameplay/editor smoke remains valid.
- [ ] Viewer mode creates no `RenderWorld`, `DeferredRenderer`, level scene,
  scene viewport, outliner, inspector, or scene gizmo.
- [ ] Viewer mode renders Hiyori on OpenGL and Vulkan, supports resize/capture,
  and shuts down without live GPU or Cubism objects.
- [ ] `RenderSystem` has no Live2D include, semantic, or registration branch
  after MODE1.5.
- [ ] Shared services remain API-neutral and no host depends on backend-native
  types.
