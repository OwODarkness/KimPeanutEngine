# Command Usage

Providers own their registration token and capture shared service state:

```cpp
auto registered = registry.Register({"engine.stats", "RuntimeStats", "Frame count",
    CommandCategory::Engine, CommandFlags::AgentAllowed, {},
    [stats = shared_stats](const CommandCall &, const CommandContext &)
    { return CommandResult{CommandStatus::Success, "", 0, {{"frame_count", stats->FrameCount()}}}; },
    CommandThread::Game});
provider.registration_ = std::move(registered.registration);
```

For agent use, execute a structured call and poll a `Pending` request ID. The
Editor `~` console is only a text frontend over that same registry.

```json
{"op":"execute","command":"capture.screenshot","arguments":{"path":"save/screenshots/validation/agent-debug.png","view":"scene_color"}}
{"op":"poll","request_id":42}
```

Terminal success includes `data.output_path`. The caller owns timeouts and can
send `{"op":"cancel","request_id":42}`; cancellation cannot stop already
submitted subsystem work.

For the live Engine path, launch `KimPeanutEngine --agent-port 37373`, then
open a local TCP connection to `127.0.0.1:37373` and send the same JSON lines.
The port is disabled unless explicitly supplied; `KimPeanutCommand` remains a
separate harness and cannot capture the live Engine frame.

Choose a checked-in startup fixture without editing Bootstrap by adding the
launch-scoped selector, for example:

```powershell
KimPeanutEngine --graphics-api opengl `
  --startup-level level/spot_shadow_validation.level `
  --agent-port 37373
```

Use `--graphics-api vulkan` or `--graphics-api opengl` to select the backend
for a reproducible capture run. The default remains Vulkan.

Once the live Engine is running, query performance from the same command
transport:

```json
{"op":"execute","command":"gpu-stats","arguments":{"json":true}}
{"op":"execute","command":"cpu-stats","arguments":{"json":true}}
{"op":"execute","command":"stats","arguments":{"json":true}}
```

From the Editor `~` console, the equivalent explicit text form is
`gpu-stats --json`, `cpu-stats --json`, or `stats --json`. The returned `data`
object uses the `kimpeanut.profiler.v1` schema.

Lua uses the same registry and result protocol on the Game/Lua thread:

```lua
local submitted = engine.command.execute("capture.screenshot", {
  path = "save/screenshots/validation/lua-debug.png",
  view = "scene_color",
})

if submitted.status == "pending" then
  local finished = engine.command.poll(submitted.request_id)
  -- finished.status, finished.message, finished.data.output_path
end
```

`engine.command.list()` returns descriptors and schema metadata; `help(name)`
returns one descriptor. `execute` accepts boolean, integer, float, and string
Lua values only. It cannot bypass `LuaAllowed` or command capabilities.

## MCP host integration

For an MCP-capable agent, use the repository's [`mcp.json`](../../mcp.json)
configuration or launch [`mcp/server.py`](../../mcp/server.py) over stdio. The
server provides explicit tools for `launch_engine`, `connect_engine`,
`gpu_stats`, `cpu_stats`, `stats`, and `capture_screenshot`. It delegates to
the same live Runtime endpoint documented above; it does not duplicate command
handlers.

The recommended workflow is:

```text
launch_engine()
stats()
capture_screenshot(view="world_normal")
capture_screenshot(view="linear_depth")
```

`world_normal` and `linear_depth` are the exact diagnostic view names. Capture
paths are restricted to `save/screenshots/validation/`.
