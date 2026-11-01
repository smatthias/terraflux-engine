"""Weather extension (v3) of the spread kernel: temperature, humidity and rain.

The key guarantees: an absent or neutral weather block reproduces v2 bit-for-bit, v1/v2 requests
ignore a weather block, heat dries and rain wets the fuel within bounded weights, and a per-step
series keeps its last sample.
"""
import unittest

from pydantic import ValidationError

from app.contracts import CONTRACT_VERSION_V1, CONTRACT_VERSION_V2, CONTRACT_VERSION_V3, SpreadRequest, WeatherSample
from app.spread_kernel import HEAT_WEIGHT, RAIN_WEIGHT, simulate, spread_score, weather_for_step, weather_term

MARGINAL = dict(fuel_load=0.5, fuel_moisture=0.5, burn_susceptibility=0.5)  # legacy score 0.425
DECENT = dict(fuel_load=0.6, fuel_moisture=0.4, burn_susceptibility=0.6)  # legacy score 0.51
GOOD = dict(fuel_load=0.8, fuel_moisture=0.2, burn_susceptibility=0.8)  # legacy score 0.68


def line(kind: dict, n: int = 4, weather=None, **overrides) -> SpreadRequest:
    cells = [{"x": x, "y": 0, **kind} for x in range(n)]
    return SpreadRequest(grid_key="weather", cells=cells, ignition={"x": 0, "y": 0}, steps=8, weather=weather, **overrides)


def burned(request: SpreadRequest) -> set[tuple[int, int]]:
    return set(simulate(request).frames[-1].burned_cells)


class WeatherTermTest(unittest.TestCase):
    def test_absent_and_neutral_samples_are_exactly_zero(self):
        self.assertEqual(weather_term(None), 0.0)
        self.assertEqual(weather_term(WeatherSample()), 0.0)
        self.assertEqual(weather_term(WeatherSample(temperature_celsius=20, relative_humidity=0.5, precipitation_mm_h=0)), 0.0)

    def test_heat_is_bounded_and_scaled_by_dryness(self):
        self.assertAlmostEqual(weather_term(WeatherSample(temperature_celsius=40, relative_humidity=0)), HEAT_WEIGHT)
        self.assertAlmostEqual(weather_term(WeatherSample(temperature_celsius=60, relative_humidity=0)), HEAT_WEIGHT)
        self.assertAlmostEqual(weather_term(WeatherSample(temperature_celsius=40, relative_humidity=1)), HEAT_WEIGHT / 2)
        self.assertAlmostEqual(weather_term(WeatherSample(temperature_celsius=0, relative_humidity=0)), -HEAT_WEIGHT)
        self.assertAlmostEqual(weather_term(WeatherSample(temperature_celsius=30, relative_humidity=0.5)), 0.5 * 0.75 * HEAT_WEIGHT)

    def test_rain_saturates_at_ten_millimetres(self):
        self.assertAlmostEqual(weather_term(WeatherSample(precipitation_mm_h=1)), -0.1 * RAIN_WEIGHT)
        self.assertAlmostEqual(weather_term(WeatherSample(precipitation_mm_h=10)), -RAIN_WEIGHT)
        self.assertAlmostEqual(weather_term(WeatherSample(precipitation_mm_h=200)), -RAIN_WEIGHT)
        self.assertAlmostEqual(weather_term(WeatherSample(temperature_celsius=40, relative_humidity=0, precipitation_mm_h=10)),
                               HEAT_WEIGHT - RAIN_WEIGHT)

    def test_score_appends_weather_last(self):
        request = line(DECENT, weather=WeatherSample(precipitation_mm_h=10))
        plain = spread_score(request, request.cells[1], 1, 0)
        wet = spread_score(request, request.cells[1], 1, 0, weather=request.weather)
        self.assertAlmostEqual(plain, 0.51)
        self.assertAlmostEqual(wet, 0.51 - RAIN_WEIGHT)

    def test_validation_bounds(self):
        for bad in ({"temperature_celsius": 61}, {"temperature_celsius": -61}, {"relative_humidity": 1.1},
                    {"relative_humidity": -0.1}, {"precipitation_mm_h": -1}, {"precipitation_mm_h": 201}):
            with self.subTest(bad=bad), self.assertRaises(ValidationError):
                WeatherSample(**bad)
        with self.assertRaises(ValidationError):
            line(GOOD, weather=[WeatherSample()] * 721)


