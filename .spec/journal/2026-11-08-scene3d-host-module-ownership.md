# Scene3D host module ownership — 2026-11-08

## Request

Move the concrete 3D scene host out of Runtime's generic `host` folder. Runtime
should keep the shared host interface and registry; the scene module should own
the scene-specific host and its application-ID registration.

## Changes

- Moved `Scene3DHost` implementation to `engine/module/scene3d` and registered
  `scene3d` there through `ApplicationHostRegistration`.
- Added `Scene3DMode` and its registration object to the always-linked `Module`
  composition. Runtime no longer compiles, includes, or registers the concrete
  scene host.
- Moved the existing lifecycle checks out of the Runtime launch-options test
  into a Scene3D module test target.
- Updated the engine host architecture and roadmap to describe the module
  boundary.

## Ownership

Runtime continues to own `IApplicationHost`, `ApplicationHostDescriptor`, and
`ApplicationHostRegistry::Global()`. `engine/module/scene3d` owns the concrete
scene host and registration. The module depends on Runtime; Runtime does not
depend on the module. No new runtime dependency was introduced.

## Validation

- `cmake --build build --config Debug --target KimPeanutEngine`: passed. CMake
  regenerated the project and compiled `Scene3DMode`,
  `Scene3DRegistration`, and the editor executable.
- CTest and `Scene3DHostTest` were not run.
- Runtime launch and visual scene startup were not rechecked for this move.
