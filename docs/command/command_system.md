# Command System

**Status:** C0–C7 landed. The live screenshot transport path has runtime-smoke
evidence; deterministic visual-regression commands remain deferred.

KimPeanutEngine has one Runtime-owned command registry. It gives Editor, tests,
agents, and Lua bindings the same validated commands without making
Runtime depend on Editor, ImGui, Lua, or graphics backend types.

## Document map

- [Architecture](architecture.md) — ownership, registration, parsing, lanes,
  pending work, shutdown, policy, and references.
- [Lifecycle](lifecycle.md) — Engine transport startup, GameTick handoff, and
  teardown order.
- [Usage](usage.md) — C++ provider/consumer examples, Editor console, and
  JSON-lines request/response examples.
- [API reference](api.md) — C++ provider/caller interfaces, results, flags,
  agent JSON-lines operations, and Lua bindings.
- [Built-in command catalogue](built_in_commands.md) — current predefined
  commands, their meanings, arguments, and result behavior.
- [Live agent transport](agent_transport.md) — wire protocol and local
  development limits.
- [Risks](risks.md) — local-user trust model and unverified runtime paths.
- [Implementation plan](TODO.md) — completed milestones and remaining work.

## Current boundary

```text
Editor console --text--> CommandRegistry <--typed-- tests / Lua / in-process callers
                                      ^
                                      | loopback JSON-lines transport
                                  running Engine
```

`KimPeanutCommand` is a protocol harness with registry built-ins only. It does
not own the live RenderSystem and therefore cannot capture a frame. Real render
debugging must target the registry inside the already running Engine, as
specified in [agent_transport.md](agent_transport.md).
