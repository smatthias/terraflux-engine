"""Fire blockers: rivers, bare rock, fuel breaks and grid holes (v2 unless stated).

A blocker is a cell with fuel_load = 0 (or a cell absent from the grid). Under v2 such cells never
ignite, and a diagonal step is not allowed between two blockers ("no corner cutting"), so a
blocker line that is continuous on the ground is continuous in the model whatever its orientation.
There is no spotting/ember model: no wind crosses an unbroken blocker.
"""
import unittest

from app.contracts import CONTRACT_VERSION_V1, SpreadRequest
from app.spread_kernel import simulate

FUEL = dict(fuel_load=0.8, fuel_moisture=0.3, burn_susceptibility=0.8)        # 0.655: spreads unaided
WATER = dict(fuel_load=0.0, fuel_moisture=1.0, burn_susceptibility=0.0)        # river, rock, road
DRY_ROCK = dict(fuel_load=0.0, fuel_moisture=0.0, burn_susceptibility=1.0)     # zero fuel but "dry and susceptible": 0.5 in v1
WEAK_BREAK = dict(fuel_load=0.2, fuel_moisture=0.6, burn_susceptibility=0.4)   # mown strip: 0.27, breached by a tail wind (+0.25)
W, H = 7, 5


def field(blocker, is_blocked, drop=None):
    cells = []
    for x in range(W):
        for y in range(H):
            if drop and drop(x, y):
                continue
            cells.append(dict(x=x, y=y, **(blocker if is_blocked(x, y) else FUEL)))
    return cells


def burned(cells, version="terraflux.compute/v2", ignition=(0, 2), **overrides):
    request = SpreadRequest(contract_version=version, grid_key="blocker", cells=cells, ignition={"x": ignition[0], "y": ignition[1]}, steps=14, **overrides)
    return set(simulate(request).frames[-1].burned_cells)


def crossed(burned_cells, boundary_x=3) -> bool:
    return any(x > boundary_x for x, _ in burned_cells)


class OrthogonalBlockerTest(unittest.TestCase):
    def test_one_cell_river_blocks_even_in_a_gale(self):
        river = field(WATER, lambda x, y: x == 3)
        for wind in (0, 80, 300):
            with self.subTest(wind=wind):
                result = burned(river, wind_speed_kmh=wind, wind_direction_degrees=270)
                self.assertFalse(crossed(result))
                self.assertEqual(result, {(x, y) for x in range(3) for y in range(H)})

    def test_absent_cells_block_like_water(self):
        hole = field(WATER, lambda x, y: False, drop=lambda x, y: x == 3)
        self.assertFalse(crossed(burned(hole, wind_speed_kmh=300, wind_direction_degrees=270)))

    def test_river_cells_never_appear_as_burned(self):
        river = field(WATER, lambda x, y: x == 3)
        self.assertFalse(any(x == 3 for x, _ in burned(river, wind_speed_kmh=300, wind_direction_degrees=270)))

    def test_gap_in_the_river_funnels_the_fire_through(self):
        gapped = field(WATER, lambda x, y: x == 3 and y != 4)
        result = burned(gapped)
        self.assertIn((3, 4), result)
        self.assertTrue(crossed(result))
        self.assertEqual(len(result), W * H - (H - 1))  # everything but the 4 river cells

    def test_v1_quirk_dry_susceptible_rock_burns_but_not_in_v2(self):
        rock = field(DRY_ROCK, lambda x, y: x == 3)
        self.assertTrue(crossed(burned(rock, CONTRACT_VERSION_V1)))
        self.assertFalse(crossed(burned(rock)))


