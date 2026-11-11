# Settings architecture

**Status: SET2 integrated; SET1 failure-injection and SET3 acceptance follow-up remain open.**

This module provides centrally discoverable settings with domain-owned behavior
and a shared Editor settings window. [TODO.md](TODO.md) owns stage acceptance;
the [execution spec](../../.spec/specs/runtime-settings.md) owns delivery scope.

## Why this exists

Engine currently exposes `SetRayTracingEnabled`, `SetPathTracingEnabled`, and
`SetVSync`. RuntimeContext carries those flags into subsystem initialization.
Render already has typed PathTrace, SSAO, TAA, and fog settings and validated
request methods, but persistence, commands, and UI do not share one settings
catalog. Adding another preference should not add another Engine member and
another Engine setter.

The host-configuration refactor already moved Live2D, Panel, and standalone
capture/resize inputs out of Engine. Preserve that ownership. Central settings
is a discovery/configuration service, not a replacement bag for those fields.

The design question is: how can a module contribute a settings page before its
live system exists, then safely apply edits after startup, without transferring
domain knowledge or mutable subsystem state to Engine or Editor?

## Ownership and source placement

Names below are planned files/targets, not existing APIs.

| Owner / proposed location | Logic and state | Exclusions |
|---|---|---|
| `engine/runtime/settings/settings_types.h` | Stable section/property keys, scope/apply policy, copied values, revisions, diagnostics; reuse Reflection value/metadata types | Domain fields, UI types, live object pointers |
| `settings_registry.*` in that directory | Schema validation, duplicate rejection, layered desired values, bounded requests/results, immutable published snapshots, provider registration tokens | GPU work, domain validation algorithms, render-loop reads |
| `settings_store.*` in that directory | Versioned JSON parsing, layer overlays, unknown-section preservation, explicit save and atomic replacement | Applying settings or guessing domain migrations |
| `i_settings_provider.h` in that directory | Optional live-provider contract for copied section requests/results | A required base class for every subsystem |
| `engine/runtime/render/render_settings.h` | Typed startup settings; retain existing `path_trace_settings.h` and other feature settings | JSON, ImGui, central string lookups |
| `engine/runtime/render/settings/render_settings_provider.*` | Rendering schema/defaults, conversion to typed domain values, cross-field validation, provider binding | Editor widgets and native backend details |
| Existing `RenderSystem` | Active typed values, pending domain requests, safe-boundary application and acknowledgement, renderer/history side effects | File persistence and settings-page layout |
| `engine/runtime/runtime_global_context.*` | Own one SettingsRegistry/service and bind shared live systems during composition | A member for each preference |
| Existing `engine/module/module_bootstrap.*` and module-owned contributors | Explicitly collect linked module schemas for the selected application | Static SettingsRegistry singleton or Runtime includes of optional modules |
| `engine/runtime/settings/editor/` (compiled only into `EditorUILib`) | Draft values, filtering, selection, request feedback, window and metadata-driven controls | RuntimeSettings linkage, live domain pointers or domain setters |
| Each optional module's own settings adapter | That module's schema, validation and live application | Extra fields or mode-name checks in Engine |

RuntimeSettings is a small independent target under `runtime/settings`, using
Reflection's neutral vocabulary and JSON privately. A RenderSettings adapter
target links RuntimeSettings and Render. Render's core stays independent of
RuntimeLib/Editor; the adapter introduces the settings dependency, not Render's
frame algorithms. RuntimeLib composes both. The optional Settings editor
sources are grouped at `runtime/settings/editor` but are added only to
`EditorUILib`; they never enter the `RuntimeSettings` target or add an ImGui
dependency to Runtime. EditorUILib consumes copied snapshots and submits
requests. Do not expand the existing Runtime/Editor cycle.

Engine remains a lifecycle coordinator. Composition initializes the settings
service and passes typed initialization values to owners. No `Engine::Get<T>()`
service locator and no preference-specific Engine setters are introduced.

## Registration and reflection

Separate schema contribution from live-provider binding:

1. Explicit module functions contribute section definitions, typed defaults,
   metadata, pure validation/conversion, and apply policy without constructing
   a renderer, audio device, host window, or Editor.
2. The registry validates keys, supported types, default validity, ranges and
   collisions, then seals the schema catalog for this run.
3. Composition loads saved layers and launch overrides and resolves typed
   startup values before any consuming system initializes.
4. A constructed system, or a small adapter owned by its module, binds live
   application through a move-only registration token.

