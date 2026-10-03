# TTS roadmap

Architecture: [PLANS.md](PLANS.md). Existing service work:
[Audio A2](../audio/.plan/A2.md). The standalone workspace has its own
[TE1 acceptance ledger](editor/TODO.md).

- [x] **TE1 — standalone speech editor:** build `--mode tts` around the current
  asynchronous TTS and Audio paths, load and create local settings, and offer
  text entry, generation, voice selection, job state, cancel, and truthful
  stream/buffer playback panels selected from settings. See
  [design](editor/.plan/TE1.md).
- [x] **TE2 — durable dialog library and WAV export:** define session/project
  persistence and an encoded-audio export seam with explicit lifetime and
  overwrite policy. Do not infer a generated WAV from the player handle.
- [ ] **TE3 — optional synthesis controls:** implement Emotion, Pitch, and
  server-supported synthesis speed only after capability mapping and tests;
  keep unsupported draft controls hidden or disabled meanwhile.
