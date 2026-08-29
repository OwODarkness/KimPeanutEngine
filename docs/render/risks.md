# Render Risks and Limits

- The explicit pass schedule is intentionally smaller than a general render
  graph. Add graph machinery only after a measured multi-pass/resource-lifetime
  need.
- Render capture currently exposes SceneColor; extra debug views require real
  producer passes and readback support, not just new command names.
- Capture is a developer-debug path. Deterministic scene setup and image
  comparison are still needed for visual regression testing.
- Resource loading is currently budgeted by the render path; a dedicated loading
  worker remains future work and must preserve Asset/Resource/Render ownership.
- Material and source resolution may be pending or failed. A proxy must not
  draw with invalid/private stale resource handles.
- Any backend-specific scene recording or presentation seam must be pushed down
  behind common Render/Graphics contracts before it spreads to callers.
