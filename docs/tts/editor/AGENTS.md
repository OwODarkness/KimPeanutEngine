# Standalone TTS editor guide

Read [module architecture](../PLANS.md), [this architecture](PLANS.md),
[roadmap](TODO.md), [TE1](.plan/TE1.md), and [TE2](.plan/TE2.md). Source
for this UI belongs in
`engine/module/tts/editor/` as a separate mode target. The host owns lifetime,
the controller owns jobs and selected voice, and presentation draws snapshots
and returns actions. Do not call synchronous synthesis from an ImGui frame or
read the Audio callback's mutable buffers.

The required settings file is `project_root/config/tts/settings.json`. Create
it on first launch if absent; migrate an existing
`project_root/tts/settings.json` by copying it only when the new file is absent.
Validate and report malformed or unwritable settings without silently replacing
them. `ref_audio_path` names a path visible to the TTS server, not a client-side
upload. Never commit local values.
