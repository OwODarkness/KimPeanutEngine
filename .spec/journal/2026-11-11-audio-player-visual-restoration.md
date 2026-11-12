# Audio Player visual restoration — 2026-11-11

- Operation and acceptance: restore the reference image's dock tabs, profiler strip, compact library and queue, centered playback scope/waveform/subtitles, transport, and spectrum. Verify an empty player and live playback in the visible Debug build.
- Candidates inspected: `598f75e` and `HEAD` Audio Player source; `AudioPlayerEditor::RenderWorkspace`; `EditorToolRowComponent::RenderWithContext`, `EditorLayoutModel::ResetToThreeColumnTwoRowDefault`, and `EditorProfileBarComponent`; neutral controls and canvas in `UiContext` and its inline backend.
- Placement: reuse the previous docking, resizing and profiler owners via the existing host seam. Keep Audio Player panels, style, signal geometry and action mapping in `engine/editor/audio`. Extend the neutral layout and presentation operations used by the panels in the existing UI API and adapter.
- Boundary: preserve the controller, playback lifecycle and feature target dependencies. The shared shell is the existing compatibility host; panel code receives `UiContext` and no toolkit state. Placement check passed before editing.

## Implementation and checks

- Reused the existing layout model, dock/tab model, splitter handles and profiler through a toolkit-free `EditorDockedWorkspace` facade. Feature callbacks own their themes and drawing; the facade owns the compatibility hosts and their destruction. No new Runtime dependency or GPU lifetime was introduced.
- Restored panel tables, source/format/output inspector, radial scope, 18-second waveform, subtitle canvas, progress/volume bars and transport icons. Signal plotting and playback action translation stay local to `audio/editor`.
- Added neutral table/layout operations, control sizes/tints and borrowed CPU icon masks. `audio_transport_icons.h` no longer includes the legacy ImGui transport header; existing TTS transport retains its compatibility adapter.
- `cmake --build build --config Debug --target KimPeanutEngine -- /m:1`: passed. Initial compile exposed an accidental legacy ImGui header dependency, corrected with the facade, and an iterator/pointer comparison, corrected locally. MSVC 14.34 C1001 inside table lambdas was avoided by constructing named descriptors outside function arguments.
- `cmake --build build --config Debug --target UiRecordingContractTest EditorLayoutModelUnitTest EditorToolRowModelUnitTest AudioUnitTest -- /m:1`: passed. Targeted editor CTest selection passed 46/46; audio label passed 27/27.
- Debug Vulkan window verified as visible `GLFW30`, desktop `Default`, foreground equality true. Empty capture saved as `save/screenshots/validation/audio-visual-restoration-vulkan-empty.png`. Command observations confirmed subtitle attachment, empty playback error and elapsed time advancing 35.742 → 36.297 seconds. User confirmed the restored interface.
- Multi-file import exceeded the initial 35-second wait while the project catalog scanned; it later progressed. A subsequent capture command reported export success but the file was absent, so that playback capture is not accepted evidence. OpenGL, full-suite, keyboard coverage and final alignment changes' runtime check remain unverified. No performance acceptance is claimed.

## 2026-11-12 RMS scope reconstruction

- Operation: restore the radial RMS/spectrum motif from the supplied reference, one visual area at a time.
- Candidates inspected: current `engine/module/audio/editor/audio_preview_widget.cpp` and the pre-neutral-UI implementation at `70ff69e^:engine/runtime/audio/editor/audio_preview_widget.cpp`.
- Placement: port the historical ring counts, radii, progress dots, 64-bin spoke sampling, dot spacing, accent threshold, and RMS label into Audio's existing Canvas drawing. Keep the rendering API and palette neutral/general; do not change the Spectrum panel in this pass.
- Change: removed the extra rings and per-dot emission that made the earlier reconstruction crowded. Radial marks remain crisp like the historical renderer; waveform emission stays on its separate primitives.

## 2026-11-12 Bloom and scrolling reconstruction

- Candidates inspected: `70ff69e^:engine/runtime/audio/editor/audio_preview_widget.cpp` for waveform fade, pulse and Bloom; `70ff69e^:engine/editor/ui/audio_transport_strip.cpp` for the 8px progress Bloom; `70ff69e^:engine/runtime/audio/editor/audio_player_editor.cpp` for spectrum bars, gaps and peak accent.
- Placement: keep scroll timing, grid, fade, pulse and Audio colors in `module/audio/editor`; keep Plot geometry and emission markers in the general UI API/backend.
- Change: restored the historical 8px glow radius, 0.55 waveform/bin emission and 1.0 playhead/progress emission. Removed wide translucent overdraw layers. Reinstated the dotted waveform grid, 40px axis fade, playhead-local animation, separated spectrum bars and a single amber peak bin.
- Validation: Debug AudioPlayerEditorUI and KimPeanutEngine targets compile. Visible Vulkan command capture at 02:39.1 shows the reference waveform and radial scope; current measured RMS is 0.279 (displayed 28), while the reference image shows 31. The rightmost Plot axis label still needed a generic edge-anchor correction during the same pass; no CTest run.