class DiagonalBlockerTest(unittest.TestCase):
    """A diagonal river touches only at corners; without the corner rule the fire slips through."""

    def test_diagonal_river_blocks(self):
        river = field(WATER, lambda x, y: x == y + 1)
        result = burned(river, ignition=(0, 4))
        self.assertEqual(result, {(x, y) for x in range(W) for y in range(H) if x < y + 1})

    def test_diagonal_river_blocks_in_a_gale_from_any_side(self):
        river = field(WATER, lambda x, y: x == y + 1)
        for direction in (0, 90, 180, 270, 45, 225):
            with self.subTest(wind_from=direction):
                self.assertFalse(any(x > y + 1 for x, y in burned(river, ignition=(0, 4), wind_speed_kmh=300, wind_direction_degrees=direction)))

    def test_diagonal_absent_cells_block(self):
        hole = field(WATER, lambda x, y: False, drop=lambda x, y: x == y + 1)
        self.assertFalse(any(x > y + 1 for x, y in burned(hole, ignition=(0, 4))))

    def test_diagonal_step_is_allowed_when_one_corner_is_passable(self):
        # Single blocker at (1,1): the diagonal (0,0)->(1,... wait: (0,0)->(1,1) is the blocker itself;
        # (0,0)->(1,-1)-style steps need corner cells. Use a 2x2 with one corner removed: the
        # diagonal across a *single* blocked corner must still be allowed.
        cells = [dict(x=0, y=0, **FUEL), dict(x=1, y=1, **FUEL), dict(x=1, y=0, **WATER), dict(x=0, y=1, **FUEL)]
        self.assertIn((1, 1), burned(cells, ignition=(0, 0)))
        cells = [dict(x=0, y=0, **FUEL), dict(x=1, y=1, **FUEL), dict(x=1, y=0, **WATER)]  # (0,1) absent, (1,0) water
        self.assertNotIn((1, 1), burned(cells, ignition=(0, 0)))

    def test_corner_rule_is_v2_only(self):
        cells = [dict(x=0, y=0, **FUEL), dict(x=1, y=1, **FUEL), dict(x=1, y=0, **WATER), dict(x=0, y=1, **WATER)]
        self.assertNotIn((1, 1), burned(cells, ignition=(0, 0)))
        self.assertIn((1, 1), burned(cells, CONTRACT_VERSION_V1, ignition=(0, 0)))

    def test_two_cell_wide_diagonal_river_blocks_too(self):
        river = field(WATER, lambda x, y: x in (y + 1, y + 2))
        self.assertFalse(any(x > y + 2 for x, y in burned(river, ignition=(0, 4), wind_speed_kmh=300, wind_direction_degrees=270)))


class FuelBreakTest(unittest.TestCase):
    def test_weak_break_holds_in_calm_and_fails_in_a_tail_wind(self):
        strip = field(WEAK_BREAK, lambda x, y: x == 3)
        self.assertFalse(crossed(burned(strip)))                                                  # 0.27
        self.assertFalse(crossed(burned(strip, wind_speed_kmh=40, wind_direction_degrees=270)))   # 0.395
        self.assertTrue(crossed(burned(strip, wind_speed_kmh=80, wind_direction_degrees=270)))    # 0.52

    def test_weak_break_holds_against_a_cross_or_head_wind(self):
        strip = field(WEAK_BREAK, lambda x, y: x == 3)
        for direction in (0, 180, 90):
            with self.subTest(wind_from=direction):
                self.assertFalse(crossed(burned(strip, wind_speed_kmh=300, wind_direction_degrees=direction)))

    def test_weak_break_on_a_slope_fails_uphill_and_holds_downhill(self):
        uphill = [dict(c, elevation_meters=11.0 * c["x"]) for c in field(WEAK_BREAK, lambda x, y: x == 3)]
        downhill = [dict(c, elevation_meters=-11.0 * c["x"]) for c in field(WEAK_BREAK, lambda x, y: x == 3)]
        self.assertFalse(crossed(burned(uphill)))                                                   # 0.27 + 0.1007 = 0.371
        self.assertTrue(crossed(burned(uphill, wind_speed_kmh=48, wind_direction_degrees=270)))     # 0.27 + 0.15 + 0.1007 = 0.521
        self.assertFalse(crossed(burned(downhill, wind_speed_kmh=48, wind_direction_degrees=270)))  # 0.27 + 0.15 - 0.0503 * 0.4 = 0.400

    def test_wider_break_needs_the_same_conditions_per_cell(self):
        two_wide = field(WEAK_BREAK, lambda x, y: x in (3, 4))
        self.assertFalse(crossed(burned(two_wide, wind_speed_kmh=40, wind_direction_degrees=270), boundary_x=4))
        self.assertTrue(crossed(burned(two_wide, wind_speed_kmh=80, wind_direction_degrees=270), boundary_x=4))


if __name__ == "__main__":
    unittest.main()


