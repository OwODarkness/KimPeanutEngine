# Conversational Audio Plan

**Status: proposed, 2026-09-30.** This is the design map for evolving the
existing [Audio](audio_module.md) and [TTS](../tts/tts_module.md) modules into
a voice-capable chatbot companion with optional Live2D and a compact music
player. [TODO.md](TODO.md) is the acceptance ledger; [the execution spec](../../.spec/specs/conversational-audio.md)
defines the cross-stage contract. No implementation is claimed here.
Current source risks are recorded in the [Audio A1 review](.review/A1.md) and
[TTS/streaming A2 review](.review/A2.md).

## Product outcome and first vertical slice

The user types a message, sees the reply text as it arrives, hears a selected
voice speak it, and sees the Live2D character respond and move its mouth in
step with audible speech. The user can interrupt and send a new turn. A compact
player can independently play a local music track and lower its volume while
the character speaks. If Live2D or a response/TTS service is absent, text and
local audio controls remain usable and report a clear state.

The conversation surface needs a scrollable transcript, text entry, Send and
Stop controls, a voice preset selector, and visible `Thinking`, `Speaking`,
`Buffering`, and error states. Voice presets hold the provider-specific
reference path and prompt configuration outside Audio; the UI receives only
safe display names. C1 owns the session state and Editor adapter for this
surface. P1 owns the separate music controls and nearby speech status.

The first slice uses typed input and a replaceable `IResponseSource` supplied
by the application. A deterministic local echo/script source is the test
fixture; an LLM adapter is a later integration decision. Microphone capture,
speech recognition, wake word, avatar visemes, playlists, and streaming radio
are later stages. This ordering delivers a real conversation loop without
making input capture or a particular AI service a foundation dependency.

## Current state and design pressure

- `engine/runtime/audio` has a 48 kHz stereo miniaudio device, buffered and
  streaming players, and a shared per-frame mixer. `AudioStream` drops old
  samples on overflow. The callback reads `players_` while other threads can
  create/reset players, reads unsynchronized player state, and handles mono
  input in the stereo branch as if a second sample existed. It also logs and
  repeatedly attempts a dry stream in the callback. See [baseline](audio_module.md).
- `engine/module/tts` has one GPT-SoVITS HTTP provider and one serial worker.
  `SyncSynthesize` creates a player and blocks through synthesis. There is no
  turn identifier, cancel/replace operation, bounded task queue, or distinction
  between synthesis completion and audible playback completion. The streaming
  provider accepts a non-200 response as success and ignores the receiver's
  abort result. See [baseline](../tts/tts_module.md).
- Live2D has motion/expression/emotion playback and a presentation-owned speech
  bubble; [L2D9](../live2d/.plan/L2D9.md) is the existing speech-integration
  roadmap slot. The model product already identifies `LipSync` parameter IDs,
  but no audio-driven value enters its canonical frame transaction.
- There is no chat/response source, voice capture, music library, or player UI
  in the current engine. The existing TTS example is manual wiring, not an
  application session.

## Ownership and flow

```text
Editor text input ──> application ConversationSession (turn ID, cancel, state)
                         ├─> IResponseSource (text events; fake/local first)
                         ├─> TTS request(s) -> decoded bounded PCM speech source
                         └─> UI copy of text/state + optional emotion intent
                                               |
Audio control thread -> callback-safe voice snapshot/commands -> device mixer
                         ├─ Speech bus -> output + played-frame/envelope telemetry
                         └─ Music bus  -> output (ducked while speech is audible)
                                               |
game/update thread <- copied playback telemetry -> Live2D L2D9 frame input
                                                -> speech bubble / player UI
```

Audio owns the authoritative **played frame cursor**. TTS owns generated
bytes/PCM and its network lifetime. The session owns turn cancellation and
presentation intent. A TTS result should identify a speech stream/job and
report its state; playback ownership and cleanup should be explicit instead
of leaking an `AudioPlayer*` across threads. The exact public API is designed
in A1/A2, with a compatibility adapter for the current example until migrated.
The provider interface must never expose GPT-SoVITS types to Audio or Live2D.

## Timing and interruption rules

- A turn carries a monotonically increasing ID. All response, synthesis,
  playback, subtitle, and emotion events carry that ID. The session discards
  late callbacks from a superseded turn.
