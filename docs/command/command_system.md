# Command System Design

**Status:** C4 landed (2026-08-29); C5+ planned

**Scope:** Runtime command registration, execution, editor console integration,
agent/automation access, and Lua integration.

## Design question

How should KimPeanutEngine expose one extensible command implementation to C++
systems while giving users and agents deliberately different entry points,
without making Runtime depend on Editor, ImGui, or Lua?

## Decision summary

Build a Runtime-owned, API-neutral `CommandRegistry`. The registry stores
command metadata and invokes type-checked handlers through an explicit
execution context. User and agent access are separate frontends with different
contracts:

- **User entry point:** the Editor `~` console accepts convenient text, history,
  completion, and human-readable output.
- **Agent entry point:** a structured in-process API and a headless/CLI adapter
  accept typed calls and return machine-readable results. Agents must not need
  to open the editor, press `~`, or scrape console logs.

Lua is another adapter over the same registry and is intended for reusable,
multi-step workflows. None of these frontends owns command semantics.

The first version should support commands only. Console variables, aliases,
pipelines, remote transport, and arbitrary Lua-defined commands are later
extensions, not prerequisites for the initial capture/render workflow.

```text
User:  Editor ~ console --text--> TextFrontend --+
                                                  |
Agent: structured API/CLI --typed--------------->+--> CommandRegistry
                                                  |
Lua:   engine.command.* ---------Lua bridge -----+
                                                  |
                                 handlers/services
```

## Local constraints

- Runtime must not depend on Editor. The current `RuntimeLib` ↔ `EditorLib`
  cycle is known technical debt and must not be expanded by this feature.
- `LuaVM` is intentionally engine-agnostic, owns one state, is not thread-safe,
  and is expected to be called from the game thread. Engine bindings belong in
  `engine/runtime/script`, above `ScriptLua`.
- Render and Graphics have explicit frame and GPU-lifetime boundaries. A
  command may request work, but it must not expose native backend objects or
  pretend asynchronous work is synchronous.
- `RuntimeScreenshotService` already owns screenshot path policy and PNG export.
  `capture.screenshot` must call that service rather than duplicate capture or
  filesystem behavior.
- Existing subsystem command queues, such as RenderWorld's frame-boundary
  commands, are subsystem implementation details. The general command system
  should enqueue work into those systems instead of replacing them.

## Entry-point contracts

### User entry point: Editor `~` console

The user console is a presentation and input feature owned by Editor/UI. It
accepts a command line such as:

```text
capture.screenshot path=save/screenshots/validation/user.png
```

It owns text parsing invocation, command history, cursor/editing behavior,
prefix completion, and human-readable output. It calls the registry's text
execution adapter and displays `Success`, `Pending`, or failure diagnostics.
It must not implement screenshot, render, gameplay, or Lua behavior.

### Agent entry point: structured/headless command API

The agent interface is not a simulated user console. It should prefer a typed
call:

```cpp
CommandResult result = command_endpoint.Execute(
    CommandCall{"capture.screenshot", {{"path", "save/screenshots/validation/agent.png"}}},
    CommandOrigin::Agent);
```

For an external process, expose the same contract through a headless CLI or
JSON-lines endpoint:

```text
kpengine --command capture.screenshot --path save/screenshots/validation/agent.png
```

The agent entry point owns serialization, timeout, cancellation, and structured
result transport. It must return command name, status, diagnostic, request ID,
and typed result data. It must not depend on ImGui, window focus, editor state,
or log-text parsing. A batch/script mode may be added for multi-step workflows,
but one-shot structured execution is the primary agent path.

### Lua entry point: in-process workflow scripting

Lua is useful when the agent or user needs a reusable sequence of commands. The
Lua bridge invokes the same structured endpoint and converts results to Lua
values. It is not the primary one-shot agent API because it requires a script
runner and VM lifecycle. Lua remains subject to the Script module's thread,
sandbox, and instruction-budget rules.

## Goals

1. Register commands near the subsystem that owns their behavior.
2. Expose stable names, help, argument schemas, categories, and availability.
3. Execute the same command through separate user, agent, test, and Lua entry
   points.
4. Keep the core registry free of ImGui, Lua/sol2, Graphics native types, and
   filesystem policy.
