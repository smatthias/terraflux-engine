"""Deterministic reference implementation. Native execution must preserve this contract."""
from math import atan2, cos, pi
from .contracts import SpreadFrame, SpreadRequest, SpreadResponse


def _clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def simulate(request: SpreadRequest, engine: str = "python-reference") -> SpreadResponse:
    by_key = {(cell.x, cell.y): cell for cell in request.cells}
    ignition = (request.ignition.x, request.ignition.y)
    if ignition not in by_key:
        raise ValueError("ignition cell is not present in cells")

    burning = {ignition}
    burned = set(burning)
    frames: list[SpreadFrame] = []
    neighbours = ((-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1))

    for step in range(request.steps):
        next_burning: set[tuple[int, int]] = set()
        for x, y in burning:
            for dx, dy in neighbours:
                target = (x + dx, y + dy)
                if target in burned or target not in by_key:
                    continue
                cell = by_key[target]
                bearing = (atan2(dx, dy) * 180 / pi + 360) % 360
                alignment = (cos((bearing - request.wind_direction_degrees) * pi / 180) + 1) / 2
                score = _clamp(
                    cell.fuel_load * 0.35
                    + (1 - cell.fuel_moisture) * 0.25
                    + cell.burn_susceptibility * 0.25
                    + _clamp(request.wind_speed_kmh / 80, 0, 1) * alignment * 0.15,
                    0,
                    1,
                )
                if score >= request.spread_threshold:
                    next_burning.add(target)
                    burned.add(target)
        burning = next_burning
        frames.append(SpreadFrame(
            step=step, active_cells=sorted(burning), burned_cells=sorted(burned),
            active_count=len(burning), burned_count=len(burned),
        ))
        if not burning:
            break

    return SpreadResponse(
        engine=engine, grid_key=request.grid_key, frames=frames,
        metrics={"burned_cells": len(burned), "completed_steps": len(frames)},
    )
