# Conversational Audio Roadmap

**Status: A1 complete; later stages proposed.** Architecture: [PLANS.md](PLANS.md). Baselines:
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
- [ ] **A2 — conversational TTS transport** ([plan](.plan/A2.md),
  [review](.review/A2.md)). Validate
  HTTP status and media format, propagate decode errors, enforce bounded PCM
  backpressure, distinguish request/stream/playback terminal states, and make
  queued and in-flight synthesis cancellable. Tests cover fragmented WAV,
  malformed/error responses, starvation, overflow, late callbacks, and drain.
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
- [ ] **P1 — compact music player** ([plan](.plan/P1.md)). Add local-track
  controls, progress, volume/mute, bounded level display, speech status, and
  music ducking in Editor. Verify visible layout, keyboard use, audible
  controls, interruption, and clean shutdown.

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
