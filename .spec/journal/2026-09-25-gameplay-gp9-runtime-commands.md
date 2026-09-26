# Gameplay GP9 — Runtime Actor commands

Date: 2026-09-25.

## Objective

Give an authorized local agent a Runtime command path to discover, inspect, and
control live Gameplay Actors without making the generic command registry own
Actor lookup or mutation.

## Changes

- Added `GameplayWorld::ListActors`, `QueryActor`, and
  `SetActorRootTransform`. Queries return copied Actor identity/state and local
  and world root transforms; control changes local position and rotation while
  preserving scale and uses SceneComponent setters.
- Synchronized `PlayerController` control rotation when an external transform
  changes the root component of its possessed camera.
- Added the separate `RuntimeGameplayCommand` adapter with Game-lane
  `actor.list`, `actor.query`, and `actor.control` commands. Level-authored
  object names are copied onto Gameplay Actors; list supports a
  case-insensitive `name_contains` substring filter. Listing is bounded to
  pages of at most 64; query and list results are value-only flat command data.
  Mutations require the existing Mutating capability.
- Registered command tokens after GameplayWorld creation and release them
  before destroying GameplayWorld.
- Updated the GP9 plan/TODO, command catalogue, and project status.

## Validation

- `cmake --build build --config Debug --target RuntimeLib` — passed via
  `tools/kp.ps1` after correcting a missing forward declaration and explicit
  lambda return types.
- `cmake --build build --config Debug --target KimPeanutEngine` — passed; the
  executable linked successfully.
- `git diff --check` — passed. Git emitted line-ending normalization notices
  for modified files but no whitespace errors.
- At the initial implementation checkpoint, focused tests and live Runtime
  command smoke were still open. See the dated follow-up below for completion
  evidence.

## Remaining work at initial checkpoint

- Add focused Gameplay and Runtime Gameplay command coverage for list paging,
  stale handles, query values, transform validation, authorization, and camera
  synchronization.
- Verify `actor.list`, `actor.query`, and `actor.control` through the live
  loopback transport against a checked-in startup level.
- Confirm reflected editor edits to a possessed camera keep its controller
  rotation coherent with command-driven changes.

## Follow-up — 2026-09-26

- Routed successful reflected camera rotation edits through GameplayWorld's
  controller synchronization helper. Added coverage for Actor name filtering
  and paging, transform query/control validation and scale preservation,
  Mutating-capability authorization, stale handles, camera/render source
  updates, possessed-camera synchronization through both the Gameplay API and
  Runtime command provider, and reflected editor camera edits.
- Added a RuntimeLevel assertion that a level-authored name reaches its
  Gameplay Actor.
- `cmake --build build --config Debug --target KimPeanutEngine` — passed.
- Built `GameplayUnitTest`, `GameplayEditorBridgeUnitTest`,
  `GameplayCommandProviderTest`, and `RuntimeLevelTest` — passed.
- `ctest --test-dir build -C Debug -L "gameplay|gameplay_editor_bridge|runtime_level|runtime" --output-on-failure` — 143 tests passed.
- `ctest --test-dir build -C Debug -R "GameplayCommandProviderTest" --output-on-failure` — all 4 provider tests passed, including command discovery/help schemas and possessed-camera control.
- `ctest --test-dir build -C Debug --output-on-failure` — all 987 tests passed.
- Live-tested `actor.list` name filtering and full listing, then queried and
  controlled `quartz_bunny` and `main_camera` through the loopback Agent
  transport on `level/pbr_showcase.level`. Post-control queries reported the
  applied poses. `capture.screenshot` exported
  `save/screenshots/validation/gp9-actor-control-20260926-165329.png` and
  `save/screenshots/validation/gp9-camera-control-20260926-165638.png`; both
  captures were inspected.
- `git diff --check` — passed. GP9 acceptance criteria are complete.

## Remaining work

None for GP9. Broader future command features remain outside this objective.
