# Command API Reference

This page defines the stable caller/provider interfaces for the Runtime command
registry. For ownership and threading rationale, see [architecture](architecture.md)
and [lifecycle](lifecycle.md).

## C++ provider API

Register a descriptor and retain the returned token for exactly as long as the
provider may be called. Destroying or replacing the token unregisters it.

```cpp
command::CommandRegistration registration_;

const auto result = registry.Register({
    "engine.stats", "RuntimeStats", "Return frame counters",
    command::CommandCategory::Engine,
    command::CommandFlags::AgentAllowed | command::CommandFlags::LuaAllowed,
    {},
    [stats = shared_stats](const command::CommandCall&, const command::CommandContext&) {
        return command::CommandResult{
            command::CommandStatus::Success, "", 0,
            {{"frame_count", static_cast<uint64_t>(stats->FrameCount())}}};
    },
    command::CommandThread::Game,
});
registration_ = std::move(result.registration);
```

`CommandDesc` fields:

| Field | Meaning |
|---|---|
| `name` | Unique canonical command name, e.g. `capture.screenshot`. |
| `provider` | Stable owner name used in diagnostics. |
| `help` / `schema` | User-facing description and typed argument contract. |
| `flags` | Allowed origins and required capabilities. |
| `handler` | C++ work entry point. It must not retain borrowed call/context references. |
| `execution_thread` | `Immediate`, `Game`, `Render`, or `Async` lane. |

`CommandValue` is one of: null/monostate, `bool`, `int64_t`, `uint64_t`,
`double`, or `std::string`. Schemas support Boolean, SignedInteger,
UnsignedInteger, Float, String, and Enum arguments.

## C++ caller API

```cpp
const command::CommandResult submitted = registry.Execute(
    {"capture.screenshot", {{"path", std::string{"save/screenshots/validation/test.png"}}}},
    {command::CommandOrigin::Test, command::CommandThread::Immediate});

if (submitted.status == command::CommandStatus::Pending) {
    registry.PumpGameThread(); // only Engine::GameTick() does this in Runtime
    const auto completed = registry.TakeCompletion(submitted.request_id);
}
```

`ExecuteText()` is the optional text-parser counterpart. `Find()` and `List()`
return descriptor metadata; `TakeCompletion(id)` returns and removes one
terminal result; `CancelRequest(id)` marks a registry request cancelled but
cannot forcibly interrupt already-submitted subsystem work.

## Result contract

Every frontend represents the same result fields:

| Field | Meaning |
|---|---|
| `status` | `success`, `invalid_arguments`, `not_found`, `denied`, `busy`, `pending`, `failed`, `cancelled`, `shutdown`, or `wrong_thread`. |
| `message` | Human-readable diagnostic. |
| `request_id` | Non-zero for deferred work and its terminal completion. |
| `data` | Typed command-specific output, such as `output_path`. |

`Pending` is not a terminal success. A handler may return it only when the
registry supplied `CommandContext::complete`; it must call that sink exactly
once with the final result. `Shutdown` terminally completes any registry-owned
request still outstanding.

## Access policy

| Flag | Required condition |
|---|---|
| `LuaAllowed` | Origin is Lua and the command explicitly allows it. |
| `AgentAllowed` | Origin is Agent and the command explicitly allows it. |
| `DevelopmentOnly` | `CommandCapability::Development`. |
| `EditorOnly` | `CommandCapability::Editor`. |
| `MutatesState` | `CommandCapability::Mutating`. |
| `Destructive` | `CommandCapability::Destructive`. |

Frontends supply an origin and capability set; the registry performs the check
before invoking a handler. Do not duplicate permission policy in providers.

## Agent JSON-lines API

Start the live Engine with `--agent-port <port>`. The local transport accepts
one JSON object per line and returns one JSON object per line.

```json
{"op":"list"}
{"op":"execute","command":"capture.screenshot","arguments":{"path":"save/screenshots/validation/agent.png","view":"scene_color"}}
{"op":"poll","request_id":1}
{"op":"cancel","request_id":1}
```

`list` returns `status` plus `commands` (`name`, `provider`, `help`). `execute`
arguments accept JSON booleans, integer/unsigned integer, floating-point, and
string values. Arrays, objects, and null argument values are rejected. See
[agent transport](agent_transport.md) for binding, queue, and trust limits.

## Lua API

On the Game/Lua thread, C7 binds:

```lua
engine.command.list()                         -- descriptor array
engine.command.help("capture.screenshot")     -- { status, command = descriptor }
engine.command.execute(name, arguments)       -- result table
engine.command.poll(request_id)               -- pending or terminal result
engine.command.cancel(request_id)             -- cancelled/not_found result
```

Lua result tables use `status`, `message`, `request_id`, and `data`. Descriptor
arguments contain `name`, `required`, numeric `type` (`CommandValueType`), and
`enum_values`. Lua accepts only booleans, integers, floats, and strings as
arguments. Calls from a non-Game/Lua thread return `wrong_thread`.

## Built-in commands

See the [built-in command catalogue](built_in_commands.md) for the complete
current meanings and examples.

| Name | Arguments | Notes |
|---|---|---|
| `commands.list` | none | Returns registered command names. |
| `help` | optional `name: string` | Returns formatted help, or the command list. |
| `capture.screenshot` | optional `path: string`, `view: enum` | `view` supports `engine_window`, `scene_color`, `linear_depth`, `world_normal`, `base_color`, `material_params`, `shadow_visibility`, `spot_shadow_depth`, `spot_shadow_visibility`, `point_shadow_depth`, and `point_shadow_visibility`; `engine_window` captures the final presented client area including Editor UI. Explicit paths must be PNGs below `save/screenshots/validation/`. |
