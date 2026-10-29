"""Extreme and boundary conditions: 0/80/150/300 km/h winds, slopes beyond 30 degrees, and their
interaction. Pins both the intended saturation behaviour and the defects found while probing:
zero-fuel cells igniting, non-finite elevations, and validation gaps between the two engines.
"""
import json
import os
import subprocess
import unittest
from pathlib import Path

from pydantic import ValidationError

from app.contracts import CONTRACT_VERSION_V1, CONTRACT_VERSION_V2, GridCell, SpreadRequest
from app.spread_kernel import (SLOPE_DOWNHILL_WEIGHT, SLOPE_UPHILL_WEIGHT, WIND_HEADWIND_WEIGHT, WIND_WEIGHT, simulate,
                               slope_term, spread_score, wind_term)

WINDS = (0, 80, 150, 300)                 # 300 is the contract maximum
STEEP = (30, 31, 45, 60, 75, 89.9)        # degrees; saturation starts at 30
TAIL, HEAD, CROSS = 270, 90, 0            # v2 meteorological: fire moves +x (bearing 90)

MARGINAL = dict(fuel_load=0.5, fuel_moisture=0.5, burn_susceptibility=0.5)  # 0.425
DECENT = dict(fuel_load=0.6, fuel_moisture=0.4, burn_susceptibility=0.6)    # 0.51
STRONG = dict(fuel_load=0.8, fuel_moisture=0.4, burn_susceptibility=0.6)    # 0.58
PERFECT = dict(fuel_load=1.0, fuel_moisture=0.0, burn_susceptibility=1.0)   # 0.85
NO_FUEL = dict(fuel_load=0.0, fuel_moisture=0.6, burn_susceptibility=0.0)   # 0.10 without any fuel


def line(kind, elevations, **overrides) -> SpreadRequest:
    cells = []
    for x, z in enumerate(elevations):
        cell = {"x": x, "y": 0, **kind}
        if z is not None:
            cell["elevation_meters"] = z
        cells.append(cell)
    return SpreadRequest(grid_key="extreme", cells=cells, ignition={"x": 0, "y": 0}, steps=8, **overrides)


def burned(request: SpreadRequest) -> set[tuple[int, int]]:
    return set(simulate(request).frames[-1].burned_cells)


def rise_for(angle_degrees: float, run: float = 30.0) -> float:
    from math import radians, tan
    return run * tan(radians(angle_degrees))


def score(kind, rise, wind_speed, wind_from, version=CONTRACT_VERSION_V2, **overrides) -> float:
    """Score for fire moving +x from a cell at z=0 to an adjacent cell at z=rise."""
    request = SpreadRequest(contract_version=version, grid_key="s", cells=[dict(x=0, y=0, elevation_meters=0.0, **MARGINAL)],
                            ignition={"x": 0, "y": 0}, wind_speed_kmh=wind_speed, wind_direction_degrees=wind_from, **overrides)
    target = GridCell(x=1, y=0, elevation_meters=rise, **kind)
    return spread_score(request, target, 1, 0, 0.0, rise)