- Cancel stops queued generation, asks an active provider to abort, drains or
  retires its speech voice safely, then publishes one terminal state. A new
  turn can start promptly even if an old network request has not returned.
  The TTS worker must not wait for audible drain before accepting the next job.
- Buffer watermarks apply backpressure before samples are dropped. An
  underrun outputs silence and retains an observable `Buffering` state; end of
  network input is distinct from end of queued audible samples. Speech resumes
  from the correct sample after a recoverable stall.
- Envelope samples are computed from the **speech voice as actually consumed**
  by the mixer, tagged with played-frame ranges, not from the full synthesized
  file or music/master output. The game thread samples a copied envelope at
  its best estimate of audible time, with device latency measured or calibrated
  and a documented fallback. Pause, underrun, cancellation, and completion
  drive the mouth toward closed. Any precomputed viseme timestamps later use
  this same playback clock.
- Speech and music have separate gain and mute policy. A short ramp on
  play/stop/duck avoids clicks. The player shows buffered duration only when
  known; streaming speech is not presented as seekable.

## Small player surface

The first player is a compact ImGui panel in the existing Editor composition:
title/source, play or pause, elapsed/total time, seek for seekable files,
volume, mute, a slim progress bar, and a small live level visual. It uses a
dark, restrained cyan/violet "cyberspace" theme with clear focus/hover and
disabled states; it must remain legible at small sizes and HiDPI scales.
Music is loaded through Asset/Audio's file contract and played on the music
bus. The level visual reads downsampled, bounded telemetry from that bus, not
the callback's mutable buffer. Speech state and a voice mute control can sit
beside it, while the character bubble remains Live2D presentation. P1 defines
the UI and runtime evidence. No GPU audio object or Render dependency is needed.

## Stages and dependencies

| Stage | Owner | Deliverable | Depends on |
|---|---|---|---|
| [A1](.plan/A1.md) | Runtime Audio | Safe mixer/control and lifetime foundation, buses and clock | current Audio |
| [A2](.plan/A2.md) | Audio + TTS | Bounded speech transport, provider validation, cancellation and telemetry | A1 |
| [C1](.plan/C1.md) | application composition | Typed turn loop, segmented replies, replaceable response source | A2 |
| [L2D9](../live2d/.plan/L2D9.md) | Live2D | Speech envelope input in canonical frame update, optional session binding | A2; C1 for end-to-end |
| [P1](.plan/P1.md) | Editor + Audio | Compact music player, speech status, ducking | A1; C1 for integrated demo |

Stage boundaries allow A1/A2 to be tested without the Editor or Cubism SDK.
The end-to-end gate joins C1, L2D9, and P1 in one visible session. Implement
one thin vertical path before broadening providers or adding microphone input.

## Reference findings and local decision

- [Godot `audio_server.cpp`, `master` (read 2026-09-30)](https://github.com/godotengine/godot/blob/master/servers/audio/audio_server.cpp)
  keeps active playback mixing and pause/fade state in its audio server.
  KimPeanutEngine should likewise make the mixer the playback-clock owner,
  but use only a speech/music bus pair until more consumers justify a graph.
- [miniaudio device manual](https://miniaud.io/docs/manual/index.html)
  defines the device callback and forbids device stop/reinitialization inside
  it. This reinforces a control path outside the callback and preallocated
  callback work; the current callback's logs and shared mutable registry need
  correction before live use.
- [Live2D Native lip-sync tutorial](https://docs.live2d.com/en/cubism-sdk-tutorials/native-lipsync-from-wav-native/)
  uses RMS to feed model lip-sync values, but its sample does not play sound.
  Here the Audio mixer, rather than a second WAV timer, must supply the value
  from the played frames. The [Live2D lip-sync manual](https://docs.live2d.com/en/cubism-sdk-manual/lipsync/)
  identifies model-authored lip-sync parameters and the 0..1 input range.
- [GPT-SoVITS `api_v2.py`, `main` (read 2026-09-30)](https://github.com/RVC-Boss/GPT-SoVITS/blob/main/api_v2.py)
  supports several streaming modes and a WAV stream with a leading header.
  The adapter must pin/test its expected mode and media format and reject
  error bodies before decoding; other providers remain behind the TTS seam.

These are source studies, not borrowed code. `master`/`main` are moving
branches; implementation should pin an exact revision before writing protocol
or lifecycle compatibility tests.
