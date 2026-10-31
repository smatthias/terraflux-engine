"""Mixed known/unknown elevation (v2).

Voids are filled in-engine from the nearest known cells along the 8 compass rays, inverse-distance
weighted (GDAL FillNodata style). Before this, a void was evaluated flat on both the step into it
and the step out of it, which acted as a barrier on climbs and a bridge on descents; filling
removes those artifacts for any void within reach of known terrain. What cannot be filled
(nothing known at all, voids cut off by grid holes, or further than the search limit) stays
flat, and both counts are reported in the metrics.
"""
import unittest

from app.contracts import CONTRACT_VERSION_V1, SpreadRequest
from app.spread_kernel import ELEVATION_FILL_MAX_SEARCH_CELLS, _fill_by_line_sweep, _fill_by_ray_cast, fill_elevation_voids, simulate

MARGINAL = dict(fuel_load=0.5, fuel_moisture=0.5, burn_susceptibility=0.5)  # 0.425: spreads only with uphill help
DECENT = dict(fuel_load=0.6, fuel_moisture=0.4, burn_susceptibility=0.6)    # 0.51: blocked by any downhill penalty


def line(kind, elevations, **overrides):
    cells = [{"x": x, "y": 0, **kind, **({"elevation_meters": z} if z is not None else {})} for x, z in enumerate(elevations)]
    return SpreadRequest(**{"grid_key": "partial", "cells": cells, "ignition": {"x": 0, "y": 0}, "steps": 8, **overrides})


def burned_x(request) -> list[int]:
    return sorted(x for x, _ in simulate(request).frames[-1].burned_cells)


def row(values):
    return {(x, 0): z for x, z in enumerate(values)}


class FillFunctionTest(unittest.TestCase):
    def test_one_dimensional_gaps_are_linearly_interpolated(self):
        self.assertEqual(fill_elevation_voids(row([0.0, 11.0, None, 33.0, 44.0])), {(2, 0): 22.0})
        self.assertEqual(fill_elevation_voids(row([0.0, 11.0, None, None, 44.0])), {(2, 0): 22.0, (3, 0): 33.0})
        six = fill_elevation_voids(row([0.0, None, None, None, None, None, 60.0]))
        for x in range(1, 6):
            self.assertAlmostEqual(six[(x, 0)], 10.0 * x)

    def test_isolated_void_on_a_plane_is_exact(self):
        plane = {(x, y): 10.0 * x + 5.0 * y for x in range(5) for y in range(5)}
        plane[(2, 2)] = None
        self.assertAlmostEqual(fill_elevation_voids(plane)[(2, 2)], 30.0)

    def test_water_body_takes_the_shore_level(self):
        lake = {(x, y): (None if 1 <= x <= 3 and 1 <= y <= 3 else 100.0) for x in range(5) for y in range(5)}
        filled = fill_elevation_voids(lake)
        self.assertEqual(len(filled), 9)
        for value in filled.values():
            self.assertAlmostEqual(value, 100.0)

    def test_edge_voids_take_the_nearest_value_without_extrapolating(self):
        self.assertEqual(fill_elevation_voids(row([None, None, 22.0, 33.0])), {(0, 0): 22.0, (1, 0): 22.0})

    def test_nothing_known_means_nothing_filled(self):
        self.assertEqual(fill_elevation_voids(row([None, None, None])), {})

    def test_rays_stop_at_grid_holes(self):
        cells = {(0, 0): 0.0, (1, 0): None, (3, 0): 90.0}  # x=2 is not in the grid
        self.assertEqual(fill_elevation_voids(cells), {(1, 0): 0.0})

    def test_search_limit(self):
        cells = {(x, 0): None for x in range(ELEVATION_FILL_MAX_SEARCH_CELLS + 8)}
        cells[(0, 0)] = 100.0
        filled = fill_elevation_voids(cells)
        self.assertIn((ELEVATION_FILL_MAX_SEARCH_CELLS, 0), filled)
        self.assertNotIn((ELEVATION_FILL_MAX_SEARCH_CELLS + 1, 0), filled)

    def test_fill_is_independent_of_input_order(self):
        forward = row([0.0, None, None, 30.0, None, 50.0])
        backward = dict(reversed(list(forward.items())))
        self.assertEqual(fill_elevation_voids(forward), fill_elevation_voids(backward))

    def test_known_cells_are_never_modified(self):
        self.assertNotIn((1, 0), fill_elevation_voids(row([0.0, 11.0, None])))