5. Support synchronous results and asynchronous completion uniformly.
6. Make registration/unregistration safe for module/plugin lifetimes.
7. Provide deterministic listing, help, and completion for humans and agents.
8. Permit command access to be restricted by build mode and execution origin.

## Non-goals for V1

- A full reflection system for every C++ function.
- A general expression language or arbitrary C++ evaluation.
- Remote networking or RCON.
- Console variables with replication/config persistence.
- UI command implementation inside the registry.
- Calling Lua from arbitrary render/worker threads.
- Allowing Lua or agents to bypass existing path, asset, or resource safety
  policies.

## Module and ownership boundary

```text
engine/runtime/command/
  CommandRegistry       owns descriptors and registration lifetime
  CommandParser         converts text into immutable arguments
  CommandExecutor       validates, authorizes, and dispatches
  CommandTypes          result, context, argument, registration token types

engine/editor/...
  Console UI             owns `~`, history, input focus, output rendering

engine/runtime/script/command/
  LuaCommandBridge       adapts CommandRegistry to LuaVM

engine/runtime/<module>/
  Native command provider registers commands for its own subsystem

engine/runtime/screenshot/
  Screenshot command provider adapts RuntimeScreenshotService to the registry
```

`CommandRegistry` depends only on Runtime Core/Base-level types. It must not
include ImGui, sol2, RenderSystem, RuntimeContext, or backend headers.

Subsystem providers may depend on the services they command. For example, the
screenshot provider may depend on `RuntimeScreenshotService`; the registry does
not depend on screenshots.

## Core model

The public model should be value-oriented and opaque to frontends:

```cpp
struct CommandDesc
{
    std::string name;                 // e.g. "capture.screenshot"
    std::string provider;
    std::string help;
    CommandCategory category;
    CommandFlags flags;
    CommandSchema schema;
    CommandHandler handler;
    CommandThread execution_thread;
};

struct CommandContext
{
    CommandOrigin origin;             // Console, Agent, Lua, Test, CLI
    CommandThread thread;              // caller's current lane
    CommandCapability capabilities;    // explicit authority grants
    std::uint64_t request_id = 0;
    CommandCompletionSink complete;   // set only for deferred registry calls
};

struct CommandResult
{
    CommandStatus status;
    std::string message;
    std::uint64_t request_id = 0;
    CommandData data;                 // optional structured result
};
```

The exact types may change during implementation. The important constraints
are that handlers receive parsed values instead of reparsing strings, results
can represent `Success`, `InvalidArguments`, `NotFound`, `Denied`, `Busy`,
`Pending`, `Failed`, `Shutdown`, and `WrongThread`, and structured data remains
available to agents and Lua without scraping console text.

### Registration lifetime

`Register(CommandDesc)` returns a result containing a move-only registration
token or a diagnostic. The descriptor includes a provider name so duplicate
names can identify the existing owner. Destroying the token unregisters the
command only if the same provider still owns the registration. Duplicate names
are rejected by default. This avoids static initialization order and stale
callback problems.

Providers should be explicit lifecycle objects, for example:

```text
RuntimeContext startup
  -> create registry
  -> register core/runtime commands
  -> register Render commands
  -> install Lua bridge
  -> editor attaches a console frontend
RuntimeContext shutdown
  -> detach frontends
  -> unregister providers
  -> destroy registry after pending commands are drained/cancelled
```

Handlers must not capture a raw subsystem pointer beyond that subsystem's
registration lifetime. `CommandRegistry::Shutdown` clears all registrations and
rejects new registration/execution. The registry does not own asynchronous
operations represented by `Pending`; the subsystem that created such work owns
its cancellation and completion semantics.

## Parsing and schemas

`CommandParser` is a RuntimeCommand utility above the registry. It converts
text into the same typed `CommandCall` accepted by structured execution, then
the registry validates both paths before invoking a handler. The parser owns
only lexical concerns—whitespace, single/double quotes, backslash escapes,
named `name=value` tokens, and positional tokens. `CommandSchema` owns value
types, required/default policy, and enum values. C2 intentionally has no array
value because no current command needs one.

