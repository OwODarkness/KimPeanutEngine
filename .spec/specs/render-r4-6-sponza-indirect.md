# R4.6 general indirect transport repair

Status: active, 2026-09-26. Parent: [general RT design](../../docs/render/.plan/R4.6-general-scene.md).

Objective: render readable Sponza indirect illumination from its authored sun,
with environment intensity zero, through the same material/transport path as
Cornell. Reference imagery is qualitative; no exact appearance match required.

Diagnosis: imported metallic/roughness factors multiply texture channels.
RT currently ignores these maps and samples diffuse continuation only, causing
factor-one materials to terminate their indirect throughput.

Scope: resolve MR texture indices/channels using existing Render material
bindings; evaluate them at hit UVs; sample diffuse/GGX continuation with its
mixture PDF and consistent BRDF; add a surface-parameter probe; bump integrator
revision. No extra lights, sky intensity changes, or level-specific shader rules.

Invariants: preserve pre-existing edits; Render owns material semantics,
Graphics owns texture slots and GPU lifetime; shared shader-record ABI remains
consistent; history resets on material/texture/integrator changes.

Acceptance: fresh Sponza beauty/direct-only/albedo/surface-parameter captures,
measurable indirect light on shaded receivers with environment disabled,
readable scene, Cornell smoke without fixture-specific shading, no logged
Vulkan errors and zero tracked RT resources at close. Build/focused tests/full
CTest and honest remaining limitations recorded in the journal.
