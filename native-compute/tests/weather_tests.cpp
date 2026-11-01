#include <terraflux/graphics/weather.hpp>
#include <terraflux/graphics/render_pass.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/graphics/water_shading.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/graphics/fire_effects.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace terraflux;
using namespace terraflux::graphics;

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template <class F> void rejects(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const std::invalid_argument&) { caught = true; }
  require(caught, message);
}
template <class F> void rejects_logic(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const std::logic_error&) { caught = true; }
  require(caught, message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

// ---------------------------------------------------------------------------
void test_wind_between_thresholds() {
  WindConfig config;
  config.direction_degrees = 270; config.speed_min_mps = 2; config.speed_max_mps = 9; config.swing_degrees = 25; config.period_seconds = 8;
  double lo = 1e9, hi = -1e9, dir_lo = 1e9, dir_hi = -1e9, previous = -1;
  std::size_t reversals = 0; double trend = 0;
  for (int i = 0; i <= 6000; ++i) {
    const auto s = wind_at(config, i * 0.05);
    require(s.speed_mps >= 2 - 1e-12 && s.speed_mps <= 9 + 1e-12, "speed stays between the thresholds");
    require(s.direction_degrees >= 0 && s.direction_degrees < 360, "direction is wrapped to [0, 360)");
    require(near(s.travel.length(), 1.0, 1e-9), "travel vector is unit length");
    require(near(s.gustiness, (s.speed_mps - 2) / 7, 1e-9), "gustiness maps the speed between the thresholds");
    // Relative to the mean, the direction never swings further than the configured half-width.
    const double offset = std::remainder(s.direction_degrees - 270.0, 360.0);
    require(std::abs(offset) <= 25 + 1e-9, "direction stays within the swing");
    lo = std::min(lo, s.speed_mps); hi = std::max(hi, s.speed_mps);
    dir_lo = std::min(dir_lo, offset); dir_hi = std::max(dir_hi, offset);
    if (previous >= 0) {
      const double d = s.speed_mps - previous;
      require(std::abs(d) < 0.6, "the speed changes smoothly, never jumps");
      if (trend * d < 0) ++reversals;
      if (d != 0) trend = d;
    }
    previous = s.speed_mps;
  }
  require(lo < 2.7 && hi > 8.3, "the speed actually visits both thresholds over time");
  require(dir_lo < -12 && dir_hi > 12, "the direction veers both ways around the mean");
  require(reversals > 30, "the wind gusts and lulls many times over five minutes");
  // Determinism and seeding.
  require(wind_at(config, 17.3).speed_mps == wind_at(config, 17.3).speed_mps, "sampling is deterministic");
  auto other = config; other.seed = 7;
  require(wind_at(config, 17.3).speed_mps != wind_at(other, 17.3).speed_mps, "the seed changes the gust pattern");
  // A constant wind: equal thresholds and no swing.
  WindConfig constant; constant.speed_min_mps = constant.speed_max_mps = 4; constant.swing_degrees = 0; constant.direction_degrees = 45;
  for (int i = 0; i < 50; ++i) {
    const auto s = wind_at(constant, i * 1.7);
    require(near(s.speed_mps, 4) && near(s.direction_degrees, 45), "equal thresholds give a constant wind");
  }
  // Compass convention shared with the water: 0 north (-z), 90 east (+x).
  WindConfig east = constant; east.direction_degrees = 90;
  const auto e = wind_at(east, 0);
  require(near(e.travel.x, 1, 1e-9) && near(e.travel.y, 0, 1e-9), "90 degrees blows toward +x");
  WindConfig north = constant; north.direction_degrees = 0;
  const auto n = wind_at(north, 0);
  require(near(n.travel.x, 0, 1e-9) && near(n.travel.y, -1, 1e-9), "0 degrees blows toward -z (north)");
  rejects([&] { auto c = config; c.speed_max_mps = 1; (void)wind_at(c, 0); }, "max below min rejected");
  rejects([&] { auto c = config; c.speed_min_mps = -1; (void)wind_at(c, 0); }, "negative speed rejected");
  rejects([&] { auto c = config; c.direction_degrees = 361; (void)wind_at(c, 0); }, "direction out of range rejected");
  rejects([&] { auto c = config; c.swing_degrees = 91; (void)wind_at(c, 0); }, "swing out of range rejected");
  rejects([&] { auto c = config; c.period_seconds = 0; (void)wind_at(c, 0); }, "zero period rejected");
  rejects([&] { (void)wind_at(config, std::numeric_limits<double>::quiet_NaN()); }, "NaN time rejected");
}

// ---------------------------------------------------------------------------
void test_sun_and_twilight() {
  DaylightConfig config;  // 47 N, June solstice
  const auto noon = sun_for_hour(config, 12);
  const auto midnight = sun_for_hour(config, 0);
  require(near(noon.altitude_degrees, 90 - 47 + 23.44, 0.1), "noon altitude at the solstice is 90 - latitude + declination");
  require(near(noon.azimuth_degrees, 180, 0.5), "the noon sun stands due south in the northern hemisphere");
  require(near(midnight.altitude_degrees, -(90 - 47) + 23.44, 0.1), "midnight altitude");
  require(noon.direction.y > 0.9 && noon.direction.z > 0.3, "the noon direction points up and south (+z)");
  require(near(noon.direction.length(), 1, 1e-9), "direction is unit length");
  const auto morning = sun_for_hour(config, 8);
  require(morning.azimuth_degrees > 60 && morning.azimuth_degrees < 120 && morning.direction.x > 0, "the morning sun is in the east");
  const auto evening = sun_for_hour(config, 18);
  require(evening.azimuth_degrees > 240 && evening.azimuth_degrees < 300 && evening.direction.x < 0, "the evening sun is in the west");
  require(near(solar_declination_degrees(81), 0, 1e-9) && near(solar_declination_degrees(172), 23.44, 0.05) &&
          near(solar_declination_degrees(355), -23.44, 0.1), "declination at the equinox and solstices");
  // Phases by altitude.
  require(daylight_phase(5) == DaylightPhase::Day && daylight_phase(0) == DaylightPhase::Day &&
          daylight_phase(-3) == DaylightPhase::CivilTwilight && daylight_phase(-6) == DaylightPhase::CivilTwilight &&
          daylight_phase(-9) == DaylightPhase::NauticalTwilight && daylight_phase(-15) == DaylightPhase::AstronomicalTwilight &&
          daylight_phase(-20) == DaylightPhase::Night, "twilight bands at 0, -6, -12 and -18 degrees");
  require(std::string(daylight_phase_name(DaylightPhase::CivilTwilight)) == "civil_twilight", "phase names");
  // Light is continuous: no jumps through sunset and twilight, monotone brightness.
  DaylightConfig equinox; equinox.latitude_degrees = 0; equinox.day_of_year = 81;
  double previous_daylight = 2, previous_sun = 2;
  math::Vec3 previous_zenith = sun_for_hour(equinox, 12).zenith_color;
  bool saw_civil = false, saw_nautical = false, saw_astronomical = false, saw_night = false;
  for (double hour = 12; hour <= 24; hour += 1.0 / 60) {
    const auto s = sun_for_hour(equinox, hour);
    require(s.daylight <= previous_daylight + 1e-9, "daylight never increases between noon and midnight");
    require(s.light_intensity >= 0 && s.ambient_intensity > 0, "some light always remains");
    require((s.zenith_color - previous_zenith).length() < 0.02, "sky colors change continuously through twilight");
    if (s.light_direction.dot(s.direction) > 0.999) require(s.light_intensity <= previous_sun + 1e-9, "sunlight fades, never brightens, toward night");
    previous_daylight = s.daylight; previous_sun = s.light_intensity; previous_zenith = s.zenith_color;
    saw_civil = saw_civil || s.phase == DaylightPhase::CivilTwilight;
    saw_nautical = saw_nautical || s.phase == DaylightPhase::NauticalTwilight;
    saw_astronomical = saw_astronomical || s.phase == DaylightPhase::AstronomicalTwilight;
    saw_night = saw_night || s.phase == DaylightPhase::Night;
  }
  require(saw_civil && saw_nautical && saw_astronomical && saw_night, "an equatorial evening passes through all twilight phases");
  const auto civil = sun_for_hour(equinox, 18.2);  // about -3 degrees
  require(civil.phase == DaylightPhase::CivilTwilight, "18:12 at the equator is civil twilight");
  require(civil.daylight > 0.1 && civil.daylight < 0.5, "twilight is neither day nor night");
  require(civil.horizon_color.x > civil.horizon_color.z * 1.5, "the civil-twilight horizon glows warm");
  require(civil.light_intensity < 0.1 && civil.ambient_intensity > 0.05, "after sunset the ambient sky light dominates");
  const auto night = sun_for_hour(equinox, 0);
  require(night.phase == DaylightPhase::Night && night.star_brightness == 1 && night.daylight == 0, "midnight is night with stars");
  require(night.light_direction.y > 0 && night.light_intensity > 0 && night.light_intensity < 0.2, "the moon lights the night faintly from above");
  require(night.light_color.z > night.light_color.x, "moonlight is bluish");
  require(noon.star_brightness == 0 && noon.light_intensity > 0.6 && noon.light_color.x >= noon.light_color.z, "noon: no stars, strong neutral sun");
  require(sun_for_hour(config, 30).hour == 6 && sun_for_hour(config, -1).hour == 23, "hours wrap around the day");
  // The moon: phase from the day of the year, a full moon opposite the sun, a new moon with it,
  // moonlight falling steeply with the phase, and the cloud light outlasting sunset in reds.
  require(near(moon_phase_for_day(1.5), 0.0) && near(moon_phase_for_day(1.5 + 29.530589 * 0.5), 0.5, 1e-9) &&
          near(moon_illumination(0.5), 1.0) && near(moon_illumination(0.0), 0.0) && near(moon_illumination(0.25), 0.5), "phase and illumination");
  DaylightConfig full = config; full.moon_phase = 0.5;
  const auto full_midnight = sun_for_hour(full, 0);
  require(near(full_midnight.moon_azimuth_degrees, 180, 0.5) && near(full_midnight.moon_altitude_degrees, 90 - 47 - 23.44, 0.2),
          "a full moon stands due south at midnight, as high as the winter noon sun");
  require(full_midnight.light_direction.dot(full_midnight.moon_direction) > 0.999 && near(full_midnight.light_intensity, 0.11, 1e-9) &&
          near(full_midnight.moon_intensity, 0.11, 1e-9), "the full moon is the night's light");
  DaylightConfig newmoon = config; newmoon.moon_phase = 0.0;
  const auto new_noon = sun_for_hour(newmoon, 12);
  require(new_noon.moon_direction.dot(new_noon.direction) > 0.999 && sun_for_hour(newmoon, 0).moon_intensity == 0.0,
          "a new moon travels with the sun and gives no light at night");
  DaylightConfig half = config; half.moon_phase = 0.25;
  require(sun_for_hour(half, 21).moon_intensity < 0.3 * 0.11 && sun_for_hour(half, 21).moon_intensity > 0.0, "a half moon (in the western sky at nine) gives well under a third of a full one");
  require(sun_for_hour(config, 0).moon_phase == moon_phase_for_day(172) && near(sun_for_hour(config, 12).moon_intensity, 0.0), "default phase from the day; no moonlight by day");
  const auto dusk = sun_for_hour(equinox, 18.25);  // sun about four degrees below the horizon
  require(dusk.light_intensity == 0.0 && dusk.cloud_light_intensity > 0.1 && dusk.cloud_light_color.x > 3.0 * dusk.cloud_light_color.z &&
          dusk.cloud_light_direction.dot(dusk.direction) > 0.999, "after sunset the true sun still lights the clouds, deep red, from below the horizon");
  const auto deep = sun_for_hour(equinox, 18.7);  // about ten degrees below
  require(deep.cloud_light_intensity < 0.05 || deep.cloud_light_direction.dot(deep.moon_direction) > 0.999, "ten degrees down the clouds go dark or to the moon");
  require(noon.horizon_away_color.z >= noon.horizon_away_color.x - 0.05 && civil.horizon_away_color.z > civil.horizon_away_color.y &&
          civil.horizon_away_color.x < civil.horizon_color.x, "the horizon opposite the sun is cool and purplish at twilight");
  rejects([&] { auto c = config; c.moon_phase = 1.5; (void)sun_for_hour(c, 0); }, "phase out of range");
  rejects([&] { auto c = config; c.moon_diameter_degrees = 10; (void)sun_for_hour(c, 0); }, "moon size out of range");
  // The clock: start hour plus time over the time scale; zero freezes.
  DaylightConfig clock; clock.start_hour = 10; clock.seconds_per_hour = 10;
  require(near(sun_at(clock, 25).hour, 12.5, 1e-9), "ten seconds per hour");
  clock.seconds_per_hour = 0;
  require(near(sun_at(clock, 1e6).hour, 10, 1e-9), "a zero time scale freezes the sun");
  // Polar day: at 80 N on the solstice the sun never sets.
  DaylightConfig polar; polar.latitude_degrees = 80;
  for (double hour = 0; hour < 24; hour += 0.5) require(sun_for_hour(polar, hour).altitude_degrees > 0, "midnight sun");
  rejects([&] { auto c = config; c.latitude_degrees = 91; (void)sun_for_hour(c, 12); }, "latitude out of range");
  rejects([&] { auto c = config; c.day_of_year = 0; (void)sun_for_hour(c, 12); }, "day out of range");
  rejects([&] { auto c = config; c.seconds_per_hour = -1; (void)sun_at(c, 0); }, "negative time scale");
  rejects([&] { (void)daylight_phase(std::numeric_limits<double>::infinity()); }, "non-finite altitude");
}

// ---------------------------------------------------------------------------
void test_cloud_profiles_and_layers() {
  std::set<std::string> names;
  for (std::size_t i = 0; i < kCloudTypeCount; ++i) {
    const auto type = static_cast<CloudType>(i);
    const auto& p = cloud_profile(type);
    require(p.type == type && parse_cloud_type(p.name) == type && names.insert(p.name).second, "profiles round-trip through their names");
    require(p.coverage >= 0 && p.coverage <= 1 && p.density >= 0 && p.density <= 1 && p.detail >= 0 && p.detail <= 1 &&
            p.precipitation >= 0 && p.precipitation <= 1 && p.storm >= 0 && p.storm <= 1 && p.visibility_meters > 0,
            "profile values are normalized");
    require(std::strlen(p.expected_weather) > 20, "every cloud form announces its weather");
  }
  const auto& cumulus = cloud_profile(CloudType::Cumulus);
  const auto& stratus = cloud_profile(CloudType::Stratus);
  const auto& nimbo = cloud_profile(CloudType::Nimbostratus);
  const auto& cb = cloud_profile(CloudType::Cumulonimbus);
  const auto& cirrus = cloud_profile(CloudType::Cirrus);
  require(cumulus.precipitation == 0 && cumulus.detail == 1 && cumulus.coverage < 0.5, "fair-weather cumulus: dry, puffy, scattered");
  require(stratus.coverage > 0.9 && stratus.detail < 0.2 && stratus.visibility_meters < 10000, "stratus: overcast, featureless, murky");
  require(nimbo.precipitation > 0.5 && nimbo.thickness_meters > 2000 && nimbo.coverage == 1, "nimbostratus: deep, overcast, rain");
  require(cb.storm == 1 && cb.precipitation == 1 && cb.thickness_meters > 5000, "cumulonimbus: thunderstorm");
  require(cirrus.base_meters_agl > 5000 && cirrus.precipitation == 0, "cirrus: high and dry");
  require(cloud_profile(CloudType::Clear).coverage == 0, "clear sky has no cloud");
  rejects([] { (void)parse_cloud_type("fog"); }, "unknown cloud name rejected");
  rejects([] { (void)cloud_profile(static_cast<CloudType>(99)); }, "unknown cloud type rejected");

  // LCL: 125 m per degree of spread; saturated air gives fog on the ground.
  require(near(lifting_condensation_level_meters(20, 12), 1000) && lifting_condensation_level_meters(10, 12) == 0, "Espy's rule");
  require(near(air_temperature_at(15, 100, 1100), 8.5), "6.5 degrees per kilometre");
  WeatherConfig config;
  config.reference_elevation_meters = 200;
  config.clouds = CloudType::Cumulus;
  const auto layer = cloud_layer_for(config);
  require(near(layer.base_meters, 200 + cumulus.base_meters_agl, 1e-6) && near(layer.top_meters - layer.base_meters, cumulus.thickness_meters, 1e-6),
          "the default dew point reproduces the profile's typical base above the valley floor");
  require(layer.coverage == cumulus.coverage && layer.density == cumulus.density && layer.type == CloudType::Cumulus, "layer carries the profile");
  config.dew_point_celsius = config.temperature_celsius - 2;  // humid: the base drops to 250 m
  require(near(cloud_layer_for(config).base_meters, 450, 1e-6), "a small dew-point spread lowers the cloud base");
  config.dew_point_celsius.reset(); config.cloud_base_meters = 900;
  require(cloud_layer_for(config).base_meters == 900, "an explicit base overrides the LCL");
  config.cloud_base_meters.reset(); config.coverage = 0.9;
  require(cloud_layer_for(config).coverage == 0.9 && cloud_layer_for(config).precipitation == 0, "coverage override; cumulus stays dry");
  config.coverage.reset(); config.clouds = CloudType::Cirrus; config.dew_point_celsius = config.temperature_celsius - 1;
  require(cloud_layer_for(config).base_meters >= 200 + cirrus.base_meters_agl, "cirrus keeps its altitude whatever the surface humidity");
  config.clouds = CloudType::Nimbostratus; config.dew_point_celsius.reset();
  require(cloud_layer_for(config).precipitation > 0.5 && cloud_layer_for(config).visibility_meters < 10000, "nimbostratus rains and cuts visibility");
  config.coverage = 0.2;
  require(cloud_layer_for(config).precipitation < 0.5 && cloud_layer_for(config).visibility_meters > 30000, "thin cover rains less and keeps the view");
  config.coverage.reset(); config.clouds = CloudType::Clear;
  require(cloud_layer_for(config).coverage == 0 && cloud_layer_for(config).precipitation == 0 && cloud_layer_for(config).visibility_meters == 50000,
          "clear: nothing");
  // Snow line from the lapse rate.
  WeatherConfig cold; cold.temperature_celsius = 5; cold.reference_elevation_meters = 0;
  require(!precipitation_is_snow(cold, 0) && precipitation_is_snow(cold, 800), "rain in the valley, snow 800 m higher");
  rejects([&] { auto c = config; c.temperature_celsius = 60; (void)cloud_layer_for(c); }, "temperature out of range");
  rejects([&] { auto c = config; c.coverage = 1.5; (void)cloud_layer_for(c); }, "coverage out of range");
  rejects([&] { auto c = config; c.stability_hz = 0; (void)cloud_layer_for(c); }, "stability out of range");
  // Lightning: deterministic, bounded, only in storms, frequent enough in a full storm.
  require(lightning_at(5, 0, 1) == 0, "no lightning without a storm");
  std::size_t flashes = 0;
  for (int i = 0; i < 6000; ++i) {
    const double v = lightning_at(i * 0.05, 1.0, 3);
    require(v >= 0 && v <= 1, "flash intensity is normalized");
    if (v > 0.9) ++flashes;
  }
  require(flashes >= 10 && lightning_at(42.1, 1.0, 3) == lightning_at(42.1, 1.0, 3), "a full storm flashes repeatedly and reproducibly");
  rejects([] { (void)lightning_at(1, 2, 0); }, "storm out of range");
}

// ---------------------------------------------------------------------------
// A ridge across the wind: 3 km of flat valley at 100 m, a ridge rising to
// `crest` over the next 1.5 km and falling again, then flat valley.
HeightField ridge_field(double crest, double spacing = 100.0) {
  HeightField f;
  f.spacing = spacing; f.width = 120; f.height = 5; f.x0 = 0; f.z0 = -200; f.reference_meters = 100;
  f.heights.resize(static_cast<std::size_t>(f.width) * f.height);
  for (std::uint32_t j = 0; j < f.height; ++j)
    for (std::uint32_t i = 0; i < f.width; ++i) {
      const double x = i * spacing;
      const double t = std::clamp((x - 4000.0) / 1500.0, -1.0, 1.0);  // crest at x = 4000
      f.heights[j * f.width + i] = static_cast<float>(100.0 + (crest - 100.0) * 0.5 * (1.0 + std::cos(t * 3.14159265358979)));
    }
  return f;
}
WindSample westerly(double speed) {
  WindConfig c; c.direction_degrees = 90; c.speed_min_mps = c.speed_max_mps = speed; c.swing_degrees = 0;
  return wind_at(c, 0);
}
const OrographicTexel& texel_at(const OrographicField& f, double x) {
  const auto i = static_cast<std::size_t>(std::lround((x - f.x0) / f.spacing));
  return f.texels[2 * f.width + i];  // middle row
}

void test_orographic_lift() {
  const auto ridge = ridge_field(900);
  require(near(ridge.sample(4000, 0), 900, 1e-3) && near(ridge.sample(0, 0), 100, 1e-3) && near(ridge.sample(-5000, 0), 100, 1e-9),
          "height field samples the ridge, the valley and the valley floor outside");
  // Cumulus whose base (700 m) lies below the crest: strong wind flows over.
  CloudLayer layer; layer.type = CloudType::Cumulus; layer.base_meters = 700; layer.top_meters = 1600; layer.coverage = 0.5; layer.density = 0.8;
  const auto strong = compute_orographic_field(ridge, westerly(12), layer, 0.01);
  require(strong.width == ridge.width && strong.height == ridge.height && strong.texels.size() == ridge.heights.size(), "field matches the terrain lattice");
  require(strong.coupling > 0.99, "a layer the ridge pierces is fully coupled");
  require(strong.froude > 1.4, "12 m/s over an 800 m barrier at N = 0.01 flows over (Froude above one)");
  const auto& valley_upwind = texel_at(strong, 1000);
  const auto& windward = texel_at(strong, 3200);
  const auto& crest = texel_at(strong, 4000);
  const auto& lee = texel_at(strong, 4800);
  const auto& far_lee = texel_at(strong, 9500);
  require(valley_upwind.thickening < 0.05 && valley_upwind.dissipation < 0.05 && std::abs(valley_upwind.base_shift_meters) < 5 &&
          near(valley_upwind.precipitation, 0.5, 0.03), "the flat valley upwind is untouched");
  require(windward.thickening > 0.6 && windward.dissipation < 0.05, "air forced uphill thickens the cloud on the windward slope");
  require(windward.precipitation > 0.75 && windward.base_shift_meters < 0, "the windward side rains more and the base sinks a little");
  require(lee.dissipation > 0.6 && lee.thickening < 0.05, "descending air dissipates the cloud in the lee (foehn)");
  require(lee.base_shift_meters > 150 && lee.precipitation < 0.25, "the lee base rises into the rain shadow");
  require(far_lee.dissipation < lee.dissipation * 0.5, "the foehn clearing fades a few kilometres downwind");
  require(crest.thickening > 0.3 && crest.dissipation < 0.3, "the crest still carries the cap cloud");
  require(strong.windward_texels > 0 && strong.lee_texels > 0 && strong.in_cloud_texels > 0 && strong.max_thickening > 0.6 &&
          strong.max_dissipation > 0.6 && strong.blocked_texels == 0, "diagnostics: windward, lee and summit-in-cloud texels, nothing blocked");
  // Symmetry: an easterly swaps the sides.
  WindConfig east; east.direction_degrees = 270; east.speed_min_mps = east.speed_max_mps = 12; east.swing_degrees = 0;
  const auto reversed = compute_orographic_field(ridge, wind_at(east, 0), layer, 0.01);
  require(texel_at(reversed, 4800).thickening > 0.6 && texel_at(reversed, 3200).dissipation > 0.6, "reversing the wind swaps windward and lee");

  // Weak wind against the same ridge: the low-level air is blocked (Froude
  // below one); the cloud piles up in front and cannot cross the crest.
  const auto weak = compute_orographic_field(ridge, westerly(2), layer, 0.01);
  require(weak.froude < 0.5 && weak.blocked_texels > 0, "2 m/s cannot lift the air over an 800 m barrier");
  const auto& dammed = texel_at(weak, 2600);
  require(dammed.thickening > 0.3 && dammed.thickening > texel_at(strong, 2600).thickening, "blocked air dams the cloud against the windward foot");
  require(texel_at(weak, 4600).dissipation > 0.9 && texel_at(weak, 9500).dissipation > 0.9, "blocked low cloud never reaches the lee, however far downwind");
  // A strong wind unblocks: Froude scales with U / (N H).
  const auto stable = compute_orographic_field(ridge, westerly(12), layer, 0.03);
  require(stable.blocked_texels > 0 && stable.froude < weak.froude * 10, "a very stable atmosphere blocks even a strong wind");

  // High clouds ignore the hills: a cirrus sheet at 7 km over a 900 m ridge.
  CloudLayer cirrus; cirrus.type = CloudType::Cirrus; cirrus.base_meters = 7100; cirrus.top_meters = 7700; cirrus.coverage = 0.4; cirrus.density = 0.2;
  const auto aloft = compute_orographic_field(ridge, westerly(12), cirrus, 0.01);
  require(aloft.coupling == 0 && aloft.max_thickening == 0 && aloft.max_dissipation == 0, "a layer far above the crests is decoupled");
  // A layer just above the crest is fully coupled but never enters the terrain;
  // two kilometres above it the coupling is only partial.
  CloudLayer above; above = layer; above.base_meters = 1100; above.top_meters = 2000;
  const auto just_above = compute_orographic_field(ridge, westerly(12), above, 0.01);
  require(just_above.coupling > 0.99 && just_above.in_cloud_texels == 0 && just_above.max_thickening > 0.6,
          "a base slightly above the crest feels the full lift but the ground stays below the cloud");
  above.base_meters = 2900; above.top_meters = 3800;
  const auto partly = compute_orographic_field(ridge, westerly(12), above, 0.01);
  require(partly.coupling > 0.3 && partly.coupling < 0.7 && partly.max_thickening < just_above.max_thickening,
          "two kilometres above the crest the layer is only partly coupled");
  // Flat terrain: neutral everywhere.
  HeightField flat = ridge; std::fill(flat.heights.begin(), flat.heights.end(), 100.0f);
  const auto none = compute_orographic_field(flat, westerly(12), layer, 0.01);
  require(none.max_thickening == 0 && none.max_dissipation == 0 && none.windward_texels == 0, "flat ground does nothing to the cloud");
  // Holes take the valley floor and cannot produce lift.
  HeightField holed = flat; holed.heights[2 * holed.width + 40] = std::numeric_limits<float>::quiet_NaN();
  require(compute_orographic_field(holed, westerly(12), layer, 0.01).max_thickening == 0, "holes are neutral");
  // Clear sky: nothing to couple.
  CloudLayer clear; clear.coverage = 0; clear.base_meters = clear.top_meters = 100;
  require(compute_orographic_field(ridge, westerly(12), clear, 0.01).coupling == 0, "no cloud, no coupling");

  // Packing: RGBA8 with 0.5 neutral for base shift and precipitation.
  const auto bytes = pack_orographic_texture(strong);
  require(bytes.size() == strong.texels.size() * 4, "four bytes per texel");
  const auto index = (2 * strong.width + static_cast<std::size_t>(std::lround(3200 / strong.spacing))) * 4;
  require(static_cast<int>(bytes[index]) > 150 && static_cast<int>(bytes[index + 1]) < 15 && static_cast<int>(bytes[index + 2]) < 128 &&
          static_cast<int>(bytes[index + 3]) > 190, "windward texel encodes thickening, no dissipation, a lowered base and more rain");
  const auto neutral = pack_orographic_texture(neutral_orographic_field());
  require(neutral.size() == 4 && static_cast<int>(neutral[0]) == 0 && static_cast<int>(neutral[1]) == 0 &&
          static_cast<int>(neutral[2]) == 128 && static_cast<int>(neutral[3]) == 128, "neutral texel");
  rejects([&] { auto bad = strong; bad.texels.pop_back(); (void)pack_orographic_texture(bad); }, "inconsistent field rejected");
  rejects([&] { (void)compute_orographic_field(ridge, westerly(12), layer, 0.0001); }, "stability out of range rejected");
  rejects([&] { auto bad = ridge; bad.heights.pop_back(); (void)compute_orographic_field(bad, westerly(12), layer, 0.01); }, "inconsistent terrain rejected");
}

// ---------------------------------------------------------------------------
void test_runoff_and_snow_line() {
  // A valley between two slopes: a gentle western slope and a cliff on the east; the drainage
  // gathers in the valley floor and the cliff gets waterfalls where streams reach it.
  HeightField f;
  f.spacing = 30; f.width = 41; f.height = 21; f.x0 = 0; f.z0 = 0; f.reference_meters = 100;
  f.heights.resize(static_cast<std::size_t>(f.width) * f.height);
  for (std::uint32_t j = 0; j < f.height; ++j)
    for (std::uint32_t i = 0; i < f.width; ++i) {
      const double x = i * 30.0, z = j * 30.0;
      double h = 100.0;
      if (x < 600) h = 100.0 + (600.0 - x) * 0.15;                       // gentle slope (8.5 degrees) down to the valley
      else if (x > 900) h = 100.0 + (x - 900.0) * 1.4;                   // cliff (54 degrees) rising east
      h += 0.02 * std::abs(z - 300.0);                                   // the valley floor tilts toward its middle row
      f.heights[j * f.width + i] = static_cast<float>(h);
    }
  const auto runoff = compute_runoff_field(f);
  require(runoff.width == f.width && runoff.height == f.height && runoff.texels.size() == f.heights.size(), "runoff matches the lattice");
  const auto at = [&](std::uint32_t i, std::uint32_t j) { return runoff.texels[j * f.width + i]; };
  require(at(0, 10).flow < 0.2 && at(20, 10).flow > 0.6 && at(15, 10).flow > at(5, 10).flow, "the drainage gathers from the ridges into the valley floor");
  require(at(10, 10).slope_degrees > 7 && at(10, 10).slope_degrees < 10 && at(35, 10).slope_degrees > 50, "slopes of the gentle side and the cliff");
  require(at(10, 10).waterfall == 0.0f, "no waterfall on a gentle slope, however much water");
  require(runoff.channel_texels > 0 && runoff.max_accumulation_cells > 50, "channels exist and accumulate many texels");
  // A stream pouring over the cliff: raise the plateau above the cliff so water gathers there
  // and drops off, over a ledge halfway down (two cliff texels) onto the foot.
  HeightField cliff = f;
  for (std::uint32_t j = 0; j < f.height; ++j)
    for (std::uint32_t i = 32; i < f.width; ++i) cliff.heights[j * f.width + i] = static_cast<float>(400.0 + (i - 32) * 2.0 + 0.05 * std::abs(double(j) - 10.0) * 30.0);
  for (std::uint32_t j = 0; j < f.height; ++j) { cliff.heights[j * f.width + 31] = 250.0f; cliff.heights[j * f.width + 30] = 110.0f; }  // the ledge and the foot
  const auto falls = compute_runoff_field(cliff);
  require(falls.waterfall_texels > 0, "a stream meeting a cliff makes a waterfall");
  const auto packed = pack_runoff_texture(falls);
  const auto channel = [&](const std::vector<std::byte>& bytes, std::size_t texel, std::size_t c) {
    return static_cast<int>(bytes[(texel * 4 + c) * 2]) | (static_cast<int>(bytes[(texel * 4 + c) * 2 + 1]) << 8);
  };
  require(packed.size() == falls.texels.size() * 8 && channel(packed, 10 * f.width + 25, 0) > 25000, "packed 16-bit flow of the valley floor");
  const auto neutral = pack_runoff_texture(neutral_runoff_field());
  require(neutral.size() == 8 && channel(neutral, 0, 0) == 0 && channel(neutral, 0, 1) == 32768 && channel(neutral, 0, 3) == 0, "neutral runoff texel");
  // Cascades: the plateau stream's first cliff texel is a lip (nothing falls into it), the
  // cliff below it carries a growing drop, and the foot texel is a plunge pool; the spray
  // emitters come from exactly those places, strongest first, with the rock's frame.
  require(falls.lip_texels > 0 && falls.plunge_texels > 0 && falls.max_drop_meters > 20.0, "lips, plunge pools and a tall drop");
  {
    const auto lip = falls.texels[10 * f.width + 32], ledge = falls.texels[10 * f.width + 31], foot = falls.texels[10 * f.width + 30];
    require(lip.waterfall > 0.3f && lip.drop_meters == 0.0f, "the lip is the first cliff texel and has no drop");
    require(ledge.waterfall > 0.3f && ledge.drop_meters > 100.0f && ledge.drop_meters < 200.0f, "the ledge below carries the drop from the lip");
    require(foot.waterfall < 0.3f && foot.impact > 0.3f && foot.drop_meters > 250.0f, "the cliff foot is a plunge pool holding the whole drop");
    const auto plateau = falls.texels[10 * f.width + 35];
    require(std::abs(std::hypot(lip.direction_x, lip.direction_z) - 1.0) < 1e-6 && lip.direction_x < -0.9 && lip.steepness > 0.6,
            "the flow direction points down the cliff (west) and carries its steepness");
    require(plateau.direction_x < -0.9 && plateau.steepness == 0.0f, "the plateau drains toward the cliff but is not steep itself");
  }
  const auto emitters = waterfall_spray_emitters(falls, cliff);
  require(!emitters.empty() && emitters.size() <= 96, "emitters are bounded");
  std::size_t lips = 0, contacts = 0, plunges = 0;
  for (std::size_t i = 0; i < emitters.size(); ++i) {
    const auto& e = emitters[i];
    if (e.kind == SprayKind::Lip) { ++lips; require(e.drop_meters == 0.0, "a lip has no drop"); }
    else if (e.kind == SprayKind::Contact) { ++contacts; require(e.drop_meters > 0.0, "a ledge contact has fallen some way"); }
    else { ++plunges; require(e.drop_meters > 3.0 && e.face.y > 0.9, "a plunge pool lies below a fall on gentle ground"); }
    require(std::abs(e.downhill.length() - 1.0) < 1e-9 && std::abs(e.face.length() - 1.0) < 1e-9 && e.downhill.y == 0.0, "unit frame vectors");
    if (e.kind != SprayKind::Plunge) require(e.face.y < 0.8 && e.face.dot(e.downhill) > 0.5, "the cliff face leans the way the water leaves");
    if (i) require(emitters[i - 1].strength >= e.strength, "strongest first");
  }
  require(lips > 0 && contacts > 0 && plunges > 0, "every kind of emitter");
  require(waterfall_spray_emitters(falls, cliff, 3).size() == 3, "the cap keeps the strongest");
  rejects([&] { auto other = cliff; other.heights.pop_back(); (void)waterfall_spray_emitters(falls, other); }, "a mismatched terrain is rejected");
  require(waterfall_spray_emitters(compute_runoff_field(f), f).empty(), "no cliff stream, no spray");
  HeightField holed = f; holed.heights[10 * f.width + 20] = std::numeric_limits<float>::quiet_NaN();
  (void)compute_runoff_field(holed);
  rejects([&] { auto bad = f; bad.heights.pop_back(); (void)compute_runoff_field(bad); }, "inconsistent terrain rejected");
  // Freezing level and the state's snow/runoff terms.
  require(near(freezing_level_meters(13, 500), 500 + 2000) && freezing_level_meters(-4, 500) == 500, "the 0 C level sits 2 km above a 13-degree ground; at the ground when it freezes");
  WeatherConfig warm_rain; warm_rain.clouds = CloudType::Nimbostratus; warm_rain.temperature_celsius = 12; warm_rain.reference_elevation_meters = 0;
  CameraBasis camera; camera.eye = {0, 100, 0};
  const auto wet = WeatherSimulation(warm_rain).state(camera, 0);
  require(wet.runoff > 0.5 && wet.freezing_level_meters > 800 && wet.freezing_level_meters < 2500 && !wet.snow, "warm rain runs off below a freezing level well above the ground");
  WeatherConfig cold_rain = warm_rain; cold_rain.temperature_celsius = -2;
  WeatherSimulation winter(cold_rain);
  for (int i = 0; i < 600; ++i) winter.advance(0.1);
  const auto snowy = winter.state(camera, 0);
  require(snowy.snow && snowy.freezing_level_meters <= 0.0 && snowy.snow_cover > 0.2, "snow at the ground, a freezing level at the surface and a growing snow cover");
  WeatherConfig dry = warm_rain; dry.clouds = CloudType::Cumulus;
  require(WeatherSimulation(dry).state(camera, 0).runoff == 0.0 && WeatherSimulation(dry).state(camera, 0).snow_cover == 0.0, "nothing runs or settles without rain");
}

compute::GridCell cell(int x, int y, double z) {
  compute::GridCell c{};
  c.x = x; c.y = y; c.elevation_meters = z; c.fuel_load = 0.5; c.fuel_moisture = 0.2;
  return c;
}
void test_height_field_from_mesh() {
  std::vector<compute::GridCell> cells;
  for (int y = 0; y < 9; ++y) for (int x = 0; x < 9; ++x) cells.push_back(cell(x, y, 10.0 * x));
  const auto mesh = terrain::build_terrain_mesh(compute::Grid(cells), 30);
  const auto field = sample_height_field(mesh, 128);
  require(field.width == 9 && field.height == 9 && near(field.spacing, 30) && field.reference_meters == 0, "one texel per lattice vertex when it fits");
  require(near(field.sample(field.x0 + 60, field.z0 + 30), 20, 1e-6), "heights follow the mesh (x east)");
  const auto coarse = sample_height_field(mesh, 4);
  require(coarse.width <= 4 && coarse.spacing == 120 && std::isfinite(coarse.heights[0]), "the texel budget coarsens the field");
  auto holed = cells; holed[40].elevation_meters = std::nullopt;
  const auto with_hole = sample_height_field(terrain::build_terrain_mesh(compute::Grid(holed), 30), 128);
  require(!std::isfinite(with_hole.heights[4 * 9 + 4]) && near(with_hole.sample(with_hole.x0 + 120, with_hole.z0 - 120), with_hole.reference_meters, 1e-6),
          "holes are NaN texels that sample as the valley floor");
  rejects([&] { (void)sample_height_field(terrain::TerrainMesh{}, 128); }, "empty mesh rejected");
  rejects([&] { (void)sample_height_field(mesh, 1); }, "resolution below two rejected");
}

// ---------------------------------------------------------------------------
void test_atmosphere_block_and_twins() {
  const auto neutral = pack_atmosphere(neutral_atmosphere());
  require(neutral.size() * sizeof(float) == kAtmosphereBytes && neutral[15] == 0, "neutral block packs with enabled = 0");
  WeatherConfig config;
  config.reference_elevation_meters = 50; config.clouds = CloudType::Clear;
  WeatherSimulation simulation(config);
  CameraBasis camera;
  camera.eye = {100, 300, 400}; camera.forward = {0, -0.5, -1}; camera.right = {1, 0, 0}; camera.up = {0, 1, -0.5};
  camera.tan_half_fov_x = 0.92; camera.tan_half_fov_y = 0.577;
  auto state = simulation.state(camera, 50);
  require(state.enabled && state.cloud.coverage == 0 && state.precipitation == 0 && !state.snow && state.lightning == 0, "clear state");
  const auto packed = pack_atmosphere(state);
  require(packed[15] == 1.0f && near(packed[12], 100) && near(packed[13], 300) && near(packed[14], 400), "eye and enabled flag");
  require(near(packed[0], camera.forward.normalized().x, 1e-6) && near(packed[1], camera.forward.normalized().y, 1e-6) &&
          near(packed[3], 0.92, 1e-6) && near(packed[7], 0.577, 1e-6), "camera basis and field of view");
  require(near(packed[16], state.sun.light_direction.x, 1e-6) && near(packed[19], state.sun.light_intensity, 1e-6) &&
          near(packed[23], state.sun.ambient_intensity, 1e-6) && near(packed[27], state.sun.daylight, 1e-6), "light terms");
  require(near(packed[31], state.sun.altitude_degrees, 1e-4) && near(packed[39], state.visibility_meters, 1e-2), "altitude and visibility");
  require(near(packed[40], state.cloud.base_meters, 1e-3) && near(packed[43], state.cloud.density, 1e-6) &&
          near(packed[47], static_cast<double>(static_cast<int>(CloudType::Clear))), "cloud layer");
  require(near(packed[56], state.wind.travel.x, 1e-6) && near(packed[59], state.time_seconds, 1e-6), "wind row");
  require(near(packed[64], state.sun.direction.x, 1e-6) && near(packed[65], state.sun.direction.y, 1e-6), "true sun direction row");
  require(near(packed[72], state.drop_diameter_mm, 1e-6) && near(packed[75], state.convection, 1e-6) && near(packed[76], state.cloud_time_seconds, 1e-3) &&
          near(packed[77], state.flood_rise_meters, 1e-6) && near(packed[78], state.moon_diameter_degrees, 1e-6), "rain and lifecycle rows");
  require(near(packed[80], state.sun.moon_direction.normalized().x, 1e-6) && near(packed[83], state.sun.moon_illumination, 1e-6) &&
          near(packed[87], state.sun.cloud_light_intensity, 1e-6) && near(packed[88], state.sun.cloud_light_direction.normalized().x, 1e-6) &&
          near(packed[91], state.sun.moon_intensity, 1e-6) && near(packed[92], state.sun.horizon_away_color.x, 1e-6), "moon, cloud light and far horizon rows");
  require(near(packed[11], state.sun.moon_altitude_degrees, 1e-4), "moon altitude rides in camUp.w");

  // CPU twins: lighting, haze and sky.
  const math::Vec3 albedo{0.5, 0.4, 0.3};
  const auto lit = atmosphere_shade(albedo, {0, 1, 0}, state);
  const double diffuse = std::max(state.sun.light_direction.normalized().y, 0.0);
  const double expected_r = albedo.x * (state.sun.ambient_color.x * state.sun.ambient_intensity + state.sun.light_color.x * state.sun.light_intensity * diffuse);
  require(near(lit.x, expected_r, 1e-9), "shade = albedo (ambient + light diffuse)");
  auto wet = state; wet.wetness = 1;
  require(atmosphere_shade(albedo, {0, 1, 0}, wet).x < lit.x * 0.8, "wet ground is darker");
  const auto hazed = atmosphere_haze({1, 0, 0}, camera.eye + math::Vec3{0, 0, -state.visibility_meters}, state);
  require(near(hazed.x, 1 * std::exp(-1.0) + state.fog_color.x * (1 - std::exp(-1.0)), 1e-9), "one visibility length leaves e^-1 of the surface color");
  require(atmosphere_haze({1, 0, 0}, camera.eye, state).x == 1, "no haze at zero distance");
  const auto up = atmosphere_sky_color({0, 1, 0}, state), side = atmosphere_sky_color({1, 0.02, 0}, state);
  require(up.z > up.x && side.x + side.y + side.z > up.x + up.y + up.z, "a blue zenith over a brighter horizon");
  require(atmosphere_sky_color(state.sun.direction, state).x >= 0.99, "the sun disc saturates");
  auto night_config = config; night_config.daylight.start_hour = 0;
  const auto night = WeatherSimulation(night_config).state(camera, 50);
  const auto night_sky = atmosphere_sky_color({0, 1, 0}, night);
  require(night_sky.x + night_sky.y + night_sky.z < 0.15 && night.sun.star_brightness == 1, "the night sky is dark and starry");
  require(atmosphere_shade(albedo, {0, 1, 0}, night).x < lit.x * 0.2, "the ground is far darker at night");
  rejects([&] { auto bad = state; bad.visibility_meters = 0; (void)pack_atmosphere(bad); }, "zero visibility rejected");
  rejects([&] { auto bad = state; bad.camera.forward = {0, 0, 0}; (void)pack_atmosphere(bad); }, "degenerate camera rejected");
  rejects([&] { auto bad = state; bad.cloud.top_meters = bad.cloud.base_meters - 1; (void)pack_atmosphere(bad); }, "inverted layer rejected");
  rejects([&] { auto bad = state; bad.precipitation = 2; (void)pack_atmosphere(bad); }, "precipitation out of range rejected");
  rejects([&] { (void)atmosphere_shade(albedo, {0, 0, 0}, state); }, "zero normal rejected");
  rejects([&] { (void)atmosphere_sky_color({0, 0, 0}, state); }, "zero direction rejected");

  // Simulation: drift integrates the wind, the clock advances, the field goes stale with the wind.
  WeatherConfig windy; windy.wind.direction_degrees = 90; windy.wind.speed_min_mps = windy.wind.speed_max_mps = 5; windy.wind.swing_degrees = 0;
  WeatherSimulation sim(windy);
  for (int i = 0; i < 100; ++i) sim.advance(0.1);
  require(near(sim.time(), 10, 1e-9) && near(sim.drift().x, 50, 1e-6) && near(sim.drift().y, 0, 1e-9), "ten seconds of 5 m/s easterly drift the clouds 50 m east");
  require(sim.field_stale(sim.wind()), "no field yet");
  const auto ridge = ridge_field(900);
  const auto field = compute_orographic_field(ridge, sim.wind(), sim.layer(), windy.stability_hz);
  sim.mark_field(sim.wind(), field);
  require(!sim.field_stale(sim.wind()) && sim.field() && sim.field()->width == ridge.width, "a fresh field is current");
  auto veered = sim.wind(); veered.direction_degrees += 5;
  require(sim.field_stale(veered), "a 5-degree veer invalidates the field");
  auto gust = sim.wind(); gust.speed_mps += 1;
  require(sim.field_stale(gust), "a 1 m/s gust invalidates the field");
  const auto mapped = sim.state(camera, 50);
  require(near(mapped.field_x0, ridge.x0) && near(mapped.field_width_meters, ridge.spacing * (ridge.width - 1)), "the state maps the field extent");
  sim.set_time(1000);
  require(near(sim.time(), 1000), "the clock can jump");
  rejects([&] { sim.advance(-1); }, "negative step rejected");
  rejects([&] { sim.set_time(-1); }, "negative time rejected");
  // Snow and wetness follow the temperature at the ground altitude.
  WeatherConfig cold; cold.clouds = CloudType::Nimbostratus; cold.temperature_celsius = 7; cold.reference_elevation_meters = 0;
  WeatherSimulation winter(cold);
  require(!winter.state(camera, 0).snow && winter.state(camera, 0).wetness > 0, "rain at the valley floor");
  require(winter.state(camera, 0).temperature_celsius < 7, "the rain cools the surface air below the reference");
  require(winter.state(camera, 900).snow && winter.state(camera, 900).wetness == 0, "snow 900 m higher");
}

// ---------------------------------------------------------------------------
void test_thunderstorm_sea_state_and_lightning() {
  // Storm wind: wider thresholds, wilder veer, quicker gusts, never out of bounds.
  WindConfig breeze; breeze.speed_min_mps = 2; breeze.speed_max_mps = 6; breeze.swing_degrees = 20; breeze.period_seconds = 12;
  const auto squall = storm_wind(breeze, 1.0);
  require(near(squall.speed_max_mps, 18) && near(squall.speed_min_mps, 2) && near(squall.swing_degrees, 45) && squall.period_seconds < 8,
          "a full thunderstorm adds 12 m/s of gusts, 25 degrees of veer and halves the gust period");
  require(storm_wind(breeze, 0.0).speed_max_mps == 6, "no storm, no change");
  WindConfig gale = breeze; gale.speed_max_mps = 55; gale.swing_degrees = 80;
  const auto capped = storm_wind(gale, 1.0);
  require(capped.speed_max_mps == 60 && capped.swing_degrees == 90, "storm wind stays within the configuration limits");
  rejects([&] { (void)storm_wind(breeze, 1.5); }, "storm above one rejected");
  // Rain bands: a storm's showers pulse between about a third and the full rate; a steady rain does not.
  double lo = 1, hi = 0;
  for (int i = 0; i < 2000; ++i) { const double b = storm_rain_band(i * 0.25, 1.0, 5); lo = std::min(lo, b); hi = std::max(hi, b); }
  require(lo < 0.5 && hi > 0.95 && lo >= 0.35 - 1e-9, "squall bands swing between a third and the full rate");
  require(storm_rain_band(77.0, 0.0, 5) == 1.0, "no storm, steady rain");
  // Surface temperature: warmer by day, cooler at night, cooled by rain and the downdraft.
  require(surface_temperature(20, 1, 0, 0) > 20 && surface_temperature(20, 0, 0, 0) < 20, "diurnal swing");
  require(near(surface_temperature(20, 0.5, 1, 1), 20 - 6 - 2), "a full downpour and downdraft cool by eight degrees");
  require(near(relative_humidity_from_dew_point(20, 20), 1.0, 1e-9) && relative_humidity_from_dew_point(30, 10) < 0.35 &&
          relative_humidity_from_dew_point(30, 10) > 0.25, "Magnus humidity: saturated at the dew point, about 29 % for a 20-degree spread");
  require(near(dew_point_from_humidity(30, relative_humidity_from_dew_point(30, 10)), 10, 1e-6), "dew point inverts the humidity");
  require(cloud_type_for_sample({26, 0.35, 0}) == CloudType::Cumulus && cloud_type_for_sample({18, 0.95, 14}) == CloudType::Cumulonimbus &&
          cloud_type_for_sample({15, 0.9, 5}) == CloudType::Nimbostratus && cloud_type_for_sample({12, 0.9, 0.5}) == CloudType::Stratus &&
          cloud_type_for_sample({12, 0.9, 0}) == CloudType::Stratocumulus, "replay samples imply their cloud form");

  // A thunderstorm in the simulation: heavy rain in bands, squalls, cooled air, negative fire weather, strikes.
  WeatherConfig storm; storm.clouds = CloudType::Cumulonimbus; storm.temperature_celsius = 28; storm.reference_elevation_meters = 100;
  WeatherSimulation sim(storm);
  CameraBasis camera; camera.eye = {0, 300, 0};
  double max_rain = 0, min_rain = 1, min_temperature = 100; bool saw_strike = false, saw_bolt_light = false;
  for (int i = 0; i < 1200; ++i) {
    sim.set_time(i * 0.25);
    const auto state = sim.state(camera, 100);
    max_rain = std::max(max_rain, state.precipitation_mm_h); min_rain = std::min(min_rain, state.precipitation_mm_h);
    min_temperature = std::min(min_temperature, state.temperature_celsius);
    require(state.fire_weather < 0, "a thunderstorm always cools the fire weather");
    require(state.wind.speed_mps <= sim.effective_wind().speed_max_mps + 1e-9, "storm gusts stay inside the storm thresholds");
    if (state.strike.active) { saw_strike = true; if (state.strike.intensity > 0.5) saw_bolt_light = true;
      require(state.strike.u >= 0 && state.strike.u <= 1 && state.strike.v >= 0 && state.strike.v <= 1, "strikes land inside the footprint"); }
  }
  require(max_rain > 20 && min_rain < 12, "torrential showers arrive in bands");
  require(min_temperature < 28 - 6, "the downpour cools the surface air by more than six degrees");
  require(saw_strike && saw_bolt_light, "strikes occur during the storm");
  require(sim.effective_wind().speed_max_mps > storm.wind.speed_max_mps + 10, "the storm widens the wind thresholds");
  WeatherConfig fair = storm; fair.clouds = CloudType::Cumulus;
  WeatherSimulation calm(fair);
  require(!calm.state(camera, 100).strike.active && calm.state(camera, 100).fire_weather > 0, "fair hot weather: no strikes, fire weather positive");
  WeatherConfig hot = fair; hot.temperature_celsius = 40; hot.dew_point_celsius = 0;
  require(WeatherSimulation(hot).state(camera, 100).fire_weather > 0.08, "hot dry air drives the fire");

  // Convection feeds cumulus by day and dies at night; rain accumulates into puddles and a
  // flood rise on the accelerated clock and drains afterwards; rainbows need sun and rain.
  WeatherConfig fair_day = fair; fair_day.daylight.start_hour = 13;
  WeatherConfig fair_night = fair; fair_night.daylight.start_hour = 1;
  WeatherConfig fair_cold = fair_day; fair_cold.temperature_celsius = 3;
  require(WeatherSimulation(fair_day).state(camera, 100).convection > 0.9 && WeatherSimulation(fair_night).state(camera, 100).convection < 0.15 &&
          WeatherSimulation(fair_cold).state(camera, 100).convection < 0.15, "updrafts feed cumulus on a warm afternoon, not at night or over cold ground");
  require(WeatherSimulation(storm).state(camera, 100).drop_diameter_mm == 3.0 && WeatherSimulation(fair).state(camera, 100).drop_diameter_mm == 1.0,
          "thunderstorm drops are big; a dry form keeps the neutral size");
  WeatherSimulation deluge(storm);
  require(deluge.clock_acceleration() == 60.0 && deluge.rainfall_mm() == 0.0, "the default ten-second hour runs the weather clock sixty times faster");
  for (int i = 0; i < 1200; ++i) deluge.advance(0.1);  // two minutes of a storm = two hours of weather time
  const auto flooded = deluge.state(camera, 100);
  require(deluge.rainfall_mm() > 15 && flooded.ponding > 0.5 && flooded.flood_rise_meters > 0.3 && flooded.flood_rise_meters <= 3.0,
          "a storm leaves tens of millimetres on the ground, puddles and a rise of standing water");
  require(near(flooded.cloud_time_seconds, 120.0 * 60.0, 1e-6), "the cell lifecycle runs on the accelerated clock");
  WeatherConfig clear = storm; clear.clouds = CloudType::Clear;
  deluge.set_config(clear);
  for (int i = 0; i < 10800; ++i) deluge.advance(0.1);  // eighteen dry hours of weather time
  require(deluge.rainfall_mm() < 5 && deluge.state(camera, 100).flood_rise_meters < 0.1, "the water drains once the rain stops");
  deluge.reset();
  require(deluge.rainfall_mm() == 0, "reset empties the ground");
  WeatherSimulation wet_ground(fair_day);
  wet_ground.set_rainfall_mm(40.0);
  require(wet_ground.state(camera, 100).runoff > 0.7 && wet_ground.state(camera, 100).ponding == 1.0 && wet_ground.state(camera, 100).rainbow == 0.0,
          "ground already wet from earlier rain runs off under a clear sky");
  rejects([&] { wet_ground.set_rainfall_mm(-1); }, "negative rainfall rejected");
  WeatherConfig shower = fair_day; shower.clouds = CloudType::Cumulonimbus; shower.coverage = 0.4;
  require(WeatherSimulation(shower).state(camera, 100).rainbow > 0.2, "sun behind the observer and rain in front make a rainbow");
  WeatherConfig overcast = fair_day; overcast.clouds = CloudType::Nimbostratus;
  require(WeatherSimulation(overcast).state(camera, 100).rainbow == 0.0, "a full overcast hides the sun: no rainbow");
  WeatherConfig night_shower = shower; night_shower.daylight.start_hour = 1;
  require(WeatherSimulation(night_shower).state(camera, 100).rainbow == 0.0, "no rainbow by moonlight");
  require(WeatherSimulation(fair_day).state(camera, 100).rainbow == 0.0, "no rain, no rainbow");
  rejects([&] { deluge.advance(-1); }, "negative step rejected");

  // Sea state follows the wind.
  WaterStyle base; base.wave_height_meters = 0.6; base.wave_length_meters = 24; base.wave_speed_mps = 3;
  WeatherConfig calm_config = fair; calm_config.wind.speed_min_mps = calm_config.wind.speed_max_mps = 0; calm_config.wind.swing_degrees = 0;
  const auto flat = WeatherSimulation(calm_config).water_for(base);
  require(near(flat.wave_height_meters, 0.6 * 0.35, 1e-9) && flat.wave_length_meters < 24 && flat.wave_speed_mps < 3, "a calm leaves a third of the authored waves");
  WeatherConfig gale_config = fair; gale_config.wind.speed_min_mps = gale_config.wind.speed_max_mps = 20; gale_config.wind.swing_degrees = 0;
  const auto rough = WeatherSimulation(gale_config).water_for(base);
  require(rough.wave_height_meters > 2.0 && rough.wave_height_meters <= 5.0 && rough.wave_length_meters > 24 && rough.wave_speed_mps > 3 &&
          rough.foam_depth_meters > base.foam_depth_meters, "a gale raises a high, long, fast sea with more foam");
  const auto stormy = sim.water_for(base);
  require(stormy.wave_height_meters > flat.wave_height_meters && stormy.choppiness >= 0.3, "the thunderstorm raises the sea");
  require(near(rough.wave_direction_degrees, gale_config.wind.direction_degrees, 1e-9), "a steady wind sets the wave direction");
  (void)pack_water_style(rough, {}, 0);

  // Lightning bolts: a seeded channel from the cloud base to the ground with branches.
  LightningStrike strike = lightning_strike_at(132.6, 1.0, 0);
  require(strike.active && strike.intensity > 0.5, "a known strike time");
  const auto bolt = build_lightning_bolt(strike, {500, 1200, -300}, 150, {900, 400, 200});
  require(bolt.segments > 20 && bolt.vertices.size() == bolt.segments * 8 && bolt.indices.size() == bolt.segments * 12, "eight vertices and twelve indices per segment");
  double top = -1e9, bottom = 1e9;
  for (const auto& v : bolt.vertices) { top = std::max(top, static_cast<double>(v.position[1])); bottom = std::min(bottom, static_cast<double>(v.position[1])); }
  require(near(top, 1200, 30) && near(bottom, 150, 30), "the channel spans from the cloud base to the ground (quad halo included)");
  const auto again = build_lightning_bolt(strike, {500, 1200, -300}, 150, {900, 400, 200});
  require(again.vertices.size() == bolt.vertices.size() && again.vertices[5].position[0] == bolt.vertices[5].position[0], "bolts are deterministic per strike");
  auto other = strike; other.seed += 1;
  require(build_lightning_bolt(other, {500, 1200, -300}, 150, {900, 400, 200}).vertices[5].position[0] != bolt.vertices[5].position[0], "another strike, another channel");
  require(build_lightning_bolt(strike, {0, 100, 0}, 100, {0, 0, 0}).segments == 0, "no channel without height");
  rejects([&] { (void)build_lightning_bolt(strike, {0, std::numeric_limits<double>::infinity(), 0}, 0, {0, 0, 0}); }, "non-finite top rejected");
  // Strike schedule: deterministic, bounded, only in storms.
  require(!lightning_strike_at(132.6, 0.0, 0).active && lightning_strike_at(132.6, 1.0, 0).slot == 44, "strike slots follow the 3-second schedule");
}

// ---------------------------------------------------------------------------
void test_layouts_and_gpu_resources() {
  for (const auto& layout : {terrain_shader_layout(), wildfire_shader_layout()}) {
    const auto runoff = std::find_if(layout.bindings.begin(), layout.bindings.end(), [](const ShaderBinding& b) { return b.name == "RunoffField"; });
    require(runoff != layout.bindings.end() && runoff->slot == kRunoffFieldSlot && runoff->stages == kFragmentStage, "the ground shaders sample the runoff field");
  }
  for (const auto& layout : {terrain_shader_layout(), wildfire_shader_layout(), water_shader_layout(), vegetation_shader_layout(),
                            fire_shader_layout(), sky_shader_layout(), precipitation_shader_layout(), spray_shader_layout()}) {
    validate_shader_layout(layout);
    const auto block = std::find_if(layout.bindings.begin(), layout.bindings.end(), [](const ShaderBinding& b) { return b.name == "Atmosphere"; });
    const auto field = std::find_if(layout.bindings.begin(), layout.bindings.end(), [](const ShaderBinding& b) { return b.name == "OrographicField"; });
    require(block != layout.bindings.end() && block->slot == kAtmosphereSlot && block->kind == ShaderResourceKind::UniformBuffer &&
            field != layout.bindings.end() && field->slot == kOrographicFieldSlot && field->kind == ShaderResourceKind::SampledTexture &&
            field->stages == kFragmentStage, "every world shader binds the atmosphere block and the orographic field");
  }
  require(shader_program_layout(ShaderProgramId::Sky).attributes.size() == 1 && shader_program_layout(ShaderProgramId::Precipitation).vertex_stride_bytes == 12 &&
          shader_program_layout(ShaderProgramId::Lightning).vertex_stride_bytes == 16 && shader_program_layout(ShaderProgramId::Lightning).bindings.size() == 2 &&
          shader_program_layout(ShaderProgramId::Spray).vertex_stride_bytes == 64 && shader_program_layout(ShaderProgramId::Spray).attributes.size() == 6,
          "registry knows the sky, precipitation, lightning and spray identities");
  ValidatingRenderDevice device;
  {
    AtmosphereGpu atmosphere(device);
    require(device.live_buffer_count() == 1 && device.live_texture_count() == 2 && atmosphere.field_width() == 1 && atmosphere.runoff_width() == 1,
            "neutral block, one-texel field and one-texel runoff");
    WeatherConfig config; WeatherSimulation sim(config);
    CameraBasis camera; camera.eye = {0, 100, 0};
    atmosphere.update(sim.state(camera, 0));
    const auto ridge = ridge_field(900);
    atmosphere.update_field(compute_orographic_field(ridge, sim.wind(), sim.layer(), 0.01));
    require(device.live_texture_count() == 2 && atmosphere.field_width() == ridge.width, "the field texture is replaced, not leaked");
    atmosphere.update_runoff(compute_runoff_field(ridge));
    require(device.live_texture_count() == 2 && atmosphere.runoff_width() == ridge.width, "the runoff texture is replaced, not leaked");
    SkyRenderer sky(device);
    PrecipitationRenderer rain(device, 500);
    LightningRenderer bolts(device, 64);
    SprayRenderer spray(device, 8, 4);
    rejects([&] { (void)SprayRenderer(device, 0, 4); }, "zero spray particles rejected");
    rejects([&] { (void)SprayRenderer(device, 8, 0); }, "zero spray emitters rejected");
    std::vector<SprayEmitter> emitters(6);
    for (std::size_t i = 0; i < emitters.size(); ++i) {
      emitters[i].position = {10.0 * i, 100.0, 0.0}; emitters[i].downhill = {1, 0, 0}; emitters[i].face = {0.7, 0.714, 0};
      emitters[i].kind = static_cast<SprayKind>(i % 3); emitters[i].strength = 0.5; emitters[i].drop_meters = 10.0 * (i % 3); emitters[i].width_meters = 10;
    }
    spray.set_emitters(emitters);
    require(spray.emitter_count() == 4 && spray.particles_per_emitter() == 8, "emitters beyond the capacity are dropped, strongest first");
    auto bad = emitters; bad[0].strength = 2.0;
    rejects([&] { spray.set_emitters(bad); }, "spray strength above one rejected");
    rejects([&] { (void)LightningRenderer(device, 0); }, "zero lightning capacity rejected");
    require(rain.capacity() == 500, "particle capacity");
    rejects([&] { (void)PrecipitationRenderer(device, 0); }, "zero particles rejected");
    rejects_logic([&] { sky.draw(atmosphere.binding()); }, "sky outside a pass rejected");
    std::array<float, 20> camera_block{};
    RenderResourceOwner owner(device);
    const auto camera_buffer = owner.create_buffer({BufferUsage::Uniform, sizeof(camera_block)}, std::as_bytes(std::span(camera_block)));
    {
      RenderPassScope pass(device, {{64, 64}});
      const auto before = device.draw_count();
      sky.draw(atmosphere.binding());
      require(device.draw_count() == before + 1, "sky draws one fullscreen triangle");
      require(rain.draw(camera_buffer, atmosphere.binding(), 0).draws == 0, "zero rate draws nothing");
      const auto stats = rain.draw(camera_buffer, atmosphere.binding(), 0.5);
      require(stats.draws == 1 && stats.particles == static_cast<std::size_t>(std::ceil(std::pow(0.5, 0.6) * 500)), "half rate draws rate^0.6 of the particles");
      require(rain.draw(camera_buffer, atmosphere.binding(), 1).particles == 500, "full rate draws every particle");
      rejects([&] { (void)rain.draw(camera_buffer, atmosphere.binding(), 1.5); }, "rate above one rejected");
      require(spray.draw(camera_buffer, atmosphere.binding(), 0.0).draws == 0, "dry ground sprays nothing");
      const auto spray_stats = spray.draw(camera_buffer, atmosphere.binding(), 0.8);
      require(spray_stats.draws == 1 && spray_stats.emitters == 4 && spray_stats.particles == 32, "running water draws every emitter's particles");
      rejects([&] { (void)spray.draw(camera_buffer, atmosphere.binding(), 1.5); }, "runoff above one rejected");
      rejects_logic([&] { spray.set_emitters(emitters); }, "emitters cannot change inside a pass");
      rejects([&] { (void)device.create_pipeline({ShaderProgramId::Spray, spray_shader_layout(), true, false, false}); }, "opaque spray rejected");
      const auto strike = lightning_strike_at(132.6, 1.0, 0);
      const auto bolt_stats = bolts.draw(camera_buffer, atmosphere.binding(), strike, {500, 1200, -300}, 150, {900, 400, 200});
      require(bolt_stats.draws == 1 && bolt_stats.segments > 0 && bolt_stats.segments <= 64, "a strike draws its channel, truncated to the capacity");
      require(bolts.draw(camera_buffer, atmosphere.binding(), lightning_strike_at(132.6, 0.0, 0), {500, 1200, -300}, 150, {900, 400, 200}).draws == 0,
              "no storm, no channel");
      rejects([&] { (void)device.create_pipeline({ShaderProgramId::Lightning, lightning_shader_layout(), true, false, false}); }, "opaque lightning rejected");
      // Pipelines: the sky must not blend or write depth; precipitation must blend.
      rejects([&] { (void)device.create_pipeline({ShaderProgramId::Sky, sky_shader_layout(), true, true, false}); }, "blended sky rejected");
      rejects([&] { (void)device.create_pipeline({ShaderProgramId::Sky, sky_shader_layout(), true, false, true}); }, "depth-writing sky rejected");
      rejects([&] { (void)device.create_pipeline({ShaderProgramId::Precipitation, precipitation_shader_layout(), true, false, false}); }, "opaque precipitation rejected");
      // A short atmosphere buffer is rejected on a terrain draw like the other style blocks.
      const std::array<float, 4> tiny{};
      const auto short_block = owner.create_buffer({BufferUsage::Uniform, sizeof(tiny)}, std::as_bytes(std::span(tiny)));
      const auto terrain = owner.create_pipeline({ShaderProgramId::Terrain, terrain_shader_layout()});
      const std::array<TerrainGpuVertex, 3> vertices{{{{0, 0, 0}, {0, 1, 0}, 0, {0, 0, 0}}, {{1, 0, 0}, {0, 1, 0}, 0, {0, 0, 0}}, {{0, 0, 1}, {0, 1, 0}, 0, {0, 0, 0}}}};
      const std::array<std::uint32_t, 3> indices{0, 1, 2};
      const auto vb = owner.create_buffer({BufferUsage::Vertex, sizeof(vertices)}, std::as_bytes(std::span(vertices)));
      const auto ib = owner.create_buffer({BufferUsage::Index, sizeof(indices)}, std::as_bytes(std::span(indices)));
      const auto style_packed = pack_terrain_style(TerrainStyle{});
      const auto style = owner.create_buffer({BufferUsage::Uniform, sizeof(style_packed)}, std::as_bytes(std::span(style_packed)));
      DrawIndexed draw{terrain, vb, ib, 0, 3, {{0, camera_buffer}, {kTerrainStyleSlot, style}, {kAtmosphereSlot, atmosphere.binding().block}}, {},
                       {{kOrographicFieldSlot, atmosphere.binding().field}, {kRunoffFieldSlot, atmosphere.binding().runoff}}};
      device.draw_indexed(draw);
      auto short_draw = draw; short_draw.uniform_buffers[2].buffer = short_block;
      rejects([&] { device.draw_indexed(short_draw); }, "short atmosphere block rejected");
      auto no_field = draw; no_field.textures.clear();
      rejects([&] { device.draw_indexed(no_field); }, "missing orographic field rejected");
      auto no_runoff = draw; no_runoff.textures.pop_back();
      rejects([&] { device.draw_indexed(no_runoff); }, "missing runoff field rejected");
    }
  }
  require(device.live_buffer_count() == 0 && device.live_texture_count() == 0 && device.live_pipeline_count() == 0, "everything released");
}
}  // namespace

int main() {
  try {
    test_wind_between_thresholds();
    test_sun_and_twilight();
    test_cloud_profiles_and_layers();
    test_orographic_lift();
    test_height_field_from_mesh();
    test_runoff_and_snow_line();
    test_atmosphere_block_and_twins();
    test_thunderstorm_sea_state_and_lightning();
    test_layouts_and_gpu_resources();
    std::cout << "Weather: gusting wind between thresholds, solar twilight, cloud forms, orographic lift/foehn/blocking, "
                 "thunderstorms, sea state, lightning, cascades and spray, atmosphere block and GPU resources passed\n";
  } catch (const std::exception& error) {
    std::cerr << "weather tests failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
