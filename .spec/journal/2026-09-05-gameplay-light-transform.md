# Gameplay Light Transform Alignment — 2026-09-05

## Objective

Land the small Gameplay stage before Reflection RF2 that removes duplicated
light transform state and makes point, spot, and directional lights consume the
SceneComponent transform family.

## Implementation

- Added a shared Gameplay scene-forward utility. `+X` is canonical; its
  pitch/yaw equations match CameraComponent and zero rotation points along
  `+X`.
- Point and spot positions now come from world transform position. Directional
  and spot directions come from world rotation.
- Removed light position/direction fields, getters, and setters. Transform
  invalidation marks each light source dirty and existing per-tick flushing
  coalesces updates.
- Factories still accept the existing level-facing position/direction structs,
  then apply Scene local location/rotation setters. Non-finite and zero
  directions are rejected before Actor creation.

## Validation

Focused Gameplay and RuntimeLevel tests were updated for direction conversion;
attachment, world-transform movement, coalescing, and invalid conversion cases
were added.

- `cmake --build build --config Debug --target GameplayUnitTest` reached CMake
  regeneration, then was blocked before compilation because MSBuild could not
  access `C:\Users\17519\AppData\Local\Microsoft SDKs` while probing the
  Windows SDK.
- MinGW `g++ -std=c++17 -fsyntax-only` passed for all changed Gameplay
  production translation units and the Gameplay/RuntimeLevel test translation
  units.
- `git diff --check` passed.
