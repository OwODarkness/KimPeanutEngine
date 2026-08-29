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

**C1 implementation note:** queued requests retain their original registration
identity and cannot execute a replacement provider after unregistration. A
`Pending` result is valid only for a registry-owned request with a completion
sink. During shutdown the registry terminally completes every outstanding
request as `Shutdown`; subsystem owners still cancel their own underlying work,
and late completion attempts are ignored. **Landed 2026-08-29.**

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

- [x] Add a console overlay owned by Editor/UI.
- [x] Bind the `~` key through the existing input/UI boundary.
- [x] Submit text to the Runtime text-execution adapter.
- [x] Display structured success/failure/pending results.
- [x] Add bounded command history and duplicate suppression.
- [x] Add prefix completion first; defer fuzzy search until the registry API is
  stable.
- [x] Ensure the console frontend can detach before Runtime shutdown.
- [x] Keep all command handlers out of Editor/UI.

**Done when:** the editor can execute `help` and `capture.screenshot` through
the same registry used by tests and agents, while the user frontend remains
optional to Runtime. **Landed 2026-08-29.**

## C6 — agent structured/headless access

- [x] Define the agent endpoint separately from the user text-console endpoint.
- [x] Expose structured `Execute(CommandCall)` to the local agent/tool seam.
- [x] Expose command listing and schema metadata for agent discovery.
- [x] Define timeout, cancellation, and pending-result polling/subscription
  behavior for automation.
- [x] Add a headless command runner or startup-command path without Editor or
  ImGui.
- [x] Add machine-readable JSON/JSON-lines result transport for external agents.
- [x] Make the one-shot structured call the primary agent path; keep text only
  as an optional compatibility/convenience path.
- [x] Make unknown commands and invalid arguments non-zero/error results.
- [ ] Add deterministic capture commands for graphics smoke and future visual
  regression tests.

**Done when:** an in-process or harness caller can discover a command,
construct a typed call without Editor/ImGui, and consume structured output.
Live-Engine external access is C6.1.

## C6.1 — live Engine agent transport

**Goal:** let a local developer agent issue structured commands to the registry
inside the already running Engine, where the live RenderSystem and scene exist.
See [agent transport design](agent_transport.md).

- [x] Add a development-only, loopback-only transport owned by Engine.
- [x] Keep transport I/O on its own thread and transfer parsed requests through
  bounded inbound/outbound queues.
- [x] Drain inbound agent requests from `Engine::GameTick()`; never invoke
  command handlers on the transport thread.
- [x] Return JSON-lines results, including pending request IDs and terminal
  completions, to the originating client.
- [x] Define startup configuration, local-user access, request-size limits,
  queue limits, disconnect handling, and teardown order.
- [x] Validate live `capture.screenshot` against the active RenderSystem and
  inspect the returned PNG as runtime-smoke evidence.

**Done when:** a local agent can discover and execute `capture.screenshot`
against the current Engine frame without accessing Editor UI, Lua, or graphics
backend objects. **Landed 2026-08-29.**

## C7 — Lua bridge, native commands first

- [x] Add `engine/runtime/script/command/` bridge code above `LuaVM`.
- [x] Bind `engine.command.list`, `help`, `execute`, and completion metadata.
- [x] Document Lua as a workflow-scripting entry point, not the required
  one-shot agent transport.
- [x] Convert Lua tables to validated `CommandValue` arguments.
- [x] Convert `CommandResult` data back to safe Lua values.
- [x] Marshal all Lua callbacks to the Lua/game thread.
- [x] Release Lua callback references before VM shutdown.
- [x] Enforce command flags and existing Lua sandbox/instruction limits.
- [x] Test pending screenshot completion and VM shutdown cancellation.

**Done when:** Lua can invoke `capture.screenshot` and receive a structured
completion without the core command target linking to Lua/sol2. **Landed
2026-08-29.**

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
