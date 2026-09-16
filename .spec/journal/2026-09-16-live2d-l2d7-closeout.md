# Live2D L2D7 Secondary Behavior Closeout — 2026-09-16

- Status: implementation complete; repository-wide fixture blocker remains
  outside the Live2D module.

## Scope closed

- L2D7.0 reconciled the L2D6 transaction into named pre-expression and
  post-expression phases while preserving the public playback contract.
- L2D7.1 added bounded Product V3 physics/pose, ordered hit-area, and immutable
  user-data values with deliberate model3 closure validation and V1/V2
  compatibility.
- L2D7.2 added the instance-local blink, gaze, breath, physics, and pose
  controllers plus the canonical `AdvanceFrame` transaction.
- L2D7.3 added immutable user-data lookup and current deformed-geometry hit
  queries in model-local coordinates.
- L2D7.4 completed capability reporting, renderer forwarding, lifecycle checks,
  deterministic viewer evidence, and the L2D8/L2D9 handoff.
- L2D7.5 completed the viewer-only Follow Mouse, Fixed Target, pause/step,
  reset, and copied diagnostics controls without adding UI or input types to
  Runtime.

## Validation

- `cmake --build build --config Debug --target Live2DCoreTest KimPeanutEngine --parallel 1` — passed.
- `ctest --test-dir build -C Debug -R Live2D --output-on-failure` — 56/56 passed.
- `cmake --build build-nolive2d --config Debug --target KimPeanutEngine --parallel 1` — passed.
- Existing Product V3 OpenGL/Vulkan viewer evidence remains valid:
  `l2d7-4-v3-opengl-1.png`, `l2d7-4-v3-vulkan-1.png`,
  `l2d7-5-control-deck-opengl.png`, and `l2d7-5-control-deck-vulkan.png`.
  Reports show typed playback and secondary behavior, deterministic behavior
  masks, host-output captures, and clean Cubism shutdown.
- Dependency search found no L2D7 semantic branch in Asset, Render, Graphics,
  Audio, or TTS; viewer-only controls remain in `Live2DViewerHost`.

## Known blocker

The repository-wide `ctest --test-dir build -C Debug --output-on-failure` run
reached 935/936 passing tests. The only failure was the unrelated
`LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures`, which cannot load
`model/rock1-bl/rock2` because that logical product is absent from the current
checked-in asset archive. No L2D7 source or fixture depends on this path.
