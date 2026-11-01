#include <terraflux/graphics/vitals.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace terraflux::graphics;

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template <class F> void rejects(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const std::invalid_argument&) { caught = true; }
  require(caught, message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) < tolerance; }
void run(Vitals& v, const Exposure& e, double seconds, const VitalsStyle& s) { for (double t = 0; t < seconds; t += 1.0 / 60.0) advance_vitals(v, e, 1.0 / 60.0, s); }

void test_vitals() {
  VitalsStyle style;
  validate_vitals_style(style);
  rejects([] { VitalsStyle s; s.lethal_impact_mps = 3; validate_vitals_style(s); }, "a lethal impact below the safe one is rejected");
  rejects([] { VitalsStyle s; s.crouched_shelter = 1; validate_vitals_style(s); }, "full shelter is rejected");
  Vitals v;
  require(v.health == 1 && v.heat == 1 && v.wetness == 0 && v.alive, "fresh");
  // A mild day: nothing happens.
  Exposure mild;
  run(v, mild, 60, style);
  require(v.health == 1 && v.heat == 1 && v.wetness == 0, "a mild day costs nothing");
  // Lightning: struck at once, dangerous less; crouching makes a strike count as farther away.
  Exposure strike; strike.lightning = LightningHazard::Struck;
  advance_vitals(v, strike, 1.0 / 60.0, style);
  require(near(v.health, 1 - style.struck_damage) && v.last_cause == DamageCause::Lightning, "struck by lightning");
  strike.lightning = LightningHazard::Dangerous;
  advance_vitals(v, strike, 1.0 / 60.0, style);
  require(near(v.health, 1 - style.struck_damage - style.dangerous_damage), "a near strike hurts less");
  require(hazard_for(12, false, style) == LightningHazard::Struck && hazard_for(12, true, style) == LightningHazard::Dangerous &&
          hazard_for(40, true, style) == LightningHazard::Close, "crouched, the same strike is a lesser hazard");
  // Fire: heat burns above the threshold, warms below it; smoke chokes slowly.
  reset_vitals(v); v.heat = 0.5;
  Exposure warm; warm.fire_heat = 0.2; warm.temperature_celsius = 5;
  run(v, warm, 10, style);
  require(v.health == 1 && v.heat > 0.5, "a fire at a distance warms without burning");
  Exposure burn; burn.fire_heat = 1.0;
  run(v, burn, 5, style);
  require(near(v.health, 1 - style.burn_per_second * 5, 0.02) && v.last_cause == DamageCause::Fire, "in the flames the health burns away");
  reset_vitals(v);
  Exposure smoke; smoke.smoke = 1.0;
  run(v, smoke, 20, style);
  require(near(v.health, 1 - style.smoke_per_second * 20, 0.02) && v.last_cause == DamageCause::Smoke, "smoke chokes");
  // Falls: safe below the safe impact, lethal at the lethal one.
  reset_vitals(v);
  Exposure fall; fall.fall_impact_mps = 5;
  advance_vitals(v, fall, 1.0 / 60.0, style);
  require(v.health == 1, "a short drop is safe");
  fall.fall_impact_mps = 13;
  advance_vitals(v, fall, 1.0 / 60.0, style);
  require(near(v.health, 0.5) && v.last_cause == DamageCause::Fall, "a fall halfway to lethal costs half the health");
  fall.fall_impact_mps = 25;
  const auto report = advance_vitals(v, fall, 1.0 / 60.0, style);
  require(!v.alive && v.health == 0 && report.collapsed, "a lethal fall collapses the walker");
  require(advance_vitals(v, mild, 1.0, style).collapsed && v.health == 0, "collapsed stays collapsed");
  // Storm debris, drowning.
  reset_vitals(v);
  Exposure gale; gale.wind_mps = 42;
  run(v, gale, 10, style);
  require(v.health < 1 && v.last_cause == DamageCause::Wind, "flying debris in a hurricane wind hurts");
  reset_vitals(v);
  Exposure drowning; drowning.swimming = true; drowning.exhausted = true;
  run(v, drowning, 5, style);
  require(near(v.health, 1 - style.drowning_per_second * 5, 0.02) && v.last_cause == DamageCause::Drowning && v.wetness == 1, "swimming without strength is drowning");
  // Cold: wind chill and wetness speed the loss; below hypothermia the health goes; warmth restores.
  reset_vitals(v);
  Exposure cold; cold.temperature_celsius = 0;
  run(v, cold, 60, style);
  const double calm_dry = 1 - v.heat;
  reset_vitals(v);
  Exposure chill = cold; chill.wind_mps = 15;
  run(v, chill, 60, style);
  const double windy = 1 - v.heat;
  reset_vitals(v); v.wetness = 1;
  Exposure wet = cold; wet.rain_mm_h = 10;
  run(v, wet, 60, style);
  const double soaked = 1 - v.heat;
  require(calm_dry > 0.2 && windy > calm_dry * 1.4 && soaked > calm_dry * 2, "wind and wet speed the cold");
  reset_vitals(v);
  Exposure icy_water = cold; icy_water.water_depth_meters = 1.0;
  run(v, icy_water, 30, style);
  require(v.heat < 0.3 && v.health < 1 && v.last_cause == DamageCause::Cold, "half a minute in icy water brings hypothermia");
  Exposure sun; sun.temperature_celsius = 28;
  v.wetness = 0; const double cold_heat = v.heat;
  run(v, sun, 30, style);
  require(v.heat > cold_heat + 0.2, "a warm day restores the body heat");
  // Wetness: rain soaks over its time, warm air dries.
  reset_vitals(v);
  Exposure rain; rain.rain_mm_h = 25; rain.temperature_celsius = 20;
  run(v, rain, style.soak_seconds, style);
  require(near(v.wetness, 1.0, 0.05), "a storm's rain soaks in its time");
  run(v, sun, style.dry_seconds, style);
  require(v.wetness < 0.2, "warm dry air dries the clothes");
  rejects([&] { advance_vitals(v, mild, -1, style); }, "negative time rejected");
  Exposure bad; bad.fire_heat = 2;
  rejects([&] { advance_vitals(v, bad, 1, style); }, "exposure out of range rejected");
  require(std::string(damage_cause_name(DamageCause::Cold)) == "cold", "cause names");
}
}  // namespace

int main() {
  try { test_vitals(); }
  catch (const std::exception& error) { std::cerr << "vitals tests failed: " << error.what() << '\n'; return 1; }
  std::cout << "vitals tests passed\n";
  return 0;
}
