"""Terrain (slope) extension of the spread kernel.

The key guarantee under test: when elevation is missing - on every cell, on some cells, or
present but flat - the result is *identical* to the pre-terrain contract.
"""
import unittest
from math import sqrt

from pydantic import ValidationError

from app.contracts import CONTRACT_VERSION, CONTRACT_VERSION_V1, CONTRACT_VERSION_V2, SpreadRequest
from app.spread_kernel import (SLOPE_DOWNHILL_WEIGHT, SLOPE_SATURATION_DEGREES, SLOPE_UPHILL_WEIGHT, WIND_HEADWIND_WEIGHT,
                               WIND_WEIGHT, WIND_WEIGHT_V1, simulate, slope_degrees, slope_term, wind_term)

MARGINAL = dict(fuel_load=0.5, fuel_moisture=0.5, burn_susceptibility=0.5)  # legacy score 0.425
DECENT = dict(fuel_load=0.6, fuel_moisture=0.4, burn_susceptibility=0.6)  # legacy score 0.51


def line(kind: dict, elevations: list[float | None], **overrides) -> SpreadRequest:
    cells = []
    for x, z in enumerate(elevations):
        cell = {"x": x, "y": 0, **kind}
        if z is not None:
            cell["elevation_meters"] = z
        cells.append(cell)
    return SpreadRequest(grid_key="terrain", cells=cells, ignition={"x": 0, "y": 0}, steps=8, **overrides)


def burned(request: SpreadRequest) -> set[tuple[int, int]]:
    return set(simulate(request).frames[-1].burned_cells)


class SlopeHelperTest(unittest.TestCase):
    def test_missing_elevation_on_either_side_yields_none(self):
        self.assertIsNone(slope_degrees(10.0, None, 1, 0, 30.0))
        self.assertIsNone(slope_degrees(None, 10.0, 1, 0, 30.0))
        self.assertEqual(slope_term(None), 0.0)

    def test_sign_convention_positive_is_uphill(self):
        self.assertAlmostEqual(slope_degrees(0.0, 30.0, 1, 0, 30.0), 45.0)
        self.assertAlmostEqual(slope_degrees(30.0, 0.0, -1, 0, 30.0), -45.0)

    def test_flat_is_exactly_zero(self):
        self.assertEqual(slope_degrees(250.0, 250.0, 1, 1, 30.0), 0.0)
        self.assertEqual(slope_term(0.0), 0.0)

    def test_diagonal_neighbours_use_sqrt2_run(self):
        orthogonal = slope_degrees(0.0, 30.0, 1, 0, 30.0)
        diagonal = slope_degrees(0.0, 30.0, 1, 1, 30.0)
        self.assertAlmostEqual(orthogonal, 45.0)
        self.assertLess(diagonal, orthogonal)
        self.assertAlmostEqual(diagonal, slope_degrees(0.0, 30.0, 1, 0, 30.0 * sqrt(2)))

    def test_cell_size_scales_angle(self):
        self.assertGreater(slope_degrees(0.0, 10.0, 1, 0, 10.0), slope_degrees(0.0, 10.0, 1, 0, 30.0))

    def test_uphill_term_is_linear_and_saturates(self):
        half = SLOPE_SATURATION_DEGREES / 2
        self.assertAlmostEqual(slope_term(half), SLOPE_UPHILL_WEIGHT / 2)
        self.assertEqual(slope_term(SLOPE_SATURATION_DEGREES), SLOPE_UPHILL_WEIGHT)
        self.assertEqual(slope_term(SLOPE_SATURATION_DEGREES * 3), SLOPE_UPHILL_WEIGHT)

    def test_downhill_penalty_is_weaker_than_uphill_bonus(self):
        self.assertLess(SLOPE_DOWNHILL_WEIGHT, SLOPE_UPHILL_WEIGHT)
        self.assertAlmostEqual(slope_term(-SLOPE_SATURATION_DEGREES), -SLOPE_DOWNHILL_WEIGHT)
        self.assertAlmostEqual(slope_term(-SLOPE_SATURATION_DEGREES * 3), -SLOPE_DOWNHILL_WEIGHT)
        self.assertLess(abs(slope_term(-20.0)), slope_term(20.0))

    def test_tail_drive_relieves_downhill_penalty_only(self):
        steep_down, steep_up = -SLOPE_SATURATION_DEGREES, SLOPE_SATURATION_DEGREES
        self.assertAlmostEqual(slope_term(steep_down, tail_drive=0.0), -SLOPE_DOWNHILL_WEIGHT)
        self.assertAlmostEqual(slope_term(steep_down, tail_drive=0.5), -SLOPE_DOWNHILL_WEIGHT / 2)
        self.assertEqual(slope_term(steep_down, tail_drive=1.0), 0.0)
        for drive in (0.0, 0.5, 1.0):  # wind never touches the uphill bonus
            self.assertEqual(slope_term(steep_up, tail_drive=drive), SLOPE_UPHILL_WEIGHT)

    def test_wind_outweighs_slope_at_saturation(self):
        self.assertGreater(WIND_WEIGHT, SLOPE_UPHILL_WEIGHT)
        self.assertGreater(WIND_WEIGHT, WIND_WEIGHT_V1)
        self.assertEqual(WIND_WEIGHT_V1, 0.15)  # frozen: v1 requests must replay bit-for-bit


