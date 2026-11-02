#include <terraflux/graphics/vitals.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::graphics {
namespace {
bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
}  // namespace

const char* damage_cause_name(DamageCause cause) {
  switch (cause) {
    case DamageCause::None: return "none";
    case DamageCause::Lightning: return "lightning";
    case DamageCause::Fire: return "fire";
    case DamageCause::Smoke: return "smoke";
    case DamageCause::Fall: return "fall";
    case DamageCause::Wind: return "wind";
    case DamageCause::Drowning: return "drowning";
    case DamageCause::Cold: return "cold";
    case DamageCause::Crushed: return "crushed";
    case DamageCause::Collapse: return "collapse";
    case DamageCause::Count: break;
  }
  throw std::invalid_argument("unknown damage cause");
}

void validate_vitals_style(const VitalsStyle& s) {
  if (!within(s.struck_damage, 0, 1) || !within(s.dangerous_damage, 0, 1) || !within(s.crouched_shelter, 0, 0.95))
    throw std::invalid_argument("lightning damage must be 0..1 and crouched shelter 0..0.95");
  if (!within(s.burn_per_second, 0, 5) || !within(s.burn_threshold, 0, 1) || !within(s.smoke_per_second, 0, 5))
    throw std::invalid_argument("fire and smoke rates out of range");
  if (!within(s.safe_impact_mps, 0, 100) || !within(s.lethal_impact_mps, s.safe_impact_mps + 0.1, 200))
    throw std::invalid_argument("fall impacts must be 0 <= safe < lethal <= 200 m/s");
  if (!within(s.debris_wind_mps, 0, 100) || !within(s.debris_per_second, 0, 5) || !within(s.drowning_per_second, 0, 5))
    throw std::invalid_argument("wind and drowning rates out of range");
  if (!within(s.comfort_celsius, -50, 60) || !within(s.cold_loss_per_degree_second, 0, 1) || !within(s.wind_chill_per_mps, 0, 1) ||
      !within(s.wet_loss_factor, 1, 20) || !within(s.water_loss_factor, 1, 50) || !within(s.warm_gain_per_second, 0, 1) ||
      !within(s.fire_warm_per_second, 0, 1) || !within(s.hypothermia_heat, 0, 1) || !within(s.hypothermia_per_second, 0, 5))
    throw std::invalid_argument("heat rates out of range");
  if (!within(s.soak_rain_mm_h, 0.1, 500) || !within(s.soak_seconds, 0.1, 3600) || !within(s.dry_seconds, 0.1, 36000) ||
      !within(s.water_soaks_at_depth, 0, 5))
    throw std::invalid_argument("wetness rates out of range");
}

LightningHazard hazard_for(double distance_meters, bool crouched, const VitalsStyle& style) {
  validate_vitals_style(style);
  if (!std::isfinite(distance_meters) || distance_meters < 0) throw std::invalid_argument("strike distance must be finite and non-negative");
  // Crouched, the walker is a smaller target: the strike counts as farther away.
  const double effective = crouched ? distance_meters / (1.0 - style.crouched_shelter) : distance_meters;
  return lightning_effect(effective).hazard;
}

void reset_vitals(Vitals& v) { v = Vitals{}; }

double crush_damage(double trunk_mass_kg) {
  if (!within(trunk_mass_kg, 0, 1e7)) throw std::invalid_argument("trunk mass out of range");
  return clamp01(trunk_mass_kg / 1000.0);
}