class FillNearBlockersTest(unittest.TestCase):
    """Void filling must not invent slopes across rivers, and no slope may carry fire over zero fuel."""

    PLATEAU = dict(fuel_load=0.6, fuel_moisture=0.4, burn_susceptibility=0.6)  # 0.51: any downhill penalty blocks it

    def gorge(self, river, bank_void=True, bank_cells=PLATEAU):
        # x=0..2 plateau at 100 m (x=2 shoreline void), x=3 river, x=4..5 gorge floor at 40 m.
        cells = []
        for x in range(6):
            kind = river if x == 3 else bank_cells
            cell = dict(x=x, y=0, **kind)
            if x < 2 or x > 3:
                cell["elevation_meters"] = 100.0 if x < 3 else 40.0
            elif x == 2 and not bank_void:
                cell["elevation_meters"] = 100.0
            cells.append(cell)
        return cells

    def test_rays_do_not_cross_an_unmeasured_river(self):
        from app.spread_kernel import fill_elevation_voids
        elev = {(x, 0): z for x, z in enumerate([100.0, 100.0, None, None, 40.0, 40.0])}
        self.assertEqual(fill_elevation_voids(elev), {(2, 0): 80.0, (3, 0): 60.0})           # without blocker knowledge: pulled to the far bank
        self.assertEqual(fill_elevation_voids(elev, blockers={(3, 0)}), {(2, 0): 100.0})     # with it: stays on its own bank, river not filled

    def test_measured_blocker_is_still_a_valid_hit(self):
        from app.spread_kernel import fill_elevation_voids
        elev = {(x, 0): z for x, z in enumerate([100.0, 100.0, None, 70.0, 40.0, 40.0])}
        self.assertEqual(fill_elevation_voids(elev, blockers={(3, 0)}), {(2, 0): 85.0})

    def test_shoreline_cell_is_not_penalised_by_the_far_bank(self):
        result = burned(self.gorge(WATER), ignition=(0, 0))
        self.assertEqual(result, {(0, 0), (1, 0), (2, 0)})                                   # flat along the bank, river holds
        self.assertEqual(simulate(SpreadRequest(grid_key="g", cells=self.gorge(WATER), ignition={"x": 0, "y": 0}, steps=6)).metrics["cells_with_filled_elevation"], 1)

    def test_no_filled_slope_can_carry_fire_over_zero_fuel(self):
        """Adversarial: make the river cell the most attractive target imaginable - a 30 deg climb in a gale."""
        cells = [dict(x=0, y=0, elevation_meters=0.0, **FUEL),
                 dict(x=1, y=0, **dict(WATER, fuel_moisture=0.0, burn_susceptibility=1.0)),  # dry, susceptible, no fuel, no DEM
                 dict(x=2, y=0, elevation_meters=60.0, **FUEL)]
        result = burned(cells, ignition=(0, 0), wind_speed_kmh=300, wind_direction_degrees=270)
        self.assertEqual(result, {(0, 0)})
        # Same geometry but the middle cell has a trace of fuel: now the (filled, 30 m) climb plus the gale carries it.
        cells[1] = dict(cells[1], fuel_load=0.01)
        self.assertEqual(burned(cells, ignition=(0, 0), wind_speed_kmh=300, wind_direction_degrees=270), {(0, 0), (1, 0), (2, 0)})

    def test_no_filled_slope_opens_a_diagonal_corner(self):
        # Blockers at (1,0) and (0,1) without DEM; target (1,1) far higher than the source. Still refused.
        cells = [dict(x=0, y=0, elevation_meters=0.0, **FUEL), dict(x=1, y=1, elevation_meters=20.0, **FUEL),
                 dict(x=1, y=0, **WATER), dict(x=0, y=1, **WATER)]
        self.assertEqual(burned(cells, ignition=(0, 0), wind_speed_kmh=300, wind_direction_degrees=225), {(0, 0)})

    def test_gap_in_a_river_is_scored_from_its_own_banks_only(self):
        """The gap cell sits between two fuel banks; the void river cells either side must not feed its estimate."""
        from app.spread_kernel import fill_elevation_voids
        elev = {(1, 0): 100.0, (2, 0): None, (3, 0): 100.0,      # gap row: bank, GAP (void), bank
                (2, 1): None, (2, -1): None,                     # river cells above and below the gap: void blockers
                (1, 1): 100.0, (3, 1): 100.0, (1, -1): 100.0, (3, -1): 100.0}
        filled = fill_elevation_voids(elev, blockers={(2, 1), (2, -1)})
        self.assertEqual(set(filled), {(2, 0)})
        self.assertAlmostEqual(filled[(2, 0)], 100.0)
