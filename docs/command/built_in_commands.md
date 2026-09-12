# Built-in Command Catalogue

This is the current predefined command set registered by Runtime. `list` APIs
may show additional commands as subsystems register them, but only the commands
below are stable built-ins today.

| Command | Meaning | Jump |
|---|---|---|
| `commands.list` | List registered command names. | [`commands.list`](#commandslist) |
| `help` | Show help for one command or list names. | [`help`](#help) |
| `capture.screenshot` | Capture a live final or diagnostic render view and export a PNG. | [`capture.screenshot`](#capturescreenshot) |
| `window.resize` | Resize the active window's client area. | [`window.resize`](#windowresize) |
| `gpu-stats` | Return the latest completed-frame GPU statistics. | [`gpu-stats`](#gpu-stats-cpu-stats-and-stats) |
| `cpu-stats` | Return the latest completed-frame CPU and frame-loop statistics. | [`cpu-stats`](#gpu-stats-cpu-stats-and-stats) |
| `stats` | Return the latest completed-frame CPU and GPU statistics. | [`stats`](#gpu-stats-cpu-stats-and-stats) |
| `live2d.model_report` | Report the loaded Live2D product and its blend distribution. Present only in Live2D viewer mode. | [`live2d.model_report`](#live2dmodel_report) |

## `commands.list`

Lists registered command names in deterministic alphabetical order.

| Property | Value |
|---|---|
| Provider | `RuntimeCommand` |
| Arguments | None |
| Allowed callers | Editor console, Agent, Lua, tests, C++ callers |
| Result | `success`; `message` contains newline-separated names and `data.count` contains the number of commands. |

Use it for human-readable discovery in the console or C++/Lua workflows. Agent
JSON `{"op":"list"}` is the structured discovery equivalent and returns name,
provider, and help text.

## `help`

Shows formatted help for one registered command, or lists names when no name is
provided.

| Property | Value |
|---|---|
| Provider | `RuntimeCommand` |
| Arguments | Optional `name: string` |
| Allowed callers | Editor console, Agent, Lua, tests, C++ callers |
| Result | `success`; command-specific help is in `message`, while `data.name` and `data.provider` identify the owner. |

Examples:

```text
help
help capture.screenshot
```

## `capture.screenshot`

Requests capture of the current final or diagnostic render view, then exports it as a PNG.
It is a native Runtime screenshot command; Editor UI, Lua, and agents all call
this same provider.

| Property | Value |
|---|---|
| Provider | `RuntimeScreenshot` |
| Execution lane | Game; the provider then waits for Render capture/export completion. |
| Allowed callers | Editor console, Agent, Lua, tests, C++ callers |
| `path` | Optional string. Explicit paths must end in `.png` and remain below `save/screenshots/validation/`. |
| `view` | Optional enum: `engine_window`, `scene_color`, `linear_depth`, `world_normal`, `base_color`, `material_params`, `shadow_visibility`, `spot_shadow_depth`, `spot_shadow_visibility`, `point_shadow_depth`, or `point_shadow_visibility`. Defaults to `scene_color`. `engine_window` includes the final Editor/ImGui composite. |
| Initial result | Normally `pending` with a request ID. |
| Terminal result | `success` with `data.output_path`, `data.status`, `data.success`, and `data.diagnostic`; otherwise an error status and diagnostic. |

Example text command:

```text
capture.screenshot path="save/screenshots/validation/frame.png" view=scene_color
```

Use `view=engine_window` to capture the final presented engine client area,
including the Editor/ImGui composite.

Example agent request:

```json
{"op":"execute","command":"capture.screenshot","arguments":{"path":"save/screenshots/validation/frame.png","view":"scene_color"}}
```

Poll the returned request ID until the terminal result. See [API reference](api.md)
for all caller forms and [usage](usage.md) for complete Agent/Lua examples.

Diagnostic views are converted by Render into displayable RGBA8 output before
the existing Graphics readback path runs. They are visualizations, not raw
attachment byte exports; linear depth is normalized by the active camera far
plane, normals are remapped from `[-1,1]` to `[0,1]`, shadow visibility is
white for visible and black for occluded, and `spot_shadow_depth` visualizes
the sampled D32 spotlight map; `point_shadow_depth` visualizes the fixed
3×2 point-shadow depth atlas.

## `window.resize`

Resizes the active window's client area. Available in every host mode, including
the standalone Live2D viewer, where the window belongs to the host rather than
to Runtime's shared window system; the host resolves its own window for the
command.

| Property | Value |
|---|---|
| Provider | `RuntimeWindow` |
| Execution lane | Game |
| Capability | `MutatesState` |
| Allowed callers | Editor console, Agent, Lua, tests, C++ callers |
| `width` | Required unsigned integer, 1 to 16384. |
| `height` | Required unsigned integer, 1 to 16384. |
| Result | `success` with `data.width`, `data.height`, `data.recorded_width`, `data.recorded_height`, and `data.applied`; otherwise `InvalidArguments` or `Failed`. |

The command **records** the request and reports `data.applied` as `false`,
because it runs on the Game lane and a real window may only be touched on the
window thread. The request is applied at that thread's next frame boundary, and
`data.recorded_width`/`data.recorded_height` are what the window system now
holds. Callers that need the new extent to have landed must observe the window
afterwards rather than treat `success` as "already resized".

Because it mutates engine state, the Agent transport only accepts it when the
engine was launched with `--agent-port`; see
[agent transport](agent_transport.md).

```json
{"op":"execute","command":"window.resize","arguments":{"width":1024,"height":768}}
```

## `live2d.model_report`

Reports the Live2D product the active host has loaded and the blend-mode
distribution authored inside it. Registered by the Live2D viewer host through
`IApplicationHost::RegisterHostCommands`, so it exists **only in Live2D viewer
mode**; in Scene3D mode it is absent from `commands.list`.

| Property | Value |
|---|---|
| Provider | `Live2DRuntime` |
| Execution lane | Game |
| Allowed callers | Agent, Lua, Editor console, tests, C++ callers |
| Arguments | None |
| Result | `success` with `data.model` and the six blend fields below; otherwise `Failed` when no product is loaded. |

| `data` field | Meaning |
|---|---|
| `model` | Asset-root-relative path of the product actually loaded. |
| `drawable_count` | Total drawables in the loaded model. |
| `normal_drawable_count` | Drawables authored with normal blend. |
| `additive_drawable_count` | Drawables authored with additive blend. |
| `multiplicative_drawable_count` | Drawables authored with multiplicative blend. |
| `unknown_blend_mode_count` | Drawables whose authored blend mode is not one of the three. |
| `covers_all_blend_modes` | True only when all three modes have at least one drawable. |

Blend mode is authored inside the `.moc3` and cannot be recovered from a
capture, so this command is how blend-coverage claims are asserted rather than
inferred from an image. `data.model` names the product that was actually loaded
so a run that silently fell back to the configured fixture cannot read as
evidence about a different one.

```json
{"op":"execute","command":"live2d.model_report"}
```

## `gpu-stats`, `cpu-stats`, and `stats`

These Runtime commands read the last fully completed render frame. They are
available from the Editor console, Agent transport, Lua, and in-process C++
callers. The `--json` text flag explicitly selects machine-readable output;
structured Agent calls use `{"json":true}`. Agent responses are already JSON
lines, and the command-specific values are in `data` under the stable
`kimpeanut.profiler.v1` schema.

```text
gpu-stats --json
cpu-stats --json
stats --json
```

```json
{"op":"execute","command":"stats","arguments":{"json":true}}
```

Unavailable GPU timings and utilization are returned as JSON `null`. The
snapshot is published by Render at the presentation boundary, so a Game-thread
command never reads a partially updated render profile.

## Planned, not registered

`engine.stats`, `render.reload_shaders`, and render debug-view commands are not
predefined yet. They remain deliberately absent until their underlying services
have stable ownership and safe execution boundaries.
