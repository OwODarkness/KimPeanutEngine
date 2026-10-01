# Conversational Audio and TTS Risk Repair

- Status: partial
- Date: 2026-10-01
- Spec: [conversational audio](../specs/conversational-audio.md)
- Parent TODO: [Audio roadmap](../../docs/audio/TODO.md)

## What was done

- Closed the unsafe device teardown path and moved callback reads to bounded,
  immutable player snapshots with shared ownership and deferred control-thread
  reclamation.
- Added a 64-active-voice cap, fixed the mono-to-stereo path, copied complete
  frames instead of returning stream-buffer pointers, and made callback refill
  use nonblocking locks once per voice per block.
- Made stream overflow explicit and terminal state synchronized with writes.
- Reworked WAV parsing to distinguish incomplete from invalid input, validate
  bounded RIFF/PCM metadata and data length, preserve channel frames, and carry
  resampling phase across chunks.
- Made TTS worker initialization transactional and shutdown RAII-safe, bounded
  queued tasks, required initialized Audio, and only returned playable handles.
- Made GPT-SoVITS response delivery validate HTTP status and content type
  before decoding, using a 32 MiB response quarantine for the current httplib
  API. Reworked the manual example to accept its server-side reference path and
  return after a bounded drain wait.

## What changed

- Architecture or behavior: Audio publishes an immutable callback snapshot;
  Audio remains the device and playback owner. TTS keeps provider and decoded
  source ownership and no longer returns raw player ownership to callers.
- Important files/modules: `engine/runtime/audio/**`, `engine/module/tts/**`,
  `engine/example/tts/**`, and audio unit contracts.
- Public API or ownership changes: `AudioSystem::GetAudioPlayer` now returns a
  `shared_ptr`; `AudioPlayer` exposes a copied-frame read; `AudioStream::PushFrames`
  reports capacity failure; decoder `Feed` returns `AudioDecodeResult`.

## Validation

- Required level: L4 — shared public Audio interfaces changed.
- Command: `cmake --build build --config Debug`
- Result: PASS.
- Command: `ctest --test-dir build -C Debug -R Audio --output-on-failure`
- Result: PASS, 12/12 audio tests.
- Command: `ctest --test-dir build -C Debug --output-on-failure`
- Result: PARTIAL, 1,071/1,076 pass. Four unrelated `AssetCatalogProviderTest`
  cases fail because expected partial/live asset nodes are absent; one unrelated
  `TerrainMeshTest.CoarseAndFineSamplingShareWorldPositionsAndGlobalNormals`
  expects 24 indices but receives 96. These failures reproduce when run by
  themselves. No Asset or Terrain source was changed.
- Local GPT-SoVITS endpoint: one valid project-shaped request returned HTTP 200,
  `audio/wav`, and 236,844 bytes beginning with a RIFF/WAVE header. An incomplete
  request returned HTTP 400, `application/json`, 40 bytes. The response was not
  saved or played; this direct HTTP probe did not execute the compiled provider.

## Remaining risks and unverified areas

- Per-job cancellation and replacement are not implemented. Shutdown can stop
  the active httplib request, but a session cannot cancel one request alone.
- The current response quarantine means streaming playback begins after the
  whole response is received. The response is bounded to 32 MiB.
- No fake-provider TTS unit target exists yet; the compiled provider path needs
  focused tests for non-200, media mismatch, empty, and consumer-abort responses.
- Device initialization/shutdown was not exercised through the visible runtime
  path, and audible drain was not observed.
- Worst-case callback duration under producer contention was not measured.
- Server revision is not pinned. The local HTTP response confirms current
  protocol shape but not compatibility with a declared revision.

## Remaining work

- Add per-job tokens/cancellation and distinguish successful decode from
  audible playback drain.
- Add fake-provider contracts and measure bounded callback time.
- Run the approved visible device lifecycle and audible playback smoke.
- Resolve or baseline the five unrelated full-suite failures before claiming a
  completely green Level 4 run.

## Documentation and follow-up

- Updated the audio and TTS module notes, A1/A2 review dispositions, and
  `docs/status.md`.
- The roadmap remains open until the remaining A1/A2 checks and later C1/L2D9/P1
  stages are completed.
