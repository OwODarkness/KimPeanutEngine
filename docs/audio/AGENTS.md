# Audio and conversational speech documentation guide

Read the [project contract](../../AGENTS.md), [architecture](PLANS.md),
[roadmap](TODO.md), and the relevant `.plan/` stage before implementation.
The [current audio](audio_module.md) and [TTS](../tts/tts_module.md) notes
describe the baseline, including known defects; verify claims against source.

## Boundaries

- Runtime Audio owns device output, mixing, playback handles, clocks, and
  callback-safe control. It has no knowledge of TTS, chat, Live2D, or ImGui.
- TTS owns synthesis requests, provider protocol, decoding, and delivery of
  speech audio. It does not own the device, mixer, or character instance.
- The application composition layer owns conversation turns and binds a response
  source, TTS, Audio, optional Live2D, and Editor presentation. A model provider
  remains replaceable; no service credentials belong in engine assets or logs.
- Live2D consumes copied speech envelope/viseme data on its update thread. It
  neither calls a TTS provider nor reads an audio device or player internals.
- The audio callback must do bounded in-memory work without network, disk,
  allocation, logging, or unbounded waits. Voice and music lifetimes must remain
  valid until the callback can no longer read them.
- Audio and TTS do not create a Runtime to Editor dependency. Optional Live2D
  and Editor targets must retain their SDK-off and headless build paths.

## Validation

Follow the [validation matrix](../validation_matrix.md). Add offline audio
contracts for mixer, clock, stream, cancellation, and envelope behavior. A
runtime speech/player stage requires a visible engine launch outside the
sandbox, audible and captured/diagnostic evidence, and clean shutdown; a build
alone cannot establish those outcomes. Use synthetic PCM and a fake provider
for deterministic tests. Keep real voice/model fixtures out of the repository
until their use and redistribution rights are established.

Record implementation evidence in `.spec/journal/` after work starts. This
directory holds the design and roadmap, not an implementation diary.