Malformed text and malformed structured calls return an argument diagnostic such
as `argument 'view': invalid enum value 'shadow'`; neither reaches the handler.
Completion is a pure query over descriptors and returns sorted command names,
argument names, or enum values. This keeps completion useful to both the future
`~` frontend and headless/agent adapters without making either one the parser
owner.

V1 should accept both a text form and a structured form:

```text
capture.screenshot path=save/screenshots/validation/test.png view=scene_color
```

```cpp
CommandCall call{
    "capture.screenshot",
    { {"path", "save/screenshots/validation/test.png"},
      {"view", "scene_color"} }
};
```

The parser owns quoting, escaping, positional arguments, named arguments, and
basic lexical errors. The schema owns required/optional arguments, enum values,
numeric conversion, defaults, and help text. Handlers should receive validated
arguments and never perform ad-hoc command-line parsing.

Aliases, command chaining, and pipes are deferred. They add ambiguity and
make agent/Lua parity harder before the typed call API is stable.

## Threading and asynchronous execution

Command invocation is not automatically thread-safe. The descriptor declares
the required execution lane and the context declares the caller's current lane:

- `Immediate`: safe on the caller's designated Runtime/game thread.
- `GameThread`: enqueue and execute during the Runtime tick.
- `RenderThread`: enqueue through the Render boundary and execute at a valid
  frame boundary.
- `Async`: run in a worker only when the handler explicitly supports it.

The registry does not silently call a handler on the wrong lane. A Game-lane
command called from another lane is queued in the RuntimeCommand FIFO and
returns `Pending` with a request ID. The Runtime/game thread drains that queue
through `PumpGameThread`; Render and Async lanes reject mismatched callers until
their owning dispatchers exist. A terminal result is retained for
`TakeCompletion`, and an optional callback is delivered exactly once outside
the registry lock. Shutdown marks queued requests `Shutdown` and delivers the
same completion path; a request already running is not forcefully interrupted.

Capability checks are performed before queueing, so authorization is not
weakened by deferral. `DevelopmentOnly` and `EditorOnly` require explicit
context capabilities; Lua and Agent origins require their corresponding
descriptor flags; mutating and destructive commands require explicit
capabilities.

For `capture.screenshot`, the command handler submits to
`RuntimeScreenshotService::RequestScreenshot`. The GPU callback and PNG write
remain behind that service; the command layer only translates the final result
into a structured `CommandResult`. A deferred registry dispatch installs a
completion sink and request ID in the handler context. This is deliberately a
small bridge: generic command code does not know about Render or file output.

The first native provider is registered by `RuntimeContext` only after the
screenshot service has been constructed. `Engine` pumps the registry's Game
queue at the start of each game tick. Final screenshot data contains `status`,
`success`, `output_path`, and `diagnostic`; invalid explicit paths remain the
responsibility of `RuntimeScreenshotService`.

## Lua integration

Lua integration belongs in `engine/runtime/script/command`, not in the generic
registry and not in `LuaVM`.

Phase 1 exposes native commands to Lua:

```lua
local request = engine.command.execute("capture.screenshot", {
    path = "save/screenshots/validation/lua-scene.png",
    view = "scene_color"
})

engine.command.on_complete(request, function(result)
    print(result.status, result.message)
end)
```

The bridge converts Lua tables to `CommandValue`, invokes the registry on the
game thread, and converts structured results back to Lua values. It must keep
Lua references owned by the bridge and release them before VM shutdown.

Phase 2 may support Lua-defined commands:

```lua
engine.command.register("test.capture_scene", {
    help = "Run the capture validation sequence",
    args = { path = "string" },
    execute = function(args)
        return engine.command.execute("capture.screenshot", args)
    end
})
```

Lua-defined handlers must be explicitly marked as Lua-owned and dispatched on
the Lua/game thread. The registry stores an opaque bridge-owned callback, not a
`sol::function`, so the core command module remains independent of sol2.

The existing Lua sandbox and instruction budget remain active. Commands exposed
to Lua must additionally be capability-filtered; a script should not gain
arbitrary file, process, or shutdown authority merely because it can execute a
command.

## Frontends

### Editor `~` console

