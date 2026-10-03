# Standalone TTS editor acceptance

Architecture: [PLANS.md](PLANS.md). First implementation design:
[TE1](.plan/TE1.md). Parent roadmap: [../TODO.md](../TODO.md).

## TE1 — usable standalone mode

- [x] Add `--mode tts`, register a `TtsEditorHost`, and build UI source from
  `engine/module/tts/editor/` without linking Editor into the core TTS target.
- [x] On first start, create `project_root/config/tts/settings.json`; on every start,
  load and validate `address`, `port`, `ref_audio_path`, and `ref_text` from it.
  Generate stays disabled with a specific reason until required values are set.
  Invalid JSON is retained, reported, and never overwritten automatically.
- [x] Edit and save connection, reference voice/text, languages, and output
  preference through settings; changed connection values take effect through
  an orderly provider reinitialize, without restarting the engine.
- [x] Read `playback_mode` (`stream` or `buffer`) from settings and let the UI
  change it. New requests map it to `TTSRequest::streaming`; each existing row
  retains its original mode and corresponding control panel.
- [x] Generate from UTF-8 multiline input using `AsyncSynthesize`; show queue,
  network, playback, cancel, and error states without blocking ImGui.
- [x] Keep a session dialog list with stable IDs, selection, source text and
  actual job state; new generation and cancellation cannot resurrect a prior
  voice or leave a stale player handle.
- [x] Show truthful Speech preview: played elapsed time, unknown duration for
  streams, pause/resume, stop/cancel distinction, and voice volume. Hide or
  disable seek/rate controls for a stream; show known duration, seek, and
  preview playback rate for a buffered clip. Do not fabricate waveform or bit
  depth from the draft.
- [x] Add host commands for status, generate, cancel, and settings inspection
  without exposing reference text in ordinary status output, so visible
  runtime validation can inspect the mode through the command registry.
- [x] Debug Audio/TTS and mode tests pass (81/81). Launch Debug Vulkan with
  validation outside the sandbox on the Default desktop; verify the `GLFW30`
  foreground window; exercise the unconfigured failure guard and a successful
  local service request; capture wide and compact layouts under
  `save/screenshots/validation/`; and verify clean shutdown.
- [ ] Review keyboard focus inside the text input in the visible editor.

## TE2 — durable dialog library and WAV export (implementation complete)

Design: [TE2](.plan/TE2.md).

- [x] Capture bounded immutable WAV bytes from both TTS result modes without
  making core TTS depend on Editor or inferring bytes from an Audio handle.
- [x] Canonicalize PCM16 WAV, including a streaming header with unspecified
  length; reject malformed and oversized input.
- [x] Persist completed dialog metadata and owned WAV artifacts, restore them
  across editor restarts, and retain malformed library files for repair.
- [x] Add import, duplicate, delete, and export actions with stable IDs,
  shared immutable artifacts, safe paths, and collision-safe atomic output.
- [x] Show the actual buffer controls for restored/imported audio and report
  operation errors in the UI and host commands.
- [x] Test cancel, failure, large output, persistence, import rejection,
  duplicate/delete lifetime, filename collisions, and atomic publication.

TE2's focused Debug suite passes. Visible window and live-provider checks are
still pending under the TE1/TE2 runtime validation note; they were deferred in
the TE2 plan and are not required to close this data/storage implementation.

## Later stages

- [ ] **TE3:** map supported synthesis emotion/speed/pitch capabilities with
  provider tests before enabling draft controls. Playback rate for buffered
  preview is distinct from synthesis speed or pitch.
