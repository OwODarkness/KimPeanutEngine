# Render Usage

## Agent/runtime capture

Run the live Engine with `--agent-port 37373`, then issue the Runtime command
through the loopback JSON-lines endpoint:

```json
{"op":"execute","command":"capture.screenshot","arguments":{"path":"save/screenshots/validation/render-debug.png","view":"scene_color"}}
```

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
