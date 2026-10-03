# TTS design map

The current TTS client and its Audio producer contract are described in
[tts_module.md](tts_module.md). The cross-module conversational path stays in
[Audio's architecture](../audio/PLANS.md) and [A2](../audio/.plan/A2.md).

For non-Debug launch instructions, see the
[standalone TTS Editor README](README.md).
The standalone `--mode tts` workspace is scoped in the
[editor architecture](editor/PLANS.md), [TE1](editor/.plan/TE1.md), and
[TE2](editor/.plan/TE2.md).
Its [roadmap](TODO.md) links to the editor's acceptance ledger. This mode is a
speech authoring tool; it does not add a conversation source or change Audio's
device and callback ownership.

Source placement: `engine/module/tts/editor/`. User-local settings placement:
`<project root>/config/tts/settings.json`, ignored by Git. Existing
`<project root>/tts/settings.json` files are copied into the new location on
first launch when no new settings file exists. Documentation placement:
`docs/tts/editor/`.
