"""KimPeanutEngine MCP server.

The MCP server uses stdio for host communication. Engine traffic stays on the
engine's loopback JSON-lines endpoint, so no engine or RHI implementation types
cross this boundary.
"""

from __future__ import annotations

import atexit
import json
import logging
from pathlib import Path
from typing import Any, Literal

try:
    from mcp.server import MCPServer
except ImportError as error:  # pragma: no cover - depends on the MCP host environment
    if __name__ == "__main__":
        raise SystemExit(
            "The MCP Python SDK is required. Install it with: "
            "python -m pip install -r mcp/requirements.txt"
        ) from error
    raise

try:
    from .engine_bridge import (
        CAPTURE_VIEWS,
        DEFAULT_GRAPHICS_API,
        DEFAULT_HOST,
        DEFAULT_PORT,
        DEFAULT_READY_TIMEOUT_SECONDS,
        DEFAULT_STARTUP_LEVEL,
        EngineBridge,
        EngineBridgeError,
    )
except ImportError:
    from engine_bridge import (  # type: ignore[no-redef]
        CAPTURE_VIEWS,
        DEFAULT_GRAPHICS_API,
        DEFAULT_HOST,
        DEFAULT_PORT,
        DEFAULT_READY_TIMEOUT_SECONDS,
        DEFAULT_STARTUP_LEVEL,
        EngineBridge,
        EngineBridgeError,
    )


LOGGER = logging.getLogger("kimpeanut-engine-mcp")
CATALOG_PATH = Path(__file__).with_name("command_catalog.json")

mcp = MCPServer(
    "KimPeanutEngine",
    version="0.1.0",
    instructions=(
        "Use launch_engine to start the validated Debug Vulkan runtime, or "
        "connect_engine when the EXE is already running with --agent-port. "
        "Use gpu_stats, cpu_stats, and stats for completed-frame performance "
        "data. Use capture_screenshot with exact diagnostic names such as "
        "world_normal and linear_depth. Async engine commands are automatically "
        "polled by the typed tools."
    ),
)
bridge = EngineBridge()
atexit.register(bridge.stop)


def _catalog_text() -> str:
    return CATALOG_PATH.read_text(encoding="utf-8")


@mcp.resource("kimpeanut://command-catalog")
def command_catalog() -> str:
    """Return the versioned agent-facing command and workflow dictionary."""

    return _catalog_text()


@mcp.tool()
def launch_engine(
    graphics_api: str = DEFAULT_GRAPHICS_API,
    startup_level: str = DEFAULT_STARTUP_LEVEL,
    agent_port: int = DEFAULT_PORT,
    executable: str | None = None,
    extra_args: list[str] | None = None,
    connect_timeout_seconds: float = 30.0,
    ready_timeout_seconds: float = DEFAULT_READY_TIMEOUT_SECONDS,
) -> dict[str, Any]:
    """Launch and connect to KimPeanutEngine.

    The default launches build/Debug/KimPeanutEngine.exe with Vulkan, the HDR+
    and Stanford bunny fixture, and loopback agent port 37373. The connection
    timeout only waits for the port; ready_timeout_seconds separately waits for
    the first completed frame, which accounts for level-loading cost. Use
    executable only when the built EXE is in another location. Extra arguments
    are passed as argv values without a shell.
    """

    return bridge.launch(
        graphics_api=graphics_api,
        startup_level=startup_level,
        port=agent_port,
        executable=executable,
        extra_args=extra_args,
        timeout_seconds=connect_timeout_seconds,
        ready_timeout_seconds=ready_timeout_seconds,
    )


@mcp.tool()
def connect_engine(
    host: str = DEFAULT_HOST,
    agent_port: int = DEFAULT_PORT,
    wait_for_ready: bool = True,
    ready_timeout_seconds: float = DEFAULT_READY_TIMEOUT_SECONDS,
) -> dict[str, Any]:
    """Attach to an existing engine and optionally wait for its first completed frame."""

    result = bridge.connect(host=host, port=agent_port)
    if wait_for_ready:
        result.update(bridge.wait_until_ready(ready_timeout_seconds))
    return result


@mcp.tool()
def engine_status() -> dict[str, Any]:
    """Return connection and ownership state without changing the engine."""

    return bridge.status()


@mcp.tool()
def disconnect_engine() -> dict[str, Any]:
    """Disconnect from the engine without stopping it."""

    return bridge.disconnect()


@mcp.tool()
def stop_engine() -> dict[str, Any]:
    """Stop only an engine process launched by this MCP server."""

    return bridge.stop()


@mcp.tool()
def list_engine_commands() -> dict[str, Any]:
    """List the live Runtime command descriptors from the connected engine."""

    return bridge.list_commands()


@mcp.tool()
def describe_engine_command(command: str) -> dict[str, Any]:
    """Ask the live engine for help and argument schema for one command."""

    return bridge.command_help(command)


@mcp.tool()
def execute_engine_command(
    command: str,
    arguments: dict[str, Any] | None = None,
    wait_for_completion: bool = True,
) -> dict[str, Any]:
    """Execute a live command using scalar JSON arguments.

    Prefer the typed tools for performance stats and screenshot capture. This
    escape hatch is for commands discovered with list_engine_commands or
    describe_engine_command. Engine arguments must be boolean, integer, float,
    or string values; arrays, objects, and null are rejected by the engine.
    """

    return bridge.execute(command, arguments, wait_for_completion=wait_for_completion)


@mcp.tool()
def poll_engine_request(request_id: int) -> dict[str, Any]:
    """Poll one request when execute_engine_command was called without waiting."""

    return bridge.poll(request_id)


@mcp.tool()
def gpu_stats() -> dict[str, Any]:
    """Return the latest completed-frame GPU statistics as kimpeanut.profiler.v1 JSON."""

    return bridge.performance_stats("gpu")


@mcp.tool()
def cpu_stats() -> dict[str, Any]:
    """Return the latest completed-frame CPU and frame-loop statistics as JSON."""

    return bridge.performance_stats("cpu")


@mcp.tool()
def stats() -> dict[str, Any]:
    """Return combined latest completed-frame CPU and GPU statistics as JSON."""

    return bridge.performance_stats("combined")


@mcp.tool()
def capture_screenshot(
    view: Literal[
        "engine_window",
        "scene_color",
        "linear_depth",
        "world_normal",
        "base_color",
        "material_params",
        "shadow_visibility",
        "spot_shadow_depth",
        "spot_shadow_visibility",
        "point_shadow_depth",
        "point_shadow_visibility",
    ] = "scene_color",
    path: str | None = None,
) -> dict[str, Any]:
    """Capture a PNG below save/screenshots/validation/.

    Use world_normal for the normal diagnostic and linear_depth for the depth
    diagnostic. The engine returns a pending request first; this tool polls it
    until the final result.
    """

    if view not in CAPTURE_VIEWS:
        raise EngineBridgeError(f"unsupported capture view: {view}")
    return bridge.capture_screenshot(view=view, path=path)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    LOGGER.info("starting KimPeanutEngine MCP server over stdio")
    mcp.run()
