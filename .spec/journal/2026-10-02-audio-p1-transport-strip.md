# Audio P1 transport-strip risk fixes

- Baseline: commit `5db1811` (`feat(audio): complete player workspace and project library`).
- Objective: address the high-risk shared-presentation findings in
  `docs/audio/.review/P1.md` without coupling core TTS to Editor or ImGui.
- Changes: added an EditorUILib-only transport strip with stable instance IDs,
  explicit playback states and action capabilities, optional known duration,
  disabled seeking for unknown-duration playback, and separate `stop_voice`
  and `cancel_job` outputs. The standalone music preview now composes it and
  maps Audio state into the presentation snapshot. Iconless buttons retain
  stable IDs. Updated the P1 ownership plan and review statuses.
- Ownership: Audio still owns playback clocks and voice lifetime. The music
  controller owns queue/import policy. EditorUILib owns only copied display
  data, draw calls, and returned intents. Core TTS has no new Editor dependency.
- Validation: `cmake --build build --config RelWithDebInfo --target
  KimPeanutEngine` passed and built the `TTS` target in the same graph. A
  visible Vulkan real-track capture was saved at
  `save/screenshots/validation/audio-p1-transport-strip-final.png`.
- Not run: unit tests, TTS compact-surface interaction, multiple simultaneous
  strip input/focus checks, and TTS cancel/late-audio runtime scenarios.
- Remaining risk: a future TTS UI owner still needs a control-thread-safe
  played-cursor snapshot keyed by job token before displaying audible
  playback progress. Unknown duration must remain indeterminate until that
  snapshot can provide a trustworthy total.
