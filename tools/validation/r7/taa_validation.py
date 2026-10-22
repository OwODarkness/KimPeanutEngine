"""Capture matched native TAA poses, lifecycle checks, and a supersampled reference."""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/validation/r6"))
from runtime_replay import RuntimeClient, flatten_actor_page


def window_handle(pid: int):
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    user.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
    handles = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def visit(handle, _):
        process = wt.DWORD()
        user.GetWindowThreadProcessId(handle, ctypes.byref(process))
        name = ctypes.create_unicode_buffer(128)
        user.GetClassNameW(handle, name, 128)
        if process.value == pid and name.value == "GLFW30":
            handles.append(handle)
        return True

    user.EnumWindows(visit, 0)
    if len(handles) != 1:
        raise RuntimeError(f"Expected one GLFW30 window for PID {pid}: {handles}")
    return handles[0]


def window_client_size(pid: int) -> tuple[int, int]:
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
    rect = wt.RECT()
    if not user.GetClientRect(window_handle(pid), ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    return rect.right, rect.bottom


def resize_reference_window(pid: int, width: int, height: int) -> None:
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
    user.SetWindowPos.argtypes = [wt.HWND, wt.HWND, ctypes.c_int, ctypes.c_int,
                                 ctypes.c_int, ctypes.c_int, wt.UINT]
    handle = window_handle(pid)
    outer = wt.RECT()
    if not user.GetWindowRect(handle, ctypes.byref(outer)):
        raise ctypes.WinError(ctypes.get_last_error())
    client_width, client_height = window_client_size(pid)
    # Bypass monitor tracking limits for an oversized reference; keep position/focus.
    flags = 0x0002 | 0x0004 | 0x0010 | 0x0400
    if not user.SetWindowPos(handle, None, 0, 0,
                            width + outer.right - outer.left - client_width,
                            height + outer.bottom - outer.top - client_height, flags):
        raise ctypes.WinError(ctypes.get_last_error())


def minimize_restore_window(pid: int) -> None:
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.ShowWindow.argtypes = [wt.HWND, ctypes.c_int]
    handle = window_handle(pid)
    user.ShowWindow(handle, 6)
    time.sleep(0.2)
    user.ShowWindow(handle, 9)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--quality", action="store_true")
    parser.add_argument("--performance", action="store_true")
    args = parser.parse_args()
    output = (ROOT / args.output).resolve()
    if not output.is_relative_to(ROOT / "save/diagnostics"):
        raise ValueError("Output must be under save/diagnostics")
    output.parent.mkdir(parents=True, exist_ok=True)
    fixture = json.loads(Path(__file__).with_name("taa_fixture.json").read_text())
    started = time.monotonic()
    while True:
        try:
            client = RuntimeClient("127.0.0.1", args.port)
            break
        except OSError:
            if time.monotonic() - started > 90:
                raise
            time.sleep(0.1)
    sources = ("asset/shader/temporal_aa.frag", "asset/shader/pbr_gbuffer.frag",
               "engine/runtime/render/deferred_renderer.cpp",
               "engine/runtime/render/passes/deferred_lighting_pass.cpp",
               "engine/runtime/render/passes/temporal_aa_pass.cpp",
               "engine/runtime/graphics/backend/vulkan/vulkan_backend.cpp")
    result = {
        "fixture": fixture, "captures": {}, "checks": {}, "stats": {},
        "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                             text=True).strip(),
        "source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                          for name in sources},
    }
    original_size = window_client_size(args.pid)

    def command(name, arguments):
        return client.command(name, arguments)

    def stats():
        return command("stats", {})["data"]

    def wait_frames(count=16):
        initial = stats()["frame_number"]
        deadline = time.monotonic() + 30
        while True:
            value = stats()
            if value["frame_number"] >= initial + count:
                return value
            if time.monotonic() > deadline:
                raise TimeoutError("Runtime frames stopped advancing")
            time.sleep(0.01)

    def capture(tag, view="scene_color"):
        request = command("capture.screenshot", {
            "view": view,
            "path": f"save/screenshots/validation/{args.prefix}-{tag}.png",
        })
        result["captures"][tag] = request
        return ROOT / request["data"]["output_path"]

    def actor(name):
        rows = flatten_actor_page(command("actor.list", {
            "offset": 0, "limit": 64, "name_contains": name,
        })["data"])
        rows = [row for row in rows if row["name"] == name]
        if len(rows) != 1:
            raise RuntimeError(f"Actor lookup failed: {name}: {rows}")
        return rows[0]

    camera = actor(fixture["camera_actor"])

    def pose(position, rotation=None, target=None):
        target = target or camera
        rotation = rotation or fixture["camera_rotation"]
        command("actor.control", {
            "id": target["id"], "generation": target["generation"],
            **dict(zip(("local_position_x", "local_position_y", "local_position_z"), position)),
            **dict(zip(("local_pitch", "local_yaw", "local_roll"), rotation)),
        })

    path_settings = json.loads((ROOT / "tools/validation/r6/sponza_motion_fixture.json").read_text())["path_tracing"]
    path_settings.update(enabled=False, hybrid_ray_query_shadows=True, sampling_policy="adaptive_camera_motion")

    def taa(enabled):
        command("render.temporal_aa_settings", {
            "enabled": enabled, "history_weight": fixture["history_weight"]})

    def ao(enabled):
        command("render.screen_space_ao_settings", {
            "enabled": enabled, "radius": 0.65, "bias": 0.025,
            "strength": 1.0, "quality": "medium",
        })

    def viewport_size(value):
        return value["viewport_width"], value["viewport_height"]

    def resize_viewport(width, height):
        client_width, client_height = window_client_size(args.pid)
        for _ in range(24):
            current = stats()
            old_width, old_height = viewport_size(current)
            if (old_width, old_height) == (width, height):
                return current
            client_width += width - old_width
            client_height += height - old_height
            command("window.resize", {"width": client_width, "height": client_height})
            wait_frames(8)
            if window_client_size(args.pid) != (client_width, client_height):
                resize_reference_window(args.pid, client_width, client_height)
                wait_frames(8)
        raise RuntimeError(f"Could not reach reference extent {width}x{height}: {viewport_size(stats())}")

    try:
        command("render.path_trace_settings", path_settings)
        taa(True)
        ao(True)
        pose(fixture["camera_position"])
        resize_viewport(fixture["viewport"]["width"], fixture["viewport"]["height"])
        ready = wait_frames(32)
        if not ready["textures_tracked_residency_complete"]:
            raise RuntimeError("Texture residency is incomplete")
        result["stats"]["combined"] = ready
        result["checks"]["active_history"] = ready["temporal_aa_effective"] and ready["temporal_aa_history_valid"]
        capture("on-window", "engine_window")
        capture("on")
        capture("albedo", "base_color")
        taa(False)
        disabled = wait_frames()
        result["stats"]["disabled"] = disabled
        result["checks"]["disabled_bypass"] = not disabled["temporal_aa_effective"] and disabled["temporal_aa_jitter_uv_x"] == 0 and disabled["temporal_aa_jitter_uv_y"] == 0
        capture("off")
        capture("off-window", "engine_window")
        taa(True)
        before_cut = wait_frames()
        pose([30.80743, 1.59222, 0.0])
        cut = wait_frames(4)
        pose(fixture["camera_position"])
        returned = wait_frames()
        result["stats"]["cut"] = cut
        result["checks"]["cut_invalidated"] = cut["temporal_aa_reset_count"] > before_cut["temporal_aa_reset_count"]
        result["checks"]["cut_recovered"] = returned["temporal_aa_history_valid"]
        moving = actor(fixture["moving_actor"])
        pose([0.0, 0.0, 0.2], [0.0, 0.0, 0.0], moving)
        wait_frames(4)
        capture("rigid-move")
        pose([0.0, 0.0, 0.0], [0.0, 0.0, 0.0], moving)
        wait_frames(4)
        capture("rigid-return")
        before_resize = stats()
        command("window.resize", {"width": 1000, "height": 640})
        resized = wait_frames(32)
        result["checks"]["resize_invalidated"] = resized["temporal_aa_reset_count"] > before_resize["temporal_aa_reset_count"]
        result["checks"]["resize_recovered"] = resized["temporal_aa_history_valid"]
        command("window.resize", {"width": original_size[0], "height": original_size[1]})
        wait_frames(32)
        before_minimize = stats()
        minimize_restore_window(args.pid)
        restored = wait_frames(16)
        result["checks"]["minimize_restore_recovered"] = (
            restored["frame_number"] > before_minimize["frame_number"] and
            restored["temporal_aa_history_valid"])
        if ready["graphics_api"] == "vulkan":
            path_settings["enabled"] = True
            command("render.path_trace_settings", path_settings)
            pt = wait_frames(32)
            result["checks"]["pt_excludes_taa"] = pt["path_trace_active"] and not pt["temporal_aa_effective"] and pt["temporal_aa_jitter_uv_x"] == 0 and pt["temporal_aa_jitter_uv_y"] == 0
            before_samples = pt["path_trace_samples"]
            taa(False)
            wait_frames(8)
            taa(True)
            pt_after = wait_frames(8)
            result["checks"]["taa_toggle_preserves_pt"] = pt_after["path_trace_samples"] >= before_samples
            result["stats"]["pt"] = pt_after
            path_settings["enabled"] = False
            command("render.path_trace_settings", path_settings)
        command("level.reload", {})
        reloaded = wait_frames(32)
        camera = actor(fixture["camera_actor"])
        result["checks"]["reload_recovered"] = reloaded["temporal_aa_effective"] and reloaded["temporal_aa_history_valid"]

        if args.quality:
            import numpy as np
            from PIL import Image
            ao(False)
            path_settings["hybrid_ray_query_shadows"] = False
            command("render.path_trace_settings", path_settings)
            pose(fixture["camera_position"])
            native_extent = (fixture["viewport"]["width"], fixture["viewport"]["height"])
            resize_viewport(*native_extent)
            images = {"off": [], "on": [], "reference": []}
            for mode in ("off", "on", "reference"):
                taa(mode == "on")
                if mode == "reference":
                    scale = fixture["reference_scale"]
                    resize_viewport(native_extent[0] * scale, native_extent[1] * scale)
                wait_frames(32)
                for index, offset in enumerate(fixture["camera_strafe_offsets"]):
                    position = list(fixture["camera_position"])
                    position[2] += offset
                    pose(position)
                    wait_frames(fixture["settle_frames"])
                    image_path = capture(f"quality-{mode}-{index}")
                    rgb = np.asarray(Image.open(image_path).convert("RGB"), dtype=np.float32) / 255
                    linear = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
                    if mode == "reference":
                        linear = np.stack([np.asarray(Image.fromarray(linear[:, :, channel]).resize(native_extent, Image.Resampling.LANCZOS)) for channel in range(3)], axis=2)
                    images[mode].append(linear)
            reference = np.stack(images["reference"])
            measurements = {}
            for mode in ("off", "on"):
                values = np.stack(images[mode])
                error = values - reference
                gradient_error = abs(np.diff(values, axis=2) - np.diff(reference, axis=2)).mean() + abs(np.diff(values, axis=1) - np.diff(reference, axis=1)).mean()
                measurements[mode] = {
                    "static_mae": float(abs(error[0]).mean()),
                    "motion_error_shimmer": float(np.std(error, axis=0).mean()),
                    "edge_gradient_error": float(gradient_error),
                }
            limits = fixture["limits"]
            for name, metric, limit in (
                ("static_reference", "static_mae", "static_error_ratio"),
                ("motion_shimmer", "motion_error_shimmer", "motion_error_shimmer_ratio"),
                ("edge_detail", "edge_gradient_error", "edge_gradient_error_ratio"),
            ):
                result["checks"][name] = measurements["on"][metric] <= measurements["off"][metric] * limits[limit]
            result["quality"] = measurements
            resize_viewport(*native_extent)
            pose(fixture["camera_position"])
            path_settings["hybrid_ray_query_shadows"] = True
            command("render.path_trace_settings", path_settings)
            ao(True)
        taa(True)
        result["stats"]["final"] = wait_frames(32)
        capture("final")
        if args.performance:
            resize_viewport(fixture["viewport"]["width"], fixture["viewport"]["height"])
            pose(fixture["camera_position"])
            result["performance"] = []
            for index in range(3):
                taa(True)
                sample = wait_frames(4)
                deadline = time.monotonic() + 60
                while not sample["summary_complete"]:
                    if time.monotonic() > deadline:
                        raise TimeoutError("GPU timing window did not complete")
                    time.sleep(0.05)
                    sample = stats()
                result["performance"].append(sample)
                limits = fixture["limits"]
                result["checks"][f"timing_{index}"] = (
                    not sample["path_trace_active"] and
                    sample["temporal_aa_effective"] and
                    sample["textures_tracked_residency_complete"] and
                    sample["pass.temporal_aa_resolve.gpu_p95_ms"] <= limits["taa_gpu_p95_ms"] and
                    sample["summary_total_gpu_p95_ms"] <= limits["combined_gpu_p95_ms"])
                allocation = sample["r7_active_texture_allocation_bytes"]
                if sample["graphics_api"] == "vulkan":
                    result["checks"][f"memory_{index}"] = (
                        allocation is not None and allocation <= limits["r7_texture_allocation_bytes"])
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        try:
            resize_reference_window(args.pid, *original_size)
        except (OSError, RuntimeError) as error:
            result["restore_error"] = str(error)
        output.write_text(json.dumps(result, indent=2))
        client.close()
    print(json.dumps({"checks": result["checks"], "quality": result.get("quality")}, indent=2))
    if not all(result["checks"].values()):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
