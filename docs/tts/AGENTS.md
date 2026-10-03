# TTS documentation guide

Read the [project guide](../../AGENTS.md), [TTS design map](PLANS.md),
[roadmap](TODO.md), and the relevant [editor plans](editor/PLANS.md) before
implementing the standalone mode. The [current client walkthrough](tts_module.md)
is historical in places; verify behavior against source and the
[A2 plan](../audio/.plan/A2.md).

Keep the provider, decoder, and Audio ownership in the existing Runtime modules.
Place standalone-mode host, controller, settings adapter, and ImGui presentation
under `engine/module/tts/editor/`, in a separate target so core `TTS` never
depends on Editor. Runtime Audio owns voices and played-frame time. Do not put
provider paths or reference text in code, committed configuration, logs, or
test captures. Follow the [validation matrix](../validation_matrix.md); a
working GUI requires a visible runtime check outside the sandbox.
