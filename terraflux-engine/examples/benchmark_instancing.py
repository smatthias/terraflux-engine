#!/usr/bin/env python3
"""Benchmark hardware instancing against legacy per-object draw submissions.

Run both variants on the same machine and scene; records are observations,
not a guaranteed speedup. Requires an OpenGL 4.3 desktop context.
"""
import argparse
import json
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent

def benchmark(viewer: Path, scene: Path, frames: int, legacy: bool) -> dict:
    cmd=[str(viewer), "--obj", str(ROOT/"examples/assets/pyramid.obj"),
         "--scene", str(scene), "--benchmark-frames", str(frames)]
    if legacy:
        cmd.append("--no-instancing")
    result=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,check=True)
    for line in result.stdout.splitlines():
        if line.startswith("BENCHMARK_JSON="):
            return json.loads(line.partition("=")[2])
    raise RuntimeError("missing benchmark output: "+result.stderr)

def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer",type=Path,
                        default=ROOT/"build-gl/terraflux-terrain-viewer")
    parser.add_argument("--scene",type=Path,
                        default=ROOT/"examples/scenes/benchmark_culling.json")
    parser.add_argument("--frames",type=int,default=120)
    args=parser.parse_args()
    if args.frames<8 or args.frames>2000:
        parser.error("--frames must be in 8..2000")
    viewer=args.viewer.resolve()
    scene=args.scene.resolve()
    legacy=benchmark(viewer,scene,args.frames,True)
    instanced=benchmark(viewer,scene,args.frames,False)
    print(f"Same {args.frames}-frame workload, 8 warmup frames, VSync off:")
    print("Metric                     Legacy      Instanced       Change")
    for key in ("cpu_render_mean_ms","gpu_draw_mean_ms","draws_per_frame"):
        old,new=legacy[key],instanced[key]
        change=(new/old-1)*100 if old else 0
        print(f"{key:<26}{old:10.3f}{new:14.3f}{change:+12.1f}%")
    print("Visible objects/frame:",instanced["visible_per_frame"])
    print("GPU samples:",legacy["gpu_samples"],"/",instanced["gpu_samples"])
    print("Repeat on real GPU hardware; Mesa software CI is functionality testing.")

if __name__=="__main__":
    main()
