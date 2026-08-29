# Command Architecture

`RuntimeCommand` owns API-neutral descriptors, schema validation, capability
checks, dispatch, Game-lane queuing, and terminal results. Subsystems register
their own commands; the registry never depends on Editor, ImGui, Lua, Render,
or native graphics types.

Registration returns a move-only `CommandRegistration`; providers retain it for
their callable lifetime and handlers retain shared service state rather than raw
pointers. Queued work keeps its original registration identity, so it cannot
call a replacement provider. Shutdown terminally completes registry-owned work;
subsystems still own their underlying async operations.

Text parses into the same typed `CommandCall` as structured callers. Game-lane
work queues to `Engine::GameTick`; `Pending` requires a registry completion sink.
Origin flags and explicit capabilities protect Agent, Lua, mutating, destructive,
development, and editor-only commands.

`capture.screenshot` belongs to the screenshot provider. It delegates path
policy and PNG export to `RuntimeScreenshotService`, while Render produces the
CPU image.

The `Script` target owns the optional Lua adapter in
`script/command/LuaCommandBridge`. It depends downward on `RuntimeCommand` and
sol2, while `RuntimeCommand` remains independent of Lua. The bridge creates
`engine.command.list`, `help`, `execute`, `poll`, and `cancel`; it translates
Lua primitives/tables into `CommandCall` values and converts structured results
back to Lua tables. Lua may execute only descriptors marked `LuaAllowed` and
still passes capability checks.
