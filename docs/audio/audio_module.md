# Audio Module Design

Callback safety, speech/music buses, and the playback clock are implemented in
A1 of [Conversational Audio](PLANS.md). TTS session integration and the
media-library player remain planned in its [roadmap](TODO.md). This document describes the
current implementation.
The [A1 baseline review](.review/A1.md) records the original findings and the
2026-10-01 repair disposition. The source notes below describe the repaired
contracts where those differ from the earlier baseline.

Location: `engine/runtime/audio/` (static lib `Audio`)

The audio module is the engine's playback layer. It owns **who plays** (players), **what feeds them** (a buffered clip or a live stream), and **how samples reach the device** (a miniaudio-backed system whose callback mixes every active player into the output). It is the consumer half of the TTS pipeline — the TTS module (→ [tts_module.md](../tts/tts_module.md)) is the producer that feeds a network stream into this module.

There are two disjoint playback paths with a shared mixer core:

- **Buffered** — the whole clip is decoded up front into an `AudioClip` and a `BufferAudioPlayer` walks it.
- **Streaming** — bytes arrive incrementally (HTTP chunked TTS), an `AudioStreamDecoder` turns them into samples, an `AudioStream` FIFO buffers them, and a `StreamAudioPlayer` slides a ring window over the FIFO. This is the path that carries streaming TTS.

## Key types

### Data types — [`engine/runtime/core/data/audio.h`](../../engine/runtime/core/data/audio.h)

