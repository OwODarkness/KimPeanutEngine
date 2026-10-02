# Conversational Audio Plan

**Status: A1 complete; A2/P1/M1 active; C1/L2D9 proposed.** M1.1 import and M1.2 archive/reimport are implemented; M1.3 bounded playback remains. This is the design map for evolving the
existing [Audio](audio_module.md) and [TTS](../tts/tts_module.md) modules into
a voice-capable chatbot companion with optional Live2D and a standalone audio
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

- `engine/runtime/audio` has a 48 kHz stereo miniaudio device, bounded stable
  voice slots, callback-boundary command mailboxes, block-copy players,
  independent Speech/Music buses, played-frame telemetry, and explicit stream
  overflow/drain state. A1 timing and device evidence is in its
  [journal entry](../../.spec/journal/conversational-audio.md). TTS transport
  cancellation and the conversation/player layers remain later stages.
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
- There is no chat/response source, voice capture, or cooked music library.
  The standalone player has a raw-file session queue and ImGui workspace;
  it does not import a native Audio product. The existing TTS example is
  manual wiring, not an application session.

## Ownership and flow

```text
Editor text input ──> application ConversationSession (turn ID, cancel, state)
                         ├─> IResponseSource (text events; fake/local first)
                         ├─> TTS request(s) -> decoded bounded PCM speech source
                         └─> UI copy of text/state + optional emotion intent
                                               |
Audio control thread -> stable voice slots / callback-boundary commands -> device mixer
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
in A1; `GetAudioPlayer` remains a compatibility access point for the existing
TTS path until A2 replaces it with the handle-oriented seam.
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

## Standalone audio player

The player is a standalone ImGui mode in the Editor, using the
[draft layout](../../save/audio_gui.png). The current mode scaffold exposes device and mixer
diagnostics; [P1](.plan/P1.md) defines the track player, queue, waveform,
transport, and reusable Audio-module playback component. Asset decodes local
music, Runtime Audio owns its voice and Music bus, and the component exposes
copied playback state to ImGui. A future Speech instance uses the same
component contract without replacing music. The waveform comes from a
bounded reduction of decoded PCM; a live spectrum waits for real bounded
telemetry. No GPU audio object or Render dependency is needed. The view must
also collapse cleanly to a compact width and remain legible at HiDPI scales.

## Proposed native audio product

The [M1 native music asset plan](.plan/M1.md) defines one versioned Asset
product from an authored audio source and an explicitly selected optional
subtitle source. Asset owns the import tool, source/dependency identity,
archive publication, and reimport. Runtime Audio owns bounded decode,
playback, and the source-frame clock; the Editor/conversation UI owns subtitle
display. The existing whole-file `AudioClip` path remains a preview/short-clip
compatibility path until native music playback is validated. TTS streams do
not require the music product.

Reference: Godot `master` imports WAV in
[`editor/import/resource_importer_wav.cpp`](https://github.com/godotengine/godot/blob/master/editor/import/resource_importer_wav.cpp)
into an `AudioStreamWAV` resource with explicit import options. The useful
pattern here is source-to-runtime-resource conversion; the optional subtitle
chunk and played-frame cue contract are KimPeanutEngine design choices.

## Stages and dependencies

| Stage | Owner | Deliverable | Depends on |
|---|---|---|---|
| [A1](.plan/A1.md) | Runtime Audio | Safe mixer/control and lifetime foundation, buses and clock | current Audio |
| [A2](.plan/A2.md) | Audio + TTS | Bounded speech transport, provider validation, cancellation and telemetry | A1 |
| [C1](.plan/C1.md) | application composition | Typed turn loop, segmented replies, replaceable response source | A2 |
| [L2D9](../live2d/.plan/L2D9.md) | Live2D | Speech envelope input in canonical frame update, optional session binding | A2; C1 for end-to-end |
| [P1](.plan/P1.md) | Audio module + Editor host | Standalone local player, reusable playback component, speech status, ducking | A1; C1 for integrated demo |
| [M1](.plan/M1.md) | Asset + Runtime Audio + Editor | Native music product, optional timed subtitles, reimport, and bounded playback | P1 controls; Asset archive/import foundation |

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
  callback work. A1 now uses stable published voice slots, bounded work, and
  keeps device stop/reinitialization on the control path.
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
