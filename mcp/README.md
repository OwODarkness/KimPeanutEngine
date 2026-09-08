# KimPeanutEngine MCP bridge

This directory contains a local MCP server that gives an agent typed tools for
the running KimPeanutEngine. The server uses MCP stdio for the host connection
and the engine's existing loopback JSON-lines endpoint for live commands.

## Install

From the repository root:

```powershell
python -m pip install -r mcp/requirements.txt
```

Python 3.10 or newer is required. The MCP SDK is intentionally not vendored.

## Configure an MCP host

Use the root [`mcp.json`](../mcp.json) as a starting point. The portable launch
command is:

```text
python mcp/server.py
```

The MCP server does not start the engine during import. The agent chooses one
of these explicit workflows:

1. Call `launch_engine()` to start `build/Debug/KimPeanutEngine.exe` with
   Vulkan, `level/performance_profile.level`, and port `37373`. It waits for
   the first completed frame; the port timeout and level-readiness timeout are
   separate and configurable.
2. Call `connect_engine()` when the user already started the EXE with
   `--agent-port 37373` or through `run-performance-profile.bat`; it also waits
   for a completed frame by default.

## Agent workflow

```text
launch_engine()
stats()
gpu_stats()
cpu_stats()
capture_screenshot(view="scene_color")
capture_screenshot(view="world_normal")
capture_screenshot(view="linear_depth")
```

The typed stats tools return the engine's `kimpeanut.profiler.v1` data. The
typed capture tool waits for the engine's asynchronous request and returns the
terminal result with `data.output_path`. Normal and depth diagnostic names are
`world_normal` and `linear_depth`; `normal` and `depth` are not valid engine
view names.

`launch_engine` reports `ready_frame` and `ready_after_seconds`. Its default
`connect_timeout_seconds` is 30 seconds for opening the port, while its
default `ready_timeout_seconds` is 120 seconds for level loading and the first
completed frame. Increase the latter for larger scenes.

Read the [`command_catalog.json`](command_catalog.json) resource for the full
transport contract, launch defaults, supported workflows, and safety rules.

## Safety and ownership

- The Python bridge connects only to loopback addresses.
- Engine processes are launched with an argument list and `shell=False`.
- `stop_engine()` terminates only a process started by this MCP server.
- `disconnect_engine()` leaves an externally started engine untouched.
- Screenshot paths are restricted to `save/screenshots/validation/` and PNG.
- Engine command permissions and thread dispatch remain owned by C++ Runtime.

## Local checks

```powershell
python -m unittest discover -s mcp -p "test_*.py"
python -m py_compile mcp/engine_bridge.py mcp/server.py
```