class WindTermTest(unittest.TestCase):
    """v2 wind is the signed projection of the wind onto the spread path."""

    def test_tail_head_and_cross(self):
        self.assertEqual(wind_term(1.0, 1.0), WIND_WEIGHT)
        self.assertEqual(wind_term(1.0, -1.0), -WIND_HEADWIND_WEIGHT)
        self.assertEqual(wind_term(1.0, 0.0), 0.0)
        self.assertEqual(wind_term(0.0, 1.0), 0.0)

    def test_headwind_penalty_is_modest_relative_to_tailwind_bonus(self):
        self.assertLess(WIND_HEADWIND_WEIGHT, WIND_WEIGHT)
        self.assertGreater(WIND_HEADWIND_WEIGHT, 0)

    def test_projection_scales_with_cosine_and_speed(self):
        from math import cos, radians
        self.assertAlmostEqual(wind_term(1.0, cos(radians(45))), cos(radians(45)) * WIND_WEIGHT)
        self.assertAlmostEqual(wind_term(0.5, 1.0), WIND_WEIGHT / 2)
        self.assertAlmostEqual(wind_term(0.5, -1.0), -WIND_HEADWIND_WEIGHT / 2)


class LegacyBehaviourPreservedTest(unittest.TestCase):
    """Old logic must be reproduced exactly whenever terrain cannot be evaluated."""

    def test_absent_none_and_flat_elevation_are_identical(self):
        cells = [dict(x=0, y=0, **DECENT), dict(x=1, y=0, **MARGINAL), dict(x=1, y=1, **DECENT), dict(x=2, y=1, **DECENT)]
        base = dict(grid_key="legacy", ignition={"x": 0, "y": 0}, steps=6, wind_speed_kmh=40, wind_direction_degrees=315)  # v2: from NW, pushes SE
        absent = SpreadRequest(cells=cells, **base)
        explicit_none = SpreadRequest(cells=[{**c, "elevation_meters": None} for c in cells], **base)
        flat = SpreadRequest(cells=[{**c, "elevation_meters": 812.5} for c in cells], **base)
        flat_other_cell_size = SpreadRequest(cells=[{**c, "elevation_meters": 812.5} for c in cells], cell_size_meters=1.0, **base)

        expected = simulate(absent).model_dump()
        self.assertEqual(simulate(explicit_none).model_dump(), expected)
        # Flat elevation gives identical frames; only the coverage metric (rightly) differs.
        for request in (flat, flat_other_cell_size):
            result = simulate(request).model_dump()
            self.assertEqual(result["frames"], expected["frames"])
            self.assertEqual(result["metrics"]["burned_cells"], expected["metrics"]["burned_cells"])
            self.assertEqual(result["metrics"]["cells_with_elevation"], 4)
            self.assertEqual(result["metrics"]["cells_with_filled_elevation"], 0)
        self.assertEqual(expected["metrics"]["burned_cells"], 4)
        self.assertEqual(expected["metrics"]["cells_with_elevation"], 0)

    def test_cell_size_default_is_thirty_meters(self):
        self.assertEqual(SpreadRequest.model_fields["cell_size_meters"].default, 30.0)

    def test_legacy_request_json_without_new_fields_still_validates(self):
        raw = {"contract_version": "terraflux.compute/v1", "grid_key": "g",
               "cells": [{"x": 0, "y": 0, "fuel_load": 1, "fuel_moisture": 0, "burn_susceptibility": 1}],
               "ignition": {"x": 0, "y": 0}}
        request = SpreadRequest.model_validate(raw)
        self.assertEqual(request.cell_size_meters, 30.0)
        self.assertIsNone(request.cells[0].elevation_meters)


