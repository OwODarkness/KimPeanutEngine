# TE1 — standalone TTS authoring mode

Status: implemented; visible runtime acceptance pending. Parent: [editor roadmap](../TODO.md). Reference:
[`save/tts_gui.png`](../../../../save/tts_gui.png). Existing capability:
[TTS client](../../tts_module.md), [Audio Player mode](../../../audio/.plan/P1.md).

## Design question and exit

Can the current asynchronous GPT-SoVITS producer and Runtime Audio players
support a responsive, honest standalone authoring workspace with all machine-
specific connection and reference values loaded from local settings? Exit when
`--mode tts` can create/load settings, generate and hear speech, cancel it,
report failures, and close without dangling jobs or voices.

## Work sequence and functions

1. **Mode seam:** extend `ApplicationModeName`, `ParseApplicationMode`, mode
   index/count, CLI tests, and module bootstrap registration. Add a separate
   TTS editor target with host/controller/presentation. Follow Audio Player's
   `Initialize`, `InitializePresentation`, `Tick`, `RenderPresentation`, and
   split shutdown pattern.
2. **Settings seam:** implement `LoadOrCreateSettings`, `ValidateSettings`,
   and `SaveSettings` in the mode adapter. Use `project_root/tts/settings.json`;
   write blank machine-specific template values only on absence. Reject
   malformed input without destroying it. Construct `ServerConfig` and
   selected voice requests exclusively from the loaded snapshot. Validate
   settings-backed `playback_mode` as `stream` or `buffer` and copy it into
   each request and dialog entry; changing it never mutates an active job.
3. **Job/controller seam:** implement `Generate`, `Cancel`, `Tick`,
   `GetView`, and voice transport actions. `Generate` validates settings and
   text, initializes Audio before TTS, submits `AsyncSynthesize`, and stores
   job/entry IDs. The callback enqueues copied results; `Tick` drains it and
   `TTSSystem::DrainEvents`. Guard late results by job ID. Own returned
   `AudioHandle` for session replay after completion, then destroy it on
   cancellation, connection reset, eviction, or shutdown. Guard retirement
   with the Audio handle generation. Establish the handoff/retirement rule
   with focused tests.
4. **Presentation:** draw the four task regions from copied controller state;
   reuse `DrawTransportStrip` with its separate stop-voice and cancel-job
   intents. Choose the stream or buffer control panel from the selected entry's
   recorded mode and actual player capability. Stream has no seek/rate;
   buffered preview can expose both. Only expose controls that the selected
   player supports. Show
   settings validity and connection failures beside Generate.
5. **Validation:** fake-provider contracts cover absent/invalid settings,
   retry, both playback modes, a mode switch while an earlier job plays,
   empty text, async result ordering, cancel during generation/playback,
   failure, and shutdown. Test real visible playback and the compact layout
   using the repository runtime validation path.

## Scope boundary

TE1 keeps dialog entries in memory. WAV export, durable dialog import/restore,
image/voice artwork, emotion, synthesis pitch, and synthesis speed require
separate data or provider contracts and belong to TE2/TE3. The mode must not
block on `SyncSynthesize`, depend on Audio Player's mode target, or alter the
Audio callback to serve a waveform.
