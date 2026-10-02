# Audio M1 Content Library implementation

## Scope and design boundary

Address the M1 Content-discovery review's first slice: project Audio imports
publish stable Content metadata, and the standalone player discovers and queues
those products through Asset-owned APIs. External Add File/Folder imports remain
session previews under `save/audio_player/`. Runtime Audio still owns playback;
the editor controller only presents catalog data and requests queue actions.

## Implementation

- Added an optional Content root to import requests. The Asset tool supplies it
  for project imports; session imports leave it empty. Audio import writes a
  deterministic `audio` ContentID, source-relative metadata path, and current
  Audio product hash after the archive transaction. The no-op path also
  republishes metadata so a missing record can be repaired. CLI output reports
  the Content identity and metadata path.
- Added Audio to the Asset catalog's archive type-name capture set. The player
  captures the Asset catalog on its worker at startup and from an explicit
  Refresh action. It filters Audio Content entries and validates their
  published products through AssetManager before publishing an immutable
  Project Library list.
- Added a separate Project Music view and filter. Queueing a project entry
  loads its existing `.audio` product through AssetManager; it does not call
  AudioImportService or create a session archive duplicate. Session queue and
  external import controls remain separate. Stable ContentID matching lets a
  refreshed product replace its queued entry while an active voice retains its
  pinned immutable product.
- Documented the project/session distinction in the Audio module design and
  updated the M1 review disposition.

## Validation

- `cmake --build build --config RelWithDebInfo --target AudioPlayerMode` — pass.
- `cmake --build build --config RelWithDebInfo --target KimPeanutAssetTool` —
  pass; linker emitted the existing `LNK4098` runtime-library warning.
- `cmake --build build --config RelWithDebInfo --target KimPeanutEngine` — pass.
- Visible Vulkan startup on the `Default` desktop and
  `capture.glfw_window` — pass. The Project Music section rendered its empty
  state; the exported capture is
  `save/screenshots/validation/audio-m1-project-library-vulkan-verified.png`.
- `git diff --check` — pass, with Git line-ending conversion warnings only.
- CTest was not run. No checked-in project music fixture is available, so
  imported Content discovery, explicit refresh, playback without recooking,
  corrupt/stale product diagnostics, and active-voice reimport still need
  asset-backed runtime validation.

## Remaining risks

- `WriteContentMetadata` is a separate file publication after the archive
  transaction. A metadata write failure is reported; a later no-op import can
  repair the Content record, but cross-file/database atomicity is not claimed.
- AssetManager product loading and recursive Content capture run off the ImGui
  frame on the existing import worker. A very large catalog can delay queued
  external imports until capture finishes.
- M1 acceptance tests and visible Vulkan playback from a project-published
  track remain open.