class TerrainSpreadTest(unittest.TestCase):
    def test_flat_marginal_fuel_does_not_spread(self):
        self.assertEqual(burned(line(MARGINAL, [0.0, 0.0, 0.0])), {(0, 0)})

    def test_uphill_promotes_marginal_fuel(self):
        self.assertEqual(burned(line(MARGINAL, [0.0, 11.0, 22.0, 33.0])), {(0, 0), (1, 0), (2, 0), (3, 0)})

    def test_downhill_blocks_decent_fuel(self):
        self.assertEqual(burned(line(DECENT, [0.0, 0.0, 0.0])), {(0, 0), (1, 0), (2, 0)})  # flat control
        self.assertEqual(burned(line(DECENT, [33.0, 22.0, 11.0])), {(0, 0)})

    def test_calm_gentle_downhill_spreads_but_steep_does_not(self):
        # 0.535 base: -9.5 deg costs 0.0237 (0.511, spreads); -20 deg costs 0.0503 (0.485, blocked).
        good = dict(fuel_load=0.6, fuel_moisture=0.4, burn_susceptibility=0.7)
        self.assertEqual(burned(line(good, [22.0, 17.0, 6.0])), {(0, 0), (1, 0)})

    def test_tailwind_carries_fire_down_a_steep_slope(self):
        descent = [33.0, 22.0, 11.0, 0.0]
        self.assertEqual(burned(line(DECENT, descent)), {(0, 0)})  # calm: blocked
        tailwind = dict(wind_speed_kmh=80, wind_direction_degrees=270)  # west wind pushes +x
        self.assertEqual(burned(line(DECENT, descent, **tailwind)), {(0, 0), (1, 0), (2, 0), (3, 0)})
        headwind = dict(wind_speed_kmh=80, wind_direction_degrees=90)  # east wind opposes +x
        self.assertEqual(burned(line(DECENT, descent, **headwind)), {(0, 0)})  # no relief against the wind

    def test_downhill_is_not_the_mirror_of_uphill(self):
        # Same |angle| (saturated), same fuel: 0.615 + 0.15 climbs, 0.615 - 0.075 = 0.54 still descends.
        rich = dict(fuel_load=0.9, fuel_moisture=0.4, burn_susceptibility=0.6)
        self.assertEqual(burned(line(rich, [0.0, 18.0, 36.0])), {(0, 0), (1, 0), (2, 0)})
        self.assertEqual(burned(line(rich, [36.0, 18.0, 0.0])), {(0, 0), (1, 0), (2, 0)})
        # ... whereas a symmetric penalty would have blocked the descent.
        self.assertLess(0.615 - SLOPE_UPHILL_WEIGHT, 0.5)
        self.assertGreaterEqual(0.615 - SLOPE_DOWNHILL_WEIGHT, 0.5)

    def test_headwind_penalises_in_v2_but_not_in_v1(self):
        strong = dict(fuel_load=0.8, fuel_moisture=0.4, burn_susceptibility=0.6)  # 0.58
        head = dict(wind_speed_kmh=80, wind_direction_degrees=90)  # v2: east wind opposes fire moving +x
        self.assertEqual(burned(line(strong, [None] * 3)), {(0, 0), (1, 0), (2, 0)})                 # calm
        self.assertEqual(burned(line(strong, [None] * 3, **head)), {(0, 0)})                          # 0.48
        head_v1 = dict(wind_speed_kmh=80, wind_direction_degrees=270)  # v1: angle is where the wind blows TOWARD
        self.assertEqual(burned(line(strong, [None] * 3, contract_version=CONTRACT_VERSION_V1, **head_v1)), {(0, 0), (1, 0), (2, 0)})

    def test_crosswind_contributes_nothing_along_the_spread_path(self):
        cross = dict(wind_speed_kmh=80, wind_direction_degrees=0)
        self.assertEqual(burned(line(MARGINAL, [None] * 3, **cross)), {(0, 0)})                       # flat: 0.425
        self.assertEqual(burned(line(MARGINAL, [0.0, 11.0, 22.0], **cross)), {(0, 0), (1, 0), (2, 0)})  # uphill bonus intact
        self.assertEqual(burned(line(DECENT, [22.0, 11.0, 0.0], **cross)), {(0, 0)})                  # no downhill relief

    def test_headwind_versus_uphill_is_a_tug_of_war(self):
        head = dict(wind_speed_kmh=80, wind_direction_degrees=90)  # east wind opposes fire moving +x
        self.assertEqual(burned(line(DECENT, [0.0, 18.0, 36.0], **head)), {(0, 0), (1, 0), (2, 0)})   # 31 deg: 0.56
        self.assertEqual(burned(line(DECENT, [0.0, 5.0, 10.0], **head)), {(0, 0)})                    # 9.5 deg: 0.457

    def test_wind_on_flat_ground_is_a_stronger_driver_in_v2(self):
        flat = [0.0, 0.0, 0.0]
        tail_v2 = dict(wind_speed_kmh=32, wind_direction_degrees=270)  # from the west, pushes +x; drive 0.4
        tail_v1 = dict(wind_speed_kmh=32, wind_direction_degrees=90)   # v1 legacy: blows toward +x
        self.assertEqual(burned(line(MARGINAL, flat, **tail_v2)), {(0, 0), (1, 0), (2, 0)})           # 0.425 + 0.10
        self.assertEqual(burned(line(MARGINAL, flat, contract_version=CONTRACT_VERSION_V1, **tail_v1)), {(0, 0)})  # 0.425 + 0.06

    def test_partial_elevation_is_filled_from_neighbours(self):
        # x=2 is estimated as 22 (linear between 11 and 33): the climb continues through it.
        self.assertEqual(burned(line(MARGINAL, [0.0, 11.0, None, 33.0])), {(0, 0), (1, 0), (2, 0), (3, 0)})
        # A void on the DEM edge takes the nearest known value (no extrapolation), so the first
        # step is flat and marginal fuel does not get its uphill bonus there.
        self.assertEqual(burned(line(MARGINAL, [None, 11.0, 22.0])), {(0, 0)})
        # With nothing known at all there is nothing to fill: legacy flat behaviour.
        self.assertEqual(burned(line(MARGINAL, [None, None, None])), {(0, 0)})

    def test_diagonal_slope_is_gentler_than_orthogonal(self):
        cells = [dict(x=0, y=0, elevation_meters=0.0, **MARGINAL),
                 dict(x=1, y=0, elevation_meters=11.0, **MARGINAL),
                 dict(x=1, y=1, elevation_meters=11.0, **MARGINAL)]
        request = SpreadRequest(grid_key="diag", cells=cells, ignition={"x": 0, "y": 0}, steps=3)
        self.assertEqual(burned(request), {(0, 0), (1, 0)})
        steeper = request.model_copy(update={"cell_size_meters": 10.0})
        self.assertEqual(burned(steeper), {(0, 0), (1, 0), (1, 1)})

    def test_no_fuel_cell_never_burns_regardless_of_slope(self):
        bare = dict(fuel_load=0.0, fuel_moisture=1.0, burn_susceptibility=0.0)
        cells = [dict(x=0, y=0, elevation_meters=0.0, **DECENT), dict(x=1, y=0, elevation_meters=1000.0, **bare)]
        request = SpreadRequest(grid_key="bare", cells=cells, ignition={"x": 0, "y": 0}, steps=2)
        self.assertEqual(burned(request), {(0, 0)})

    def test_result_is_deterministic_with_terrain(self):
        request = line(DECENT, [5.0, 9.0, 2.0, 30.0, 31.0], wind_speed_kmh=20, wind_direction_degrees=270)
        self.assertEqual(simulate(request).model_dump(), simulate(request).model_dump())