class WeatherSimulationTest(unittest.TestCase):
    def test_absent_and_neutral_weather_reproduce_v2(self):
        v2 = simulate(line(DECENT, contract_version=CONTRACT_VERSION_V2)).model_dump()
        absent = simulate(line(DECENT)).model_dump()
        neutral = simulate(line(DECENT, weather=WeatherSample())).model_dump()
        for result in (absent, neutral):
            result["contract_version"] = CONTRACT_VERSION_V2
            self.assertEqual(result, v2)

    def test_hot_dry_afternoon_carries_marginal_fuel(self):
        self.assertEqual(burned(line(MARGINAL)), {(0, 0)})
        self.assertEqual(burned(line(MARGINAL, weather=WeatherSample(temperature_celsius=40, relative_humidity=0.1))),
                         {(0, 0), (1, 0), (2, 0), (3, 0)})
        self.assertEqual(burned(line(MARGINAL, weather=WeatherSample(temperature_celsius=40, relative_humidity=1.0))), {(0, 0)})

    def test_cold_air_holds_decent_fuel(self):
        self.assertEqual(burned(line(DECENT)), {(0, 0), (1, 0), (2, 0), (3, 0)})
        self.assertEqual(burned(line(DECENT, weather=WeatherSample(temperature_celsius=0))), {(0, 0)})

    def test_rain_cools_the_fire(self):
        self.assertEqual(burned(line(GOOD, weather=WeatherSample(precipitation_mm_h=1))), {(0, 0), (1, 0), (2, 0), (3, 0)})
        self.assertEqual(burned(line(GOOD, weather=WeatherSample(precipitation_mm_h=10))), {(0, 0)})
        # A gale on good fuel still creeps through a downpour: rain is strong, not absolute.
        self.assertEqual(burned(line(GOOD, weather=WeatherSample(precipitation_mm_h=10), wind_speed_kmh=80, wind_direction_degrees=270)),
                         {(0, 0), (1, 0), (2, 0), (3, 0)})

    def test_series_applies_per_step_and_persists(self):
        dry = WeatherSample(temperature_celsius=25, relative_humidity=0.4)
        wet = WeatherSample(temperature_celsius=18, relative_humidity=0.9, precipitation_mm_h=12)
        request = line(GOOD, n=8, weather=[dry, dry, dry, wet])
        self.assertIs(weather_for_step(request, 0), request.weather[0])
        self.assertIs(weather_for_step(request, 3), wet)
        self.assertIs(weather_for_step(request, 7), wet)
        self.assertEqual(burned(request), {(0, 0), (1, 0), (2, 0), (3, 0)})
        self.assertEqual(burned(line(GOOD, n=8, weather=[wet])), {(0, 0)})
        self.assertIsNone(weather_for_step(line(GOOD, weather=[]), 0))

    def test_weather_is_ignored_under_earlier_contracts(self):
        for version in (CONTRACT_VERSION_V1, CONTRACT_VERSION_V2):
            with self.subTest(version=version):
                request = line(GOOD, weather=WeatherSample(precipitation_mm_h=10), contract_version=version)
                self.assertIsNone(weather_for_step(request, 0))
                self.assertEqual(burned(request), {(0, 0), (1, 0), (2, 0), (3, 0)})

    def test_response_echoes_v3(self):
        self.assertEqual(simulate(line(GOOD, weather=WeatherSample())).contract_version, CONTRACT_VERSION_V3)


if __name__ == "__main__":
    unittest.main()
