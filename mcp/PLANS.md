# MCP bridge architecture

The MCP server is a thin host-facing adapter over the existing Runtime command
registry.

```text
MCP host --stdio--> mcp/server.py --loopback JSON-lines--> Runtime CommandAgentEndpoint
                                                               ├─ Game queue
                                                               └─ Render metrics/capture
```

`server.py` owns MCP tool descriptions and lifecycle entry points. The
stdlib-only `engine_bridge.py` owns one serialized socket session, optional
process ownership, JSON-lines framing, async polling, and safe capture-path
validation. `command_catalog.json` is the agent-facing dictionary/resource;
the C++ command registry remains the authority for live command descriptors and
permissions.

The bridge supports both attaching to a user-launched EXE and launching the
validated Debug Vulkan fixture. It never terminates an externally attached
process, and it never prints diagnostics to MCP stdout.
