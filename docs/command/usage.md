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