class FillAlgorithmEquivalenceTest(unittest.TestCase):
    """The O(cells) line sweep and the per-void ray cast must agree bit for bit."""

    @staticmethod
    def both(elev, blockers=frozenset()):
        xs = [x for x, _ in elev]; ys = [y for _, y in elev]
        return (_fill_by_ray_cast(elev, blockers),
                _fill_by_line_sweep(elev, blockers, min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1))

    def test_random_grids_with_holes_blockers_and_clusters(self):
        import random
        rng = random.Random(12345)
        for _ in range(300):
            w, h = rng.randint(1, 12), rng.randint(1, 12); cov = rng.choice([0.05, 0.3, 0.8, 1.0])
            elev, blockers = {}, set()
            for x in range(w):
                for y in range(h):
                    if rng.random() < 0.1:
                        continue
                    key = (x - 5, y + 7)
                    if rng.random() < 0.15:
                        blockers.add(key)
                    elev[key] = round(rng.uniform(0, 500), 2) if rng.random() < cov else None
            if elev:
                a, b = self.both(elev, blockers)
                self.assertEqual(a, b)

    def test_sparse_layout_uses_ray_cast_and_matches(self):
        elev = {(x * 40, y * 40): (float(x + y) if (x + y) % 2 else None) for x in range(4) for y in range(4)}
        a, b = self.both(elev)
        self.assertEqual(fill_elevation_voids(elev), a)   # sparse -> ray cast path
        self.assertEqual(a, b)

    def test_long_chains_beyond_the_old_cap(self):
        elev = {(x, 0): None for x in range(200)}; elev[(0, 0)] = 0.0; elev[(199, 0)] = 199.0
        filled = fill_elevation_voids(elev)
        self.assertEqual(len(filled), 198)
        for x in (1, 50, 100, 150, 198):
            self.assertAlmostEqual(filled[(x, 0)], float(x))   # exact linear interpolation across the whole gap

    def test_large_contiguous_hole_is_recovered(self):
        plane = lambda x, y: 100.0 + 2.0 * x - 1.0 * y
        elev = {(x, y): (None if 20 <= x < 80 and 20 <= y < 80 else plane(x, y)) for x in range(100) for y in range(100)}
        filled = fill_elevation_voids(elev)
        self.assertEqual(len(filled), 3600)
        worst = max(abs(v - plane(*k)) for k, v in filled.items())
        self.assertLess(worst, 1e-6)   # a plane is recovered exactly, however big the hole


