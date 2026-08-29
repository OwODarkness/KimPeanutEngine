# Scene Render-Target Capture TODO

Working ledger for the design in [render_capture.md](render_capture.md).

## C1 — render-capture service and pixel callback policy

- [x] Define a narrow public `IRenderCaptureService` that completes with owned
  `CapturedImage` pixels; it has no output path or image-format parameter.
- [x] Implement it as render-private `RenderCaptureService`, owned by
  `RenderSystem`. RenderSystem supplies scheduled frame inputs and service
  lifetime only; it does not implement capture mechanics.
- [x] Add `IRenderCaptureService::RequestCapture(CaptureRequest,
  CapturedImageCallback)`. Permit one pending capture and return `false` only
  when the callback is empty or another SceneColor capture is pending.
- [x] Define internal completion/failure delivery so the future GPU readback
  seam invokes an accepted request's callback exactly once with an owned image
  or diagnostic; do not publish a request handle or polling API.
- [x] Add a semantic `CaptureView` selector. Reserve SceneColor, LinearDepth,
  WorldNormal, BaseColor, MaterialParams, and ShadowVisibility; implement
  SceneColor only.
- [x] Return an explicit `Unavailable` result for reserved but unimplemented
  debug views; never substitute SceneColor silently.

**Done when:** callers can request pixels without knowing a backend, native
image handle, renderer facade, output path, or job identity, and every accepted
request receives one success/failure callback. **Landed 2026-08-28; actual
SceneColor readback completion is C2/C3 work.**

## C1.4 — focused ImageIO module

- [x] Create an `ImageIO` Runtime target that depends only on Core/Base and
  image codec libraries. Do not make a broad UniversalIO module.
- [x] Define its CPU `ImageBuffer`/format/result types and narrow decode/encode
  APIs. ImageIO owns no asset identity, output-path policy, render-target
  handle, frame, or GPU-resource type.
- [x] Move the existing stb-based source-image decode behind ImageIO, while
  Asset retains `TextureResource`, `AssetRegisterInfo`, and the conversion to
  `data::TextureData`/texture-format policy.
- [x] Add lossless RGBA8 PNG output for RuntimeScreenshotService. Keep HDR/EXR
  and additional codecs deferred until they have consumers.
- [x] Unit-test known decode fixtures and PNG encode/decode round trips without
  loading assets or creating a graphics backend.
- [x] Keep the public ImageIO API codec-neutral behind a private `IImageCodec`
  seam; the current stb adapter is replaceable without changing Asset or
  Runtime callers.

**Done when:** Asset texture loading and screenshot export share codec logic,
while their asset and runtime-output policies remain independent. **Landed
2026-08-28.**

## C1.5 — runtime screenshot export adapter

- [x] Define `RuntimeScreenshotService` outside Render. It composes
  `IRenderCaptureService` with ImageIO's reusable PNG writer.
- [x] Generate UTC timestamped default PNG paths below `save/screenshots/` and
  create the directory on demand.
- [x] Permit an explicit test output only below `save/screenshots/validation/`;
  reject absolute paths and traversal outside the capture root.
- [x] Move path containment validation, filename collision handling, and final
  file-export callbacks into this service.
- [x] Expose `RequestScreenshot` to RuntimeContext, tests, editor tooling,
  scripting, and future agents; it completes with file-export success/failure.
- [x] Keep the file writer reusable and independent of render targets, frames,
  `CaptureView`, and Graphics.
- [x] Add unit coverage for output-path shape, collisions, containment, and
  export error diagnostics.

**Done when:** Render can be used for in-memory visual analysis without any
filesystem dependency, while screenshot callers get the requested PNG export.
**Landed 2026-08-28; GPU-backed SceneColor completion remains C2/C3.**

## C2 — narrow common-RHI readback contract

- [x] Define an owned CPU `CapturedImage` (extent, tightly packed RGBA8 pixels,
  submission/frame provenance) and backend-private readback state.
- [x] Add internal enqueue/collect/drain operations for a render target's
  completed color attachment; do not expose generic native texture access or
  a public readback handle.
- [x] Specify resize and shutdown behavior: requests retain their original
  attachment until completion or fail before that attachment is destroyed.
- [x] Add contract tests for invalid target requests and readback state
  transitions where they can run without a GPU; backend stale-handle checks
  remain C3 implementation work.

