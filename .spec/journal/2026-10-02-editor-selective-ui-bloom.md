# Editor selective UI bloom — 2026-10-02

Status: in progress; implementation is present, but visual and performance
acceptance are still open.

## Changes so far

- Added copied public ImGui callback markers for bounded glow regions and
  emission styles, plus finalized marker-stream validation.
- Added a frame-packet conversion contract that rebases indexed geometry to
  32-bit indices and preserves command ordering, clipping, display origin, and
  framebuffer scale.
- Added a Tool-menu UI Glow switch and loading wire/status regions. The initial
  layered-geometry prototype has been replaced by explicit emission replay and
  filtered composition.
- Added a common active-frame bridge callback and portable sampled texture,
  format, and origin metadata on borrowed render-target views.
- Added an Editor-owned common ImGui RHI adapter with copied geometry, index
  rebasing, texture-origin correction, linear UI color composition, a
  half-resolution separable Gaussian filter, ordered halo composition, memory
  and geometry budgets, cached resource bindings, and native-renderer fallback.
- Added a selected World Outliner accent region and connected the global toggle
  to both loading and selection accents.
- Published the CPU shader catalog at a Render frame boundary and protected
  Editor reads/publication with the existing request mutex, so loading does not
  wait for scene promotion and the catalog remains available after publication.
- OpenGL now requests an sRGB-capable window and reports the actual default
  framebuffer color encoding through the common format contract.
- Updated ED5 status, architecture notes, roadmap, and active execution spec.

## Validation

- `cmake --build build --config Debug --target EditorUILib` — passed.
- `cmake --build build --config Debug` — passed, including app and test targets.
- A later full build initially failed at link because the visible editor was
  still holding `KimPeanutEngine.exe`; after closing the task-launched editor,
  the full Debug build passed again.
- Vulkan and OpenGL full-window captures were exported through the Runtime
  command bridge. Vulkan is readable; OpenGL UI colors are brighter than
  Vulkan's, so zero-emission parity is not accepted. The OpenGL default
  framebuffer query/request change did not yet close that difference.
- The level-reload command completed, but the corresponding loading-transition
  screenshot remained pending when the diagnostic instance was closed.
- Unit tests were built but not run. RelWithDebInfo performance measurements,
  selected-row visual confirmation, and resize/retirement stress remain open.

## Remaining work

- Diagnose and correct the OpenGL UI color mismatch, then compare matched
  Vulkan/OpenGL captures with the selected-row accent enabled.
- Confirm loading wire/status visibility during a loading transition and verify
  selected accents, clipping, later-UI occlusion, and global disable.
- Add marker/packet/lifecycle contracts and perform resize/retirement stress.
- Build RelWithDebInfo and record matched performance evidence after the visual
  parity and runtime paths pass.

## Follow-up: OpenGL viewport orientation (2026-10-02)

- Matched live Cornell Box captures showed the OpenGL viewport was vertically
  inverted while its standalone scene-color capture was upright. The wrapper
  already reversed V for ImGui's top-left UV convention, then the common adapter
  applied the `BottomLeft` correction again.
- Registered the OpenGL scene view with the adapter as already normalized to
  top-left UI coordinates. This preserves the wrapper's UV flip for both the
  common path and the native fallback without applying it twice.
- Full Debug build passed. A fresh visible OpenGL full-window capture confirms
  the ceiling light is at the top and the floor is at the bottom. Both OpenGL
  and Vulkan Cornell Box windows were reopened; Vulkan reports active path
  tracing while OpenGL reports it disabled because this backend does not expose
  the required ray-tracing pipeline capability.
- OpenGL UI color parity, loading-transition visibility, selected-row visual,
  resource-retirement stress, and RelWithDebInfo measurements remain open.