VitalsReport advance_vitals(Vitals& v, const Exposure& e, double dt, const VitalsStyle& s) {
  validate_vitals_style(s);
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("vitals step must be finite and non-negative");
  if (!std::isfinite(e.temperature_celsius) || !within(e.wind_mps, 0, 200) || !within(e.rain_mm_h, 0, 1e4) ||
      !within(e.water_depth_meters, 0, 1e4) || !within(e.fire_heat, 0, 1) || !within(e.smoke, 0, 1) || !within(e.fall_impact_mps, 0, 1e3) ||
      !within(e.crushed, 0, 1) || !within(e.collapse, 0, 1))
    throw std::invalid_argument("exposure out of range");
  VitalsReport report;
  if (!v.alive) { report.collapsed = true; return report; }
  double step_worst = 0.0;  // the biggest single loss of this step names the cause
  const auto hurt = [&](DamageCause cause, double amount) {
    if (amount <= 0) return;
    amount = std::min(amount, v.health);
    v.health -= amount;
    v.lost[static_cast<std::size_t>(cause)] += amount;
    report.health_lost += amount;
    if (amount > step_worst) { step_worst = amount; v.last_cause = cause; v.last_loss = amount; }
    if (report.worst == DamageCause::None || v.lost[static_cast<std::size_t>(cause)] >= v.lost[static_cast<std::size_t>(report.worst)]) report.worst = cause;
  };
  // Wetness: rain soaks, water soaks at once, warm dry air dries.
  if (e.water_depth_meters >= s.water_soaks_at_depth || e.swimming) v.wetness = 1.0;
  else if (e.rain_mm_h > 0 && !e.sheltered) v.wetness = clamp01(v.wetness + (e.rain_mm_h / s.soak_rain_mm_h) * dt / s.soak_seconds);
  else {
    const double warmth = std::max(0.3, 1.0 + (e.temperature_celsius - s.comfort_celsius) / 20.0);
    v.wetness = clamp01(v.wetness - warmth * dt / s.dry_seconds);
  }
  // Body heat.
  const double below = s.comfort_celsius - e.temperature_celsius;
  if (below > 0) {
    double loss = s.cold_loss_per_degree_second * below * (1.0 + s.wind_chill_per_mps * e.wind_mps);
    loss *= 1.0 + (s.wet_loss_factor - 1.0) * v.wetness;
    if (e.water_depth_meters >= s.water_soaks_at_depth || e.swimming) loss *= s.water_loss_factor;
    v.heat = clamp01(v.heat - loss * dt);
  } else if (v.wetness < 0.5) {
    v.heat = clamp01(v.heat + s.warm_gain_per_second * (1.0 + (-below) / 10.0) * dt);
  }
  if (e.fire_heat > 0 && e.fire_heat < s.burn_threshold) v.heat = clamp01(v.heat + s.fire_warm_per_second * (e.fire_heat / s.burn_threshold) * dt);
  // Damage.
  if (e.lightning == LightningHazard::Struck) hurt(DamageCause::Lightning, s.struck_damage);
  else if (e.lightning == LightningHazard::Dangerous) hurt(DamageCause::Lightning, s.dangerous_damage);
  if (e.fire_heat >= s.burn_threshold) hurt(DamageCause::Fire, s.burn_per_second * e.fire_heat * dt);
  if (e.smoke > 0) hurt(DamageCause::Smoke, s.smoke_per_second * e.smoke * dt);
  if (e.fall_impact_mps > s.safe_impact_mps)
    hurt(DamageCause::Fall, clamp01((e.fall_impact_mps - s.safe_impact_mps) / (s.lethal_impact_mps - s.safe_impact_mps)));
  if (e.wind_mps > s.debris_wind_mps) hurt(DamageCause::Wind, s.debris_per_second * (e.wind_mps - s.debris_wind_mps) / 10.0 * dt);
  if (e.swimming && e.exhausted) hurt(DamageCause::Drowning, s.drowning_per_second * dt);
  if (e.crushed > 0) hurt(DamageCause::Crushed, e.crushed);
  if (e.collapse > 0) hurt(DamageCause::Collapse, e.collapse);
  if (v.heat < s.hypothermia_heat) hurt(DamageCause::Cold, s.hypothermia_per_second * (1.0 - v.heat / s.hypothermia_heat) * dt);
  if (v.health <= 0) { v.health = 0; v.alive = false; report.collapsed = true; }
  return report;
}

}  // namespace terraflux::graphics