class ExtremeWindTest(unittest.TestCase):
    def test_wind_term_saturates_at_80_kmh(self):
        """150 and 300 km/h are modelled identically to 80 km/h. Documented limitation."""
        for wind in (80, 150, 300):
            with self.subTest(wind=wind):
                self.assertEqual(wind_term(min(wind / 80, 1), 1.0), WIND_WEIGHT)
                self.assertEqual(wind_term(min(wind / 80, 1), -1.0), -WIND_HEADWIND_WEIGHT)

    def test_80_150_300_produce_identical_simulations(self):
        for direction in (TAIL, HEAD, CROSS, 135):
            reference = simulate(line(MARGINAL, [0.0, 11.0, 22.0, 5.0], wind_speed_kmh=80, wind_direction_degrees=direction)).model_dump()
            for wind in (150, 300):
                with self.subTest(wind=wind, direction=direction):
                    got = simulate(line(MARGINAL, [0.0, 11.0, 22.0, 5.0], wind_speed_kmh=wind, wind_direction_degrees=direction)).model_dump()
                    self.assertEqual(got, reference)

    def test_zero_wind_is_direction_independent(self):
        reference = simulate(line(DECENT, [0.0, 11.0, 0.0], wind_speed_kmh=0, wind_direction_degrees=0)).model_dump()
        for direction in (45, 90, 180, 270, 359.9):
            with self.subTest(direction=direction):
                self.assertEqual(simulate(line(DECENT, [0.0, 11.0, 0.0], wind_speed_kmh=0, wind_direction_degrees=direction)).model_dump(), reference)

    def test_cardinal_winds_at_every_speed_on_flat_ground(self):
        expect = {  # marginal fuel 0.425: tail needs +0.075 (>= 24 km/h); head and cross never help
            0: {TAIL: False, HEAD: False, CROSS: False},
            80: {TAIL: True, HEAD: False, CROSS: False},
            150: {TAIL: True, HEAD: False, CROSS: False},
            300: {TAIL: True, HEAD: False, CROSS: False},
        }
        for wind, by_direction in expect.items():
            for direction, spreads in by_direction.items():
                with self.subTest(wind=wind, direction=direction):
                    got = burned(line(MARGINAL, [None, None], wind_speed_kmh=wind, wind_direction_degrees=direction))
                    self.assertEqual((1, 0) in got, spreads)

    def test_max_headwind_blocks_strong_but_not_perfect_fuel(self):
        self.assertEqual(burned(line(STRONG, [None, None], wind_speed_kmh=300, wind_direction_degrees=HEAD)), {(0, 0)})       # 0.48
        self.assertEqual(burned(line(PERFECT, [None, None], wind_speed_kmh=300, wind_direction_degrees=HEAD)), {(0, 0), (1, 0)})  # 0.75

    def test_score_stays_within_unit_interval_at_extreme_wind(self):
        for wind in WINDS:
            for direction in (TAIL, HEAD, CROSS, 45, 225):
                for kind in (NO_FUEL, MARGINAL, PERFECT):
                    with self.subTest(wind=wind, direction=direction, kind=kind):
                        value = score(kind, 0.0, wind, direction)
                        self.assertGreaterEqual(value, 0.0)
                        self.assertLessEqual(value, 1.0)

    def test_wind_above_contract_maximum_or_negative_is_rejected(self):
        for bad in (300.0001, 301, 1000, -1, -0.001):
            with self.subTest(wind=bad), self.assertRaises(ValidationError):
                line(MARGINAL, [None, None], wind_speed_kmh=bad)
        line(MARGINAL, [None, None], wind_speed_kmh=300)  # boundary accepted
        line(MARGINAL, [None, None], wind_speed_kmh=0)


class VerySteepSlopeTest(unittest.TestCase):
    def test_uphill_and_downhill_saturate_beyond_30_degrees(self):
        for angle in STEEP:
            with self.subTest(angle=angle):
                self.assertEqual(slope_term(angle), SLOPE_UPHILL_WEIGHT)
                self.assertEqual(slope_term(-angle), -SLOPE_DOWNHILL_WEIGHT)

    def test_cliff_and_tiny_cells_saturate_rather_than_blow_up(self):
        for rise, cell_size in ((5_000.0, 30.0), (1e9, 30.0), (0.6, 0.5), (100.0, 1e-3)):
            with self.subTest(rise=rise, cell_size=cell_size):
                up = score(MARGINAL, rise, 0, 0, cell_size_meters=cell_size)
                down = score(MARGINAL, -rise, 0, 0, cell_size_meters=cell_size)
                self.assertAlmostEqual(up, 0.425 + SLOPE_UPHILL_WEIGHT)
                self.assertAlmostEqual(down, 0.425 - SLOPE_DOWNHILL_WEIGHT)

    def test_steep_slopes_give_same_outcome_as_30_degrees(self):
        reference_up = burned(line(MARGINAL, [0.0, rise_for(30), 2 * rise_for(30)]))
        reference_down = burned(line(DECENT, [2 * rise_for(30), rise_for(30), 0.0]))
        self.assertEqual(reference_up, {(0, 0), (1, 0), (2, 0)})
        self.assertEqual(reference_down, {(0, 0)})
        for angle in STEEP[1:]:
            with self.subTest(angle=angle):
                r = rise_for(angle)
                self.assertEqual(burned(line(MARGINAL, [0.0, r, 2 * r])), reference_up)
                self.assertEqual(burned(line(DECENT, [2 * r, r, 0.0])), reference_down)

    def test_non_finite_elevation_is_rejected(self):
        for bad in (float("inf"), float("-inf"), float("nan")):
            with self.subTest(value=bad), self.assertRaises(ValidationError):
                line(MARGINAL, [0.0, bad])
        line(MARGINAL, [0.0, 1e300])  # finite, if absurd, is the caller's business


