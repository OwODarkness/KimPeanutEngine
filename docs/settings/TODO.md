# Settings roadmap

**Status: SET2 Runtime/Render integration complete; SET1 failure-injection
acceptance remains open; SET3 is ready for implementation.**
Architecture: [PLANS.md](PLANS.md). Delivery contract:
[spec](../../.spec/specs/runtime-settings.md). Current evidence:
[planning journal](../../.spec/journal/2026-11-10-runtime-settings.md).

- [ ] **SET1 — headless registry and persistence.** Implement neutral schemas,
  explicit registration/seal, copied snapshots, scope/provenance, section edits,
  provider tokens and bounded requests/results; add layered versioned JSON with
  explicit safe save. Accept when invalid/stale edits, unknown sections, save
  failure, cancellation, late acknowledgements and provider shutdown are covered
  without Editor/Render/window construction. [Design](.plan/SET1.md).
- [x] **SET2 — Rendering startup and live settings.** Add the Render-owned
  adapter; load typed startup values before device creation, acknowledge live
  changes at a safe frame boundary, distinguish requested/active/available, and
  remove Engine's RT/PT forwarding setters and duplicated RuntimeContext flags.
  Preserve existing CLI and settings commands through the common edit path.
  Verified with headless contracts, visible Debug Vulkan/OpenGL startup and
  captures, safe-frame command edits and clean shutdown. [Design](.plan/SET2.md);
  [evidence](../../.spec/journal/2026-11-10-runtime-settings.md).
- [ ] **SET3 — Settings window.** Design captured; implementation next. Add a
  Settings entry to the main Editor header that opens a searchable window with
  category/section navigation and a Render page. Implement an Editor model and
  metadata-driven pages with search, scope/source,
  Apply/Revert/Save, conflicts, defaults, unsupported and restart feedback.
  Accept keyboard-only editing, narrow/wide layouts, no-results/unavailable
  states, failed apply/save, reopening/restart and visible Vulkan/OpenGL captures.
  All UI writes use the same validated path as commands. [Design](.plan/SET3.md).
- [ ] **SET4 — second domain and existing preferences.** Prove extension with
  one Audio section and one Editor preference; define explicit migrations for
  existing files before switching writers. Accept an extension without adding
  Engine preference fields, domain branches in SettingsRegistry, or UI types in
  runtime providers. Keep nonselected optional modules unloadable/unconstructed.
- [ ] **SET5 — remaining startup configuration.** Move VSync/window policy and
  Scene3D startup-level ownership to their domain adapters after documenting
  initialization and Asset-loading boundaries. Select only genuinely persistent
  host preferences for later pages; keep one-run host configuration typed and
  transient. Accept explicit compatibility/migration tests and relevant host
  smoke. Do not fold a general Engine startup refactor into SET1/SET2.

## Acceptance ledger

- [ ] Central catalog and settings pages require no per-preference Engine API.
- [ ] Schemas/defaults load before consuming systems initialize.
- [ ] Runtime algorithms consume typed owner-local state; UI sees copied facts.
- [ ] Queue acceptance cannot be reported as application success.
- [ ] Invalid/conflicting edits and failed writes preserve previous valid state.
- [ ] Project/user/session scopes and CLI provenance behave as documented.
- [ ] Module removal preserves unknown saved data and invalidates live bindings.
- [ ] Rendering integration preserves both APIs and GPU lifetime/history rules.
