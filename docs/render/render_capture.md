# Scene Render-Target Capture

**Status:** planned.  This document defines the first visual-evidence slice
before shadow, G-buffer, and deferred PBR work.

## Problem and acceptance criteria

`GraphicsSmoke` currently establishes that Vulkan and OpenGL can render and
shut down without an error, but it cannot show whether a render result is
correct.  The engine already renders ScenePass into a render-owned offscreen
`scene_render_target_`; capture must make that image available as a lossless
PNG without coupling Render to Vulkan, OpenGL, ImGui, or swapchain presentation.

The first milestone is complete when both backends can capture the completed
SceneColor attachment for a selected frame into a valid PNG, normal captures
default to `save/screenshots/<UTC timestamp>-f<frame>.png`, and a smoke target
can request a deterministic output path for visual inspection. Captures must
survive normal resize and shutdown without reading a recycled attachment.

## Scope and non-goals

The first implemented capture view is SceneColor, after `ScenePass` completes
and before `EditorCompositePass`.  It deliberately excludes ImGui and the
swapchain, so results are stable across editor layouts and presentation paths.

It is not yet a generic texture downloader, raw depth capture, HDR export,
video recording, image-difference test framework, or a render graph. Shadow
and G-buffer stages will add explicit debug views or conversion passes before
their depth/packed attachments are captured; they must not force raw backend
texture objects into Render.

## Extensible debug views

`CaptureRequest` should select a render-semantic `CaptureView`, rather than a
texture handle or attachment index. The initial enum reserves these names, but
only `SceneColor` is implemented in the first milestone:

| Capture view | Future producer | Visual output contract |
| --- | --- | --- |
| `SceneColor` | scene/deferred lighting | tonemapped sRGB RGBA8 |
| `LinearDepth` | depth-debug conversion pass | near-to-far grayscale RGBA8 |
| `WorldNormal` | normal/G-buffer debug pass | remapped `normal * 0.5 + 0.5` RGBA8 |
| `BaseColor` | G-buffer debug pass | sRGB RGBA8 |
| `MaterialParams` | G-buffer debug pass | documented packed visual RGBA8 |
| `ShadowVisibility` | lighting/shadow-debug pass | lit/shadow factor grayscale RGBA8 |

Render resolves a requested view to a completed color target. If its native
source is D32, HDR, packed normal, or a multi-channel G-buffer attachment,
Render schedules a small visualization pass that converts it to a dedicated
RGBA8 debug target. The capture subsystem then reads that target using the
same path as SceneColor. This gives agents and humans meaningful PNGs while
keeping format interpretation, tone mapping, and pass policy in Render.

The initial enum is a vocabulary, not a promise that every view exists. A
request for an unsupported view fails with an explicit `Unavailable` result;
it must never silently capture SceneColor instead. Avoid an `AttachmentN` or
`TextureHandle` escape hatch: those would make capture callers depend on a
specific deferred layout and leak RHI resource policy upward.

## Service boundary and public request contract

`RenderCaptureService` is a dedicated render-module service. `RenderSystem`
creates and destroys it with the renderer, supplies its frame-boundary inputs,
and invokes it at the scheduled capture point; it does not own capture request
state, output paths, file encoding, filesystem I/O, or callbacks beyond
delivering the captured pixels.

The render surface is a small `IRenderCaptureService`, which provides completed
CPU images without exposing `RenderSystem`, `RenderBackend`, or native API
objects. Its first API is callback-based and permits one pending capture:

```cpp
class IRenderCaptureService {
public:
    virtual bool RequestCapture(
        CaptureRequest request,
        CapturedImageCallback on_completed) = 0;
};
```

`false` means Render cannot accept the request—for example, because the
callback is empty or another SceneColor capture is already pending. A reserved
but unimplemented view is accepted and completes its callback immediately with
an explicit `Unavailable` diagnostic. Once a SceneColor request is accepted,
Render invokes `on_completed` exactly once after GPU readback succeeds or
fails. The success value contains an owned `CapturedImage`; failure contains a
diagnostic string.

The public API intentionally exposes no request handle and offers no polling.
GPU work completes after frame submission, so an immediate final result would
need to block the render thread. The callback preserves that asynchronous
lifetime without making callers manage a job identity. Multiple concurrent,
cancellable, or UI-listed capture jobs are deferred; they may introduce an
explicit job model later if they gain a real consumer.

`RuntimeScreenshotService` is a separate Runtime/application-facing adapter.
It owns the default `save/screenshots/` policy, explicit-output-path validation,
filesystem I/O, and the final `ScreenshotResult` callback. It requests
`CapturedImage` from `IRenderCaptureService` and passes its CPU pixels to the
separate `ImageIO` module for PNG export. Editor controls, scripts, agent
tooling, and `GraphicsSmoke` use this runtime screenshot service when they want
a file. A caller that only needs pixels—for example an in-memory image
comparison—uses `IRenderCaptureService` directly. Neither service belongs in
`Graphics`.