The optional `ISettingsProvider` interface serves step 4. Inheriting it is a
choice; free registration functions are sufficient for step 1. A separate
settings object can implement it for an existing subsystem. A module contributes
one or more sections; do not create a page for every object instance.

Reuse `ReflectionValue`, `ReflectionPropertyMetadata`, enum options, range and
tooltip vocabulary. Settings adds persistence scope, application policy and
change status. It does not add a second reflection backend. The early schema
path uses metadata values directly and does not require the later live
ReflectionSystem/Gameplay initialization or expose `IReflectionAccess` to UI.

Schema IDs are canonical strings such as `render.path_tracing`; property keys
are stable within the section. Labels may change without renaming saved keys.
Reject duplicate canonical IDs; do not let registration order decide a winner.

## Values and safe application

Distinguish four kinds of state:

| State | Owner / meaning |
|---|---|
| Draft | Editor model; unsubmitted form values |
| Desired | Settings service; validated configuration layers and accepted session intent |
| Active | Subsystem; typed configuration actually consumed by work |
| Published | Copied active/desired values, availability, provenance and request status for tools |

Registry values are configuration/control-plane data. Render and Audio consume
typed local state; they do not query a string-keyed registry in a hot loop.

An edit submits one section transaction with its base revision. Validate the
complete candidate, including relationships between properties. Reject stale
drafts with current values so a page cannot overwrite a newer command edit.
Transactions do not span subsystems in the first implementation.

The registry queues requests; an owner-lane pump invokes providers outside the
registry mutex. Recheck the base revision when committing validation performed
outside the lock, so a concurrent edit cannot invalidate the candidate silently.
Providers queue domain work through existing request APIs.
Render applies it before frame recording and reports an ID/revision with the
result. A successful `RequestPathTraceSettings()` return currently means
accepted, not applied; extend acknowledgement explicitly before reporting UI
success. Latest-wins domain queues must report older requests as superseded.

Public results distinguish Rejected, Pending, Applied, Superseded, Cancelled,
and RestartRequired. Successful live edits update accepted session intent when
acknowledged; pending values remain separately visible. A failed edit retains
the last active and accepted values and publishes its diagnostic. Restart-only
edits update next-launch intent while active values remain unchanged.

Existing console/agent settings commands must submit through the same domain
adapter/service path. Keep their command names and compatible result fields;
map acceptance to pending, not immediate application. Debug probes and failure
injection remain commands rather than saved preferences. Direct internal domain
requests must also publish their resulting active state so UI never invents it.

The Settings service does not create threads or duplicate game/render queues.
Use the existing game-lane pump, domain request mutexes and frame boundaries.
Bound queued requests and retained results; reject overflow with Busy feedback.

## Startup and lifetime

The service belongs to Runtime composition and is available in modes without
Scene3D. It must not depend on `InitializeSceneServices()` or a command registry.
The application startup boundary collects schemas and resolves configuration
before host initialization or shared presentation/device creation. Existing
host construction accepts copied launch input; do not initialize GPU state
during schema registration or require a running system to load its preferences.

Startup sequence: construct service -> register linked schemas -> seal catalog
-> load/validate layers -> apply explicit CLI overrides -> construct/initialize
owners with typed configuration -> bind live providers -> publish active state
-> attach commands and optional Editor consumers.

On close, stop new edits, cancel queued work, detach consumers/providers, drain
or cancel in-flight work on its owner lane, then destroy systems. A token uses
shared registration state and tolerates service shutdown. Late acknowledgements
carry registration generation and request ID and cannot mutate a new provider.
Token invalidation alone does not stop a callback already executing. Composition
must let that owner-lane dispatch finish before destroying the provider/owner;
never wait for it while holding the registry mutex. Never retain a provider
callback after its owner has been released. No plugin hot reload or dynamic
schema registration is required initially.

## Persistence and override policy

| Layer | Storage / behavior |
|---|---|
| Compiled defaults | Defined and validated by the contributing domain |
| Project defaults | Proposed `config/project_settings.json`; shared, explicit writes |
| User overrides | Proposed ignored `save/settings/user_settings.json`; checkout-local preferences |
| Session overrides | Explicit CLI flags and successful live edits; memory only until saved |

Resolution is defaults < project < user < session, but each property declares
allowed persistent scopes. A user preference cannot be written to project
defaults accidentally. The first Rendering slice permits project defaults and
user overrides. CLI values apply only when the user supplied the flag: parser
defaults must not overwrite saved settings. An explicit UI edit may replace a
CLI session value and its provenance; saving is always a separate action.

