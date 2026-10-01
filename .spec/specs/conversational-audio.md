# Conversational Audio and Live2D Speech

- Status: proposed
- Parent roadmap: [docs/audio/TODO.md](../../docs/audio/TODO.md)
- Architecture: [docs/audio/PLANS.md](../../docs/audio/PLANS.md)

## Objective

Deliver a typed-input chatbot companion that streams response text, speaks it
through TTS, optionally animates a Live2D character from audible speech, and
offers a compact independent local music player.

## Scope and invariants

The first source of response text is deterministic and local. Real LLM and
microphone/STT adapters are follow-up decisions. Runtime Audio owns playback
and its clock; TTS owns synthesis and decoding; the application owns turns;
Live2D consumes copied speech presentation data; Editor owns UI. No Runtime
to Editor dependency, TTS to Live2D dependency, or GPU/audio backend type in
their common contracts. A callback cannot read retired memory or block on
network/disk. A cancelled turn cannot publish late audible or visible events.

## Stages

1. [A1](../../docs/audio/.plan/A1.md): safe mixer, voice retirement, buses,
   and played-frame telemetry.
2. [A2](../../docs/audio/.plan/A2.md): bounded cancellable TTS transport and
   correct provider/decoder terminal states.
3. [C1](../../docs/audio/.plan/C1.md): deterministic conversation session and
   ordered text-to-speech segments.
4. [L2D9](../../docs/live2d/.plan/L2D9.md): amplitude lip-sync from played
   speech in the canonical Live2D frame transaction.
5. [P1](../../docs/audio/.plan/P1.md): compact local-track player and music
   ducking, joined with the speech demo.

## Acceptance

- [ ] Text appears incrementally, speech follows in segment order, and a new
  turn reliably interrupts the old turn.
- [ ] Fake-provider and offline audio tests cover malformed responses,
  stalls, overflow/backpressure, cancellation, frame order, and teardown.
- [ ] Live2D mouth motion follows the audible speech clock within a measured
  tolerance and closes correctly on silence/cancellation; SDK-off works.
- [ ] Music controls work independently and duck during speech without losing
  the selected volume.
- [ ] Debug builds/focused tests and a visible, audible runtime demonstration
  are recorded with configuration, fixtures, and known limitations.

## Validation and evidence

Use [validation matrix](../../docs/validation_matrix.md) level 1–2 per stage,
then visible runtime level 3 for the joined application. Do not run the engine
inside the sandbox. A dated `.spec/journal/` records actual commands, results,
audio/visual observations, and unverified paths after implementation begins.
