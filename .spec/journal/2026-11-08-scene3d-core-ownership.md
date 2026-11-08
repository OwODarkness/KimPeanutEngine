# Scene3D core ownership — 2026-11-08

## Direction correction

The prior migration placed `Scene3DHost` in `engine/module/scene3d`. The user
clarified that the 3D scene is a core engine application path, not an optional
feature module. The module placement is superseded by this correction.

## Current ownership

- `engine/runtime/application/scene_3d_host.*` owns the built-in scene-mode
  lifecycle adapter.
- Runtime's global host registry installs the built-in `scene3d` descriptor
  during first access. The direct registration function reference keeps the
  registration object linked from static `RuntimeLib`.
- Optional hosts such as Signal Lab, Audio Player, and Live2D continue to
  register through their feature modules.
- Runtime retains both the generic host contract and the core scene host, but
  has no dependency on optional feature modules.

## Validation

- `cmake --build build --config Debug --target KimPeanutEngine`: passed. The
  build compiled the core scene host and registration into `RuntimeLib` and
  linked the editor executable.
- CTest and `Scene3DHostTest` were not run.
- Runtime launch and visual scene startup were not rechecked for this move.
