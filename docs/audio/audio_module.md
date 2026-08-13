# Audio Module Design

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

The abstract base. Two contracts matter:

- **`GetFrameData(src, out_data)`** — `src` is a **sample offset** (`frame * channels`), matching the `Mix` loop, which passes `frame * in_channels`. It must return a pointer to `channels` interleaved samples at that offset, or `false` when the frame isn't available yet. `Mix` treats a `false` as "output silence this frame" — it is **not** an end signal.
- **`ResolveFrame(new_frame)`** — decides whether `new_frame` is playable; the derived player fills `new_frame` in (e.g. loop-wrap, clamp-to-end).
- **`SetCurrentFrame(new_frame)`** — the bridge: calls `ResolveFrame`, then **always** writes `current_frame_ = new_frame` regardless of the result. This was a deliberate fix (see [History](#history--bug-log)): for a streaming player the playhead must follow the buffer window even while the source is dry, or the player re-reads the last frame in a loop.

`FillBuffer()` (virtual, default no-op) is the streaming player's hook: pull more source data in so a currently-unavailable frame can become available. `AdvanceFrame`/`SeekFrames`/`SeekSeconds` are the movement API on top of `SetCurrentFrame`.

### `BufferAudioPlayer` — [`buffer_audio_player.cpp`](../../engine/runtime/audio/buffer_audio_player.cpp)

Holds a whole `AudioClip`. `GetFrameData` is a direct index into `clip_->pcm` (so `src` must be a sample offset). `ResolveFrame`: in-range → ready; past the end → wrap on `looping_`, else clamp to the last frame and set `Finished`. Fully synchronous — no refill, no lock.

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

A **thread-safe FIFO ring buffer** — the producer/consumer seam. Capacity is `sample_rate * channels * buffer_seconds`, where `buffer_seconds` is a constructor parameter defaulting to **20** (a caller's trade-off: large enough to absorb a producer that bursts faster than real-time playback, small enough to bound memory — the TTS path sizes it from the request, see [tts_module.md](../tts/tts_module.md)). `PushFrames` (producer) and `ReadFrames` (consumer) are each a `memcpy`-style wrap around a `vector<float>` guarded by `mutex_`. `PushFrames` drops the oldest samples when full; a packet larger than the whole buffer keeps only its newest tail. `Finish()`/`IsFinished()` mark end-of-stream (a flag, not a buffer condition). **The FIFO is destructive** — `ReadFrames` consumes; a consumer that falls behind loses data, and a dry FIFO returns 0 frames, not a stall.

### `AudioStreamDecoder` — [`audio_stream_decoder.cpp`](../../engine/runtime/audio/audio_stream_decoder.cpp)

The incremental decoder. `Feed(data, size)` accumulates bytes, parses the RIFF/WAVE header once (chunks scanned, `fmt `/`data` located, header bytes erased), then decodes **16-bit PCM** into `float` and **linearly resamples** from the file's rate to the stream's rate (e.g. 32 kHz → 48 kHz). Only 16-bit PCM is supported; anything else logs an error and refuses. **Important:** the stream rate it targets comes from the `AudioStream` it was constructed with, not from the file.

### `AudioSystem` / `MiniAudioSystem` — [`audio_system.cpp`](../../engine/runtime/audio/audio_system.cpp), [`miniaudio_audio_system.cpp`](../../engine/runtime/audio/miniaudio_audio_system.cpp)

`AudioSystem` is the handle registry: `CreateAudioPlayer(type)` allocates a handle and constructs the matching player (slots double as the handle→player map); `GetAudioPlayer` validates the generation; `DestroyAudioPlayer` resets the player. `MiniAudioSystem` is the concrete backend: it initializes a `ma_device` (48 kHz stereo, `f32`, playback) and installs `Mix` as the data callback. **`Mix` runs on the miniaudio callback thread** — this is the audio thread everything downstream is measured against.

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
         └─ AudioStream::PushFrames(samples)    # thread-safe 10 s FIFO
                                                   ↑ (consumer)
audio callback (miniaudio thread)
    └─ Mix(frame by frame)
         └─ StreamAudioPlayer::GetFrameData / AdvanceFrame
              └─ ring window ← AudioStream::ReadFrames  (via Refill/FillBuffer)
```

The two threads never touch each other's structures directly — the seam is the `AudioStream` FIFO (`mutex_`), and the player's ring buffer (`buffer_mutex_`) mediates between the playhead and the FIFO.

## The streaming window

`Refill(at_frame)` (caller holds `buffer_mutex_`) keeps the window ahead of the playhead:

1. If the playhead is **outside** the window (ahead of it or behind `start_frame`), restart the window there (`start_frame = at_frame`, `filled_frames = 0`). In steady state this never fires — it only rescues a stale playhead.
2. Compute `unplayed = (start_frame + filled_frames) - at_frame`. If `unplayed > LOW_WATER_MARK`, nothing to do — plenty of data ahead.
3. If the buffer is full, **slide** the window: `memmove` the unplayed tail to the front, advance `start_frame`. (This replaces what used to be a reset-from-zero, which is what caused the stutter — see [History](#history--bug-log).)
4. Read up to `min(max_write, CACHE_SIZE_FRAMES / 4)` frames from the stream into the free tail, increment `filled_frames`.

`ResolveFrame(new_frame)` calls `Refill(new_frame)`, then checks `new_frame < start_frame + filled_frames`. If the frame is available → ready. If not, it is a **temporary underrun**: return false and leave the playhead put (Mix outputs silence and retries); only `stream_->IsFinished()` promotes the player to `Finished`.

`FillBuffer()` is the same refill driven from `Mix`: it locks `buffer_mutex_`, `Refill(current_frame_)`, and reports whether the playhead is now readable. `Mix` calls it only when `GetFrameData` failed.

## Threading model

| Structure | Guard | Owner threads |
|---|---|---|
| `AudioStream` FIFO | `AudioStream::mutex_` | producer = network/httplib thread, consumer = audio thread |
| `StreamAudioPlayer` ring window | `buffer_mutex_` | audio thread (Mix), plus `Play()`/`SetStream` from the network thread |
| `BufferAudioPlayer` clip | **none** | set from worker/network thread, read from audio thread — see smells |

Lock order is **buffer → stream** (`Refill` holds `buffer_mutex_`, then locks `AudioStream::mutex_` inside `ReadFrames`); there is no reverse path, so no deadlock. The audio callback is the real-time constraint: `Mix` must never block on disk or network — `FillBuffer`/`ReadFrames` are pure in-memory moves guarded by short-held mutexes.

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

- **FIFO is destructive.** The seam between producer and consumer loses data if the consumer falls behind. The 10 s capacity is the whole protection; if a network burst outpaces playback for >10 s, the head is dropped.
- **`Mix` never signals "waiting" to the caller.** A dry source is expressed as repeated `GetFrameData` failures — every output frame in a dry stretch locks `buffer_mutex_` and re-attempts `ReadFrames`. Correct, but a hot path when starved (see smells).

## Known smells / next steps

- **Mono stream → stereo mix reads one sample out of bounds.** In `MiniAudioSystem::Mix`, the stereo branch reads `data[0]` and `data[1]`. For a mono `StreamAudioPlayer` (`channels == 1`), `data[1]` is the *next* frame's sample (and one-past-the-window at the last buffered frame). Audibly it's a sub-sample shift, but at the window boundary it's a heap over-read. The mixer should upmix mono explicitly (`v`, `v`) when `in_channels == 1`.
- **Buffer path has a data race on `clip_`.** `BufferAudioPlayer::SetClip` runs on the TTS worker/network thread while `GetFrameData` reads `clip_` on the audio thread with no lock. Benign in practice on x86 (pointer-sized store), but formally UB — worth a `std::atomic` or a set-before-play convention enforced at the call site.
- **`state_`/`current_frame_` are written from non-audio threads.** `Play()` is called from the httplib receiver thread; `Stop()`/`current_frame_ = 0` writes race with the audio thread's reads. `AudioState` is `uint8_t`; an `std::atomic` or a tiny command queue would close it.
- **Dry-source hot path.** When the stream is empty, `Mix` calls `FillBuffer()` once per output frame — each grabs two mutexes for a no-op read. A "dry since last fill" guard or a periodic (not per-frame) refill would cut the lock churn.
- **`AudioStreamDecoder` is single-format.** Only 16-bit PCM is accepted; `bits_per_sample != 2` is an error. 24/32-bit and float WAVs would be rejected loudly — fine for GPT-SoVITS today, but the decoder should grow a format switch rather than a hardcoded path.
- **`ResampleLinear` last-sample edge.** When `idx >= input.size() - 1` it repeats `input.back()`, so the final resampled samples can duplicate the last source sample at the tail. Cosmetically irrelevant here, but a clamped linear interpolator would be exact.
- **Window math assumes `at_frame >= start_frame` after the reset check.** `GetReadOffset` computes `frame_index - start_frame` as unsigned; when `frame_index < start_frame` it underflows to a huge value and correctly reports "not in window" → reset. That is intentional, but the subtraction makes the *intent* non-obvious to a future reader.

## Dead code

- `AudioChunk` ([`data/audio.h`](../../engine/runtime/core/data/audio.h)) — declared, never constructed or read. Slated for removal unless a chunked API is planned.
- `audio_listener.h` — `AudioListenerData` is defined but nothing includes it; it also has a **missing trailing semicolon** on the struct, so it does not compile as-is. Not in the build — do not "fix" it as live (see [dead_code.md](../dead_code.md)).
