"""Local client for KimPeanutEngine's loopback agent transport.

The engine owns command validation, permissions, game-thread dispatch, and
render-thread synchronization. This module only frames JSON-lines requests and
provides typed convenience methods for the MCP server.
"""

from __future__ import annotations

import json
import socket
import subprocess
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from threading import RLock
from typing import Any, BinaryIO, Mapping, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXECUTABLE = REPOSITORY_ROOT / "build" / "Debug" / "KimPeanutEngine.exe"
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 37373
DEFAULT_GRAPHICS_API = "vulkan"
DEFAULT_STARTUP_LEVEL = "level/performance_profile.level"
DEFAULT_CONNECT_TIMEOUT_SECONDS = 30.0
DEFAULT_READY_TIMEOUT_SECONDS = 120.0
DEFAULT_COMMAND_TIMEOUT_SECONDS = 20.0

CAPTURE_VIEWS = (
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
)

ScalarValue = bool | int | float | str


class EngineBridgeError(RuntimeError):
    """A local transport, process, or command contract failure."""


@dataclass(frozen=True)
class EngineConnectionInfo:
    host: str
    port: int
    process_id: int | None
    owns_process: bool


def normalize_capture_path(path: str | None) -> str:
    """Validate a capture path against the engine's safe output directory."""

    if path is None or not path.strip():
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        candidate = Path("save") / "screenshots" / "validation" / f"mcp-{timestamp}.png"
    else:
        candidate = Path(path)

    if candidate.is_absolute():
        raise EngineBridgeError("capture path must be relative to the repository")
    if candidate.suffix.lower() != ".png":
        raise EngineBridgeError("capture path must end in .png")

    validation_root = (REPOSITORY_ROOT / "save" / "screenshots" / "validation").resolve()
    resolved = (REPOSITORY_ROOT / candidate).resolve()
    try:
        resolved.relative_to(validation_root)
    except ValueError as error:
        raise EngineBridgeError(
            "capture path must remain below save/screenshots/validation/"
        ) from error
    return candidate.as_posix()


def validate_capture_view(view: str) -> str:
    """Return a supported capture view or raise a useful MCP tool error."""

    if view not in CAPTURE_VIEWS:
        supported = ", ".join(CAPTURE_VIEWS)
        raise EngineBridgeError(f"unsupported capture view '{view}'; use one of: {supported}")
    return view


