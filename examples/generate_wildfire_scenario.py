#!/usr/bin/env python3
"""Generate a reproducible wildfire scenario for the native OpenGL viewer.

By default a v3 request: a warm, fairly dry afternoon (26 C, 35 % humidity) that lets the fire
run, a thunderstorm arriving at step 30 with 14 mm/h of rain for ten steps (which stops the
front: rain outweighs every other driver), then cool humid air. The viewer mirrors the series
while it replays: clouds build into a cumulonimbus, rain falls, lightning flashes and the flames
shrink as the fire weather turns.

Usage:
  python3 examples/generate_wildfire_scenario.py > /tmp/terraflux-request.json
  python3 examples/generate_wildfire_scenario.py --temperature 38 --humidity 0.12 --no-storm > hot.json
  python3 examples/generate_wildfire_scenario.py --contract v2 > v2.json        # no weather term
  ./build-gl/terraflux-compute-runner < /tmp/terraflux-request.json > /tmp/terraflux-response.json
  ./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json --response /tmp/terraflux-response.json
"""
import argparse
import json
import math

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--contract", choices=("v2", "v3"), default="v3", help="contract version (v2 has no weather term)")
parser.add_argument("--steps", type=int, default=44)
parser.add_argument("--temperature", type=float, default=26.0, help="air temperature before the storm, degrees Celsius")
parser.add_argument("--humidity", type=float, default=0.35, help="relative humidity before the storm, 0..1")
parser.add_argument("--storm-at-step", type=int, default=30, help="first step of the thunderstorm")
parser.add_argument("--storm-steps", type=int, default=10, help="how many steps the downpour lasts")
parser.add_argument("--storm-mm-h", type=float, default=6.0, help="rainfall during the storm, mm/h (14 and more stop the fire outright)")
parser.add_argument("--no-storm", action="store_true", help="constant weather for the whole run")
arguments = parser.parse_args()

SIDE = 60
cells = []
for y in range(SIDE):
    for x in range(SIDE):
        elevation = 75 + 22 * math.sin(x * 0.10) * math.cos(y * 0.11) + 0.10 * x
        is_river = x == 37 and not (27 <= y <= 31)
        cells.append({
            "x": x, "y": y,
            "fuel_load": 0 if is_river else 0.86,
            "fuel_moisture": 0.28,
            "burn_susceptibility": 0.87,
            "elevation_meters": round(elevation, 5)
        })

request = {
    "contract_version": f"terraflux.compute/{arguments.contract}",
    "grid_key": "opengl-wildfire-playback-demo",
    "cell_size_meters": 30,
    "cells": cells,
    "ignition": {"x": 16, "y": 30},
    "steps": arguments.steps,
    "wind_speed_kmh": 30,
    "wind_direction_degrees": 270,
    "spread_threshold": 0.58
}
if arguments.contract == "v3":
    before = {"temperature_celsius": arguments.temperature, "relative_humidity": arguments.humidity, "precipitation_mm_h": 0.0}
    if arguments.no_storm:
        request["weather"] = before
    else:
        # Downdraft and rain cool the air by several degrees and saturate it; the cool humid
        # airmass lingers after the shower (the last sample persists).
        storm = {"temperature_celsius": round(arguments.temperature - 8.0, 2), "relative_humidity": 0.95,
                 "precipitation_mm_h": arguments.storm_mm_h}
        after = {"temperature_celsius": round(arguments.temperature - 6.0, 2), "relative_humidity": 0.85, "precipitation_mm_h": 0.0}
        series = [before] * max(arguments.storm_at_step, 0) + [storm] * max(arguments.storm_steps, 0) + [after]
        request["weather"] = series[:max(arguments.steps, 1)]

print(json.dumps(request, separators=(",", ":")))
