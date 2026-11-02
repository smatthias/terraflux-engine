#pragma once

#include <terraflux/graphics/camera_rig.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace terraflux::graphics {

// The walker's body: health, body heat and wetness, and what the world does
// to them. Everything is a rate in gameplay seconds with documented
// constants; the viewer gathers the exposure from the weather, the fire, the
// water and the walker's own state and advances the vitals every frame.
//
//  health    1 fresh .. 0 collapsed. Lost to lightning (at once), fire (by
//            heat), smoke (by density), falls (by impact), wind-borne debris
//            in a storm, drowning (swimming without strength), the cold
//            once the body heat is below hypothermia, a trunk landing on the
//            walker, and a bridge deck collapsing under it.
//  heat      1 warm .. 0 frozen. Lost below the comfort temperature, faster
//            in wind (chill), wet, and in water; regained in warm air, and
//            beside a fire (the same heat that burns you up close warms you
//            at a distance).
//  wetness   0 dry .. 1 soaked: rises in rain and water, dries in the warm.
enum class DamageCause : std::uint8_t { None, Lightning, Fire, Smoke, Fall, Wind, Drowning, Cold, Crushed, Collapse, Count };
const char* damage_cause_name(DamageCause cause);

struct VitalsStyle {
  // Lightning (fractions of health, at once)
  double struck_damage = 0.6, dangerous_damage = 0.15;
  double crouched_shelter = 0.5;        // 0 .. 1: a crouched walker is this much less exposed (hazard distance / (1 - shelter))
  // Fire: heat 0..1 from the viewer (1 inside the flames)
  double burn_per_second = 0.12;        // at heat 1; 0 .. 5
  double burn_threshold = 0.25;         // heat below this warms instead of burning
  double smoke_per_second = 0.015;      // at smoke 1; 0 .. 5
  // Falls: damage = (impact - safe) / (lethal - safe) of health
  double safe_impact_mps = 6.0, lethal_impact_mps = 20.0;
  // Wind: debris above this speed
  double debris_wind_mps = 32.0, debris_per_second = 0.01;
  // Drowning: swimming without strength
  double drowning_per_second = 0.1;
  // Heat
  double comfort_celsius = 18.0;
  double cold_loss_per_degree_second = 0.0004;   // per degree below comfort, per second, dry and calm: 0 C costs 1 in ~140 s
  double wind_chill_per_mps = 0.04;              // each m/s adds this fraction of the cold loss
  double wet_loss_factor = 2.5;                  // soaked: cold loss x this
  double water_loss_factor = 4.0;                // immersed: cold loss x this (on top of wet)
  double warm_gain_per_second = 0.01;            // above comfort, dry
  double fire_warm_per_second = 0.05;            // at warming heat (below the burn threshold), x heat
  double hypothermia_heat = 0.3;                 // below it health is lost
  double hypothermia_per_second = 0.01;
  // Wetness
  double soak_rain_mm_h = 25.0, soak_seconds = 30.0;  // a storm's rain soaks in this time
  double dry_seconds = 120.0;                    // soaked to dry in warm, dry air
  double water_soaks_at_depth = 0.3;             // standing deeper than this soaks at once
};
void validate_vitals_style(const VitalsStyle& style);

struct Exposure {
  double temperature_celsius = 18.0;
  double wind_mps = 0.0;
  double rain_mm_h = 0.0;
  double water_depth_meters = 0.0;  // at the feet
  bool swimming = false;
  bool exhausted = false;           // no strength left (drowning when swimming)
  double fire_heat = 0.0;           // 0 .. 1: 1 inside the flames
  double smoke = 0.0;               // 0 .. 1
  LightningHazard lightning = LightningHazard::None;  // a strike this step
  double fall_impact_mps = 0.0;     // a landing this step
  bool crouched = false;
  bool sheltered = false;           // under a roof: no rain (unused by the viewer for now)
  double crushed = 0.0;             // 0 .. 1: a falling trunk landed on the walker this step; the fraction of health it takes
  double collapse = 0.0;            // 0 .. 1: a bridge deck gave way under the walker this step; the fraction of health it takes (the fall is extra)
};
// What a trunk of this mass falling on a walker takes (a fraction of health):
// a sapling of 100 kg a tenth, a tonne and more all of it.
double crush_damage(double trunk_mass_kg);
// The hazard a strike at a distance is to a walker of this posture.
LightningHazard hazard_for(double distance_meters, bool crouched, const VitalsStyle& style);

struct Vitals {
  double health = 1.0, heat = 1.0, wetness = 0.0;
  bool alive = true;
  std::array<double, static_cast<std::size_t>(DamageCause::Count)> lost{};  // cumulative health lost per cause
  DamageCause last_cause = DamageCause::None;  // of the most recent loss
  double last_loss = 0.0;
};
struct VitalsReport { double health_lost = 0.0; DamageCause worst = DamageCause::None; bool collapsed = false; };
// Advances the vitals by dt seconds under an exposure. Collapsed (health 0)
// stays collapsed until reset.
VitalsReport advance_vitals(Vitals& vitals, const Exposure& exposure, double dt, const VitalsStyle& style);
void reset_vitals(Vitals& vitals);

}  // namespace terraflux::graphics
