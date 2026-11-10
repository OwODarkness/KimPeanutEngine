# Settings module guide

Follow the repository [agent contract](../../AGENTS.md),
[implementation workflow](../agent_implementation_workflow.md), and
[validation matrix](../validation_matrix.md).

Read [PLANS.md](PLANS.md) for ownership and [TODO.md](TODO.md) for stage status.
The architecture is proposed; these documents do not establish implemented APIs.

- Settings infrastructure owns registration, configuration layers, persistence,
  edit routing, and published status. Each domain owns typed settings,
  semantic validation, capability resolution, and application side effects.
- Register schemas explicitly before loading startup settings. Bind live
  providers after constructing their owners; detach them before owner teardown.
- Reuse Reflection values/metadata and Render's request boundary. Do not expose
  live subsystem objects, EnTT access, ImGui, or native GPU types to consumers.
- Keep RuntimeSettings independent of RuntimeLib, Editor, and concrete domains.
  Place domain adapters beside their domain, not inside SettingsRegistry.
- Test layered resolution, failed saves, stale revisions, pending application,
  provider lifetime, and cancellation. A queued request is not an applied edit.
- GUI and rendering integration require visible Debug Vulkan/OpenGL checks
  through the approved external-desktop launch and Runtime capture path.
- Keep launch captures, exit-after-capture, fixture inputs, and runtime status
  outside persisted preferences unless a stage explicitly establishes a use case.

Architecture belongs in PLANS.md, stage details in `.plan/`, acceptance in
TODO.md, and factual evidence in the linked `.spec/journal/` record.
