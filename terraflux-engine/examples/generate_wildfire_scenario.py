#!/usr/bin/env python3
"""Generate a reproducible v2 wildfire scenario for the native OpenGL viewer.

Usage:
  python3 examples/generate_wildfire_scenario.py > /tmp/terraflux-request.json
  ./build-gl/terraflux-compute-runner < /tmp/terraflux-request.json > /tmp/terraflux-response.json
  ./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json --response /tmp/terraflux-response.json
"""
import json
import math

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

print(json.dumps({
    "contract_version": "terraflux.compute/v2",
    "grid_key": "opengl-wildfire-playback-demo",
    "cell_size_meters": 30,
    "cells": cells,
    "ignition": {"x": 16, "y": 30},
    "steps": 44,
    "wind_speed_kmh": 30,
    "wind_direction_degrees": 270,
    "spread_threshold": 0.58
}, separators=(",", ":")))
