#!/usr/bin/env python3
"""Apply an R6 path-tracing preset and replay the checked-in Sponza camera path."""

from __future__ import annotations

import argparse
import json
import socket
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
FIXTURE = Path(__file__).with_name("sponza_motion_fixture.json")
PRESETS = {
    "1raw": (1, "raw", "all_lights", "fixed"),
    "1guided": (1, "guided_preview", "all_lights", "fixed"),
    "1one-light": (1, "guided_preview", "uniform_one_light", "fixed"),
    "2raw": (2, "raw", "all_lights", "fixed"),
    "4beauty": (4, "raw", "all_lights", "fixed"),
    "adaptive": (1, "raw", "all_lights", "adaptive_camera_motion"),
}


class RuntimeClient:
    def __init__(self, host: str, port: int) -> None:
        self.socket = socket.create_connection((host, port), timeout=10.0)
        self.file = self.socket.makefile("rwb", buffering=0)

    def close(self) -> None:
        self.file.close()
        self.socket.close()

    def request(self, payload: dict[str, Any]) -> dict[str, Any]:
        self.file.write((json.dumps(payload, separators=(",", ":")) + "\n").encode())
        line = self.file.readline()
        if not line:
            raise RuntimeError("Runtime closed the command connection")
        return json.loads(line)

    def command(self, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
        result = self.request({"op": "execute", "command": name, "arguments": arguments})
        deadline = time.monotonic() + 30.0
        while result.get("status") == "pending":
            if time.monotonic() >= deadline:
                raise TimeoutError(f"Runtime command timed out: {name}")
            time.sleep(0.025)
            result = self.request({"op": "poll", "request_id": result["request_id"]})
        if result.get("status") != "success":
            raise RuntimeError(f"{name} failed: {result}")
        return result


def flatten_actor_page(data: dict[str, Any]) -> list[dict[str, Any]]:
    actors = []
    for index in range(int(data.get("count", 0))):
        prefix = f"actors.{index}."
        actors.append({
            "id": data[prefix + "id"],
            "generation": data[prefix + "generation"],
            "name": data[prefix + "name"],
        })
    return actors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=37373)
    parser.add_argument("--preset", choices=PRESETS, default="1raw")
    parser.add_argument("--fixture", type=Path, default=FIXTURE)
    parser.add_argument("--output", type=Path, required=True,
                        help="JSON result path under save/diagnostics/")
    parser.add_argument("--capture", action="store_true",
                        help="Save a screenshot after the last camera pose")
    parser.add_argument("--motion", action="store_true",
                        help="Apply all camera keyframes; default holds the start pose")
    parser.add_argument("--settle-frames", type=int,
                        help="Override the adaptive policy's still-frame threshold")
    parser.add_argument("--poll-seconds", type=float, default=0.20)
    args = parser.parse_args()

    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    if fixture.get("schema") != "kimpeanut.r6.motion_fixture.v1":
        raise RuntimeError("Unsupported R6 fixture schema")
    output_path = (ROOT / args.output).resolve()
    diagnostics_root = (ROOT / "save/diagnostics").resolve()
    if diagnostics_root not in output_path.parents:
        raise ValueError("--output must be inside save/diagnostics/")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    (samples_per_dispatch, reconstruction, light_sampling,
     sampling_policy) = PRESETS[args.preset]
    settings = dict(fixture["path_tracing"])
    settings.update({
        "enabled": True,
        "hybrid_ray_query_shadows": True,
        "samples_per_dispatch": samples_per_dispatch,
        "reconstruction": reconstruction,
        "direct_light_sampling": light_sampling,
        "sampling_policy": sampling_policy,
    })
    if args.settle_frames is not None:
        settings["settle_frame_threshold"] = args.settle_frames
    client = RuntimeClient(args.host, args.port)
    result: dict[str, Any] = {
        "fixture": fixture,
        "preset": args.preset,
        "motion": args.motion,
        "camera_keyframes": [],
    }
    try:
        actors = flatten_actor_page(client.command(
            "actor.list", {"offset": 0, "limit": 64,
                           "name_contains": fixture["camera_actor"]}
        )["data"])
        if len(actors) != 1 or actors[0]["name"] != fixture["camera_actor"]:
            raise RuntimeError(f"Could not uniquely resolve camera Actor: {actors}")
        camera = actors[0]
        start_pose = fixture["camera_path"][0]
        x, y, z = start_pose["position"]
        pitch, yaw, roll = start_pose["rotation_degrees"]
        initial_control = client.command("actor.control", {
            "id": camera["id"],
            "generation": camera["generation"],
            "local_position_x": x,
            "local_position_y": y,
            "local_position_z": z,
            "local_pitch": pitch,
            "local_yaw": yaw,
            "local_roll": roll,
        })
        result["initial_camera_control"] = initial_control
        previous = client.command("stats", {"json": True})["data"]
        previous_frame = int(previous.get("frame_number", 0))
        result["settings_command"] = client.command(
            "render.path_trace_settings", settings
        )
        profile = client.command("stats", {"json": True})["data"]
        deadline = time.monotonic() + 30.0
        while not (
            int(profile.get("frame_number", 0)) > previous_frame
            and profile.get("summary_samples_collected") == 0
            and profile.get("summary_warmup_frames_completed", 0) < 120
            and profile.get("path_trace_active")
            and profile.get("textures_tracked_residency_complete")
            and profile.get("path_trace_settings_effective_sampling_policy")
                == {"fixed": 0, "adaptive_camera_motion": 1}[sampling_policy]
            and (sampling_policy == "adaptive_camera_motion" or
                 profile.get("path_trace_settings_effective_samples_per_dispatch")
                    == samples_per_dispatch)
            and profile.get("path_trace_settings_effective_reconstruction")
                == {"raw": 0, "guided_preview": 1, "variance_denoise": 2}[reconstruction]
            and profile.get("path_trace_settings_effective_direct_light_sampling")
                == {"all_lights": 0, "uniform_one_light": 1}[light_sampling]
        ):
            if time.monotonic() >= deadline:
                raise TimeoutError("Runtime did not reach the requested resident PT mode")
            time.sleep(args.poll_seconds)
            profile = client.command("stats", {"json": True})["data"]

        expected = fixture["viewport"]
        if (profile.get("viewport_width"), profile.get("viewport_height")) != (
            expected["width"], expected["height"]
        ):
            raise RuntimeError(
                f"Viewport mismatch: {profile.get('viewport_width')}x"
                f"{profile.get('viewport_height')} != {expected['width']}x{expected['height']}"
            )
        if profile.get("ray_tracing_light_records") != len(fixture["authored_lights"]):
            raise RuntimeError("Runtime light-record count differs from the fixture")
        result["activation_stats"] = profile
        base_frame = int(profile["gpu_frame_number"])
        keyframes = fixture["camera_path"][1:] if args.motion else []
        for keyframe in keyframes:
            target_frame = base_frame + int(keyframe["frame_offset"])
            while int(profile.get("gpu_frame_number") or 0) < target_frame:
                time.sleep(args.poll_seconds)
                profile = client.command("stats", {"json": True})["data"]
            previous_gpu_frame = int(profile.get("gpu_frame_number") or 0)
            x, y, z = keyframe["position"]
            pitch, yaw, roll = keyframe["rotation_degrees"]
            applied = client.command("actor.control", {
                "id": camera["id"],
                "generation": camera["generation"],
                "local_position_x": x,
                "local_position_y": y,
                "local_position_z": z,
                "local_pitch": pitch,
                "local_yaw": yaw,
                "local_roll": roll,
            })
            profile = client.command("stats", {"json": True})["data"]
            deadline = time.monotonic() + 5.0
            expected_position = keyframe["position"]
            def pose_observed() -> bool:
                actual = [profile.get(f"path_trace_camera_{axis}")
                          for axis in ("x", "y", "z")]
                return all(value is not None and abs(value - expected) < 1.0e-3
                           for value, expected in zip(actual, expected_position))

            while (int(profile.get("gpu_frame_number") or 0) < previous_gpu_frame + 2
                   or not pose_observed()):
                if time.monotonic() >= deadline:
                    raise TimeoutError("Runtime did not publish the camera keyframe pose")
                time.sleep(0.005)
                profile = client.command("stats", {"json": True})["data"]
            result["camera_keyframes"].append({
                "label": keyframe["label"],
                "target_gpu_frame": target_frame,
                "applied": applied,
                "rendered_gpu_frame": profile.get("gpu_frame_number"),
                "sampling_state": profile.get("path_trace_sampling_state"),
                "samples_per_dispatch": profile.get("path_trace_samples_per_dispatch"),
                "reconstruction": profile.get(
                    "path_trace_settings_effective_reconstruction"),
                "accumulated_samples": profile.get("path_trace_samples"),
                "camera_position": [profile.get(f"path_trace_camera_{axis}")
                                    for axis in ("x", "y", "z")],
            })

        deadline = time.monotonic() + 60.0
        while not (
            profile.get("summary_complete")
            and profile.get("summary_samples_collected", 0) >= 300
        ):
            if time.monotonic() >= deadline:
                raise TimeoutError("The Runtime 120-warmup/300-sample profile did not complete")
            time.sleep(args.poll_seconds)
            profile = client.command("stats", {"json": True})["data"]

        if args.capture:
            image = f"save/screenshots/validation/r6-{args.preset}-motion-final.png"
            result["final_capture"] = client.command("capture.screenshot", {
                "path": image,
                "view": "scene_color",
            })
        result["final_stats"] = profile
    finally:
        client.close()

    output_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(output_path)
    print(json.dumps({
        "preset": args.preset,
        "settings": {
            "samples_per_dispatch": profile.get("path_trace_settings_effective_samples_per_dispatch"),
            "reconstruction": profile.get("path_trace_settings_effective_reconstruction"),
            "direct_light_sampling": profile.get("path_trace_settings_effective_direct_light_sampling"),
            "sampling_policy": profile.get("path_trace_settings_effective_sampling_policy"),
            "sampling_state": profile.get("path_trace_sampling_state"),
        },
        "frame": profile.get("gpu_frame_number"),
        "total_gpu_p50_ms": profile.get("summary_total_gpu_p50_ms"),
        "total_gpu_p95_ms": profile.get("summary_total_gpu_p95_ms"),
        "profile_samples": profile.get("summary_samples_collected"),
        "camera_keyframes": len(result["camera_keyframes"]),
        "capture": bool(args.capture),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
