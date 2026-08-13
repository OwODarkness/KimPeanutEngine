# TTS Module Design

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

`TTSSystem` owns a `unique_ptr<ITTSProvider>`, an `IAudioLoader` (for the buffered path), a worker thread, and a public `audio::AudioSystem* audio_system` — **the caller must assign this before synthesizing**; the system does not own or initialize the audio backend (see the example). `ShutDown` flips `running_ = false`, notifies the worker, joins it, then shuts the provider down.

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

The FIFO `buffer_seconds` is **sized from the request text** (`EstimateStreamBufferSeconds`): the server may deliver audio faster than real-time playback, so the FIFO must hold the whole lead or the head of the clip gets dropped. The heuristic treats UTF-8 CJK as ~3 bytes/char at ~5 chars/sec of speech (~15 bytes/sec), clamped to **[5, 60] seconds** — over-sizing only costs memory, under-sizing drops audio. Each `OnData` chunk is fed to the decoder **and** starts the player (harmless if already playing — `Play()` is idempotent in `StreamAudioPlayer`). The decoder parses the WAV header out of the first chunk, then pushes resampled samples into the FIFO as they arrive; the audio callback pulls them in real time. **`Play()` per chunk is what lets playback begin as soon as the first chunk is decodable** — this is the correct call pattern for a stream.

**Buffered (`request.streaming == false`)**:

```cpp
CreateAudioPlayer(Buffer) → BufferAudioPlayer
OnData:  player->SetClip(audio_loader_->LoadFromMemory(data, size).data); player->Play();
```

The whole response body is decoded into an `AudioClip` at once via the asset module's `MiniAudio_AudioLoader::LoadFromMemory`, and the buffered player plays it. Note the buffered `OnData` captures `player_handle` — the streaming path assigns it on `TTSResult`; the buffered path assigns it inside the callback. (Both end up returning a handle, but the streaming one also has `stream_` wired before synthesis.)

## Provider — [`gpt_sovits_tts.cpp`](../../engine/module/tts/gpt_sovits_tts.cpp)

`GPTSovitsTTS` is the GPT-SoVITS client over `httplib::Client`:

- `BuildRequest` — a JSON body for the server's `/tts` API: `text`, `text_lang`, `ref_audio_path`, `prompt_lang`, `prompt_text`, plus synthesis knobs (`text_split_method: "cut4"`, `batch_size: 1`, `streaming_mode`, `sample_steps: 16`, `overlap_length: 2`, `min_chunk_length: 16`).
- `SynthesizeBuffer` — one blocking `client_->Post`; the whole body goes to `OnData` once. Errors (connect failure, non-200) go to `OnError` and return `false`.
- `SynthesizeStream` — `client_->Post` with a content receiver: every HTTP chunk arrives via `OnData`. On a successful response it calls `OnFinish()`. Note the success/failure branches are ordered oddly (success is checked first, then `!response`, then a dead `status != 200` branch that is unreachable because a non-null response with a bad status returns `true` from the first branch) — see smells.

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

`TTSExample()` (invoked from `engine/editor/main.cpp` by uncommenting) shows the full wiring: `ServerConfig` (localhost:9880 `/tts`, 180 s timeout), a `MiniAudioSystem` created and initialized **first**, assigned to `tts->audio_system`, then `SyncSynthesize` with a Japanese request (`streaming = true`). The example then blocks in `while(1)` — run it by hand, not from an agent shell.

## Known smells / next steps

- **`SynthesizeStream` has a dead error branch.** The final `response->status != 200` check is unreachable: the success branch (`if (response) { OnFinish(); return true; }`) already returns true for *any* non-null response, including a 500. Non-200 bodies are therefore treated as success. Fix: check `response && response->status == 200` up front.
- **`OnData` return value is ignored by the streaming path.** The receiver lambda always returns `true`, so a consumer can't abort mid-stream; the `AudioDataCallback`'s `bool` is only meaningful if the provider honors it.
- **Buffered path assigns `player_handle` inside the callback; streaming assigns it on the result.** Inconsistent — and if the buffered `OnData` never fires (empty response), the handle is unassigned. Assign on the result path consistently.
- **`SyncSynthesize` blocks the caller for the full synthesis.** `AsyncSynthesize` is the intended non-blocking route; `Sync` exists for examples and single-shot use. The worker loop serializes all tasks — there is no concurrency limit knob and no per-task cancellation.
- **No retry / timeouts beyond httplib's.** `config.timeout` (180 s) is applied as connect+read timeout; a stalled stream stalls the caller's worker thread until then. A per-request watchdog is a future hardening step.
- **`TTSRequest` is server-path-addressed.** `ref_audio_path`/`prompt_text` refer to server-side state; there is no file upload or multipart. Document this at the API boundary so it isn't mistaken for a client-side asset reference.
- **`total` global in `gpt_sovits_tts.cpp` is dead.** A file-scope `size_t total = 0` is written by the progress lambda and never read; remove it.