**Done when:** Render has one API-neutral readback seam and Graphics remains
the owner of GPU synchronization and staging lifetime. **Landed 2026-08-28;
C3 still attaches the contract to Vulkan and OpenGL.**

## C3 — Vulkan and OpenGL implementations

- [x] Vulkan: have `VulkanBackend` implement the common
  `IRenderTargetReadback` interface and expose it through the common backend
  facade. It delegates native target lookup, image transitions, staging-buffer
  ownership, and mapping to a Vulkan-private readback/target manager.
- [x] Vulkan: transition/copy SceneColor into backend-private host-visible
  readback storage, associate it with the submitted frame serial, and normalize
  to RGBA8 only after completion. `VulkanBackend` collects only after the
  corresponding frame fence completes, and drains callbacks before resize or
  shutdown destroys a referenced target.
- [x] OpenGL: issue the equivalent framebuffer/texture readback into a
  backend-private pixel pack buffer or synchronous fallback, with the same
  ownership and result semantics. `OpenglBackend` implements the common
  interface and delegates to `OpenglRenderTargetReadback`, which performs a
  synchronous `glGetTextureSubImage` read at the next frame boundary.
- [x] Keep queue/fence, image-layout, framebuffer, and native pixel-format
  details private to each backend.
- [x] Verify capture after resize and normal backend cleanup. `GraphicsSmoke`
  now requests a post-resize SceneColor capture on both APIs, drains frames
  until the completion callback, and validates owned RGBA8 image metadata and
  non-uniform pixels; the smoke passes on Vulkan and OpenGL.

**Vulkan and OpenGL landed 2026-08-28.** Vulkan records SceneColor
image-to-staging copies before submission, completes callbacks after the
corresponding frame fence, and cancels target-specific work before attachment
destruction. OpenGL performs the equivalent synchronous texture read into
backend-private CPU storage with the same ownership and result semantics. The
common readback contract now returns equivalent CPU image metadata on both
APIs; visual PNG smoke evidence remains C4.

**Done when:** the same logical capture request returns equivalent CPU image
metadata on both APIs.

## C1.6 — debug-view resolver (after C2/C3 and producer passes)

- [ ] Add a render-owned resolver from `CaptureView` to a completed visual
  color target or a short debug visualization pass.
- [ ] Convert depth, normals, packed material data, and shadow factors to
  documented RGBA8 PNG views before readback; do not export raw attachment
  bytes through the screenshot API.
- [ ] Keep debug-target lifetime and pass dependencies render-owned, with the
  same resize and submission-safety rules as SceneColor.
- [ ] Add one visually distinct test fixture for each newly enabled view.

**Done when:** adding a new diagnostic image changes render policy and shaders,
not the common RHI readback API or agent-facing file format.

## C4 — runtime PNG export and visual smoke evidence

- [x] `RuntimeScreenshotService` encodes a completed `CapturedImage` as a
  lossless PNG; file I/O and compression remain outside Render. It will receive
  GPU-backed completions after C2/C3 land.
- [x] Extend `GraphicsSmoke` to render a stable frame, request a deterministic
  `save/screenshots/validation/graphics-smoke-<api>.png` capture, pump frames
  until its completion callback, and validate PNG signature, dimensions, and
  non-empty pixels.
- [x] Run and manually inspect the Vulkan and OpenGL captures.
- [x] Add the generated `save/` directory to Git ignore rules; never commit
  captures, baselines, or temporary reports.

**Landed 2026-08-28.** `GraphicsSmoke` routes the post-resize SceneColor
request through `RuntimeScreenshotService::RequestScreenshot` with an explicit
validation path, pumps frames until the export callback, and validates the
written PNG on disk: signature, IHDR extent, a decode round trip, and non-uniform
pixels. The `save/` tree is git-ignored. Both backends produce a visually
inspectable 1600x1024 SceneColor PNG and the smoke target fails if capture
cannot complete.

**Done when:** each backend produces a visually inspectable SceneColor PNG and
the smoke target fails if capture cannot complete.

## Deferred follow-ups

- [ ] Shadow-map depth and visibility visualization through explicit render
  conversion passes, not raw D32 export.
- [ ] G-buffer normal, base-color, and material-parameter visualization.
- [ ] HDR/EXR export, cropped captures, and editor-including screenshots.
- [ ] Golden images, tolerance/diff metrics, reports, and CI baselines.
- [ ] Video/GIF recording and asynchronous multi-frame capture queues.