`AudioFormat` is the sample contract: `channels`, `sample_rate`, `bits_per_sample`. Defaults are **mono 48 kHz**. `AudioClip` is a fully-decoded buffer (`pcm` as `vector<float>`, `frame_count`), used by the buffered path. `AudioChunk` is declared but **unused** — dead code (see [Dead code](#dead-code)).

### Handles and state — [`audio_types.h`](../../engine/runtime/audio/audio_types.h)

- `AudioHandle` — a `Handle<AudioTag>` from the shared `HandleSystem` (same slot+generation scheme as asset IDs).
- `AudioState { Stopped, Playing, Buffering, FadingOut, Paused, Finished,
  Cancelled }` — the player state machine. Buffering is recoverable starvation;
  it is distinct from a finished stream.
- `AudioBus { Speech, Music }` — independent gain and mute controls.
- `AudioStreamState { Open, ProducerFinished, Cancelled, Drained }` — stream
  producer and consumer lifecycle.
- `AudioPlayerType { Buffer, Stream, Seekable }` — selects which player a
  system constructs.

### `AudioPlayer` — [`audio_player.h`](../../engine/runtime/audio/audio_player.h)

The abstract base. Callback reads use a bounded block-copy contract:

- **`CopyFrames(first_frame, out, max_frames, channels)`** — copies complete interleaved frames into mixer-owned scratch. No pointer into clip or stream storage escapes its ownership/lifetime. `CopyFrameData` remains as a one-frame compatibility wrapper.
- Playback commands enter a fixed-capacity per-player mailbox and are applied at a callback boundary. Overflow increments command-rejection telemetry.
- The played-frame cursor advances only for consumed source frames; temporary stream starvation leaves it in place and reports `Buffering`.

`FillBuffer()` is called once per active stream voice per mixer block. Refill and
frame-copy paths use try-locks, so producer contention yields silence for that
block instead of blocking the device callback.

### `BufferAudioPlayer` — [`buffer_audio_player.cpp`](../../engine/runtime/audio/buffer_audio_player.cpp)

Holds a retained immutable `AudioClip` owner and publishes its raw pointer to
the callback. The clip can only be assigned once. `CopyFrames` validates and
copies complete mono or stereo frames. The mixer input contract is fixed at
48 kHz; empty, malformed, unsupported-channel, and non-48 kHz clips cannot
start. Buffered playback supports 0.5×–2.0× rate through linear interpolation;
the source cursor advances at the same rate, preserving seek, progress, loop,
and end behavior. This changes pitch with speed. Looping wraps within the
source frame count; a non-looping end marks the player `Finished` after the
last consumed frame. Stream players retain normal 1.0× playback.

### `SeekableAudioPlayer` — [`seekable_audio_player.cpp`](../../engine/runtime/audio/seekable_audio_player.cpp)

Plays the encoded-audio range of a validated native `.audio` product. The
`NativeAudioLoader` copies bounded metadata, waveform peaks, and subtitle cues
into `AudioResource::native_product`; it leaves the encoded payload in the
product file. `SetSource` receives that path/range and retains the product as
a lifetime pin. A decode worker owns the file and miniaudio decoder, seeks in
the encoded range, converts to 48 kHz stereo float, and fills a fixed
96,000-frame cache in 1,024-frame chunks. The cache and decode scratch use
under 1 MiB per voice regardless of song duration. The mixer callback only
try-locks the cache and copies/interpolates samples; it performs no file I/O
or decoding. A miss reports `Buffering` without advancing the played-frame
cursor. Pause, seek, 0.5×–2.0× rate, and loop all use that source-frame clock.
`NativeAudioFileProduct::SubtitleTextAt(frame)` returns the active half-open
cue intervals for presentation code; Audio itself does not own text rendering.

This is the native product playback path used by the standalone Audio Player
since M1.4. Its externally selected files are cooked into per-source session
archives and are not registered as project Asset entries.

### `StreamAudioPlayer` — [`stream_audio_player.cpp`](../../engine/runtime/audio/stream_audio_player.cpp)

The streaming player keeps a **ring window** over the `AudioStream` FIFO. The
mixer attempts one refill per block and copies available frames under a
nonblocking ring lock. Dry streams output silence and remain `Buffering`; after
producer completion the buffered tail drains once before the player finishes.
Cancellation clears queued audio and marks the player cancelled. The storage is:

```
struct RingBuffer {
    std::vector<float> data;        // CACHE_SIZE_FRAMES * channels
    uint64_t start_frame;           // absolute frame index at data[0]
    uint64_t filled_frames;         // how many frames are valid
    uint64_t capacity_frames;       // CACHE_SIZE_FRAMES = 20240
}
```

`CACHE_SIZE_FRAMES = 20240` (≈0.42 s at 48 kHz), `LOW_WATER_MARK = 5120` (≈0.11 s). The window slides forward as the playhead advances; when less than the low-water mark remains unplayed, `Refill` pulls more from the stream. See [The streaming window](#the-streaming-window).

### `AudioStream` — [`audio_stream.cpp`](../../engine/runtime/audio/audio_stream.cpp)

A **thread-safe FIFO ring buffer** — the producer/consumer seam. Capacity is `sample_rate * channels * buffer_seconds`, where `buffer_seconds` defaults to **20**. `PushFrames` rejects an invalid, post-finish, or over-capacity write without changing queued samples and counts overflow rejections. The callback uses `TryReadFrames`, which never waits for the producer mutex. `Finish()` serializes with writes; `ProducerFinished` remains distinct from `Drained` while queued frames remain.

### `AudioStreamDecoder` — [`audio_stream_decoder.cpp`](../../engine/runtime/audio/audio_stream_decoder.cpp)

The incremental decoder. `Feed(data, size)` returns `NeedMoreData`, `DataDecoded`, or `InvalidData`. It bounds and validates RIFF chunks, reads little-endian fields without unaligned casts, tracks declared data length, retains partial channel frames, and resamples with phase carried across chunks. It accepts PCM16 mono/stereo from 8–192 kHz and explicitly maps channels to the destination stream format.

### `AudioSystem` / `MiniAudioSystem` — [`audio_system.cpp`](../../engine/runtime/audio/audio_system.cpp), [`miniaudio_audio_system.cpp`](../../engine/runtime/audio/miniaudio_audio_system.cpp)

`AudioSystem` owns a fixed 64-slot storage array and generation-checked handles.
`AudioSystemSettings` lets callers lower the voice budget, maximum callback work
frames, gain-ramp duration, and requested device period count. Values are
clamped to the preallocated 64-voice/512-frame ceiling. Oversized callback
blocks process only the configured work limit and leave the remainder silent;
the clipped frame count is reported. `DestroyAudioPlayer` unpublishes a slot,
invalidates its generation, then releases its owner after callback readers exit.
`MiniAudioSystem` owns an idempotent lifecycle (48 kHz stereo, `f32` by
default), reports actual device format and estimated period-buffer latency,
and stops/uninitializes before its callback target is destroyed. `Mix` performs
bounded block mixing with independent Speech/Music and master gain ramps.

The standalone Audio Player is an application host, separate from the Scene3D
Editor workspace. Its host owns a `MiniAudioSystem` and supplies the Runtime
error material as a render-catalog root so startup needs no game level. Its
presentation borrows that instance, starts output only after an explicit user
action, and displays copied device/mixer telemetry. The M1.4 library imports
selected local tracks through AssetImport into session-scoped `.audio`
products, then plays them with `SeekableAudioPlayer`; the active timed subtitle
is selected from the played-frame cursor and drawn in a larger borderless area
below the waveform. RMS and spectrum visualization use a nonblocking snapshot
of decoded cache frames rather than callback-owned samples. The explicit
subtitle path is attached during import, and
selected tracks can be reimported without replacing an active voice's pinned
product. These external files are session previews rather than project Asset
registrations. Audio has no TTS dependency: TTS can submit
generated streams through Audio's player/stream API, while provider requests,
cancellation, and synthesis state remain owned by TTS and application
composition.

## Data flow

### Buffered path (non-streaming TTS and short-clip compatibility)

```
[bytes] → MiniAudio_AudioLoader::LoadFromMemory → AudioClip → BufferAudioPlayer::SetClip → Play
                                                            ↑
                                      Mix copies bounded blocks with CopyFrames
```

### Native music path (standalone Audio Player)

```
external music + optional subtitle
    └─ AssetImport::AudioImportService -> session `.audio` product
         ├─ copied waveform peaks and timed subtitle cues -> Now Playing UI
         └─ verified encoded payload + lifetime pin
              └─ SeekableAudioPlayer decode worker -> fixed PCM cache -> callback mixer
```

The UI reads subtitle text from the voice's played-frame cursor. It does not
advance cues from wall-clock time, so pause and seek follow the audio position.

### Streaming path (streaming TTS)

```
httplib receiver callback (network thread)
    └─ AudioStreamDecoder::Feed(chunk)          # parse header once, then decode+resample
         └─ AudioStream::PushFrames(samples)    # fixed-capacity FIFO; rejects overflow
                                                   ↑ (consumer)
audio callback (miniaudio thread)
    └─ Mix(callback block)
         └─ StreamAudioPlayer::CopyFrames / cursor commit
              └─ ring window ← AudioStream::TryReadFrames (via one FillBuffer attempt)
```

The TTS worker produces into the FIFO. The callback uses nonblocking reads and ring-buffer copies; producer contention emits silence for that callback block.

## The streaming window

`Refill(at_frame)` (caller holds `buffer_mutex_`) keeps the window ahead of the playhead:

1. If the playhead is **outside** the window (ahead of it or behind `start_frame`), restart the window there (`start_frame = at_frame`, `filled_frames = 0`). In steady state this never fires — it only rescues a stale playhead.
2. Compute `unplayed = (start_frame + filled_frames) - at_frame`. If `unplayed > LOW_WATER_MARK`, nothing to do — plenty of data ahead.
3. If the buffer is full, **slide** the window: `memmove` the unplayed tail to the front, advance `start_frame`. (This replaces what used to be a reset-from-zero, which is what caused the stutter — see [History](#history--bug-log).)
4. Read up to `min(max_write, CACHE_SIZE_FRAMES / 4)` frames from the stream into the free tail, increment `filled_frames`.

Seek commands are applied at callback boundaries. The cursor advances by the
number of frames actually copied and consumed. A missing stream frame is a
temporary underrun unless the producer has finished and the buffered tail has
drained.

`FillBuffer()` is called once per active stream voice at the start of a mixer block. It tries to acquire the ring mutex; refill then tries the FIFO mutex. A failed try-lock defers refill to the next callback.

## Threading model

| Structure | Guard | Owner threads |
|---|---|---|
| `AudioStream` FIFO | `AudioStream::mutex_` | producer = network/httplib thread, consumer = audio thread |
| `StreamAudioPlayer` ring window | `buffer_mutex_` with callback try-lock | callback copies frames; control thread assigns stream once |
| `BufferAudioPlayer` clip | retained immutable owner plus atomic raw publication | control thread assigns once; callback copies frames |
| Voice slot owner | `voices_mutex_` on control path plus callback reader count | create/destroy on control threads; callback reads a stable published pointer |

Lock order is **buffer → stream** for refill. Callback-side stream operations
use try-locks; callback code performs no logging, I/O, allocation, or waits.
Runtime budgets default to 64 active voices and 512 work frames per device
callback. These are tunable through `AudioSystemSettings` within the storage
ceiling.

## History / bug log

### Incident: startup stutter in the streaming player (2026-08-09) — fixed

**Symptom.** `StreamAudioPlayer` stuttered/stalled at the start of streaming TTS playback; when it did play, it reset its buffer every ~0.21 s.

**Root cause — three compounding defects:**

1. **The first chunk never produced audio.** `AudioStreamDecoder::Feed` parsed the WAV header and *returned* — the audio bytes in the same packet stayed in `pending_bytes_`. `Play()`'s prefill then found an empty stream → logged `Failed to prefill buffer` and **bailed without starting** the player. Playback only began after a later chunk arrived, and the playhead had already been abandoned.
2. **The low-water refill was dead code.** `RingBuffer::filled_frames` was incremented on reads from the stream but **never decremented on consumption**, so `filled_frames > LOW_WATER_MARK` was always true. The only path that ever filled the buffer was the dry-FIFO *reset* (`Buffer reset: read 10120 frames`), which restarted the window from frame 0 every ~10120 frames — hence the periodic re-stall.
3. **A temporary underrun was treated as end-of-playback.** `Mix` called `player->Stop()` unconditionally whenever `AdvanceFrame()` returned false. Because `GetFrameData` also treated `src` as a *frame* index (double-multiplying by channels), the last readable frame kept failing; combined with a dry FIFO returning 0 frames, the player tore itself down mid-stream.

**Fix (6 files).**

- [`audio_stream_decoder.cpp`](../../engine/runtime/audio/audio_stream_decoder.cpp) — `Feed` falls through to `DecodePCM()` after a successful header parse, so the first chunk's audio is playable immediately.
- [`stream_audio_player.cpp`](../../engine/runtime/audio/stream_audio_player.cpp) / `.h` — `GetFrameData` converts the sample offset to a frame index (`src / channels`); `Play()` prefill is best-effort (break on dry, still start); new `Refill(at_frame)` slides the window instead of resetting; new `FillBuffer()`; `ResolveFrame` treats an unavailable frame as *wait* and only `IsFinished()` ends playback.
- [`audio_player.cpp`](../../engine/runtime/audio/audio_player.cpp) / `.h` — `SetCurrentFrame` always syncs `current_frame_` (even when `ResolveFrame` says not-ready), so the playhead follows the window while waiting; `FillBuffer` added to the interface (no-op default).
- [`miniaudio_audio_system.cpp`](../../engine/runtime/audio/miniaudio_audio_system.cpp) — on `GetFrameData` failure, `Mix` calls `player->FillBuffer()` instead of just `continue`; on `AdvanceFrame() == false` it stops the player **only** when `IsFinished()`.

**Post-fix log evidence.** `Stream player started` immediately after the header parse; `Buffer refilled` events with a continuously sliding `start_frame` (0 → 15120 → 30240 → … → 483840 = full ~10 s played in real time); **zero** `Buffer reset` events; a single clean `Audio stop play` at true end-of-stream.

### Incident notes worth keeping

The August startup-stutter fixes above remain part of the playback history. The
2026-10-01 safety repair adds bounded refill and explicit FIFO rejection; the
device callback no longer retries a dry stream once per output frame.

## Known smells / next steps

- **Sanitizer validation remains unrun.** The create/control/destroy stress contract passes, including handle slot reuse, but no sanitizer build was available in this pass.
- **Callback timing is host-specific.** The Debug 64-voice x 512-frame offline measurement was 1.819 ms maximum in this run; visible one-voice device smoke observed 0.454 ms. These are measurements, not cross-machine guarantees.
- **Rate conversion is a fixed input contract for buffered clips.** The loader produces 48 kHz stereo; callers supplying other buffered formats are rejected and need an explicit conversion path.
- **Window math assumes `at_frame >= start_frame` after the reset check.** `GetReadOffset` computes `frame_index - start_frame` as unsigned; when `frame_index < start_frame` it underflows to a huge value and correctly reports "not in window" → reset. That is intentional, but the subtraction makes the *intent* non-obvious to a future reader.

## Dead code

- `AudioChunk` ([`data/audio.h`](../../engine/runtime/core/data/audio.h)) — declared, never constructed or read. Slated for removal unless a chunked API is planned.
- `audio_listener.h` — `AudioListenerData` is defined but nothing includes it; it also has a **missing trailing semicolon** on the struct, so it does not compile as-is. Not in the build — do not "fix" it as live (see [dead_code.md](../dead_code.md)).
