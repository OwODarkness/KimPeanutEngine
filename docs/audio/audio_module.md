# Audio Module Design

Future callback safety, speech/music buses, playback clock, and the compact
player are planned in [Conversational Audio](PLANS.md) and its
[roadmap](TODO.md). This document describes the current implementation.
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
- `AudioState { Stopped, Playing, Paused, Finished }` — the player state machine.
- `AudioPlayerType { Buffer, Stream }` — selects which player a system constructs.

### `AudioPlayer` — [`audio_player.h`](../../engine/runtime/audio/audio_player.h)

The abstract base. Callback reads use a copy contract:

- **`CopyFrameData(frame, out, capacity, channels)`** — copies one complete interleaved frame into callback-owned storage. No pointer into a clip or stream window escapes its lock/lifetime. A missing frame means silence for this callback block; `IsFinished()` distinguishes terminal drain from a temporary underrun.
- **`ResolveFrame(new_frame)`** — decides whether `new_frame` is playable; the derived player fills `new_frame` in (e.g. loop-wrap, clamp-to-end).
- **`SetCurrentFrame(new_frame)`** — the bridge: calls `ResolveFrame`, then **always** writes `current_frame_ = new_frame` regardless of the result. This was a deliberate fix (see [History](#history--bug-log)): for a streaming player the playhead must follow the buffer window even while the source is dry, or the player re-reads the last frame in a loop.

`FillBuffer()` (virtual, default no-op) is called once per active voice per mixer block. Stream refill and frame-copy paths use try-locks, so producer contention yields silence for that block instead of blocking the device callback. `AdvanceFrame`/`SeekFrames`/`SeekSeconds` are the movement API on top of `SetCurrentFrame`.

### `BufferAudioPlayer` — [`buffer_audio_player.cpp`](../../engine/runtime/audio/buffer_audio_player.cpp)

Holds an immutable `AudioClip` snapshot through an atomic shared pointer. `CopyFrameData` validates and copies a complete mono or stereo frame. The mixer contract is fixed at 48 kHz; empty, malformed, unsupported-channel, and non-48 kHz clips are rejected by `Play`. `ResolveFrame`: in-range → ready; past the end → wrap on `looping_`, else clamp to the last frame and set `Finished`.

### `StreamAudioPlayer` — [`stream_audio_player.cpp`](../../engine/runtime/audio/stream_audio_player.cpp)

The streaming player. It keeps a **ring window** over the `AudioStream` FIFO:

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

A **thread-safe FIFO ring buffer** — the producer/consumer seam. Capacity is `sample_rate * channels * buffer_seconds`, where `buffer_seconds` defaults to **20**. `PushFrames` rejects an invalid, post-finish, or over-capacity write without changing queued samples. The callback uses `TryReadFrames`, which never waits for the producer mutex. `Finish()` serializes with writes; `IsFinished()` is an atomic producer-terminal flag, separate from queued-frame drain.

### `AudioStreamDecoder` — [`audio_stream_decoder.cpp`](../../engine/runtime/audio/audio_stream_decoder.cpp)

The incremental decoder. `Feed(data, size)` returns `NeedMoreData`, `DataDecoded`, or `InvalidData`. It bounds and validates RIFF chunks, reads little-endian fields without unaligned casts, tracks declared data length, retains partial channel frames, and resamples with phase carried across chunks. It accepts PCM16 mono/stereo from 8–192 kHz and explicitly maps channels to the destination stream format.

### `AudioSystem` / `MiniAudioSystem` — [`audio_system.cpp`](../../engine/runtime/audio/audio_system.cpp), [`miniaudio_audio_system.cpp`](../../engine/runtime/audio/miniaudio_audio_system.cpp)

`AudioSystem` is the handle registry: `GetAudioPlayer` returns a `shared_ptr` after generation validation. `DestroyAudioPlayer` retires the generation and player from the published immutable callback snapshot; retired snapshots and players are reclaimed on the control thread after callback readers release them. The active voice count is capped at 64. `MiniAudioSystem` owns an idempotent miniaudio lifecycle (48 kHz stereo, `f32`, 512-frame requested period) and stops/uninitializes the device before its callback target is destroyed. **`Mix` runs on the miniaudio callback thread.**

## Data flow

### Buffered path (non-streaming TTS, file playback)

```
[bytes] → MiniAudio_AudioLoader::LoadFromMemory → AudioClip → BufferAudioPlayer::SetClip → Play
                                                            ↑
                                              Mix pulls clip_->pcm directly
```

### Streaming path (streaming TTS)

```
httplib receiver callback (network thread)
    └─ AudioStreamDecoder::Feed(chunk)          # parse header once, then decode+resample
         └─ AudioStream::PushFrames(samples)    # fixed-capacity FIFO; rejects overflow
                                                   ↑ (consumer)
audio callback (miniaudio thread)
    └─ Mix(callback block)
         └─ StreamAudioPlayer::CopyFrameData / AdvanceFrame
              └─ ring window ← AudioStream::TryReadFrames (via one FillBuffer attempt)
```

The TTS worker produces into the FIFO. The callback uses nonblocking reads and ring-buffer copies; producer contention emits silence for that callback block.

## The streaming window

`Refill(at_frame)` (caller holds `buffer_mutex_`) keeps the window ahead of the playhead:

1. If the playhead is **outside** the window (ahead of it or behind `start_frame`), restart the window there (`start_frame = at_frame`, `filled_frames = 0`). In steady state this never fires — it only rescues a stale playhead.
2. Compute `unplayed = (start_frame + filled_frames) - at_frame`. If `unplayed > LOW_WATER_MARK`, nothing to do — plenty of data ahead.
3. If the buffer is full, **slide** the window: `memmove` the unplayed tail to the front, advance `start_frame`. (This replaces what used to be a reset-from-zero, which is what caused the stutter — see [History](#history--bug-log).)
4. Read up to `min(max_write, CACHE_SIZE_FRAMES / 4)` frames from the stream into the free tail, increment `filled_frames`.

`ResolveFrame(new_frame)` is used for seeks and initialization. During mixing, `AdvanceFrame` only advances the atomic cursor. A missing frame is a temporary underrun unless the producer has finished and the buffered tail has drained.

`FillBuffer()` is called once per active stream voice at the start of a mixer block. It tries to acquire the ring mutex; refill then tries the FIFO mutex. A failed try-lock defers refill to the next callback.

## Threading model

| Structure | Guard | Owner threads |
|---|---|---|
| `AudioStream` FIFO | `AudioStream::mutex_` | producer = network/httplib thread, consumer = audio thread |
| `StreamAudioPlayer` ring window | `buffer_mutex_` | audio thread (Mix), plus `Play()`/`SetStream` from the network thread |
| `BufferAudioPlayer` clip | atomic `shared_ptr` snapshot | control/TTS thread publishes; callback holds a local copy while reading |

Lock order is **buffer → stream** for control-side operations. The callback uses try-locks for both structures, performs no logging or I/O, and mixes at most 64 active voices over the requested 512-frame device period.

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

- **Device lifecycle smoke remains unverified.** The device now has idempotent stop/uninit and callback retirement, but this change was not launched through the visible device execution path.
- **Mixer timing remains unmeasured.** Voice count and requested callback period are bounded, but worst-case callback duration under producer load still needs a measurement.
- **Rate conversion is a fixed input contract for buffered clips.** The loader produces 48 kHz stereo; callers supplying other buffered formats are rejected and need an explicit conversion path.
- **Window math assumes `at_frame >= start_frame` after the reset check.** `GetReadOffset` computes `frame_index - start_frame` as unsigned; when `frame_index < start_frame` it underflows to a huge value and correctly reports "not in window" → reset. That is intentional, but the subtraction makes the *intent* non-obvious to a future reader.

## Dead code

- `AudioChunk` ([`data/audio.h`](../../engine/runtime/core/data/audio.h)) — declared, never constructed or read. Slated for removal unless a chunked API is planned.
- `audio_listener.h` — `AudioListenerData` is defined but nothing includes it; it also has a **missing trailing semicolon** on the struct, so it does not compile as-is. Not in the build — do not "fix" it as live (see [dead_code.md](../dead_code.md)).
