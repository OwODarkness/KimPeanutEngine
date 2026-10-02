# Conversational Audio Roadmap

**Status: A1 complete; A2 and M1 in progress.** Architecture: [PLANS.md](PLANS.md). Baselines:
[Audio](audio_module.md), [TTS](../tts/tts_module.md). Cross-stage acceptance:
[spec](../../.spec/specs/conversational-audio.md). Checkboxes describe work
to do, not completion evidence.

The [A1 Audio baseline review](.review/A1.md) and
[A2 TTS/streaming baseline review](.review/A2.md) record open findings with
source locations. Resolve their P0/P1 findings before the corresponding stage
is accepted; record resolution evidence in those review files.

## Ordered work

- [x] **A1 — safe playback core** ([plan](.plan/A1.md),
  [review](.review/A1.md)). Correct mono/stereo
  mixing and callback lifetime races; bound callback work; expose independent
  speech/music gains and an authoritative played-frame clock. Offline tests
  prove repeated create/play/pause/seek/stop/destroy and clean device shutdown.
  Twenty-one audio contracts pass and the visible device smoke drains all frames;
  measurements and the remaining sanitizer limitation are in the journal.
- [ ] **A2 — conversational TTS transport** (in progress; [plan](.plan/A2.md),
  [review](.review/A2.md)). Validate
  HTTP status and media format, propagate decode errors, enforce bounded PCM
  backpressure, distinguish request/stream/playback terminal states, and make
  queued and in-flight synthesis cancellable. Job tokens, per-job HTTP abort,
  lifecycle events, and audible drain are implemented; two fake-provider
  cancellation/drain contracts pass. Remaining tests cover malformed/error
  responses, starvation, overflow, late callbacks, and visible service playback.
- [ ] **C1 — typed conversation session** ([plan](.plan/C1.md)). Integrate a
  deterministic response source with text streaming and ordered speech
  segments and a small transcript/input/voice-selection surface. Interruption
  removes old text/audio/emotion events. The same session works without Live2D
  and has a documented provider adapter seam.
- [ ] **L2D9 — speech animation**
  ([plan](../live2d/.plan/L2D9.md)). Feed played speech envelope into the
  instance frame transaction after authored motion/expression at a defined
  priority. Handle missing lip-sync metadata, pauses, underruns, and character
  changes; verify optional SDK-off builds and both graphics backends.
- [ ] **P1 — standalone audio player** ([plan](.plan/P1.md)). Plan a
  three-pane ImGui player based on the [draft](../../save/audio_gui.png), with local queue,
  waveform, transport, and a reusable Audio-module playback component.
  Add separate speech status and music ducking for the integrated demo;
  verify visible layout, audible controls, interruption, and clean shutdown.
- [ ] **M1 — native music asset and reimport** ([plan](.plan/M1.md)). Extend
  Asset import/archive with an Audio product from a required music source and
  optional explicit subtitle source. Reimport must track both inputs and cook
  settings, skip unchanged work, publish atomically, preserve the last Ready
  product on failure, and report stale/missing/corrupt status. Switch project
  music to bounded native playback after seeking, clock/cue sync, active-voice
  lifetime, and long-track memory are verified. M1.1's offline provider, WAV/
  MP3/FLAC product cooking, UTF-8 SRT/WebVTT/LRC parsing, deterministic `.audio`
  container, and malformed-input rejection are implemented and validated;
  archive/reimport and runtime playback remain M1.2–M1.4.

## End-to-end acceptance

- [ ] Typed input produces incremental visible reply text and audible speech;
  `Cancel` or a new turn prevents any prior-turn audio or subtitle from
  reappearing.
- [ ] Mouth motion follows consumed speech samples within a measured and
  documented sync tolerance; it closes on silence, buffering, pause, and stop.
- [ ] A local music track can play under speech, ducks and recovers smoothly,
  and remains independently controllable.
- [ ] Device, provider, Editor, and optional Live2D teardown leave no active
  callback reading freed state. Debug offline contracts and visible runtime
  evidence are recorded in a journal.
- [ ] An SDK-off/headless build retains text, TTS, and Audio paths without
  linking Live2D or Editor into Runtime Audio.

## Deferred decisions

- [ ] Select an LLM/response adapter and credential/storage policy after the
  deterministic C1 session contract exists.
- [ ] Decide whether microphone capture and speech recognition are needed for
  the first shipped chatbot surface; if so, plan capture permissions, echo
  handling, VAD, transcript correction, and privacy separately.
- [ ] Evaluate phoneme/viseme timing only after amplitude-driven L2D9 is
  measured. Do not infer phonemes from raw amplitude.
- [ ] Evaluate playlist/library persistence and streaming music only after P1
  local playback is sound.