The editor owns the input overlay, history, completion display, and output
formatting. It calls `ExecuteText` and subscribes to completion events. It must
not contain command handlers. This is the user entry point only; agent tooling
must not depend on it.

### Agent/test API

Agents and tests should use structured `Execute(CommandCall)` through an
agent/test endpoint. Text execution is useful for parity checks and
human-authored scripts, but structured calls avoid quoting and parsing
ambiguity. The external agent adapter should be headless-capable and emit
machine-readable status, diagnostic, request ID, and typed data. It may expose
text as a convenience fallback, but text is not the agent contract.

### CLI/config frontend

A future headless entry point implements the agent/automation transport. It can
execute one structured startup command or a command script without Editor or
ImGui. This is useful for smoke tests and visual captures. It must report
unknown commands as errors rather than silently ignoring them, and should offer
a JSON result mode for agents.

## Initial command set

Register commands only when their underlying service exists:

| Command | Owner | Initial behavior |
|---|---|---|
| `help` | Command | List commands or show schema/help |
| `commands.list` | Command | Structured command descriptors |
| `capture.screenshot` | Screenshot | SceneColor PNG through existing service |
| `render.reload_shaders` | Render/Resource | Deferred until a safe reload API exists |
| `render.debug_view` | Render | Register only for implemented views |
| `engine.stats` | Runtime | Return structured runtime counters |
| `engine.quit` | Runtime | Debug/agent permission required |

Do not register placeholder commands that claim unsupported functionality.

## Safety and policy

Every command has flags such as `DevelopmentOnly`, `EditorOnly`, `LuaAllowed`,
`AgentAllowed`, `MutatesState`, and `RequiresRenderThread`. The execution
context enforces them. Release builds may omit development commands entirely.

The command system does not weaken subsystem validation. Screenshot output
paths still pass through `RuntimeScreenshotService`; render handles remain
opaque; and destructive commands require an explicit capability.

## Reference comparison

The references were used to identify patterns, not to copy implementation:

- [Unreal `IConsoleManager`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/IConsoleManager)
  separates registration, lookup, input processing, help-facing metadata,
  flags, variables, and unregistration. KimPeanutEngine adopts the explicit
  registry and metadata ideas, but avoids a global singleton and does not mix
  variables into V1.
- [Source cvar utilities](https://github.com/perilouswithadollarsign/cstrike15_src/blob/master/engine/cvar.h)
  shows the value of built-in listing, help, revert/toggle operations, and
  completion. KimPeanutEngine adopts listing/help/completion as registry
  services, while deferring mutable console variables.
- [ImGui command palette](https://github.com/hnOsmium0001/imgui-command-palette)
  demonstrates dynamic registration, unregistration, subcommands, and fuzzy
  search. Those are useful editor presentation features, but the registry must
  remain UI-free and the first console can use prefix completion.
- [Godot command-line workflow](https://github.com/godotengine/godot-docs/blob/master/tutorials/editor/command_line_tutorial.rst)
  demonstrates headless, benchmark, and script-driven workflows. KimPeanut
  Engine should expose the same command registry to a future headless runner,
  while preserving structured errors instead of ignoring unknown input.

## Validation strategy

- Registry tests: duplicate names, token lifetime, lookup, listing, help, and
  deterministic ordering.
- Parser/schema tests: quoting, named/positional arguments, defaults, invalid
  enum/numeric values, and useful diagnostics.
- Context tests: origin flags, wrong-thread rejection/enqueue, and permission
  checks.
- Async tests: pending request, exactly-once completion, cancellation, and
  frontend callback marshalling.
- Native command tests: queued screenshot execution, deferred completion,
  structured export data, and service-owned path diagnostics.
- Lua tests: native command invocation, structured result conversion, script
  errors, instruction limits, VM shutdown with pending callbacks, and
  capability rejection.
- Integration: `capture.screenshot` through the structured registry seam; the
  `~` frontend remains C5, with the existing Vulkan/OpenGL smoke evidence
  retained.

## Implementation boundary

The first implementation should land before shadows/G-buffer work, but remain
small: registry, typed call/result model, parser/schema, `help`, the separate
user text and agent structured execution endpoints, and `capture.screenshot`.
Render debug-view commands should be added only as the corresponding render
producers become real.
