"""Check fog bypass, composition and analytic sky limits through Runtime commands."""

import argparse
import hashlib
import json
import math
import re
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/validation/r6"))
from runtime_replay import RuntimeClient, flatten_actor_page


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--fixture", choices=("sponza", "indoor", "terrain"), required=True)
    parser.add_argument("--measure", action="store_true", help="Collect three Vulkan RelWithDebInfo windows")
    parser.add_argument("--regenerate", action="store_true", help="Publish a seeded Terrain Viewer preview first")
    args = parser.parse_args()
    if args.regenerate and args.fixture != "terrain":
        parser.error("--regenerate requires the terrain fixture")
    if not re.fullmatch(r"[a-z0-9-]+", args.prefix):
        parser.error("prefix must contain lowercase letters, digits and hyphens")
    output = ROOT / "save/diagnostics" / f"{args.prefix}.json"
    if output.exists():
        parser.error("choose an unused prefix")
    result = {"fixture": args.fixture, "checks": {}, "captures": {}, "stats": {}}
    result["revision"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    result["shader_sha256"] = hashlib.sha256((ROOT / "asset/shader/height_fog.frag").read_bytes()).hexdigest()
    checks = result["checks"]
    ready_deadline = time.monotonic() + 45
    while True:
        try:
            client = RuntimeClient("127.0.0.1", args.port)
            break
        except OSError:
            if time.monotonic() >= ready_deadline:
                raise
            time.sleep(0.25)
    command = client.command

    def stats():
        return command("stats", {})["data"]

    def settle(frames=16):
        target = stats()["frame_number"] + frames
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            current = stats()
            if current["frame_number"] >= target:
                return current
            time.sleep(0.04)
        raise TimeoutError("frames did not advance")

    def capture(tag, view="scene_color"):
        path = f"save/screenshots/validation/{args.prefix}-{tag}.png"
        if (ROOT / path).exists():
            raise FileExistsError(path)
        data = command("capture.screenshot", {"view": view, "path": path})["data"]
        result["captures"][tag] = data["output_path"]
        return ROOT / data["output_path"]

    settings = dict(enabled=True, affect_sky=False, density=0.04, reference_height=1.5,
                    height_falloff=0.12, max_distance=120.0, red=0.45, green=0.55, blue=0.65)

    def fog(**values):
        command("render.height_fog_settings", settings | values)
        return settle()

    def taa(enabled):
        command("render.temporal_aa_settings", {"enabled": enabled, "history_weight": 0.9})

    path_settings = json.loads((ROOT / "tools/validation/r6/sponza_motion_fixture.json").read_text())["path_tracing"]
    path_settings.update(enabled=False, hybrid_ray_query_shadows=False)
    try:
        if args.regenerate:
            def preview():
                return command("terrain.preview_status", {})["data"]

            def wait_preview(min_revision):
                deadline = time.monotonic() + 45
                while time.monotonic() < deadline:
                    current = preview()
                    if current["status"].startswith(("Failed", "Invalid", "Busy", "Cancelled")):
                        raise RuntimeError(current["status"])
                    if current["revision"] >= min_revision and current["status"] == "Ready":
                        return current
                    time.sleep(0.1)
                raise TimeoutError("Terrain preview did not publish")

            previous = wait_preview(0)
            recipe = dict(seed=7426, lattice_size=8, octaves=5, persistence=0.5, lacunarity=2.0)
            command("terrain.regenerate", recipe)
            generated = wait_preview(previous["revision"] + 1)
            result["terrain_generation"] = {"before": previous, "after": generated, "recipe": recipe}
            checks["terrain_regenerated"] = generated["revision"] > previous["revision"]
        command("render.path_trace_settings", path_settings)
        taa(False)
        if args.fixture == "terrain":
            settings.update(density=0.007, reference_height=60.0, height_falloff=0.008,
                            max_distance=600.0)
        before = fog(enabled=False)
        deadline = time.monotonic() + 45
        while not before["textures_tracked_residency_complete"]:
            if time.monotonic() >= deadline:
                raise TimeoutError("texture residency did not settle")
            before = settle()
        baseline = capture("off")
        capture("albedo", "base_color")
        zero = fog(density=0.0)
        checks["zero_bypass"] = not zero["height_fog_effective"] and zero["height_fog_requested"]
        checks["zero_pixel_identity"] = baseline.read_bytes() == capture("zero").read_bytes()
        on = fog()
        result["stats"]["on"] = on
        checks["fog_executes"] = (on["height_fog_effective"] and
                                  on["pass.height_fog.outcome"] == "executed")
        checks["fog_changes_scene"] = baseline.read_bytes() != capture("on").read_bytes()
        capture("transmittance", "fog_transmittance")
        taa(True)
        prior = settle()
        combined = fog(density=settings["density"] * 1.5)
        result["stats"]["combined"] = combined
        checks["taa_recovers_after_edit"] = (combined["temporal_aa_history_valid"] and
            combined["temporal_aa_reset_count"] > prior["temporal_aa_reset_count"])
        capture("with-taa")
        if args.measure:
            if args.fixture != "sponza" or on["graphics_api"] != "vulkan":
                raise ValueError("measurement requires the Vulkan Sponza fixture")
            result["performance"] = []
            for _ in range(3):
                sample = fog(density=settings["density"] * 1.5)
                deadline = time.monotonic() + 60
                while not sample["summary_complete"]:
                    if time.monotonic() >= deadline:
                        raise TimeoutError("performance window did not complete")
                    time.sleep(0.1)
                    sample = stats()
                result["performance"].append(sample)
            checks["fog_gpu_budget"] = all(
                sample["pass.height_fog.gpu_p95_ms"] <= 0.5 for sample in result["performance"])
        if on["graphics_api"] == "vulkan" and args.fixture == "sponza":
            command("render.path_trace_settings", path_settings | {"enabled": True})
            pt = settle()
            checks["pt_bypasses_fog"] = pt["path_trace_active"] and not pt["height_fog_effective"]
            command("render.path_trace_settings", path_settings)
        if args.fixture == "indoor":
            taa(False)
            rows = flatten_actor_page(command("actor.list", {
                "offset": 0, "limit": 16, "name_contains": "R7 Fixture Camera",
            })["data"])
            camera = next(row for row in rows if row["name"] == "R7 Fixture Camera")
            pose = {
                "id": camera["id"], "generation": camera["generation"],
                "local_position_x": 0.0, "local_position_y": 1000.0, "local_position_z": 0.0,
                "local_pitch": 0.0, "local_yaw": -90.0, "local_roll": 0.0,
            }
            command("actor.control", pose)
            fog(affect_sky=False)
            with Image.open(capture("sky-bypass", "fog_transmittance")) as img:
                checks["sky_bypass"] = img.convert("RGB").getpixel((img.width // 2, img.height // 2)) == (255,) * 3
            for tag, falloff, distance, pitch in (("homogeneous", 0.0, 10.0, 0.0),
                    ("horizontal", 0.25, 10.0, 0.0), ("farther", 0.25, 20.0, 0.0),
                    ("uphill", 0.25, 10.0, 45.0), ("downhill", 0.25, 10.0, -45.0)):
                command("actor.control", pose | {"local_pitch": pitch})
                fog(affect_sky=True, density=0.07, reference_height=1000.0,
                    height_falloff=falloff, max_distance=distance)
                with Image.open(capture(tag, "fog_transmittance")) as img:
                    measured = img.convert("RGB").getpixel((img.width // 2, img.height // 2))[0] / 255.0
                linear = measured / 12.92 if measured <= 0.04045 else ((measured + 0.055) / 1.055) ** 2.4
                q = falloff * distance * math.sin(math.radians(pitch))
                expected = math.exp(-0.07 * distance * (-math.expm1(-q) / q if q else 1.0))
                result.setdefault("analytic", {})[tag] = {"linear": linear, "expected": expected}
                checks[tag] = abs(linear - expected) <= 0.015
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2), encoding="utf-8")
        client.close()
    print(json.dumps(checks, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
