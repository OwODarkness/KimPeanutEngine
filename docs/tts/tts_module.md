# TTS Module Design

Cancellable conversational speech and Live2D integration are planned in the
[Conversational Audio architecture](../audio/PLANS.md) and
[roadmap](../audio/TODO.md). This document describes the current client.
The [A2 baseline review](../audio/.review/A2.md) preserves the original
findings and their 2026-10-01 repair disposition. This page's source walkthrough
is the earlier baseline where it conflicts with the current contracts below.

## Current safety contract (2026-10-01)

- Initialization starts the worker only after provider initialization succeeds;
  destruction and repeated shutdown stop the provider before joining the worker.
- The asynchronous queue is capped at 16 pending requests. Rejected and
  shutdown-abandoned requests receive a failed result.
- Synthesis requires an initialized Audio system and returns a valid player
  handle only for decoded playable audio. Error codes are initialized.
- The provider quarantines at most 32 MiB until HTTP status and WAV-compatible
  content type are known, then delivers bounded chunks. Failed responses never
  reach the decoder. This preserves correctness with the current httplib API,
  at the cost of delaying playback until the response body is received.
- WAV parsing is bounded and incremental; malformed sizes, truncated final
  frames, unsupported formats, and rejected FIFO writes fail the job.
- Per-job cancellation and audible-drain completion events remain open work.

Location: `engine/module/tts/` (static lib `TTS`, folded into the `Module` INTERFACE target)

The TTS module is the engine's **text-to-speech client** — the producer half of the audio pipeline. It turns a request (text + voice reference) into audible audio by talking to a TTS server, then hands the result to the [audio module](../audio/audio_module.md) for playback. Today the only provider is **GPT-SoVITS over HTTP**, but the provider interface is the seam where any TTS backend (cloud API, local service, offline engine) would slot in.

`TTS` links `Core` and `Audio` PUBLIC, and `Asset`/`Data`/`Log`/`httplib`/`nlohmann`/`miniaudio` PRIVATE. The `IAudioLoader` it uses for the buffered path comes from the asset module.

## Key types — [`types.h`](../../engine/module/tts/types.h)

| Type | Meaning |
|---|---|
| `TTSProviderType` | Which backend. Only `GPT_SOVITS` today — the enum exists so a second backend doesn't touch `TTSSystem`. |
| `ServerConfig` | `host`, `port`, `api_path`, `timeout` (e.g. `127.0.0.1:9880 /tts`, 180 s). |
| `TTSRequest` | `text`, `text_lang`, `ref_audio_path`, `prompt_text`, `prompt_lang`, `streaming`. The voice reference is a *path on the server's machine*, not a file uploaded by the client. |
| `TTSResult` | `success`, `error_code`, `error_message`, and — the interesting part — `audio::AudioHandle player_handle`. The system returns the *player* it created, not the bytes; the caller controls playback through the handle. |

## Interface — [`tts_provider.h`](../../engine/module/tts/tts_provider.h)

`ITTSProvider` is the backend contract:

- `Initialize(ServerConfig)` / `ShutDown()` — set up and tear down the client.
- `SynthesizeBuffer(request, OnData, OnError)` — one-shot; the whole audio arrives in a single `OnData` call.
- `SynthesizeStream(request, OnData, OnFinish, OnError)` — incremental; `OnData` fires per received chunk, `OnFinish` at end-of-stream.

The callbacks are `std::function`s: `AudioDataCallback = std::function<bool(const uint8_t*, size_t)>`, `ErrorCallback`, `FinishCallback`. The `bool` return on `OnData` lets a consumer abort an in-flight synthesis.

## System — [`tts_system.h`](../../engine/module/tts/tts_system.h), [`tts_system.cpp`](../../engine/module/tts/tts_system.cpp)

`TTSSystem` is the facade. Two entry points:

- **`SyncSynthesize(request)`** — blocks until synthesis finishes. It creates an audio player, wires the provider callbacks into the player/decoder, calls the provider, and returns a `TTSResult` carrying the player handle.
- **`AsyncSynthesize(request, callback)`** — enqueues a `TTSTask` (request + callback) on a queue; a dedicated worker thread (`WorkerLoop`) pops tasks and runs `SyncSynthesize` on each, invoking the callback with the result. The queue is guarded by `mutex_` and signaled by `cv_`.

