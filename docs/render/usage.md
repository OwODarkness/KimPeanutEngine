# Render Usage

## Rectangular path-tracing light

Level objects can author `kind: "area_light"` with `position`, `color`,
`intensity`, `range`, `enabled`, `casts_shadow`, `half_axis_u`, and
`half_axis_v`. The half axes must be finite, nonzero, and perpendicular.
Their cross product defines the emitting face; Cornell's +X/+Z axes emit
downward. Full side lengths are twice the axis lengths.

For path tracing, intensity multiplies emitted radiance, not the point-light
intensity convention. Uniform rectangle sampling includes area, emitter
cosine, inverse squared distance, BRDF, surface cosine, and visibility.
`range` is a hard influence limit. Each path sample and bounce selects a new
point; increasing intensity does not change softness.

The rectangle shares the local-light record/component/source lifecycle.
`PointLightComponent::SetAreaHalfAxes` selects a rectangle; zero axes restore
a punctual source. Axes follow world rotation; sizes are explicit world units
and do not inherit component scale. Render owns the copied shape and packs it
into the existing 64-byte PT light record. Graphics ownership and the shader
table layout are unchanged. Raster lighting/shadow maps currently use the
existing center-point approximation.

This is an analytic light, independent of emissive mesh materials. It does
not automatically create visible geometry or bind to an existing emitter
mesh. Cornell retains its existing weak emissive ceiling mesh; mesh-light
sampling and MIS between mesh emission and light sampling remain future work.

## Agent/runtime capture

Run the live Engine with `--agent-port 37373`, then issue the Runtime command
through the loopback JSON-lines endpoint:

```json
{"op":"execute","command":"capture.screenshot","arguments":{"path":"save/screenshots/validation/render-debug.png","view":"scene_color"}}
```

To capture the complete presented engine window, including Editor UI, use
`"view":"engine_window"`.

The first result is normally `pending` with `request_id`. Poll it until terminal:

```json
{"op":"poll","request_id":1}
```

On success inspect `data.output_path`. Explicit outputs must be `.png` files
under `save/screenshots/validation/`. `KimPeanutCommand` is only a protocol
harness; it cannot capture a live frame because it does not own RenderSystem.

## Validation

Follow [the validation matrix](../validation_matrix.md) for changed render
paths. A render behavior change needs more than compilation: run the impacted
unit/contract tests and an appropriate graphics or runtime smoke path, then
inspect capture output when visual behavior changed.
