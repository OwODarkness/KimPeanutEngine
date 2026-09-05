# GP8 — Light Transform Alignment

Status: landed 2026-09-05.

## Design question

How can Gameplay lights expose one transform family to the upcoming Reflection
stage while preserving the existing level-file position/direction schema?

## Decision

Scene forward is `+X`, matching `CameraComponent` at zero rotation. A finite,
non-zero direction is normalized and maps to
`Rotatorf{asin(y), atan2(z, x), 0}` in degrees. Point and spot positions come
from world transform position; directional and spot directions come from world
rotation.

## Acceptance

- Light components contain no duplicate position/direction state or setters.
- Transform changes, including parent changes, coalesce to one source update per
  Gameplay tick.
- Factories translate legacy level descriptions through Scene transform setters
  and reject invalid direction vectors.
- Focused Gameplay tests cover attachment, world-transform changes, source
  update coalescing, and invalid conversion.

## Boundary

This is a Gameplay stage before Reflection RF2. The level asset schema remains
unchanged. Reflection may therefore reuse the Transform property family for
Mesh, Camera, and lights, while light-specific properties remain color,
intensity, range, cone, enabled, and shadow state.