`ImageIO` is a focused Runtime module, depending only on Core/Base and codec
libraries. It owns CPU image-buffer types, image decode, and image encoding;
it does not own asset identity, screenshot naming, render views, frames, or GPU
resources. Asset uses it to decode source files, then maps the result into its
asset-owned `data::TextureData` and texture-format policy. Runtime screenshot
export uses its PNG writer. This gives ImageIO two concrete consumers without
turning Asset into a generated-output owner or making a broad “universal I/O”
module.

## Ownership and data flow

```text
tool / GraphicsSmoke
    -> RuntimeScreenshotService::RequestScreenshot(request, completion callback)
    -> IRenderCaptureService::RequestCapture(request, captured-image callback)
    -> RenderCaptureService resolves the view to a completed visual color target
    -> RenderSystem calls the service at the scheduled frame boundary
    -> RenderBackend schedules API-private image -> readback-buffer copy
    -> backend completion tracking makes CPU pixels safe to consume
    -> RuntimeScreenshotService encodes PNG, writes it, and invokes callback
```

- **RenderCaptureService** owns request policy, semantic view selection/debug
  conversion, and `CapturedImage` completion. It does not name an output path
  or invoke an image encoder.
- **RenderSystem** owns service lifetime and pass scheduling only. It supplies
  the completed target/frame context at the correct point, then lets the
  service progress pending work; it does not implement capture mechanics.
- **RuntimeScreenshotService** owns default path generation, filename collision
  handling, path containment validation, lossless PNG encoding, filesystem I/O,
  and the file-export result callback. It delegates image encoding to ImageIO.
  Timestamp filenames use UTC and include the submitted frame number.
- **ImageIO** owns codec-specific CPU image decode/encode and codec-library
  dependencies. It has no Asset, Render, Graphics, or Runtime dependency.
- **Graphics/RHI** owns render-target attachment access, image-layout/state
  transitions, staging/readback allocation, submission lifetime, synchronization,
  row-pitch normalization, and API pixel-format conversion to a documented CPU
  image format.  Its common contract names only handles and CPU image data.
- **Vulkan/OpenGL** implement the copy/readback privately.  No render or test
  code names `Vk*`, `gl*`, framebuffer IDs, image views, or swapchain images.
The initial common-RHI operation should be narrow: `RenderCaptureService`
enqueues a readback for a `RenderTargetHandle`'s color attachment while that
target is active or has just finished recording, then internally collects an
owned `CapturedImage` once its submission is complete. `CapturedImage` contains
width, height, tightly packed RGBA8 pixels, and the submission/frame provenance
needed for diagnostics. It does not expose a raw texture, mapped backend memory,
or public readback handle.

The implementation must pin the target/readback state until the relevant
submission has completed.  Resize and shutdown either finish pending captures
or fail them explicitly before attachment destruction; silently reading a
replacement target is invalid.

## Request policy

`RuntimeScreenshotService` creates `save/screenshots/` on demand and writes:

```text
save/screenshots/20260828-093015-123-f42.png
```

The timestamp is UTC, and an in-process sequence suffix is added if the clock
and frame number collide.  The `save/` tree is generated local output and is
ignored by Git.

Tests and agent workflows pass an explicit relative path below
`save/screenshots/validation/`, such as
`save/screenshots/validation/graphics-smoke-vulkan.png`.  Explicit paths are
validated to remain within `save/screenshots/`; capture must not become an
arbitrary file-write API.  A failed request, empty extent, unsupported format,
GPU-copy failure, PNG-encode failure, or output failure returns a diagnostic
result and leaves no success claim.

## Reference findings

gkNextEngine has a dedicated screenshot service that accepts a capture request
with completion callbacks separate from the renderer, performs a GPU readback,
and moves conversion and file encoding out of the render path for interactive
captures. Its validation workflow uses a deterministic screenshot output and
synchronous completion when the process must exit immediately. See its
[`IScreenShotService`](https://github.com/gameknife/gkNextEngine/blob/main/src/Engine/Runtime/Interface/ScreenShotService.hpp)
and
[`ScreenShotExport.cpp`](https://github.com/gameknife/gkNextEngine/blob/main/src/Modules/NextCapture/ScreenShotExport.cpp).

That separation transfers directly: KimPeanutEngine also needs GPU completion
before CPU encoding and a stable test artifact.  Its Vulkan-only swapchain
capture does **not** transfer directly, because KimPeanutEngine has a common
RHI and already owns a cross-API SceneColor target.  Therefore the initial
source is the render target, not the presented window.

## Validation

1. Headless/unit tests cover default-path shape, explicit-path containment,
   collision behavior, request state transitions, and PNG encoding of known
   RGBA pixels.
2. `GraphicsSmoke` renders the existing scene, requests one capture after a
   stable frame, waits for completion, and verifies the PNG signature,
   dimensions, and non-empty pixels for Vulkan and OpenGL.
3. Manual visual review opens the two saved PNGs.  This is required before the
   capture feature is marked complete because image correctness is not proven
   by a file signature alone.
4. The smoke path repeats a resize before capture and exercises teardown with
   no pending readback allocations or use-after-destroy diagnostics.

Pixel baselines and tolerance-based comparison become a follow-up only after
the first captures establish a trustworthy artifact.
