# Live Agent Transport

## Goal

An external agent must command the registry in the **running Engine**, because
that process owns the live render world. `KimPeanutCommand` is only a protocol
harness and cannot capture the running Engine's frame.

## C6.1 implementation

Implement a development-only, loopback-only JSON-lines transport:

```text
Agent -> loopback I/O worker -> inbound queue -> Engine::GameTick()
      <- outbound queue    <- CommandAgentEndpoint <- CommandRegistry
```

The transport worker only frames I/O. It never invokes a handler; `GameTick`
executes requests against the live registry. Start the Engine with
`--agent-port 37373` to enable it (the default is disabled). It binds
`127.0.0.1` only, accepts one sequential client, limits a request to 64 KiB,
limits each handoff queue to 128 entries, and drains at most 32 requests per
game tick. Only Agent-allowed commands and configured capabilities pass the
registry policy check.

Each request gets exactly one immediate JSON-line response. Async commands
return `pending` plus a Registry request ID; send a later `poll` request to
receive the terminal completion. Disconnecting a client does not cancel work
already accepted by the registry. Engine joins the worker before Runtime clears
the registry.

For render debugging: discover commands, execute `capture.screenshot` with a
path below `save/screenshots/validation/`, poll its request ID, then inspect
the returned `data.output_path` PNG. Lua remains a future in-process developer
workflow, not the external transport. See [lifecycle](lifecycle.md) and
[risks](risks.md) for ownership and security limits.

To capture a particular checked-in level, select it before startup:

```powershell
build/Debug/KimPeanutEngine.exe `
  --graphics-api vulkan `
  --startup-level level/point_shadow_validation.level `
  --agent-port 37373
```

`--startup-level` has precedence over Bootstrap's `startup_level` for this
process only. It is validated before Engine initialization and never writes the
Bootstrap file. Omit it to use the durable default.

## MCP bridge

The repository provides a local MCP adapter in [`mcp/`](../../mcp/README.md).
It uses MCP stdio for the host-facing connection and this endpoint for live
engine work. Install `mcp/requirements.txt`, register `python mcp/server.py`
with the host, then call `launch_engine()` or `connect_engine()` before using
the typed `gpu_stats`, `cpu_stats`, `stats`, and `capture_screenshot` tools.
The machine-readable [`command_catalog.json`](../../mcp/command_catalog.json)
is also exposed as the `kimpeanut://command-catalog` MCP resource.

MCP launch readiness is separate from transport readiness: `launch_engine()`
first waits for the port, then waits for a successful `stats` result with a
completed frame. The default port timeout is 30 seconds and the default render
readiness timeout is 120 seconds; both are configurable in the MCP tool.
