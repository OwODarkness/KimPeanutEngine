# Audio Player Library presentation — 2026-10-02

## Scope and acceptance

Refine the top-left Library dock to follow the user's draft and the accepted
playlist styling. Keep only real All/Recent/Favorites categories and real
project music. Counts must fit their rows; selected categories/tracks must
use orange outlines, ordinary names cyan, and format labels white. Primary
import actions should remain easy to reach at wide and compact widths.

## Implementation

- `engine/runtime/audio/editor/audio_player_editor.cpp`: a local navigation
  row helper shared with Playlists; folder/clock/star category icons; clipped
  project rows with music/play masks and stable content IDs; filtered-view
  clipping; explicit no-match feedback and Clear Filter; queue-tab activation
  on category/project selection; paired import buttons; optional collapsed
  subtitle controls; wrapped status/error text; disabled empty-path queue
  actions. Existing importer, file pickers, subtitle attachment, and queue
  controller calls remain the data/behavior owners.
- `engine/runtime/audio/editor/audio_player_theme.h`: a font-relative project
  track row height alongside the existing collection measurements.

This is presentation and navigation only. No Asset, Audio, RHI, backend,
public interface, or GPU resource ownership changes. Existing transport,
profile, layout, and earlier player working-tree changes are preserved.

## Validation

- ImGui scope checker and `git diff --check` passed.
- `.\tools\kp.ps1 build KimPeanutEngine`: Debug build passed.
- `cmake --build build --config RelWithDebInfo --target KimPeanutEngine`:
  passed, with the existing LNK4098 MSVCRTD dependency warning. Only
  RelWithDebInfo was launched, as requested by the user.
- Vulkan PID 19112's `GLFW30` window was verified visible on desktop
  `Default` outside the sandbox. The initial Library showed six real project
  catalog tracks with cyan names, music icons, and white format labels. The
  empty queue, aligned category counts, paired import controls, and collapsed
  optional subtitle section were visible.
- `capture.glfw_window` plus polling exported
  [initial Library](../../save/screenshots/validation/audio-library-empty.png).
  The user stopped Computer Use with physical Escape before interaction,
  compact-width, and real-track playback checks. No further Computer Use
  actions were taken in that task.

OpenGL, HiDPI, filter/selection interaction, compact layout, and final shutdown
were not separately checked for the Library changes. The initial capture does
not establish playback or import behavior. Earlier audio tests and player
captures are recorded in the linked playlist journals; they predate this
Library refinement. P1's broader acceptance remains open.

## Commit preparation

At the user's request, the final commit also includes the existing loop
transport control, reusable Editor profile-metrics factory, and fixed profile
footer in the six-dock layout. The factory returns Editor-owned metrics that
borrow the host's existing Engine, memory sampler, and RenderSystem; no GPU
resource ownership or RHI contract changes are introduced. Both READMEs now
include the user-supplied `resouce/example/audio_player.png` under a clean
title, with updated Audio feature descriptions and module links. Generated
`content/` archive products and their local import metadata are excluded.

- `.\tools\kp.ps1 build`: full Debug build passed.
- `ctest --test-dir build -C Debug -L 'audio|editor' --output-on-failure`:
  all 180 tests passed (27 Audio, 140 Editor, 13 Gameplay Editor bridge).
- The full `.\tools\kp.ps1 test` run reported nine failures among 1115
  discovered tests. The command wrapper was interrupted by the user's README
  correction, while its CTest process continued to completion. Its failure
  list was retained; `ctest --test-dir build -C Debug --rerun-failed
  --output-on-failure` reproduced all nine with exit code 8.
- Eight failures are in unchanged Asset targets: the checked-in gameplay
  level fixture lacks an archived model; four catalog tests fail diagnostic
  or live-node expectations; two native-model fixtures fail canonical archive
  path validation; one import-registry fixture fails provider/product
  expectations. One unchanged Terrain test expects 24 coarse indices but
  receives 96. These targets do not depend on the edited player presentation
  or Editor controls. No baseline-only run is claimed.
- README local links and image paths exist. ImGui scope checker and staged
  diff whitespace checks passed. Logs remain under ignored `build/`.

The full suite is not green. Those Asset/Terrain failures and the runtime
validation limits above remain follow-up work outside this presentation commit.
