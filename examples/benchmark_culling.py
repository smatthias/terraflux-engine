#!/usr/bin/env python3
"""Run same-scene, same-machine GPU/CPU culling A/B benchmark.

Requires an OpenGL 4.3 context/display and built native viewer.
This does not assert that culling always helps; it prints measured outcomes.
"""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def run(viewer: Path, scene: Path, frames: int, no_culling: bool) -> dict:
    command = [
        str(viewer), "--obj", str(ROOT / "examples/assets/pyramid.obj"),
        "--scene", str(scene), "--benchmark-frames", str(frames),
        "--no-instancing"  # isolate frustum culling from instancing
    ]
    if no_culling:
        command.append("--no-culling")
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                               check=True)
    for line in completed.stdout.splitlines():
        if line.startswith("BENCHMARK_JSON="):
            return json.loads(line.partition("=")[2])
    raise RuntimeError(f"viewer emitted no benchmark data: {completed.stderr}")

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer", type=Path, default=ROOT/"build-gl/terraflux-terrain-viewer")
    parser.add_argument("--scene", type=Path,
                        default=ROOT/"examples/scenes/benchmark_culling.json")
    parser.add_argument("--frames", type=int, default=120)
    args = parser.parse_args()
    if not 8 <= args.frames <= 2000:
        parser.error("--frames must be in 8..2000")
    viewer=args.viewer.resolve()
    scene=args.scene.resolve()
    without = run(viewer, scene, args.frames, True)
    with_culling = run(viewer, scene, args.frames, False)
    print(f"Same scene ({args.frames} measured frames, 8 warmup), no VSync:")
    print("                       Culling off     Culling on      Difference")
    for key,label in (
        ("cpu_render_mean_ms","CPU render (ms)"),
        ("gpu_draw_mean_ms","GPU clear+draw (ms)"),
        ("draws_per_frame","Draw calls/frame"),
    ):
        a,b=without[key],with_culling[key]
        delta=((b-a)/a)*100 if a else 0
        print(f"{label:<22} {a:12.3f} {b:14.3f} {delta:+12.1f}%")
    print("Culled objects/frame:",with_culling["culled_per_frame"])
    print("GPU samples:",without["gpu_samples"],"/",with_culling["gpu_samples"])
    print("Software Mesa/Xvfb results are not hardware GPU benchmarks.")
    print("GPU time covers clear and draw work; CPU render time includes submission.")
    print("Run several trials on the same GPU/driver for meaningful conclusions.")

if __name__ == "__main__":
    main()