class WindSlopeInteractionTest(unittest.TestCase):
    def test_gale_up_a_cliff_is_the_worst_case_and_still_bounded(self):
        value = score(PERFECT, rise_for(60), 300, TAIL)
        self.assertEqual(value, 1.0)  # 0.85 + 0.25 + 0.15 clamps to 1
        self.assertEqual(score(MARGINAL, rise_for(60), 300, TAIL), 0.425 + WIND_WEIGHT + SLOPE_UPHILL_WEIGHT)

    def test_no_fuel_never_ignites_even_in_a_gale_up_a_cliff(self):
        """Regression: the additive score reached 0.50 here with fuel_load = 0 (0.10 + 0.25 + 0.15)."""
        cells = [dict(x=0, y=0, elevation_meters=0.0, **PERFECT), dict(x=1, y=0, elevation_meters=rise_for(60), **NO_FUEL)]
        request = SpreadRequest(grid_key="nofuel", cells=cells, ignition={"x": 0, "y": 0}, steps=2, wind_speed_kmh=300, wind_direction_degrees=TAIL)
        self.assertEqual(burned(request), {(0, 0)})
        lenient = request.model_copy(update={"spread_threshold": 0.0})
        self.assertEqual(burned(lenient), {(0, 0)})  # not even with threshold 0
        # v1 is frozen and keeps the quirk (documented, not endorsed): no terrain term, so the
        # zero-fuel cell scores 0.10 + 0.15 = 0.25 - below 0.5, but a lenient threshold lets it burn.
        v1 = request.model_copy(update={"contract_version": CONTRACT_VERSION_V1, "wind_direction_degrees": 90})
        self.assertEqual(burned(v1), {(0, 0)})
        self.assertEqual(burned(v1.model_copy(update={"spread_threshold": 0.2})), {(0, 0), (1, 0)})

    def test_ignition_on_a_cell_without_fuel_does_not_take(self):
        """The zero-fuel rule applies to the ignition itself: bare rock cannot seed a fire."""
        rock = dict(fuel_load=0.0, fuel_moisture=1.0, burn_susceptibility=0.0)
        cells = [dict(x=0, y=0, elevation_meters=0.0, **rock)] + [dict(x=x, y=y, elevation_meters=30.0, **PERFECT)
                                                                  for x in (-1, 0, 1) for y in (-1, 0, 1) if x or y]
        request = SpreadRequest(grid_key="rock", cells=cells, ignition={"x": 0, "y": 0}, steps=5, wind_speed_kmh=300, wind_direction_degrees=TAIL)
        response = simulate(request)
        self.assertEqual(response.metrics, {"burned_cells": 0, "completed_steps": 1, "cells_with_elevation": 9, "cells_with_filled_elevation": 0})
        self.assertEqual(len(response.frames), 1)
        self.assertEqual(response.frames[0].burned_cells, [])
        self.assertEqual(response.frames[0].active_cells, [])
        self.assertEqual(burned(request.model_copy(update={"spread_threshold": 0.0})), set())  # not even with threshold 0
        # v1 is frozen: it seeds the ignition unconditionally and the rock "burns" its neighbours.
        v1 = request.model_copy(update={"contract_version": CONTRACT_VERSION_V1, "wind_direction_degrees": 90})
        self.assertEqual(simulate(v1).metrics["burned_cells"], 9)
        # A tiny amount of fuel is enough to seed it under v2 (the rule is fuel_load == 0, not "little").
        trace = SpreadRequest(grid_key="trace", cells=[{**cells[0], "fuel_load": 1e-9}] + cells[1:], ignition={"x": 0, "y": 0}, steps=5)
        self.assertEqual(simulate(trace).metrics["burned_cells"], 9)

    def test_gale_headwind_down_a_cliff_cannot_block_perfect_fuel(self):
        """Environment alone never extinguishes maximal fuel: 0.85 - 0.10 - 0.075 = 0.675."""
        self.assertAlmostEqual(score(PERFECT, -rise_for(60), 300, HEAD), 0.85 - WIND_HEADWIND_WEIGHT - SLOPE_DOWNHILL_WEIGHT)
        self.assertEqual(burned(line(PERFECT, [2 * rise_for(60), rise_for(60), 0.0], wind_speed_kmh=300, wind_direction_degrees=HEAD)), {(0, 0), (1, 0), (2, 0)})
        self.assertEqual(burned(line(DECENT, [2 * rise_for(60), rise_for(60), 0.0], wind_speed_kmh=300, wind_direction_degrees=HEAD)), {(0, 0)})

    def test_gale_tailwind_down_a_cliff_equals_gale_on_flat(self):
        for wind in (80, 150, 300):
            with self.subTest(wind=wind):
                self.assertEqual(score(MARGINAL, -rise_for(75), wind, TAIL), score(MARGINAL, 0.0, wind, TAIL))

    def test_gale_crosswind_on_a_cliff_equals_calm_on_the_cliff(self):
        for angle in (45, -45, 75, -75):
            with self.subTest(angle=angle):
                self.assertAlmostEqual(score(MARGINAL, rise_for(angle), 300, CROSS), score(MARGINAL, rise_for(angle), 0, 0), places=12)

    def test_steep_uphill_beats_any_headwind_and_gentle_uphill_loses(self):
        for wind in (80, 150, 300):
            with self.subTest(wind=wind):
                self.assertEqual(burned(line(DECENT, [0.0, rise_for(45), 2 * rise_for(45)], wind_speed_kmh=wind, wind_direction_degrees=HEAD)), {(0, 0), (1, 0), (2, 0)})  # 0.56
                self.assertEqual(burned(line(DECENT, [0.0, rise_for(8), 2 * rise_for(8)], wind_speed_kmh=wind, wind_direction_degrees=HEAD)), {(0, 0)})              # 0.45

    def test_monotone_in_wind_speed(self):
        """More tail wind never shrinks the fire; more head wind never grows it."""
        graded = [dict(x=i, y=0, fuel_load=0.2 + 0.1 * i, fuel_moisture=0.5, burn_susceptibility=0.5, elevation_meters=float(7 * i)) for i in range(8)]
        def run(wind, direction):
            return burned(SpreadRequest(grid_key="m", cells=graded, ignition={"x": 0, "y": 0}, steps=10, wind_speed_kmh=wind, wind_direction_degrees=direction))
        previous_tail, previous_head = set(), run(0, HEAD)
        for wind in (0, 24, 40, 64, 80, 150, 300):
            with self.subTest(wind=wind):
                tail, head = run(wind, TAIL), run(wind, HEAD)
                self.assertTrue(tail >= previous_tail)
                self.assertTrue(head <= previous_head)
                previous_tail, previous_head = tail, head

    def test_threshold_one_requires_a_saturated_score(self):
        self.assertEqual(burned(line(PERFECT, [0.0, rise_for(45)], wind_speed_kmh=300, wind_direction_degrees=TAIL, spread_threshold=1.0)), {(0, 0), (1, 0)})
        self.assertEqual(burned(line(PERFECT, [0.0, rise_for(45)], wind_speed_kmh=0, spread_threshold=1.0)), {(0, 0), (1, 0)})  # 0.85 + 0.15
        self.assertEqual(burned(line(PERFECT, [0.0, rise_for(20)], wind_speed_kmh=0, spread_threshold=1.0)), {(0, 0)})         # 0.95


