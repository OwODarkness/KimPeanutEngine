# Built-in Command Catalogue

This is the current predefined command set registered by Runtime. `list` APIs
may show additional commands as subsystems register them, but only the commands
below are stable built-ins today.

| Command | Meaning | Jump |
|---|---|---|
| `commands.list` | List registered command names. | [`commands.list`](#commandslist) |
| `help` | Show help for one command or list names. | [`help`](#help) |
| `capture.screenshot` | Capture the live SceneColor and export a PNG. | [`capture.screenshot`](#capturescreenshot) |

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

Requests capture of the current rendered SceneColor, then exports it as a PNG.
It is a native Runtime screenshot command; Editor UI, Lua, and agents all call
this same provider.

| Property | Value |
|---|---|
| Provider | `RuntimeScreenshot` |
| Execution lane | Game; the provider then waits for Render capture/export completion. |
| Allowed callers | Editor console, Agent, Lua, tests, C++ callers |
| `path` | Optional string. Explicit paths must end in `.png` and remain below `save/screenshots/validation/`. |
| `view` | Optional enum; currently only `scene_color`. |
| Initial result | Normally `pending` with a request ID. |
| Terminal result | `success` with `data.output_path`, `data.status`, `data.success`, and `data.diagnostic`; otherwise an error status and diagnostic. |

Example text command:

```text
capture.screenshot path="save/screenshots/validation/frame.png" view=scene_color
```

Example agent request:

```json
{"op":"execute","command":"capture.screenshot","arguments":{"path":"save/screenshots/validation/frame.png","view":"scene_color"}}
```

Poll the returned request ID until the terminal result. See [API reference](api.md)
for all caller forms and [usage](usage.md) for complete Agent/Lua examples.

## Planned, not registered

`engine.stats`, `render.reload_shaders`, and render debug-view commands are
not predefined yet. They remain deliberately absent until their underlying
services have stable ownership and safe execution boundaries.
