# MCP bridge agent guide

The MCP bridge is a local integration adapter, not a second command system.

- Keep engine command names, argument types, status names, async behavior, and
  capture view names aligned with `docs/command/api.md` and the Runtime
  `CommandAgentEndpoint` contract.
- Keep stdout reserved for MCP stdio protocol traffic. Use `logging` for
  diagnostics so they go to stderr.
- Do not access RenderSystem, RHI objects, or engine files directly from Python;
  use the loopback command endpoint.
- Keep the bridge loopback-only and use `subprocess.Popen(..., shell=False)`.
- Preserve process ownership: an attached engine must never be terminated by
  `stop_engine()`.
- Validate capture paths before sending them to the engine.

Run the checks in `mcp/README.md` after changes.