class CellSizeValidationTest(unittest.TestCase):
    def test_rejects_non_positive_and_absurd_cell_sizes(self):
        for bad in (0, -5, 10_001):
            with self.subTest(cell_size=bad), self.assertRaises(ValidationError):
                line(DECENT, [0.0, 1.0], cell_size_meters=bad)

    def test_accepts_boundary(self):
        line(DECENT, [0.0, 1.0], cell_size_meters=10_000)
        line(DECENT, [0.0, 1.0], cell_size_meters=0.5)


if __name__ == "__main__":
    unittest.main()


class ContractVersionTest(unittest.TestCase):
    """Terrain is a v2 feature. v1 requests must replay exactly as before, elevation or not."""

    RIDGE = [0.0, 11.0, 22.0, 33.0]

    def test_new_requests_default_to_current_contract(self):
        request = line(MARGINAL, self.RIDGE)
        self.assertEqual(request.contract_version, CONTRACT_VERSION)
        self.assertEqual(request.contract_version, CONTRACT_VERSION_V2)

    def test_v1_ignores_elevation_and_matches_elevation_free_request(self):
        v1_with_elevation = line(MARGINAL, self.RIDGE, contract_version=CONTRACT_VERSION_V1)
        v1_without = line(MARGINAL, [None] * len(self.RIDGE), contract_version=CONTRACT_VERSION_V1)
        self.assertEqual(burned(v1_with_elevation), {(0, 0)})
        self.assertEqual(simulate(v1_with_elevation).model_dump(), simulate(v1_without).model_dump())

    def test_v2_applies_terrain_to_the_same_request(self):
        v2 = line(MARGINAL, self.RIDGE, contract_version=CONTRACT_VERSION_V2)
        self.assertEqual(burned(v2), {(0, 0), (1, 0), (2, 0), (3, 0)})

    def test_v1_ignores_cell_size_too(self):
        steep = line(MARGINAL, self.RIDGE, contract_version=CONTRACT_VERSION_V1, cell_size_meters=1.0)
        self.assertEqual(burned(steep), {(0, 0)})

    def test_response_echoes_request_version(self):
        for version in (CONTRACT_VERSION_V1, CONTRACT_VERSION_V2):
            with self.subTest(version=version):
                response = simulate(line(MARGINAL, self.RIDGE, contract_version=version))
                self.assertEqual(response.contract_version, version)

    def test_unknown_version_is_rejected(self):
        for bad in ("terraflux.compute/v3", "terraflux.compute/v0", "v2", ""):
            with self.subTest(version=bad), self.assertRaises(ValidationError):
                line(MARGINAL, self.RIDGE, contract_version=bad)