RUNNER = Path(os.environ.get("TERRAFLUX_NATIVE_RUNNER", Path(__file__).resolve().parents[2] / "build" / "terraflux-compute-runner"))


@unittest.skipUnless(RUNNER.exists(), f"native runner not built at {RUNNER}")
class NativeValidationParityTest(unittest.TestCase):
    """The C++ runner must reject exactly what Pydantic rejects, so raw-JSON clients see the same contract."""

    def raw(self, **overrides) -> dict:
        base = {"contract_version": CONTRACT_VERSION_V2, "grid_key": "g",
                "cells": [{"x": 0, "y": 0, "elevation_meters": 0.0, **MARGINAL}, {"x": 1, "y": 0, "elevation_meters": 0.0, **MARGINAL}],
                "ignition": {"x": 0, "y": 0}, "steps": 2, "wind_speed_kmh": 0, "wind_direction_degrees": 0, "spread_threshold": 0.5}
        base.update(overrides)
        return base

    def native(self, payload: str) -> subprocess.CompletedProcess:
        return subprocess.run([str(RUNNER)], input=payload, text=True, capture_output=True, timeout=15)

    def test_native_rejects_out_of_range_wind_like_python(self):
        for overrides in (dict(wind_speed_kmh=301), dict(wind_speed_kmh=-50), dict(wind_direction_degrees=360), dict(wind_direction_degrees=-90)):
            with self.subTest(**overrides):
                with self.assertRaises(ValidationError):
                    SpreadRequest.model_validate(self.raw(**overrides))
                completed = self.native(json.dumps(self.raw(**overrides)))
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertIn("invalid simulation parameters", completed.stderr)

    def test_native_accepts_contract_maximum_wind(self):
        for overrides in (dict(wind_speed_kmh=300), dict(wind_speed_kmh=300, wind_direction_degrees=359.999)):
            with self.subTest(**overrides):
                SpreadRequest.model_validate(self.raw(**overrides))
                self.assertEqual(self.native(json.dumps(self.raw(**overrides))).returncode, 0)

    def test_native_rejects_non_finite_numbers(self):
        template = json.dumps(self.raw()).replace('"elevation_meters": 0.0, "fuel_load": 0.5', '"elevation_meters": ELEV, "fuel_load": 0.5', 1)
        self.assertIn("ELEV", template)
        for literal in ("inf", "-inf", "1e999", "nan", "Infinity"):
            with self.subTest(literal=literal):
                completed = self.native(template.replace("ELEV", literal))
                self.assertEqual(completed.returncode, 2, f"{literal!r} was accepted: {completed.stdout[:80]}")

    def test_native_ignition_without_fuel_does_not_take(self):
        rock = dict(fuel_load=0.0, fuel_moisture=1.0, burn_susceptibility=0.0)
        payload = self.raw(cells=[{"x": 0, "y": 0, **rock}, {"x": 1, "y": 0, **PERFECT}], wind_speed_kmh=300, wind_direction_degrees=TAIL)
        result = json.loads(self.native(json.dumps(payload)).stdout)
        self.assertEqual(result["metrics"], {"burned_cells": 0, "completed_steps": 1, "cells_with_elevation": 0, "cells_with_filled_elevation": 0})
        self.assertEqual(result["frames"], [{"step": 0, "active_cells": [], "burned_cells": [], "active_count": 0, "burned_count": 0}])

    def test_native_enforces_the_cell_count_cap_like_python(self):
        from app.contracts import SpreadRequest as R
        cap = next(m.max_length for m in R.model_fields["cells"].metadata if hasattr(m, "max_length"))
        self.assertEqual(cap, 1_000_000)
        # Build the oversized payload with string joins (Pydantic would validate a million items before
        # reporting too_long, which is slow; the contract bound itself is asserted above).
        cell = '{"x":%d,"y":0,"fuel_load":1,"fuel_moisture":0,"burn_susceptibility":1}'
        payload = ('{"contract_version":"terraflux.compute/v2","grid_key":"g","cells":[' + ",".join(cell % i for i in range(cap + 1))
                   + '],"ignition":{"x":0,"y":0},"steps":1}')
        completed = self.native(payload)
        self.assertEqual(completed.returncode, 2)
        self.assertIn("too many cells", completed.stderr)
        self.assertEqual(self.native(json.dumps(self.raw(cells=[], steps=1))).returncode, 2)

    def test_native_applies_zero_fuel_guard(self):
        payload = self.raw(cells=[{"x": 0, "y": 0, "elevation_meters": 0.0, **PERFECT}, {"x": 1, "y": 0, "elevation_meters": rise_for(60), **NO_FUEL}],
                           wind_speed_kmh=300, wind_direction_degrees=TAIL)
        result = json.loads(self.native(json.dumps(payload)).stdout)
        self.assertEqual(result["metrics"]["burned_cells"], 1)


if __name__ == "__main__":
    unittest.main()
