# Command System TODO

Implementation ledger for [command_system.md](command_system.md).

## C0 — contract and module skeleton

- [x] Create `engine/runtime/command/` with a dedicated Runtime/CMake target.
- [x] Define command names, categories, flags, origins, execution lanes, and
  result statuses.
- [x] Define `CommandDesc`, `CommandCall`, `CommandContext`, `CommandResult`,
  `CommandValue`, and move-only registration tokens.
- [x] Keep the public command headers independent of Editor, ImGui, Lua/sol2,
  RenderSystem, and Graphics backend types.
- [x] Add the command module to the Runtime dependency graph without adding a
  new Runtime → Editor dependency.

**Done when:** a unit test can register, find, execute, and unregister a fake
command without creating a window, Lua VM, renderer, or editor. **Landed
2026-08-29.**

## C1 — registry and lifecycle

- [x] Implement deterministic registration and lookup by canonical name.
- [x] Reject duplicate names by default and report the owning provider.
- [x] Implement registration-token lifetime and safe unregistration.
- [x] Reject execution after provider unregistration.
- [x] Add `help`, `commands.list`, and structured descriptor enumeration.
- [x] Define shutdown behavior for queued and pending command completions.

**Done when:** registry lifecycle tests cover duplicate registration, token move,
token destruction, provider shutdown, and exactly-once completion.

**C1 implementation note:** the registry clears registrations and rejects new
work during shutdown. Asynchronous operations returned as `Pending` remain
owned by their subsystem handler; that owner must cancel them and deliver any
domain-specific completion before its own shutdown. **Landed 2026-08-29.**

## C2 — argument schema and text parser

- [x] Implement typed command values: boolean, signed/unsigned integer, float,
  string, and enum. Arrays remain deferred because no first consumer requires
  them.
- [x] Implement positional and named arguments with quoting and escaping.
- [x] Implement schema defaults, required arguments, enum validation, and help
  formatting.
- [x] Produce stable diagnostics with argument name and error reason.
- [x] Keep text parsing separate from structured command execution.
- [x] Add completion queries based on command names and schema values.

**Done when:** the same command produces equivalent results from text and
structured calls, and malformed input never reaches the handler. **Landed
2026-08-29.**

## C3 — execution context and scheduling

- [x] Add `CommandOrigin` for Console, Agent, Lua, Test, and CLI. (Defined in
  C0 and enforced in C3.)
- [x] Add execution-lane declarations and wrong-thread handling.
- [x] Provide a Runtime/game-thread queue for commands that cannot run
  immediately.
- [x] Define `Pending` request IDs and exactly-once completion delivery.
- [x] Add capability/flag checks for development-only, mutating, Lua, agent,
  and destructive commands.
- [x] Test cancellation during Runtime shutdown.

**Done when:** a command cannot accidentally execute a render- or Lua-owned
handler from an arbitrary caller thread. **Landed 2026-08-29.**

## C4 — first native commands

- [x] Register `capture.screenshot` through `RuntimeScreenshotService`.
- [x] Preserve existing screenshot path validation and PNG export ownership.
- [x] Return structured screenshot status, output path, diagnostic, and request
  ID without scraping log text.
- [ ] Register `engine.stats` if stable counters already exist.
- [ ] Register `render.reload_shaders` only after a safe reload boundary exists.
- [ ] Register render debug-view commands only for implemented views.

**Done when:** an agent/test can execute `capture.screenshot` without Editor or
direct access to Render/Graphics internals. **Landed 2026-08-29.**

## C5 — user `~` frontend

- [ ] Add a console overlay owned by Editor/UI.
- [ ] Bind the `~` key through the existing input/UI boundary.
- [ ] Submit text to the Runtime text-execution adapter.
- [ ] Display structured success/failure/pending results.
- [ ] Add bounded command history and duplicate suppression.
- [ ] Add prefix completion first; defer fuzzy search until the registry API is
  stable.
- [ ] Ensure the console frontend can detach before Runtime shutdown.
- [ ] Keep all command handlers out of Editor/UI.

**Done when:** the editor can execute `help` and `capture.screenshot` through
the same registry used by tests and agents, while the user frontend remains
optional to Runtime.

## C6 — agent structured/headless access

- [ ] Define the agent endpoint separately from the user text-console endpoint.
- [ ] Expose structured `Execute(CommandCall)` to the local agent/tool seam.
- [ ] Expose command listing and schema metadata for agent discovery.
- [ ] Define timeout, cancellation, and pending-result polling/subscription
  behavior for automation.
- [ ] Add a headless command runner or startup-command path without Editor or
  ImGui.
- [ ] Add machine-readable JSON/JSON-lines result transport for external agents.
- [ ] Make the one-shot structured call the primary agent path; keep text only
  as an optional compatibility/convenience path.
- [ ] Make unknown commands and invalid arguments non-zero/error results.
- [ ] Add deterministic capture commands for graphics smoke and future visual
  regression tests.

**Done when:** an automation client can discover a command, construct a typed
call without opening the editor or pressing `~`, await completion, and consume
structured output.

## C7 — Lua bridge, native commands first

- [ ] Add `engine/runtime/script/command/` bridge code above `LuaVM`.
- [ ] Bind `engine.command.list`, `help`, `execute`, and completion metadata.
- [ ] Document Lua as a workflow-scripting entry point, not the required
  one-shot agent transport.
- [ ] Convert Lua tables to validated `CommandValue` arguments.
- [ ] Convert `CommandResult` data back to safe Lua values.
- [ ] Marshal all Lua callbacks to the Lua/game thread.
- [ ] Release Lua callback references before VM shutdown.
- [ ] Enforce command flags and existing Lua sandbox/instruction limits.
- [ ] Test pending screenshot completion and VM shutdown cancellation.

**Done when:** Lua can invoke `capture.screenshot` and receive a structured
completion without the core command target linking to Lua/sol2.

## C8 — Lua-defined commands, optional

- [ ] Define explicit Lua-owned registration tokens.
- [ ] Support Lua command help/schema metadata.
- [ ] Dispatch Lua-defined handlers only on the Lua/game thread.
- [ ] Unregister all Lua commands on script reload or VM shutdown.
- [ ] Prevent Lua command names from silently replacing native commands.
- [ ] Restrict Lua-defined commands to safe namespaces such as `game.*` and
  `test.*` unless explicitly elevated.

**Done when:** script reload leaves no stale Lua callbacks in the registry and
native command behavior is unchanged.

## Deferred extensions

- [ ] Console variables with typed storage, defaults, flags, and sinks.
- [ ] Aliases and command macros.
- [ ] Command pipelines and output piping.
- [ ] Fuzzy search and subcommand UI.
- [ ] Persistent history/configuration.
- [ ] Remote command transport.
- [ ] Replicated/server-authoritative commands.