class WindDirectionConventionTest(unittest.TestCase):
    """Regression: wind_direction_degrees is meteorological under v2 (where the wind comes FROM).

    Grid frame: bearing 0 = +y (north), 90 = +x (east), 180 = -y, 270 = -x. On a plus-shaped grid
    with an 80 km/h wind only the downwind arm burns (v2: tail 0.675, cross 0.425, head 0.325).
    """

    ARMS = ((0, 1), (1, 0), (0, -1), (-1, 0))
    LEGACY = dict(fuel_load=0.4, fuel_moisture=0.5, burn_susceptibility=0.6)  # 0.415: v1 tail .565, cross .49, head .415

    def plus(self, kind):
        return [dict(x=0, y=0, **kind)] + [dict(x=dx * i, y=dy * i, **kind) for dx, dy in self.ARMS for i in (1, 2)]

    def run_wind(self, version, kind, wind_from):
        request = SpreadRequest(contract_version=version, grid_key="wind", cells=self.plus(kind),
                                ignition={"x": 0, "y": 0}, steps=4, wind_speed_kmh=80, wind_direction_degrees=wind_from)
        return burned(request) - {(0, 0)}

    def test_v2_wind_from_cardinal_directions_pushes_fire_the_opposite_way(self):
        expected = {0: (0, -1), 90: (-1, 0), 180: (0, 1), 270: (1, 0)}  # north wind -> south, east wind -> west, ...
        for wind_from, (ax, ay) in expected.items():
            with self.subTest(wind_from=wind_from):
                self.assertEqual(self.run_wind(CONTRACT_VERSION_V2, MARGINAL, wind_from), {(ax, ay), (2 * ax, 2 * ay)})

    def test_v2_wind_from_intercardinal_directions(self):
        cells = [dict(x=0, y=0, **MARGINAL)] + [dict(x=dx, y=dy, **MARGINAL) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy]
        for wind_from, target in {45: (-1, -1), 135: (-1, 1), 225: (1, 1), 315: (1, -1)}.items():
            with self.subTest(wind_from=wind_from):
                request = SpreadRequest(grid_key="w", cells=cells, ignition={"x": 0, "y": 0}, steps=2,
                                        wind_speed_kmh=80, wind_direction_degrees=wind_from)
                # Diagonal downwind cell gets the full tail wind (0.675); its two orthogonal
                # neighbours get cos(45) of it (0.602); everything else is cross or head wind.
                downwind = burned(request) - {(0, 0)}
                self.assertIn(target, downwind)
                self.assertEqual(len(downwind), 3)
                self.assertNotIn((-target[0], -target[1]), downwind)

    def test_v1_legacy_angle_is_where_the_wind_blows_toward(self):
        for angle, (ax, ay) in {0: (0, 1), 90: (1, 0), 180: (0, -1), 270: (-1, 0)}.items():
            with self.subTest(angle=angle):
                self.assertEqual(self.run_wind(CONTRACT_VERSION_V1, self.LEGACY, angle), {(ax, ay), (2 * ax, 2 * ay)})

    def test_same_angle_reverses_between_v1_and_v2(self):
        self.assertEqual(self.run_wind(CONTRACT_VERSION_V2, MARGINAL, 270), {(1, 0), (2, 0)})
        self.assertEqual(self.run_wind(CONTRACT_VERSION_V1, self.LEGACY, 270), {(-1, 0), (-2, 0)})

    def test_meteorological_angle_wraps(self):
        self.assertEqual(self.run_wind(CONTRACT_VERSION_V2, MARGINAL, 359.999), self.run_wind(CONTRACT_VERSION_V2, MARGINAL, 0))