Apply changes the running session or next-launch intent. Save writes validated,
accepted desired values to the chosen allowed layer, never pending drafts.
Do not implicitly save on slider movement, shutdown, or command execution.
Revert discards a draft; remove-override reveals lower layers; restore-defaults
creates a draft and still requires Apply. Show an active session override when
removing a persistent override does not change the running value.

Store a format version and per-section schema version. Domain-owned pure
migrations handle known older sections. Missing files use defaults; malformed
sections produce diagnostics and fall back as a whole. Invalid explicit CLI
input fails startup rather than silently changing the user's request. Preserve
unknown sections/properties so a build without an optional module does not erase
its preferences. Do not overwrite malformed or newer-version input silently.
Write to a same-directory temporary file and replace only after successful
flush/close. A failed save leaves the previous file intact and the page unsaved.

Existing `config/settings.json` is Editor palette/log configuration, not this
new store. TTS/Live2D configuration files remain authoritative until an explicit
migration stage; do not create two writers for one preference. Secret values,
asset content, runtime statistics, capture paths, exit-after-capture and fixture
inputs are outside the first persisted schema.

## Rendering first

Separate `render.startup` from `render.path_tracing`. Startup RT support changes
device feature requests and is RestartRequired. The live path-tracing and
hybrid-shadow toggles select rendering policy within available startup support.
Samples, bounce count and reconstruction reuse existing validation/application.
Detailed mapping and acknowledgement work are in [SET2](.plan/SET2.md).

Publish requested policy and effective capability separately. OpenGL or a
Vulkan device lacking RT reports unavailable controls with a reason; this must
not rewrite a user's Vulkan preference on disk. Disabling path tracing does
not imply that ray-query shadows are disabled. Preserve existing history reset,
resource retirement and backend fallback behavior.

## Editor presentation

One Settings window has searchable left navigation (Project/User, category,
section) and a property form on the right. A section's metadata supplies labels,
groups, tooltips, enums and numeric bounds. Editor owns widget choice and stable
IDs built from section/property keys. Runtime providers never implement DrawUI.

The window displays draft, pending, applied, unsupported, restart-required,
validation-error and save-error states separately. Show desired and active
values when different, plus value source. Use Apply, Revert, Restore Defaults
and an explicit Save destination. Search has a no-results state; unavailable
systems retain visible configured values with an explanation. Preserve drafts
across page navigation and refresh; flag conflicts instead of silently replacing
edited fields. Close with unsaved drafts prompts Apply/Discard/Cancel.

Use existing Editor window/tool-row and theme conventions. Standard forms use
generic controls; specialized editors can be added in Editor only after a
concrete property requires one. No UI dependency enters RuntimeSettings/Render.

## References and decisions

- Unreal's documented `UDeveloperSettings` defines settings objects and
  container/category/section metadata. `ISettingsModule::RegisterSettings`
  accepts settings objects or custom widgets. Adopt module-contributed sections;
  do not require subsystem inheritance or copy Unreal's UObject/Slate stack.
  API docs inspected: 5.8; paths
  `Engine/Source/Runtime/DeveloperSettings/Public/Engine/DeveloperSettings.h`
  and `Engine/Source/Developer/Settings/Public/ISettingsModule.h`.
  [DeveloperSettings](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/DeveloperSettings/UDeveloperSettings?lang=en-US),
  [RegisterSettings](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Developer/Settings/ISettingsModule/RegisterSettings).
- Godot source inspected on `master`: `core/config/project_settings.h`, file
  blob SHA `1e5c6f1138b434979bb65df04bbd81600184cfec`. It exposes property
  metadata, initial values and restart-if-changed state. Adopt explicit restart
  metadata; retain this engine's typed domain state instead of global Variant
  reads in rendering. Only this header was verified, not Godot's full apply path.
  [Source](https://github.com/godotengine/godot/blob/master/core/config/project_settings.h).
- Local reuse: Reflection's value/metadata vocabulary, CommandRegistration's
  lifetime-token pattern, Render's validated request/safe-frame boundary, and
  the Actor Editor's copied-snapshot/draft-edit separation. These are precedents
  to adapt; Settings must not depend on Gameplay to reuse the pattern.

## Navigation

- [SET1: headless registry and store](.plan/SET1.md)
- [SET2: Rendering integration](.plan/SET2.md)
- [SET3: Editor settings window](.plan/SET3.md)
- [Roadmap and GUI/expansion stages](TODO.md)
- [Engine composition](../engine/PLANS.md)
- [Reflection architecture](../reflection/PLANS.md)
- [Planning evidence](../../.spec/journal/2026-11-10-runtime-settings.md)
