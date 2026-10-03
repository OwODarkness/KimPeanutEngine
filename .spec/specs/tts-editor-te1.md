# Standalone TTS editor TE1

- Status: active
- Owner: KimPeanutEngine TTS editor
- Parent TODO: [TE1](../../docs/tts/editor/TODO.md)

## Objective

Provide a standalone `--mode tts` workspace that loads local GPT-SoVITS
settings, submits speech asynchronously, and previews the resulting speech
through the existing Audio system.

## Current state

`TTSSystem` provides asynchronous jobs and state events. Runtime Audio owns
stream and buffer players. `EditorUILib` provides a shared transport strip.
The visual draft is `save/tts_gui.png`; its portrait, waveform, export, and
synthesis controls lack TE1 data or provider contracts.

## Scope

TE1 adds the Runtime launch mode, a separate TTS editor target under
`engine/module/tts/editor/`, a versioned local settings file, voice presets,
an in-memory dialog list, and stream/buffer preview. WAV export and durable
dialog storage are TE2. Provider-dependent emotion, pitch, and synthesis speed
are TE3.

## Invariants

- Core `TTS` remains independent of Editor and ImGui.
- Runtime Audio owns voices; the editor retains handles only while the session
  needs them and retires them after cancellation or shutdown.
- Provider callbacks copy results to an inbox; ImGui reads a copied view and
  queues actions to the host update thread.
- Address, port, reference audio, and reference text come from the Git-ignored
  `project_root/tts/settings.json`. A missing file gets a blank template;
  malformed data is not overwritten automatically.
- The playback mode is copied into each new request and session entry, so
  changing settings does not relabel existing audio.

## Stages

1. Register the standalone host and mode.
2. Load, validate, edit, and atomically save local settings.
3. Bridge TTS jobs and Audio handles through a controller.
4. Render the preview, dialog list, settings, and text input.
5. Validate contracts and the visible runtime.

## Acceptance criteria

- [x] Separate host and Editor-facing target build for `--mode tts`.
- [x] Settings and voice selection drive provider requests without compiled
  local connection or reference values.
- [x] The UI supports asynchronous generation, cancellation, status, and
  stream/buffer-specific preview controls.
- [x] Host commands expose status and actions without reference text.
- [ ] Final Debug build, targeted and broad tests, and visible Vulkan runtime
  checks establish TE1 behavior.

## Validation plan

Level 4 applies because Runtime mode and CMake module wiring changed. Build
Debug, run TTS and launch mode tests, then the full CTest suite. Launch the
matching Vulkan GUI outside the sandbox with validation enabled; inspect the
Default-desktop GLFW window at wide and compact sizes, capture it through the
Runtime command service, exercise configured speech if a service is available,
and verify clean shutdown.

## Risks and open questions

The machine has no checked-in service address or reference values. Real
service playback requires local configuration and a reachable provider.
Visual acceptance requires a visible window, not a compile result.
