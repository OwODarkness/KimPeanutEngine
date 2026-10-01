# Conversational Audio and TTS Risk Repair

- Status: A1 complete; A2 partial
- Date: 2026-10-01
- Spec: [conversational audio](../specs/conversational-audio.md)
- Parent TODO: [Audio roadmap](../../docs/audio/TODO.md)

## What was done

- Closed unsafe device teardown and moved callback reads to fixed stable voice
  slots with reader-count retirement after callback access ends.
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
- Completed A1 with per-player callback-boundary command queues,
  configurable `AudioSystemSettings` bounded by fixed 64-voice/512-frame
  storage, independent bus/master ramps, a played-frame clock, and callback
  telemetry. Added a visible `AudioDeviceSmoke` path and stream
  overflow/cancellation contracts.

## What changed

- Architecture or behavior: Audio owns stable published voice slots and
  callback reader retirement; `AudioSystemSettings` adjusts voice/work/ramp/
  period budgets within fixed storage ceilings. Audio remains the device and
  playback owner. TTS keeps provider and decoded
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
- Command: `cmake --build build --config Debug --target AudioUnitTest AudioDeviceSmoke`
- Result: PASS.
- Command: `ctest --test-dir build -C Debug -L audio --output-on-failure`
- Result: PASS, 21/21 audio contracts. `-R AudioUnitTest` matches no tests
  because CTest exposes the discovered GoogleTest case names; the audio label
  selects the module suite.
- Command: `AudioUnitTest.exe --gtest_filter=AudioMixerTest.MeasuresWorstCaseVoiceMixDuration`
- Result: PASS; 64 stereo voices x 512 frames measured 1,819,000 ns maximum
  callback duration in Debug on this host.
- Command: approved host execution of `AudioDeviceSmoke.exe`
- Result: PASS; Senary Audio output at 48 kHz stereo, estimated 32 ms device
  buffer latency, all 24,000 tone frames consumed, maximum observed callback
  453,800 ns, device stopped and uninitialized cleanly.
- Command: `ctest --test-dir build -C Debug --output-on-failure`
- Result: PARTIAL, 1,080/1,085 pass. Four unrelated `AssetCatalogProviderTest`
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
- No sanitizer run was available in this pass. The checked stress contract
  exercises concurrent create/control/destroy and callback mixing, including
  repeated slot reuse.
- Callback timing is one Debug host measurement with 64 active clip voices;
  a separate producer-contention timing distribution was not captured.
- Server revision is not pinned. The local HTTP response confirms current
  protocol shape but not compatibility with a declared revision.

## Remaining work

- A1 is complete. Add per-job tokens/cancellation and distinguish successful decode from
  audible playback drain.
- Add fake-provider contracts and measure callback timing under producer load.
- Resolve or baseline the five unrelated full-suite failures before claiming a
  completely green Level 4 run.

## Documentation and follow-up

- Updated the audio and TTS module notes, A1/A2 review dispositions, and
  `docs/status.md`.
- The roadmap remains open until the remaining A1/A2 checks and later C1/L2D9/P1
  stages are completed.