class FilledVoidsInSimulationTest(unittest.TestCase):
    def test_void_on_a_climb_is_no_longer_a_barrier(self):
        self.assertEqual(burned_x(line(MARGINAL, [0, 11, 22, 33, 44])), [0, 1, 2, 3, 4])
        self.assertEqual(burned_x(line(MARGINAL, [0, 11, None, 33, 44])), [0, 1, 2, 3, 4])

    def test_void_on_a_descent_is_no_longer_a_bridge(self):
        self.assertEqual(burned_x(line(DECENT, [44, 33, 22, 11, 0])), [0])
        self.assertEqual(burned_x(line(DECENT, [44, None, 22, 11, 0])), [0])
        self.assertEqual(burned_x(line(DECENT, [44, None, 22, None, 0])), [0])

    def test_two_dimensional_routing_is_closed(self):
        def grid(void):
            cells = []
            for y in range(3):
                for x in range(3):
                    cell = dict(x=x, y=y, **DECENT)
                    if (x, y) != void:
                        cell["elevation_meters"] = 40.0 - 11.0 * y  # ridge at y=0, 20 deg drop per row
                    cells.append(cell)
            return set(simulate(SpreadRequest(grid_key="ridge", cells=cells, ignition={"x": 1, "y": 0}, steps=6)).frames[-1].burned_cells)
        self.assertEqual(grid(None), {(0, 0), (1, 0), (2, 0)})
        self.assertEqual(grid((2, 1)), {(0, 0), (1, 0), (2, 0)})

    def test_filled_result_matches_full_coverage_on_a_plane(self):
        full = [dict(x=x, y=y, elevation_meters=10.0 * x + 5.0 * y, **DECENT) for x in range(5) for y in range(5)]
        holes = [dict(c) for c in full]
        for index in (6, 12, 13, 18):
            del holes[index]["elevation_meters"]
        run = lambda cells: simulate(SpreadRequest(grid_key="p", cells=cells, ignition={"x": 2, "y": 2}, steps=6)).frames
        self.assertEqual(run(holes), run(full))

    def test_void_on_the_ignition_is_filled_too(self):
        cells = [dict(x=0, y=0, **MARGINAL)] + [dict(x=dx, y=dy, elevation_meters=11.0 if dx == 1 else -11.0 if dx == -1 else 0.0, **MARGINAL)
                                                 for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy]
        # The ignition is estimated from its ring (mean ~0): east is uphill (+20 deg), west downhill.
        burned = set(simulate(SpreadRequest(grid_key="ign", cells=cells, ignition={"x": 0, "y": 0}, steps=3)).frames[-1].burned_cells)
        self.assertIn((1, 0), burned)
        self.assertNotIn((-1, 0), burned)

    def test_unfillable_voids_fall_back_to_flat(self):
        self.assertEqual(burned_x(line(MARGINAL, [None, None, None])), [0])              # nothing known
        self.assertEqual(burned_x(line(DECENT, [None] * 5)), [0, 1, 2, 3, 4])
        far = [0.0] + [None] * (ELEVATION_FILL_MAX_SEARCH_CELLS + 3)                      # beyond the search limit
        request = line(MARGINAL, far, steps=60)
        self.assertEqual(simulate(request).metrics["cells_with_filled_elevation"], ELEVATION_FILL_MAX_SEARCH_CELLS)
        self.assertGreaterEqual(ELEVATION_FILL_MAX_SEARCH_CELLS, 1024)

    def test_wind_still_applies_in_filled_cells(self):
        tail = dict(wind_speed_kmh=80, wind_direction_degrees=270)
        self.assertEqual(burned_x(line(MARGINAL, [0, None, None, 0], **tail)), [0, 1, 2, 3])


class CoverageMetricTest(unittest.TestCase):
    def test_measured_and_filled_counts(self):
        metrics = simulate(line(DECENT, [1.0, None, 3.0, None])).metrics
        self.assertEqual(metrics["cells_with_elevation"], 2)
        self.assertEqual(metrics["cells_with_filled_elevation"], 2)
        metrics = simulate(line(DECENT, [None] * 4)).metrics
        self.assertEqual((metrics["cells_with_elevation"], metrics["cells_with_filled_elevation"]), (0, 0))
        metrics = simulate(line(DECENT, [0.0, 0.0, 0.0, 0.0])).metrics
        self.assertEqual((metrics["cells_with_elevation"], metrics["cells_with_filled_elevation"]), (4, 0))

    def test_exact_metrics_shape(self):
        self.assertEqual(simulate(line(DECENT, [44, 33, 22, 11, 0])).metrics,
                         {"burned_cells": 1, "completed_steps": 1, "cells_with_elevation": 5, "cells_with_filled_elevation": 0})

    def test_v1_neither_fills_nor_reports(self):
        metrics = simulate(line(DECENT, [1.0, None, 3.0], contract_version=CONTRACT_VERSION_V1)).metrics
        self.assertEqual(set(metrics), {"burned_cells", "completed_steps"})


if __name__ == "__main__":
    unittest.main()
