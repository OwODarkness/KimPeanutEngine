# Audio module ownership and Editor built-in icons

- Status: complete within scope; broad-suite failures recorded
- Date: 2026-11-11
- Parent: [Audio P1](../../docs/audio/TODO.md), [Editor ED6](../../docs/editor/TODO.md)

## Objective and scope

Keep Runtime Audio limited to playback infrastructure. Move the standalone
player controller and presentation into `engine/module/audio`; provide shared,
toolkit-neutral audio editor controls for both the player and TTS. Editor owns
a built-in icon catalog and CPU loading, rather than an audio-named general UI
library. Preserve existing image assets, playback behavior and player layout.

## Ownership and reuse decision

Inspected `AudioPlayerController`, `audio_preview_widget`, the shared legacy
`audio_transport_strip`/`audio_transport_icons`, the asset browser's icon
settings/decoder, `UiIconMask`, and the Editor UI targets. Reuse ImageIO and
the existing mask rasterization policy inside an independent `EditorIcons`
owner. Keep asset-type mapping and custom icon settings in Asset Browser.
The user's JSON clarification makes `resouce/icon/setting.json` the authority
for icon-name to relative resource paths. `EditorIconCatalog` parses it and
resolves resources against the configuration directory; there are no PNG-path
fallbacks in code. Asset Browser keeps domain type matching, using the same
catalog for built-in and custom names.
Use the existing neutral UI controls and canvas in `AudioEditorWidgets`;
do not move ImGui calls into the audio module. TTS's unrelated legacy UI stays
outside this refactor, while its audio controls consume the neutral widgets.

## Invariants

- Runtime Audio does not link player, module, UI, AssetImport or Editor targets.
- The player controller owns its current worker, queue and import lifetime.
- Icons own CPU pixels; widget mask views are borrowed only while drawing.
- The icon catalog knows no Audio/TTS services or GPU resources.
- Feature controls expose no ImGui types, includes or calls.
- Preserve unrelated dirty reconstruction and Settings changes.

## Acceptance and validation

- [x] Player controller/presentation reside in the audio module, with split targets.
- [x] Player and TTS use Editor's built-in icon loader and shared audio controls.
- [x] Icon paths come from JSON and remapping works relative to its directory.
- [x] Old audio-named general UI sources and Runtime Audio editor target are removed.
- [x] Icon decoding/orientation/failure tests and neutral audio-widget contracts pass.
- [x] Reconfigure, build affected targets and engine, run relevant tests and inspect dependencies.
- [x] Visible Debug Vulkan/OpenGL player smoke and captures under the prescribed directory.

Validation is Level 4 because target dependencies and public feature headers change.
Record actual results and skipped checks in the journal; a build alone is not
visual or audio-quality evidence.

The user closed this task on 2026-11-11. See the
[journal](../journal/2026-11-11-audio-module-editor-icons.md) for full-suite
failures and presentation/coverage limitations; this does not accept ED6 or P1
as a whole.