`TTSSystem` owns a `unique_ptr<ITTSProvider>`, an `IAudioLoader` (for the buffered path), a worker thread, and a non-owning `audio::AudioSystem* audio_system`. The caller initializes and assigns Audio before synthesis. Initialization is transactional, the destructor calls idempotent shutdown, shutdown asks the provider to stop before joining, and the pending queue is bounded.

### The two synthesis paths

**Streaming (`request.streaming == true`)** — the audio module's streaming path ([audio_module.md](../audio/audio_module.md)):

```cpp
CreateAudioPlayer(Stream) → StreamAudioPlayer
set stream = make_shared<AudioStream>({ channels=1, sample_rate=48000 }, buffer_seconds)
AudioStreamDecoder decoder(stream)
player->SetStream(stream)

OnData:  decoder.Feed(data, size); player->Play();
OnFinish: decoder.Finish();
```

The FIFO has a fixed **20-second** capacity. Overflow returns failure without discarding older speech. The provider currently quarantines the complete response (up to 32 MiB) to validate HTTP status and content type before feeding 64 KiB chunks to the decoder. Each valid chunk starts or continues the player; WAV EOF and playback drain remain distinct states.

**Buffered (`request.streaming == false`)**:

```cpp
CreateAudioPlayer(Buffer) → BufferAudioPlayer
OnData:  player->SetClip(audio_loader_->LoadFromMemory(data, size).data); player->Play();
```

The whole response body is decoded into an `AudioClip` at once via the asset module's `MiniAudio_AudioLoader::LoadFromMemory`, and the buffered player plays it. Note the buffered `OnData` captures `player_handle` — the streaming path assigns it on `TTSResult`; the buffered path assigns it inside the callback. (Both end up returning a handle, but the streaming one also has `stream_` wired before synthesis.)

## Provider — [`gpt_sovits_tts.cpp`](../../engine/module/tts/gpt_sovits_tts.cpp)

`GPTSovitsTTS` is the GPT-SoVITS client over `httplib::Client`:

- `BuildRequest` — a JSON body for the server's `/tts` API: `text`, `text_lang`, `ref_audio_path`, `prompt_lang`, `prompt_text`, plus synthesis knobs (`text_split_method: "cut4"`, `batch_size: 1`, `streaming_mode`, `sample_steps: 16`, `overlap_length: 2`, `min_chunk_length: 16`).
- `SynthesizeBuffer` — a blocking `client_->Post` with a bounded receiver. It validates HTTP status and content type before delivering the complete body once to `OnData`.
- `SynthesizeStream` — a bounded receiver quarantines the response because this vendored streaming `Post` overload does not expose headers before body callbacks. It validates status/media type, delivers chunks to `OnData`, propagates consumer abort, and calls `OnFinish` only after successful delivery.

## Data flow

```
TTSRequest (text, voice ref)
    └─ TTSSystem::AsyncSynthesize / SyncSynthesize
         └─ GPTSovitsTTS.Synthesize{Buffer,Stream}
              └─ httplib HTTP → GPT-SoVITS server (/tts)
                   ├─ buffered: full body → MiniAudio_AudioLoader → AudioClip → BufferAudioPlayer
                   └─ streaming: chunk → AudioStreamDecoder → AudioStream FIFO → StreamAudioPlayer
                                                                      ↑ audio callback (audio module)
```

The server is external and stateful — the voice reference (`ref_audio_path`) and prompt live on the server, so the client is effectively stateless per request.

## Example — [`engine/example/tts/tts_example.cpp`](../../engine/example/tts/tts_example.cpp)

`TTSExample(reference_audio_path)` shows the localhost:9880 `/tts` wiring with a caller-supplied server-side voice path. It checks initialization and synthesis results, waits at most three minutes for audible drain, and returns instead of spinning forever.

## Known smells / next steps

- **`SyncSynthesize` blocks the caller for the full synthesis.** `AsyncSynthesize` is the intended non-blocking route; `Sync` exists for examples and single-shot use. The worker serializes requests and caps the pending queue at 16; per-task cancellation is still open.
- **Per-job cancellation and audible completion events are not implemented.** Shutdown aborts the active provider request and queue capacity is bounded, but callers cannot replace or cancel one queued/in-flight request independently.
- **Quarantining delays streaming playback.** The current httplib overload does not surface response headers before the body callback, so playback begins after full response validation rather than as bytes arrive.
- **`TTSRequest` is server-path-addressed.** `ref_audio_path`/`prompt_text` refer to server-side state; there is no file upload or multipart. Document this at the API boundary so it isn't mistaken for a client-side asset reference.