class EngineBridge:
    """One serialized client session for the engine's loopback endpoint."""

    def __init__(self) -> None:
        self._lock = RLock()
        self._socket: socket.socket | None = None
        self._reader: BinaryIO | None = None
        self._process: subprocess.Popen[bytes] | None = None
        self._host = DEFAULT_HOST
        self._port = DEFAULT_PORT
        self._ready_frame: int | None = None

    def connect(
        self,
        host: str = DEFAULT_HOST,
        port: int = DEFAULT_PORT,
        timeout_seconds: float = DEFAULT_CONNECT_TIMEOUT_SECONDS,
    ) -> dict[str, Any]:
        """Attach to an already-running engine without taking process ownership."""

        if host not in {"127.0.0.1", "localhost", "::1"}:
            raise EngineBridgeError("the engine transport is intentionally loopback-only")
        if not 1 <= port <= 65535:
            raise EngineBridgeError("agent port must be between 1 and 65535")

        with self._lock:
            self._close_socket_locked()
            try:
                connection = socket.create_connection((host, port), timeout=timeout_seconds)
            except OSError as error:
                raise EngineBridgeError(
                    f"could not connect to engine at {host}:{port}; launch the EXE with --agent-port"
                ) from error
            connection.settimeout(DEFAULT_COMMAND_TIMEOUT_SECONDS)
            self._socket = connection
            self._reader = connection.makefile("rb")
            self._host = host
            self._port = port
            self._ready_frame = None
            return self._status_locked()

    def launch(
        self,
        graphics_api: str = DEFAULT_GRAPHICS_API,
        startup_level: str = DEFAULT_STARTUP_LEVEL,
        port: int = DEFAULT_PORT,
        executable: str | None = None,
        extra_args: Sequence[str] | None = None,
        timeout_seconds: float = DEFAULT_CONNECT_TIMEOUT_SECONDS,
        ready_timeout_seconds: float = DEFAULT_READY_TIMEOUT_SECONDS,
    ) -> dict[str, Any]:
        """Launch a runtime with the agent endpoint and attach to it."""

        with self._lock:
            if self._socket is not None or (
                self._process is not None and self._process.poll() is None
            ):
                raise EngineBridgeError("an engine session is already active; stop or disconnect it first")
        executable_path = Path(executable) if executable else DEFAULT_EXECUTABLE
        if not executable_path.is_absolute():
            executable_path = REPOSITORY_ROOT / executable_path
        executable_path = executable_path.resolve()
        if not executable_path.is_file():
            raise EngineBridgeError(f"engine executable not found: {executable_path}")
        if not graphics_api.strip():
            raise EngineBridgeError("graphics_api must not be empty")
        if not startup_level.startswith("level/"):
            raise EngineBridgeError("startup_level must be an engine level path beginning with level/")

        arguments = [
            str(executable_path),
            "--graphics-api",
            graphics_api,
            "--startup-level",
            startup_level,
            "--agent-port",
            str(port),
        ]
        if extra_args:
            arguments.extend(str(argument) for argument in extra_args)

        try:
            process = subprocess.Popen(
                arguments,
                cwd=REPOSITORY_ROOT,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                shell=False,
            )
        except OSError as error:
            raise EngineBridgeError(f"could not launch engine: {error}") from error

        with self._lock:
            self._process = process
            self._host = DEFAULT_HOST
            self._port = port

        deadline = time.monotonic() + timeout_seconds
        last_error: OSError | None = None
        connected = False
        while time.monotonic() < deadline:
            if process.poll() is not None:
                self._clear_process()
                raise EngineBridgeError(
                    f"engine exited during startup with code {process.returncode}"
                )
            try:
                self.connect(DEFAULT_HOST, port, timeout_seconds=0.5)
                with self._lock:
                    self._process = process
                connected = True
                break
            except EngineBridgeError as error:
                last_error = error.__cause__ if isinstance(error.__cause__, OSError) else None
                time.sleep(0.1)

        if not connected:
            self.stop()
            detail = f": {last_error}" if last_error else ""
            raise EngineBridgeError(f"engine did not open agent port {port} before timeout{detail}")

        try:
            readiness = self.wait_until_ready(ready_timeout_seconds)
        except EngineBridgeError:
            self.stop()
            raise
        result = self.status()
        result.update(readiness)
        return result

    def disconnect(self) -> dict[str, Any]:
        """Close the transport while leaving an owned engine process running."""

        with self._lock:
            self._close_socket_locked()
            return self._status_locked()

    def stop(self) -> dict[str, Any]:
        """Close the session and terminate only a process launched by this bridge."""

        with self._lock:
            self._close_socket_locked()
            process = self._process
            self._process = None

        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5.0)
        return self.status()

    def status(self) -> dict[str, Any]:
        with self._lock:
            return self._status_locked()

    def wait_until_ready(
        self,
        timeout_seconds: float = DEFAULT_READY_TIMEOUT_SECONDS,
    ) -> dict[str, Any]:
        """Wait until the live Runtime publishes its first completed frame."""

        if timeout_seconds <= 0:
            raise EngineBridgeError("ready timeout must be greater than zero")
        started = time.monotonic()
        deadline = started + timeout_seconds
        last_result: dict[str, Any] | None = None
        while time.monotonic() < deadline:
            try:
                result = self.execute(
                    "stats",
                    {"json": True},
                    wait_for_completion=True,
                    timeout_seconds=min(5.0, max(0.1, deadline - time.monotonic())),
                )
                last_result = result
                data = result.get("data")
                frame_number = data.get("frame_number") if isinstance(data, dict) else None
                if result.get("status") == "success" and isinstance(frame_number, int) and frame_number > 0:
                    with self._lock:
                        self._ready_frame = frame_number
                    return {
                        "ready": True,
                        "ready_frame": frame_number,
                        "ready_after_seconds": round(time.monotonic() - started, 3),
                    }
            except EngineBridgeError:
                if not self.is_connected:
                    raise
            time.sleep(0.25)

        status = last_result.get("status") if last_result else "no response"
        raise EngineBridgeError(
            f"engine port opened but no completed frame was published within "
            f"{timeout_seconds:.1f} seconds (last stats status: {status})"
        )

    @property
    def is_connected(self) -> bool:
        with self._lock:
            return self._socket is not None and self._reader is not None

    def list_commands(self) -> dict[str, Any]:
        return self._request({"op": "list"})

    def command_help(self, command: str) -> dict[str, Any]:
        if not command.strip():
            raise EngineBridgeError("command must not be empty")
        return self.execute("help", {"name": command}, wait_for_completion=True)

    def execute(
        self,
        command: str,
        arguments: Mapping[str, ScalarValue] | None = None,
        *,
        wait_for_completion: bool = True,
        timeout_seconds: float = DEFAULT_COMMAND_TIMEOUT_SECONDS,
    ) -> dict[str, Any]:
        if not command.strip():
            raise EngineBridgeError("command must not be empty")
        if arguments is not None:
            for name, value in arguments.items():
                if not isinstance(value, (bool, int, float, str)):
                    raise EngineBridgeError(
                        f"argument '{name}' must be a scalar boolean, integer, float, or string"
                    )

        result = self._request(
            {
                "op": "execute",
                "command": command,
                "arguments": dict(arguments or {}),
            }
        )
        if result.get("status") == "pending" and wait_for_completion:
            return self.wait_for_completion(int(result["request_id"]), timeout_seconds)
        return result

    def poll(self, request_id: int) -> dict[str, Any]:
        if request_id <= 0:
            raise EngineBridgeError("request_id must be positive")
        return self._request({"op": "poll", "request_id": request_id})

    def wait_for_completion(
        self,
        request_id: int,
        timeout_seconds: float = DEFAULT_COMMAND_TIMEOUT_SECONDS,
    ) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            result = self.poll(request_id)
            if result.get("status") != "pending":
                return result
            time.sleep(0.05)
        raise EngineBridgeError(f"engine request {request_id} did not complete before timeout")

    def performance_stats(self, kind: str) -> dict[str, Any]:
        command_by_kind = {"gpu": "gpu-stats", "cpu": "cpu-stats", "combined": "stats"}
        try:
            command = command_by_kind[kind]
        except KeyError as error:
            raise EngineBridgeError("stats kind must be gpu, cpu, or combined") from error
        return self.execute(command, {"json": True}, wait_for_completion=True)

    def capture_screenshot(self, view: str = "scene_color", path: str | None = None) -> dict[str, Any]:
        return self.execute(
            "capture.screenshot",
            {"path": normalize_capture_path(path), "view": validate_capture_view(view)},
            wait_for_completion=True,
        )

    def close(self) -> None:
        """Release the socket while retaining ownership for a later stop."""

        self.disconnect()

    def _request(self, request: Mapping[str, Any]) -> dict[str, Any]:
        with self._lock:
            if self._socket is None or self._reader is None:
                raise EngineBridgeError("no engine session; call launch_engine or connect_engine first")
            try:
                payload = json.dumps(request, separators=(",", ":"), ensure_ascii=False).encode("utf-8") + b"\n"
                self._socket.sendall(payload)
                line = self._reader.readline()
            except (OSError, ValueError) as error:
                self._close_socket_locked()
                raise EngineBridgeError(f"engine transport failure: {error}") from error
            if not line:
                self._close_socket_locked()
                raise EngineBridgeError("engine closed the agent connection")
            try:
                response = json.loads(line.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                raise EngineBridgeError(f"engine returned invalid JSON: {error}") from error
            if not isinstance(response, dict):
                raise EngineBridgeError("engine response must be a JSON object")
            return response

    def _status_locked(self) -> dict[str, Any]:
        process = self._process
        return {
            "connected": self._socket is not None and self._reader is not None,
            "host": self._host,
            "port": self._port,
            "process_id": process.pid if process is not None else None,
            "owns_process": process is not None,
            "process_running": process is not None and process.poll() is None,
            "ready": self._ready_frame is not None,
            "ready_frame": self._ready_frame,
        }

    def _close_socket_locked(self) -> None:
        reader = self._reader
        connection = self._socket
        self._reader = None
        self._socket = None
        self._ready_frame = None
        if reader is not None:
            reader.close()
        if connection is not None:
            connection.close()

    def _clear_process(self) -> None:
        with self._lock:
            self._process = None


__all__ = [
    "CAPTURE_VIEWS",
    "DEFAULT_EXECUTABLE",
    "DEFAULT_GRAPHICS_API",
    "DEFAULT_HOST",
    "DEFAULT_PORT",
    "DEFAULT_READY_TIMEOUT_SECONDS",
    "DEFAULT_STARTUP_LEVEL",
    "EngineBridge",
    "EngineBridgeError",
    "normalize_capture_path",
    "validate_capture_view",
]
