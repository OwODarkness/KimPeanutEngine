# RF4 Review — World Outliner and Actor Inspector

- Status: implementation review complete; runtime/visual evidence open
- Plan: [RF4 plan](../.plan/RF4.md)
- Journal: [RF4 journal](../../../.spec/journal/2026-09-05-runtime-reflection-rf4.md)

## Scope reviewed

Reviewed the RF4 Editor model, widget policy, World Outliner, Actor Inspector,
EditorUI dependency injection, workspace layout, and focused tests against the
RF4 plan and the RF2/RF3 consumer boundary.

## Findings

No source-level boundary findings remain in the reviewed implementation.

The live startup ordering did expose and resolve one integration defect during
follow-up: render-thread presentation is initialized before game-thread
Reflection publication. The Editor now refreshes the borrowed catalog/snapshot
source/edit sink at workspace commit, before constructing RF4 panels.

- Editor owns selection, drafts, widget policy, and presentation feedback.
- Runtime/Gameplay remain owners of the frozen catalog, copied snapshots,
  command validation, and mutable object access.
- The Editor sources contain no EnTT or live Gameplay object access.
- Full Actor handle identity is used for selection and Outliner widget IDs.
- Snapshot values are never optimistically overwritten by controls.
- Pending/result state and string drafts are bounded.

## Validation and open evidence

`EditorUILib`, `EditorLib`, and `ActorEditorModelUnitTest` build under MinGW;
the focused executable passes 3/3 tests. The existing Editor lifecycle target
reaches linking but is blocked by mixed MSVC/MinGW third-party archives. Native
MSVC is separately blocked by the recorded Windows SDK permission failure.
Vulkan/OpenGL selection/edit smoke has not yet been run, so the RF4 roadmap
remains partially open until those environment-dependent checks pass.
