#include <terraflux/graphics/weather.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegrees = kPi / 180.0;

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool unit_color(math::Vec3 c) { return finite(c) && c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x <= 1 && c.y <= 1 && c.z <= 1; }
double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double smoothstep(double e0, double e1, double x) {
  const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3 - 2 * t);
}
math::Vec3 mix(math::Vec3 a, math::Vec3 b, double t) { return a * (1 - t) + b * t; }
double wrap_degrees(double d) {
  d = std::fmod(d, 360.0);
  return d < 0 ? d + 360.0 : d;
}
math::Vec2 compass_vector(double degrees) {
  const double r = degrees * kDegrees;
  return {std::sin(r), -std::cos(r)};  // east +x, north -z
}
float f(double v) {
  const auto value = static_cast<float>(v);
  if (!std::isfinite(value)) throw std::invalid_argument("atmosphere value is not representable in float");
  return value;
}

std::uint32_t hash_u32(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
double hash01(std::int64_t i, std::uint32_t seed, std::uint32_t channel) {
  const auto h = hash_u32(static_cast<std::uint32_t>(i) * 0x9E3779B1U ^ hash_u32(seed * 0x85EBCA77U + channel * 0xC2B2AE3DU + 0x27D4EB2FU));
  return static_cast<double>(h & 0xFFFFFFu) / 16777216.0;
}
double value_noise_1d(double t, std::uint32_t seed, std::uint32_t channel) {
  const double ft = std::floor(t);
  const auto i = static_cast<std::int64_t>(ft);
  const double u = t - ft, s = u * u * (3 - 2 * u);
  return hash01(i, seed, channel) * (1 - s) + hash01(i + 1, seed, channel) * s;
}

// Piecewise-linear ramps over the sun altitude (degrees) for the sky, light
// and ambient terms. Keys are sorted ascending.
struct AltitudeKey { double altitude; math::Vec3 color; double scalar; };
template <std::size_t N>
std::pair<math::Vec3, double> ramp(const std::array<AltitudeKey, N>& keys, double altitude) {
  if (altitude <= keys.front().altitude) return {keys.front().color, keys.front().scalar};
  if (altitude >= keys.back().altitude) return {keys.back().color, keys.back().scalar};
  for (std::size_t i = 1; i < N; ++i)
    if (altitude <= keys[i].altitude) {
      const double t = (altitude - keys[i - 1].altitude) / (keys[i].altitude - keys[i - 1].altitude);
      return {mix(keys[i - 1].color, keys[i].color, t), keys[i - 1].scalar * (1 - t) + keys[i].scalar * t};
    }
  return {keys.back().color, keys.back().scalar};
}
// zenith color, daylight
constexpr std::array<AltitudeKey, 9> kZenith{{
    {-18, {0.010, 0.014, 0.030}, 0.00}, {-12, {0.018, 0.026, 0.060}, 0.02}, {-6, {0.045, 0.065, 0.160}, 0.08},
    {-3, {0.090, 0.120, 0.290}, 0.20},  {0, {0.150, 0.210, 0.440}, 0.40},   {3, {0.210, 0.310, 0.590}, 0.60},
    {10, {0.270, 0.440, 0.780}, 0.90},  {30, {0.300, 0.480, 0.800}, 1.00},  {90, {0.300, 0.480, 0.800}, 1.00}}};
// horizon color, star brightness
constexpr std::array<AltitudeKey, 9> kHorizon{{
    {-18, {0.020, 0.022, 0.040}, 1.00}, {-12, {0.070, 0.060, 0.120}, 1.00}, {-6, {0.250, 0.140, 0.240}, 0.55},
    {-3, {0.560, 0.290, 0.250}, 0.15},  {0, {0.900, 0.500, 0.280}, 0.00},   {3, {0.950, 0.660, 0.400}, 0.00},
    {10, {0.820, 0.830, 0.880}, 0.00},  {30, {0.700, 0.800, 0.920}, 0.00},  {90, {0.700, 0.800, 0.920}, 0.00}}};
// direct sun color, direct sun intensity
constexpr std::array<AltitudeKey, 7> kSun{{
    {-3, {1.00, 0.30, 0.10}, 0.00}, {0, {1.00, 0.36, 0.12}, 0.10}, {3, {1.00, 0.62, 0.30}, 0.38},
    {10, {1.00, 0.85, 0.62}, 0.62}, {30, {1.00, 0.97, 0.90}, 0.72}, {60, {1.00, 0.99, 0.96}, 0.74},
    {90, {1.00, 1.00, 0.98}, 0.74}}};
// ambient (sky light) color, ambient intensity
constexpr std::array<AltitudeKey, 8> kAmbient{{
    {-18, {0.55, 0.65, 1.00}, 0.035}, {-12, {0.58, 0.66, 1.00}, 0.045}, {-6, {0.65, 0.62, 0.95}, 0.090},
    {-3, {0.78, 0.66, 0.90}, 0.170},  {0, {0.92, 0.78, 0.80}, 0.230},   {3, {0.92, 0.86, 0.86}, 0.260},
    {10, {0.88, 0.91, 1.00}, 0.290},  {30, {0.86, 0.90, 1.00}, 0.300}}};

constexpr std::array<CloudProfile, kCloudTypeCount> kProfiles{{
    {CloudType::Clear, "clear", 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 50000,
     "Fair and dry; strong sunshine and a cold, clear night with a large temperature swing"},
    {CloudType::Cirrus, "cirrus", 7000, 600, 0.40, 0.22, 0.35, 0.0, 0.0, 0.0, 0.0, 0.0, 40000,
     "Fair for now; high ice wisps often run ahead of a warm front and rain within a day"},
    {CloudType::Cumulus, "cumulus", 2600, 900, 0.35, 0.85, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 30000,
     "Fair-weather convection: scattered puffy clouds, dry, gusty afternoon breezes"},
    {CloudType::Stratocumulus, "stratocumulus", 800, 600, 0.70, 0.70, 0.55, 0.05, 0.3, 0.5, 0.3, 0.0, 20000,
     "Dull but mostly dry; a lumpy grey sheet with occasional light drizzle"},
    {CloudType::Stratus, "stratus", 300, 400, 0.95, 0.60, 0.12, 0.15, 0.5, 0.4, 0.0, 0.0, 5000,
     "Overcast and damp: drizzle, poor visibility, hilltops lost in fog"},
    {CloudType::Nimbostratus, "nimbostratus", 500, 3000, 1.00, 1.00, 0.25, 0.70, 6.0, 1.4, 0.0, 0.0, 4000,
     "Steady, moderate rain or snow for hours under a dark, featureless sky"},
    {CloudType::Cumulonimbus, "cumulonimbus", 1000, 8000, 0.75, 1.00, 1.0, 1.00, 25.0, 3.0, 1.0, 1.0, 6000,
     "Thunderstorms: torrential showers of big drops in squall bands, gusts, lightning and possibly hail"}}};

}  // namespace

// ---------------------------------------------------------------------------
// Wind
void validate_wind(const WindConfig& c) {
  if (!within(c.direction_degrees, 0, 360)) throw std::invalid_argument("wind direction must be 0..360 compass degrees");
  if (!within(c.speed_min_mps, 0, 60) || !within(c.speed_max_mps, 0, 60) || c.speed_max_mps < c.speed_min_mps)
    throw std::invalid_argument("wind speed thresholds must satisfy 0 <= min <= max <= 60 m/s");
  if (!within(c.swing_degrees, 0, 90)) throw std::invalid_argument("wind swing must be 0..90 degrees");
  if (!within(c.period_seconds, 1, 3600)) throw std::invalid_argument("wind period must be 1..3600 seconds");
}

double weather_noise(double t, std::uint32_t seed, std::uint32_t channel) {
  if (!std::isfinite(t)) throw std::invalid_argument("weather noise time must be finite");
  return 0.55 * value_noise_1d(t, seed, channel) + 0.30 * value_noise_1d(t * 2.1 + 7.3, seed, channel + 11) +
         0.15 * value_noise_1d(t * 4.3 + 1.9, seed, channel + 23);
}

WindSample wind_at(const WindConfig& c, double time_seconds) {
  validate_wind(c);
  if (!std::isfinite(time_seconds)) throw std::invalid_argument("wind time must be finite");
  // The noise sum rarely reaches its extremes; a contrast stretch lets the
  // speed actually touch both thresholds while staying clamped inside them.
  const double u = clamp01((weather_noise(time_seconds / c.period_seconds, c.seed, 0) - 0.5) * 1.9 + 0.5);
  const double v = clamp01((weather_noise(time_seconds / (c.period_seconds * 1.7), c.seed, 1) - 0.5) * 1.9 + 0.5);
  WindSample s;
  s.gustiness = u;
  s.speed_mps = c.speed_min_mps + (c.speed_max_mps - c.speed_min_mps) * u;
  s.direction_degrees = wrap_degrees(c.direction_degrees + c.swing_degrees * (2 * v - 1));
  s.travel = compass_vector(s.direction_degrees);
  return s;
}

// ---------------------------------------------------------------------------
// Daylight
void validate_daylight(const DaylightConfig& c) {
  if (!within(c.latitude_degrees, -90, 90)) throw std::invalid_argument("latitude must be -90..90 degrees");
  if (!within(c.day_of_year, 1, 366)) throw std::invalid_argument("day of year must be 1..366");
  if (!within(c.start_hour, 0, 24)) throw std::invalid_argument("start hour must be 0..24");
  if (!within(c.seconds_per_hour, 0, 3600)) throw std::invalid_argument("seconds per hour must be 0..3600");
  if (c.moon_phase && !within(*c.moon_phase, 0, 1)) throw std::invalid_argument("moon phase must be 0..1");
  if (!within(c.moon_diameter_degrees, 0.25, 6)) throw std::invalid_argument("moon diameter must be 0.25..6 degrees");
}

double moon_phase_for_day(double day_of_year) {
  if (!within(day_of_year, 1, 366)) throw std::invalid_argument("day of year must be 1..366");
  const double cycles = (day_of_year - 1.5) / 29.530589;
  return cycles - std::floor(cycles);
}
double moon_illumination(double phase) {
  if (!within(phase, 0, 1)) throw std::invalid_argument("moon phase must be 0..1");
  return 0.5 * (1.0 - std::cos(2 * kPi * phase));
}

double solar_declination_degrees(double day_of_year) {
  if (!within(day_of_year, 1, 366)) throw std::invalid_argument("day of year must be 1..366");
  return 23.44 * std::sin(2 * kPi * (day_of_year - 81.0) / 365.0);
}

DaylightPhase daylight_phase(double altitude_degrees) {
  if (!std::isfinite(altitude_degrees)) throw std::invalid_argument("sun altitude must be finite");
  if (altitude_degrees >= 0) return DaylightPhase::Day;
  if (altitude_degrees >= -6) return DaylightPhase::CivilTwilight;
  if (altitude_degrees >= -12) return DaylightPhase::NauticalTwilight;
  if (altitude_degrees >= -18) return DaylightPhase::AstronomicalTwilight;
  return DaylightPhase::Night;
}

const char* daylight_phase_name(DaylightPhase phase) {
  switch (phase) {
    case DaylightPhase::Day: return "day";
    case DaylightPhase::CivilTwilight: return "civil_twilight";
    case DaylightPhase::NauticalTwilight: return "nautical_twilight";
    case DaylightPhase::AstronomicalTwilight: return "astronomical_twilight";
    case DaylightPhase::Night: return "night";
  }
  throw std::invalid_argument("unknown daylight phase");
}

namespace {
struct SkyBody { double altitude_degrees, azimuth_degrees; math::Vec3 direction; };
// Altitude/azimuth of a body with a declination at a local hour angle (degrees east of the
// meridian negative), in the engine frame (x east, y up, -z north).
SkyBody body_position(double latitude_degrees, double declination_degrees, double hour_angle_degrees) {
  const double phi = latitude_degrees * kDegrees, delta = declination_degrees * kDegrees, hour_angle = hour_angle_degrees * kDegrees;
  const double sin_alt = std::clamp(std::sin(phi) * std::sin(delta) + std::cos(phi) * std::cos(delta) * std::cos(hour_angle), -1.0, 1.0);
  const double altitude = std::asin(sin_alt), cos_alt = std::cos(altitude);
  double azimuth = 0.0;
  if (cos_alt > 1e-9 && std::abs(std::cos(phi)) > 1e-9) {
    const double cos_az = std::clamp((std::sin(delta) - sin_alt * std::sin(phi)) / (cos_alt * std::cos(phi)), -1.0, 1.0);
    azimuth = std::acos(cos_az);
    if (std::sin(hour_angle) > 0) azimuth = 2 * kPi - azimuth;  // afternoon: west of the meridian
  } else if (std::abs(std::cos(phi)) <= 1e-9) {
    azimuth = hour_angle + kPi;  // at the poles the body circles the horizon
  }
  return {altitude / kDegrees, wrap_degrees(azimuth / kDegrees), {cos_alt * std::sin(azimuth), sin_alt, -cos_alt * std::cos(azimuth)}};
}
// Cloud light: the true sun keeps lighting the cloud layer from the side and from below for
// a few degrees after it has set for the ground, in deepening reds (the last rays pass a very
// long, scattering path and lose their blue and green).
constexpr std::array<AltitudeKey, 8> kCloudSun{{
    {-8, {1.00, 0.18, 0.06}, 0.00}, {-5, {1.00, 0.22, 0.07}, 0.12}, {-2, {1.00, 0.32, 0.10}, 0.40},
    {0, {1.00, 0.42, 0.15}, 0.58},  {3, {1.00, 0.60, 0.28}, 0.68},  {10, {1.00, 0.84, 0.60}, 0.74},
    {30, {1.00, 0.97, 0.90}, 0.74}, {90, {1.00, 1.00, 0.98}, 0.74}}};
// Horizon opposite the sun: by day the same haze, at sunset a cool purple band (the Belt of
// Venus over the Earth's shadow), at night dark.
constexpr std::array<AltitudeKey, 8> kHorizonAway{{
    {-18, {0.020, 0.022, 0.040}, 0}, {-12, {0.06, 0.05, 0.11}, 0}, {-6, {0.16, 0.12, 0.26}, 0},
    {-3, {0.30, 0.20, 0.38}, 0},     {0, {0.50, 0.36, 0.48}, 0},   {3, {0.62, 0.55, 0.64}, 0},
    {10, {0.74, 0.78, 0.86}, 0},     {30, {0.70, 0.80, 0.92}, 0}}};
}  // namespace

SunState sun_for_hour(const DaylightConfig& c, double hour) {
  validate_daylight(c);
  if (!std::isfinite(hour)) throw std::invalid_argument("hour must be finite");
  SunState s;
  s.hour = hour - 24.0 * std::floor(hour / 24.0);
  const double declination = solar_declination_degrees(c.day_of_year);
  const auto sun = body_position(c.latitude_degrees, declination, (s.hour - 12.0) * 15.0);
  s.altitude_degrees = sun.altitude_degrees;
  s.azimuth_degrees = sun.azimuth_degrees;
  s.direction = sun.direction;
  s.phase = daylight_phase(s.altitude_degrees);
  const auto [zenith, daylight] = ramp(kZenith, s.altitude_degrees);
  const auto [horizon, stars] = ramp(kHorizon, s.altitude_degrees);
  const auto [sun_color, sun_intensity] = ramp(kSun, s.altitude_degrees);
  const auto [ambient_color, ambient_intensity] = ramp(kAmbient, s.altitude_degrees);
  const auto [cloud_color, cloud_intensity] = ramp(kCloudSun, s.altitude_degrees);
  const auto [horizon_away, unused] = ramp(kHorizonAway, s.altitude_degrees);
  (void)unused;
  s.zenith_color = zenith; s.horizon_color = horizon; s.daylight = daylight; s.star_brightness = stars;
  s.ambient_color = ambient_color; s.ambient_intensity = ambient_intensity;
  s.horizon_away_color = horizon_away;
  // The moon: a simplified ephemeris. Its elongation from the sun follows the phase (a full
  // moon stands opposite the sun, a new moon travels with it) and its declination swings
  // against the sun's over the month; the lit fraction is (1 - cos(phase)) / 2.
  s.moon_phase = c.moon_phase.value_or(moon_phase_for_day(c.day_of_year));
  s.moon_illumination = moon_illumination(s.moon_phase);
  const auto moon = body_position(c.latitude_degrees, declination * std::cos(2 * kPi * s.moon_phase),
                                  (s.hour - 12.0) * 15.0 - s.moon_phase * 360.0);
  s.moon_altitude_degrees = moon.altitude_degrees;
  s.moon_azimuth_degrees = moon.azimuth_degrees;
  s.moon_direction = moon.direction;
  // Moonlight: up to 0.11 of the scale at full moon, falling steeply with the phase (a half
  // moon gives about a tenth of a full one), only once the sun has left civil twilight.
  s.moon_intensity = 0.11 * std::pow(s.moon_illumination, 2.2) * smoothstep(-3.0, -9.0, s.altitude_degrees) *
                     smoothstep(0.0, 6.0, s.moon_altitude_degrees);
  if (sun_intensity >= s.moon_intensity) {
    s.light_direction = s.direction; s.light_color = sun_color; s.light_intensity = sun_intensity;
  } else {
    s.light_direction = s.moon_direction; s.light_color = {0.62, 0.70, 0.95}; s.light_intensity = s.moon_intensity;
  }
  if (s.light_direction.y < 0.02) {  // never light from below the ground
    s.light_direction.y = 0.02;
    s.light_direction = s.light_direction.normalized();
  }
  // Clouds take the true sun while it still reaches them, the moon afterwards.
  if (cloud_intensity >= s.moon_intensity) {
    s.cloud_light_direction = s.direction; s.cloud_light_color = cloud_color; s.cloud_light_intensity = cloud_intensity;
  } else {
    s.cloud_light_direction = s.moon_direction; s.cloud_light_color = {0.62, 0.70, 0.95}; s.cloud_light_intensity = s.moon_intensity;
  }
  return s;
}

SunState sun_at(const DaylightConfig& c, double time_seconds) {
  validate_daylight(c);
  if (!std::isfinite(time_seconds)) throw std::invalid_argument("daylight time must be finite");
  const double hour = c.seconds_per_hour > 0 ? c.start_hour + time_seconds / c.seconds_per_hour : c.start_hour;
  return sun_for_hour(c, hour);
}

// ---------------------------------------------------------------------------
// Clouds
const CloudProfile& cloud_profile(CloudType type) {
  const auto index = static_cast<std::size_t>(type);
  if (index >= kProfiles.size()) throw std::invalid_argument("unknown cloud type");
  return kProfiles[index];
}
const char* cloud_type_name(CloudType type) { return cloud_profile(type).name; }
CloudType parse_cloud_type(const char* name) {
  if (!name) throw std::invalid_argument("cloud type name is null");
  for (const auto& p : kProfiles) if (std::strcmp(p.name, name) == 0) return p.type;
  throw std::invalid_argument(std::string("unknown cloud type: ") + name +
                              " (expected clear, cirrus, cumulus, stratocumulus, stratus, nimbostratus or cumulonimbus)");
}

double lifting_condensation_level_meters(double temperature_celsius, double dew_point_celsius) {
  if (!std::isfinite(temperature_celsius) || !std::isfinite(dew_point_celsius))
    throw std::invalid_argument("temperature and dew point must be finite");
  return std::max(0.0, 125.0 * (temperature_celsius - dew_point_celsius));
}

double air_temperature_at(double reference_temperature_celsius, double reference_elevation_meters, double altitude_meters) {
  if (!std::isfinite(reference_temperature_celsius) || !std::isfinite(reference_elevation_meters) || !std::isfinite(altitude_meters))
    throw std::invalid_argument("temperature profile needs finite inputs");
  return reference_temperature_celsius - 0.0065 * (altitude_meters - reference_elevation_meters);
}

void validate_weather(const WeatherConfig& c) {
  (void)cloud_profile(c.clouds);
  if (c.coverage && !within(*c.coverage, 0, 1)) throw std::invalid_argument("cloud coverage must be 0..1");
  if (c.cloud_base_meters && !within(*c.cloud_base_meters, -12000, 20000)) throw std::invalid_argument("cloud base must be -12000..20000 meters");
  if (!within(c.temperature_celsius, -40, 50)) throw std::invalid_argument("temperature must be -40..50 degrees Celsius");
  if (c.dew_point_celsius && !within(*c.dew_point_celsius, -60, 50)) throw std::invalid_argument("dew point must be -60..50 degrees Celsius");
  if (c.precipitation_mm_h && !within(*c.precipitation_mm_h, 0, 200)) throw std::invalid_argument("precipitation must be 0..200 mm/h");
  if (!within(c.reference_elevation_meters, -12000, 12000)) throw std::invalid_argument("reference elevation must be -12000..12000 meters");
  if (!within(c.stability_hz, 0.002, 0.05)) throw std::invalid_argument("stability frequency must be 0.002..0.05 per second");
  validate_wind(c.wind);
  validate_daylight(c.daylight);
}

CloudLayer cloud_layer_for(const WeatherConfig& c) {
  validate_weather(c);
  const auto& p = cloud_profile(c.clouds);
  CloudLayer layer;
  layer.type = c.clouds;
  layer.visibility_meters = p.visibility_meters;
  layer.dew_point_celsius = c.dew_point_celsius.value_or(c.temperature_celsius - p.base_meters_agl / 125.0);
  if (c.clouds == CloudType::Clear) {
    layer.coverage = 0.0;
    layer.base_meters = layer.top_meters = c.cloud_base_meters.value_or(c.reference_elevation_meters);
    return layer;
  }
  // Low clouds form where the lifted air saturates (the LCL); high ice
  // clouds keep their characteristic altitude whatever the surface humidity.
  const double lcl = lifting_condensation_level_meters(c.temperature_celsius, layer.dew_point_celsius);
  const double agl = c.clouds == CloudType::Cirrus ? std::max(lcl, p.base_meters_agl) : lcl;
  layer.base_meters = c.cloud_base_meters.value_or(c.reference_elevation_meters + agl);
  layer.top_meters = layer.base_meters + p.thickness_meters;
  layer.coverage = c.coverage.value_or(p.coverage);
  layer.density = p.density;
  layer.detail = p.detail;
  layer.precipitation = p.precipitation * (0.35 + 0.65 * layer.coverage / std::max(p.coverage, 0.05));
  layer.precipitation = clamp01(layer.precipitation);
  layer.storm = p.storm * smoothstep(0.1, 0.5, layer.coverage);
  layer.rain_intensity_mm_h = p.rain_intensity_mm_h;
  layer.drop_diameter_mm = p.drop_diameter_mm;
  layer.convective = p.convective;
  if (c.precipitation_mm_h) {
    // An authored rainfall: 12 mm/h fills the sky with drops; the rate and intensity
    // reproduce the millimetres exactly (precipitation x intensity).
    layer.precipitation = clamp01(*c.precipitation_mm_h / 12.0);
    layer.rain_intensity_mm_h = layer.precipitation > 0 ? *c.precipitation_mm_h / layer.precipitation : 0.0;
  }
  layer.scale_meters = c.clouds == CloudType::Cumulus ? 1100.0 : c.clouds == CloudType::Cumulonimbus ? 2600.0 :
                       c.clouds == CloudType::Cirrus ? 4000.0 : 2200.0;
  // Thin coverage keeps the surface visibility of clear air.
  layer.visibility_meters = 50000.0 + (p.visibility_meters - 50000.0) * smoothstep(0.0, 0.6, layer.coverage);
  return layer;
}

bool precipitation_is_snow(const WeatherConfig& c, double altitude_meters) {
  validate_weather(c);
  return air_temperature_at(c.temperature_celsius, c.reference_elevation_meters, altitude_meters) <= 1.0;
}

double lightning_at(double time_seconds, double storm, std::uint32_t seed) {
  if (!std::isfinite(time_seconds) || !within(storm, 0, 1)) throw std::invalid_argument("lightning needs finite time and storm 0..1");
  if (storm <= 0.0) return 0.0;
  // One candidate strike per 3-second slot, accepted with probability storm / 2;
  // each flash is a fast double pulse inside its slot.
  constexpr double kSlot = 3.0;
  const double slot = std::floor(time_seconds / kSlot);
  const auto index = static_cast<std::int64_t>(slot);
  if (hash01(index, seed, 41) >= 0.5 * storm) return 0.0;
  const double start = hash01(index, seed, 43) * 1.5;
  const double local = time_seconds - slot * kSlot - start;
  if (local < 0) return 0.0;
  // The leader flash, then two return strokes re-illuminating the channel.
  const double pulse = std::exp(-local * 9.0) + 0.7 * std::exp(-std::max(local - 0.12, 0.0) * 12.0) * (local > 0.12 ? 1.0 : 0.0) +
                       0.45 * std::exp(-std::max(local - 0.3, 0.0) * 14.0) * (local > 0.3 ? 1.0 : 0.0);
  return clamp01(pulse);
}

LightningStrike lightning_strike_at(double time_seconds, double storm, std::uint32_t seed) {
  LightningStrike strike;
  strike.intensity = lightning_at(time_seconds, storm, seed);
  constexpr double kSlot = 3.0;
  const double slot = std::floor(time_seconds / kSlot);
  strike.slot = static_cast<std::int64_t>(slot);
  strike.seed = hash_u32(static_cast<std::uint32_t>(strike.slot) * 0x9E3779B1U ^ seed);
  strike.u = hash01(strike.slot, seed, 47);
  strike.v = hash01(strike.slot, seed, 53);
  const double start = hash01(strike.slot, seed, 43) * 1.5;
  strike.age_seconds = time_seconds - slot * kSlot - start;
  // A channel stays drawn while its flash decays (about half a second).
  strike.active = storm > 0 && hash01(strike.slot, seed, 41) < 0.5 * storm && strike.age_seconds >= 0 && strike.age_seconds < 0.6;
  return strike;
}

double relative_humidity_from_dew_point(double temperature_celsius, double dew_point_celsius) {
  if (!std::isfinite(temperature_celsius) || !std::isfinite(dew_point_celsius))
    throw std::invalid_argument("humidity needs finite temperatures");
  const auto magnus = [](double t) { return std::exp(17.62 * t / (243.12 + t)); };
  return clamp01(magnus(std::min(dew_point_celsius, temperature_celsius)) / magnus(temperature_celsius));
}

double dew_point_from_humidity(double temperature_celsius, double relative_humidity) {
  if (!std::isfinite(temperature_celsius) || !within(relative_humidity, 0, 1)) throw std::invalid_argument("dew point needs a finite temperature and humidity 0..1");
  const double rh = std::max(relative_humidity, 0.01);
  const double gamma = std::log(rh) + 17.62 * temperature_celsius / (243.12 + temperature_celsius);
  return 243.12 * gamma / (17.62 - gamma);
}

CloudType cloud_type_for_sample(const compute::WeatherSample& sample, CloudType fair) {
  if (!compute::valid_weather_sample(sample)) throw std::invalid_argument("invalid weather sample");
  if (sample.precipitation_mm_h >= 10.0) return CloudType::Cumulonimbus;
  if (sample.precipitation_mm_h >= 3.0) return CloudType::Nimbostratus;
  if (sample.precipitation_mm_h > 0.0) return CloudType::Stratus;
  if (sample.relative_humidity >= 0.85) return CloudType::Stratocumulus;
  return fair;
}

WindConfig storm_wind(const WindConfig& base, double storm) {
  validate_wind(base);
  if (!within(storm, 0, 1)) throw std::invalid_argument("storm must be 0..1");
  WindConfig c = base;
  // Squalls: the gust front adds up to 12 m/s, veers wildly and changes quickly.
  c.speed_max_mps = std::min(60.0, c.speed_max_mps + 12.0 * storm);
  c.swing_degrees = std::min(90.0, c.swing_degrees + 25.0 * storm);
  c.period_seconds = std::max(1.0, c.period_seconds * (1.0 - 0.45 * storm));
  return c;
}

double storm_rain_band(double time_seconds, double storm, std::uint32_t seed) {
  if (!std::isfinite(time_seconds) || !within(storm, 0, 1)) throw std::invalid_argument("rain band needs finite time and storm 0..1");
  if (storm <= 0) return 1.0;
  // Showers arrive in cells: the rate swings between a third and the full rate every minute or so.
  const double band = clamp01((weather_noise(time_seconds / 20.0, seed, 7) - 0.5) * 2.4 + 0.5);
  return 1.0 - storm * 0.65 * (1.0 - band);
}

double surface_temperature(double reference_celsius, double daylight, double rain_rate, double storm) {
  if (!std::isfinite(reference_celsius) || !within(daylight, 0, 1) || !within(rain_rate, 0, 1) || !within(storm, 0, 1))
    throw std::invalid_argument("surface temperature needs finite, normalized inputs");
  // Diurnal swing about the reference (+2 at full daylight, -2 at night), evaporative cooling
  // of rain (up to -6) and the cold downdraft of a thunderstorm (up to -2).
  return reference_celsius + 4.0 * (daylight - 0.5) - 6.0 * rain_rate - 2.0 * storm;
}

// ---------------------------------------------------------------------------
// Terrain interaction
double HeightField::sample(double x, double z) const {
  if (!width || !height || heights.size() != static_cast<std::size_t>(width) * height) return reference_meters;
  const double u = (x - x0) / spacing, v = (z - z0) / spacing;
  if (u <= 0 || v <= 0 || u >= width - 1 || v >= height - 1) {
    // Edges: nearest inside texel, outside: the valley floor.
    if (u < -1 || v < -1 || u > width || v > height) return reference_meters;
    const auto ix = static_cast<std::size_t>(std::clamp(std::lround(u), 0L, static_cast<long>(width - 1)));
    const auto iz = static_cast<std::size_t>(std::clamp(std::lround(v), 0L, static_cast<long>(height - 1)));
    const float h = heights[iz * width + ix];
    return std::isfinite(h) ? h : reference_meters;
  }
  const auto ix = static_cast<std::size_t>(u), iz = static_cast<std::size_t>(v);
  const double fu = u - static_cast<double>(ix), fv = v - static_cast<double>(iz);
  const auto at = [&](std::size_t i, std::size_t j) {
    const float h = heights[j * width + i];
    return std::isfinite(h) ? static_cast<double>(h) : reference_meters;
  };
  return (at(ix, iz) * (1 - fu) + at(ix + 1, iz) * fu) * (1 - fv) + (at(ix, iz + 1) * (1 - fu) + at(ix + 1, iz + 1) * fu) * fv;
}

HeightField sample_height_field(const terrain::TerrainMesh& mesh, std::uint32_t max_texels) {
  if (mesh.vertices.empty()) throw std::invalid_argument("height field needs terrain vertices");
  if (max_texels < 2 || max_texels > 1024) throw std::invalid_argument("height field resolution must be 2..1024 texels");
  if (!(mesh.cell_size_meters > 0) || !std::isfinite(mesh.cell_size_meters)) throw std::invalid_argument("height field needs a valid cell size");
  double min_x = mesh.vertices.front().position.x, max_x = min_x, min_z = mesh.vertices.front().position.z, max_z = min_z;
  for (const auto& v : mesh.vertices) {
    if (!finite(v.position)) throw std::invalid_argument("height field needs finite vertices");
    min_x = std::min(min_x, v.position.x); max_x = std::max(max_x, v.position.x);
    min_z = std::min(min_z, v.position.z); max_z = std::max(max_z, v.position.z);
  }
  const double extent = std::max({max_x - min_x, max_z - min_z, mesh.cell_size_meters});
  // Coarsen until the larger side fits the texel budget (at least the mesh spacing).
  double spacing = mesh.cell_size_meters;
  while (extent / spacing + 1 > max_texels) spacing *= 2;
  HeightField field;
  field.x0 = min_x; field.z0 = min_z; field.spacing = spacing;
  field.width = static_cast<std::uint32_t>(std::floor((max_x - min_x) / spacing + 0.5)) + 1;
  field.height = static_cast<std::uint32_t>(std::floor((max_z - min_z) / spacing + 0.5)) + 1;
  field.reference_meters = mesh.min_elevation_meters;
  std::vector<double> sum(static_cast<std::size_t>(field.width) * field.height, 0.0);
  std::vector<std::uint32_t> count(sum.size(), 0);
  for (const auto& v : mesh.vertices) {
    const auto ix = static_cast<std::size_t>(std::clamp<long>(std::lround((v.position.x - min_x) / spacing), 0, field.width - 1));
    const auto iz = static_cast<std::size_t>(std::clamp<long>(std::lround((v.position.z - min_z) / spacing), 0, field.height - 1));
    sum[iz * field.width + ix] += v.position.y;
    ++count[iz * field.width + ix];
  }
  field.heights.resize(sum.size());
  for (std::size_t i = 0; i < sum.size(); ++i)
    field.heights[i] = count[i] ? static_cast<float>(sum[i] / count[i]) : std::numeric_limits<float>::quiet_NaN();
  return field;
}

OrographicField neutral_orographic_field() {
  OrographicField field;
  field.width = field.height = 1;
  field.texels.assign(1, OrographicTexel{});
  return field;
}

OrographicField compute_orographic_field(const HeightField& terrain, const WindSample& wind, const CloudLayer& layer,
                                         double stability_hz, double fetch_meters) {
  if (!terrain.width || !terrain.height || terrain.heights.size() != static_cast<std::size_t>(terrain.width) * terrain.height)
    throw std::invalid_argument("orographic field needs a consistent height field");
  if (!(terrain.spacing > 0) || !std::isfinite(terrain.spacing)) throw std::invalid_argument("orographic field needs a positive spacing");
  if (!within(wind.speed_mps, 0, 60) || !std::isfinite(wind.travel.x) || !std::isfinite(wind.travel.y))
    throw std::invalid_argument("orographic field needs a valid wind sample");
  if (!within(stability_hz, 0.002, 0.05)) throw std::invalid_argument("stability frequency must be 0.002..0.05 per second");
  if (!within(fetch_meters, 100, 100000)) throw std::invalid_argument("fetch must be 100..100000 meters");
  if (!std::isfinite(layer.base_meters) || !std::isfinite(layer.top_meters) || layer.top_meters < layer.base_meters)
    throw std::invalid_argument("orographic field needs a finite cloud layer");

  OrographicField field;
  field.width = terrain.width; field.height = terrain.height;
  field.x0 = terrain.x0; field.z0 = terrain.z0; field.spacing = terrain.spacing;
  field.texels.assign(static_cast<std::size_t>(field.width) * field.height, OrographicTexel{});
  const math::Vec2 w = wind.travel.length() > 1e-9 ? wind.travel.normalized() : math::Vec2{1, 0};
  const double U = wind.speed_mps, N = stability_hz;
  const double step = terrain.spacing;
  const int steps = static_cast<int>(std::clamp(std::ceil(fetch_meters / step), 1.0, 96.0));
  const double depth = std::max(layer.top_meters - layer.base_meters, 1.0);
  field.base_shift_scale_meters = std::clamp(depth, 200.0, 1500.0);

  // Coupling: how much this layer feels the ground. The highest crest of the
  // field against the base: a layer whose base is more than ~2 km above the
  // crests is decoupled (cirrus over hills), one that the terrain pierces is
  // fully coupled.
  double crest = -std::numeric_limits<double>::infinity(), floor = std::numeric_limits<double>::infinity();
  for (const float h : terrain.heights) if (std::isfinite(h)) { crest = std::max(crest, static_cast<double>(h)); floor = std::min(floor, static_cast<double>(h)); }
  if (!std::isfinite(crest)) { crest = floor = terrain.reference_meters; }
  const double relief = std::max(crest - floor, 0.0);
  const double gap = layer.base_meters - crest;
  field.coupling = layer.coverage <= 0.0 ? 0.0 : (1.0 - smoothstep(std::max(500.0, relief), std::max(2500.0, 4.0 * relief), gap));
  field.froude = relief > 1.0 ? U / (N * relief) : std::numeric_limits<double>::infinity();
  if (field.coupling <= 0.0) return field;

  const double w0 = 0.8;                  // vertical velocity (m/s) of full windward thickening
  double sum_thick = 0, sum_diss = 0;
  for (std::uint32_t j = 0; j < field.height; ++j) {
    for (std::uint32_t i = 0; i < field.width; ++i) {
      const double x = terrain.x0 + i * step, z = terrain.z0 + j * step;
      const double h0 = terrain.sample(x, z);
      // Upwind march: the crest the arriving air crossed and the valley it came from.
      double h_up_max = h0, h_up_min = h0, crest_distance = 0.0;
      for (int k = 1; k <= steps; ++k) {
        const double hq = terrain.sample(x - w.x * step * k, z - w.y * step * k);
        if (hq > h_up_max) { h_up_max = hq; crest_distance = step * k; }
        h_up_min = std::min(h_up_min, hq);
      }
      // Downwind march: the barrier the air is about to meet.
      double h_down_max = h0;
      for (int k = 1; k <= steps; ++k) h_down_max = std::max(h_down_max, terrain.sample(x + w.x * step * k, z + w.y * step * k));
      // Local slope along the flow: positive where the air is forced uphill.
      const double behind = terrain.sample(x - w.x * 2 * step, z - w.y * 2 * step);
      const double slope = (h0 - behind) / (2 * step);
      const double vertical = U * slope;
      const double lift = h0 - h_up_min;
      const double descent = h_up_max - h0;

      // Blocking (Froude number U / (N H)) only matters for barriers that reach
      // into the cloud layer; air below a crest that stays under the base flows
      // around or over it without touching the cloud.
      const double barrier_ahead = h_down_max - h0;
      const double barrier_crossed = h_up_max - h_up_min;
      const auto blocked_for = [&](double barrier, double barrier_top) {
        if (barrier < 50.0) return 0.0;
        const double froude = U / (N * barrier);
        return (1.0 - smoothstep(0.7, 1.5, froude)) * smoothstep(layer.base_meters - 100.0, layer.base_meters + 100.0, barrier_top);
      };
      const double blocked_ahead = blocked_for(barrier_ahead, h_down_max);
      const double blocked_crossed = blocked_for(barrier_crossed, h_up_max);

      // Windward: local uphill forcing plus the cumulative ascent from the valley,
      // plus the dammed air piling up in front of a barrier it cannot cross.
      const double local = smoothstep(0.0, w0, vertical);
      // Air that has already dropped below the crest it crossed is descending,
      // not still rising from the valley.
      const double cumulative = smoothstep(0.0, 400.0, lift) * (1.0 - smoothstep(0.0, 150.0, descent));
      double thickening = clamp01(0.7 * local + 0.45 * cumulative * (0.4 + 0.6 * local) + 0.5 * blocked_ahead * smoothstep(0.0, 150.0, barrier_ahead));
      // Lee: descending air warms and evaporates the cloud, fading out a few
      // kilometres downwind; blocked low cloud never crosses the crest at all.
      const double decay = std::exp(-crest_distance / 4000.0);
      double foehn = smoothstep(0.0, 300.0, descent) * decay;
      const double cut_off = blocked_crossed * smoothstep(0.0, 80.0, descent);
      double dissipation = clamp01(std::max(foehn * (1.0 - 0.5 * local), cut_off));
      // Base: rises in the drier lee, sinks a little on the moist windward side.
      double base_shift = foehn * std::min(0.6 * descent, depth) - 60.0 * thickening;
      base_shift = std::clamp(base_shift, -field.base_shift_scale_meters, field.base_shift_scale_meters);
      thickening *= field.coupling; dissipation *= field.coupling; base_shift *= field.coupling;

      auto& t = field.texels[static_cast<std::size_t>(j) * field.width + i];
      t.thickening = static_cast<float>(thickening);
      t.dissipation = static_cast<float>(dissipation);
      t.base_shift_meters = static_cast<float>(base_shift);
      t.precipitation = static_cast<float>(clamp01(0.5 + 0.5 * thickening - 0.5 * dissipation));
      sum_thick += thickening; sum_diss += dissipation;
      field.max_thickening = std::max(field.max_thickening, thickening);
      field.max_dissipation = std::max(field.max_dissipation, dissipation);
      if (thickening > 0.2) ++field.windward_texels;
      if (dissipation > 0.2) ++field.lee_texels;
      if (std::max(blocked_ahead, blocked_crossed) > 0.5) ++field.blocked_texels;
      if (h0 >= layer.base_meters + base_shift && h0 <= layer.top_meters) ++field.in_cloud_texels;
    }
  }
  field.mean_thickening = sum_thick / static_cast<double>(field.texels.size());
  field.mean_dissipation = sum_diss / static_cast<double>(field.texels.size());
  return field;
}

std::vector<std::byte> pack_orographic_texture(const OrographicField& field) {
  if (!field.width || !field.height || field.texels.size() != static_cast<std::size_t>(field.width) * field.height)
    throw std::invalid_argument("orographic field is inconsistent");
  if (!(field.base_shift_scale_meters > 0) || !std::isfinite(field.base_shift_scale_meters))
    throw std::invalid_argument("orographic field needs a positive base shift scale");
  std::vector<std::byte> bytes(field.texels.size() * 4);
  const auto encode = [](double v) { return static_cast<std::byte>(std::lround(clamp01(v) * 255.0)); };
  for (std::size_t i = 0; i < field.texels.size(); ++i) {
    const auto& t = field.texels[i];
    if (!std::isfinite(t.thickening) || !std::isfinite(t.dissipation) || !std::isfinite(t.base_shift_meters) || !std::isfinite(t.precipitation))
      throw std::invalid_argument("orographic texel is not finite");
    bytes[i * 4] = encode(t.thickening);
    bytes[i * 4 + 1] = encode(t.dissipation);
    bytes[i * 4 + 2] = encode(0.5 + 0.5 * t.base_shift_meters / field.base_shift_scale_meters);
    bytes[i * 4 + 3] = encode(t.precipitation);
  }
  return bytes;
}

RunoffField neutral_runoff_field() {
  RunoffField field;
  field.width = field.height = 1;
  field.texels.assign(1, RunoffTexel{});
  return field;
}

RunoffField compute_runoff_field(const HeightField& terrain) {
  if (!terrain.width || !terrain.height || terrain.heights.size() != static_cast<std::size_t>(terrain.width) * terrain.height)
    throw std::invalid_argument("runoff field needs a consistent height field");
  if (!(terrain.spacing > 0) || !std::isfinite(terrain.spacing)) throw std::invalid_argument("runoff field needs a positive spacing");
  RunoffField field;
  field.width = terrain.width; field.height = terrain.height;
  field.x0 = terrain.x0; field.z0 = terrain.z0; field.spacing = terrain.spacing;
  const std::size_t count = static_cast<std::size_t>(field.width) * field.height;
  field.texels.assign(count, RunoffTexel{});
  const auto height_at = [&](std::size_t i) { const float h = terrain.heights[i]; return std::isfinite(h) ? static_cast<double>(h) : terrain.reference_meters; };
  // D8: every texel drains to its steepest lower neighbour; processing from the highest texel
  // down accumulates the contributing area in one pass.
  std::vector<std::size_t> order(count);
  for (std::size_t i = 0; i < count; ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return height_at(a) > height_at(b); });
  std::vector<double> accumulation(count, 1.0);
  std::vector<double> slope(count, 0.0);
  std::vector<std::size_t> target(count);
  const int w = static_cast<int>(field.width), h = static_cast<int>(field.height);
  for (const auto i : order) {
    const int x = static_cast<int>(i % field.width), z = static_cast<int>(i / field.width);
    const double here = height_at(i);
    double best_drop = 0.0; std::size_t best = i;
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
      if (!dx && !dz) continue;
      const int nx = x + dx, nz = z + dz;
      if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
      const auto n = static_cast<std::size_t>(nz) * field.width + static_cast<std::size_t>(nx);
      const double run = terrain.spacing * ((dx && dz) ? std::sqrt(2.0) : 1.0);
      const double drop = (here - height_at(n)) / run;
      if (drop > best_drop) { best_drop = drop; best = n; }
    }
    slope[i] = std::atan(best_drop) * 180.0 / kPi;
    target[i] = best;
    if (best != i) accumulation[best] += accumulation[i];
  }
  // Flow direction: the downhill gradient, smoothed over five texels (weighted by the slope,
  // so cliffs set the direction of the ground around them) — smooth across a face, unlike
  // the eight-way D8 choice, so the shader's along/across frame does not jump between texels.
  {
    const auto height_xy = [&](int x, int z) { return height_at(static_cast<std::size_t>(std::clamp(z, 0, h - 1)) * field.width + static_cast<std::size_t>(std::clamp(x, 0, w - 1))); };
    std::vector<double> gx(count, 0.0), gz(count, 0.0);
    for (int z = 0; z < h; ++z) for (int x = 0; x < w; ++x) {
      const auto i = static_cast<std::size_t>(z) * field.width + static_cast<std::size_t>(x);
      gx[i] = -(height_xy(x + 1, z) - height_xy(x - 1, z)) / (2.0 * terrain.spacing);
      gz[i] = -(height_xy(x, z + 1) - height_xy(x, z - 1)) / (2.0 * terrain.spacing);
    }
    constexpr int kRadius = 4;
    for (int z = 0; z < h; ++z) for (int x = 0; x < w; ++x) {
      double sx = 0.0, sz = 0.0;
      for (int dz = -kRadius; dz <= kRadius; ++dz) for (int dx = -kRadius; dx <= kRadius; ++dx) {
        const int nx = std::clamp(x + dx, 0, w - 1), nz = std::clamp(z + dz, 0, h - 1);
        const auto n = static_cast<std::size_t>(nz) * field.width + static_cast<std::size_t>(nx);
        const double weight = std::exp(-0.5 * (dx * dx + dz * dz) / 3.0);
        sx += gx[n] * weight; sz += gz[n] * weight;
      }
      const double len = std::hypot(sx, sz);
      auto& t = field.texels[static_cast<std::size_t>(z) * field.width + static_cast<std::size_t>(x)];
      if (len > 1e-6) { t.direction_x = static_cast<float>(sx / len); t.direction_z = static_cast<float>(sz / len); }
      // The packed direction carries the steepness as its length (1 at 60 degrees): the slope
      // of the same smoothed gradient, so the shader paints the cliff where the lattice found
      // it (consistent with the spray) without the texel-by-texel jumps of the D8 slope.
      // A gentle texel (by its own descent) stays gentle: the smoothing must not climb a crest.
      const double own = slope[static_cast<std::size_t>(z) * field.width + static_cast<std::size_t>(x)];
      t.steepness = static_cast<float>(std::min(std::atan(len) * 180.0 / kPi / 60.0, 1.0) * smoothstep(18.0, 32.0, own));
    }
  }
  for (std::size_t i = 0; i < count; ++i) field.max_accumulation_cells = std::max(field.max_accumulation_cells, accumulation[i]);
  const double scale = std::log1p(std::max(field.max_accumulation_cells, 2.0));
  for (std::size_t i = 0; i < count; ++i) {
    auto& t = field.texels[i];
    t.flow = static_cast<float>(clamp01(std::log1p(accumulation[i]) / scale));
    t.slope_degrees = static_cast<float>(slope[i]);
    // A waterfall needs a cliff and a stream to pour over it.
    t.waterfall = static_cast<float>(smoothstep(40.0, 60.0, slope[i]) * smoothstep(0.3, 0.6, t.flow));
    if (t.flow > 0.35) ++field.channel_texels;
    if (t.waterfall > 0.3) ++field.waterfall_texels;
  }
  // Cascades: walking the drainage from the top down, every falling texel hands the height
  // its water has dropped since the lip on to the texel it drains into; the lip is a falling
  // texel nothing falls into, and the first gentle texel below a fall is its plunge pool.
  std::vector<double> arriving(count, 0.0);
  const auto falling = [&](std::size_t i) { return field.texels[i].waterfall > 0.3; };
  for (const auto i : order) {
    auto& t = field.texels[i];
    const std::size_t next = target[i];
    if (falling(i)) {
      t.drop_meters = static_cast<float>(arriving[i]);
      if (arriving[i] <= 0.0) ++field.lip_texels;
      if (next != i) arriving[next] = std::max(arriving[next], arriving[i] + height_at(i) - height_at(next));
    } else if (arriving[i] > 0.0) {
      t.drop_meters = static_cast<float>(arriving[i]);
      t.impact = static_cast<float>(smoothstep(3.0, 40.0, arriving[i]));
      if (t.impact > 0.3) ++field.plunge_texels;
    }
    field.max_drop_meters = std::max(field.max_drop_meters, static_cast<double>(t.drop_meters));
  }
  return field;
}

std::vector<SprayEmitter> waterfall_spray_emitters(const RunoffField& field, const HeightField& terrain, std::size_t max_emitters) {
  if (!field.width || !field.height || field.texels.size() != static_cast<std::size_t>(field.width) * field.height)
    throw std::invalid_argument("runoff field is inconsistent");
  if (terrain.width != field.width || terrain.height != field.height || terrain.heights.size() != field.texels.size())
    throw std::invalid_argument("spray emitters need the terrain the runoff was computed from");
  if (!(field.spacing > 0) || !std::isfinite(field.spacing)) throw std::invalid_argument("runoff field needs a positive spacing");
  std::vector<SprayEmitter> emitters;
  const int w = static_cast<int>(field.width), h = static_cast<int>(field.height);
  const auto height_at = [&](int x, int z) {
    x = std::clamp(x, 0, w - 1); z = std::clamp(z, 0, h - 1);
    const float v = terrain.heights[static_cast<std::size_t>(z) * field.width + static_cast<std::size_t>(x)];
    return std::isfinite(v) ? static_cast<double>(v) : terrain.reference_meters;
  };
  for (int z = 0; z < h; ++z) for (int x = 0; x < w; ++x) {
    const auto& t = field.texels[static_cast<std::size_t>(z) * field.width + static_cast<std::size_t>(x)];
    const bool falling = t.waterfall > 0.3, plunge = !falling && t.impact > 0.3;
    if (!falling && !plunge) continue;
    // The water leaves along the texel's (smoothed) flow direction, the same frame the
    // ground shader's ropes follow. The face it leaves from leans that way by the texel's
    // steepest descent (a crest's central difference would look flat): its normal is the
    // direction the drops must clear the rock in.
    math::Vec3 downhill{t.direction_x, 0.0, t.direction_z};
    downhill = downhill.length() > 1e-6 ? downhill.normalized() : math::Vec3{1, 0, 0};
    const double slope = (plunge ? std::min(t.slope_degrees, 20.0f) : std::max(t.slope_degrees, 40.0f)) * kPi / 180.0;
    math::Vec3 face{downhill.x * std::sin(slope), std::cos(slope), downhill.z * std::sin(slope)};
    SprayEmitter e;
    e.downhill = downhill; e.face = face;
    e.drop_meters = t.drop_meters; e.width_meters = field.spacing;
    // The lip sits at the crest, up the slope from the texel's centre; the plunge pool's
    // plume rises where the water lands, near the cliff foot (the upstream edge of the pool).
    double back = 0.0;
    if (plunge) { e.kind = SprayKind::Plunge; e.strength = t.impact * smoothstep(0.3, 0.7, t.flow); back = 0.3; }
    else if (t.drop_meters <= 0.0) { e.kind = SprayKind::Lip; e.strength = t.waterfall; back = 0.15; }
    else { e.kind = SprayKind::Contact; e.strength = t.waterfall * smoothstep(2.0, 30.0, t.drop_meters); }
    const double px = field.x0 + x * field.spacing - downhill.x * back * field.spacing;
    const double pz = field.z0 + z * field.spacing - downhill.z * back * field.spacing;
    e.position = {px, back > 0.0 ? terrain.sample(px, pz) : height_at(x, z), pz};
    if (e.strength > 0.05) emitters.push_back(e);
  }
  std::stable_sort(emitters.begin(), emitters.end(), [](const SprayEmitter& a, const SprayEmitter& b) { return a.strength > b.strength; });
  if (emitters.size() > max_emitters) emitters.resize(max_emitters);
  return emitters;
}

std::vector<std::byte> pack_runoff_texture(const RunoffField& field) {
  if (!field.width || !field.height || field.texels.size() != static_cast<std::size_t>(field.width) * field.height)
    throw std::invalid_argument("runoff field is inconsistent");
  // Sixteen bits per channel: the shader builds its along/across frame from the direction,
  // and a frame a thousand metres from the origin turns an 8-bit step into metres of shift.
  std::vector<std::byte> bytes(field.texels.size() * 8);
  const auto encode = [&](std::size_t slot, double v) {
    const auto q = static_cast<std::uint16_t>(std::lround(clamp01(v) * 65535.0));
    bytes[slot * 2] = static_cast<std::byte>(q & 0xFF);
    bytes[slot * 2 + 1] = static_cast<std::byte>(q >> 8);
  };
  for (std::size_t i = 0; i < field.texels.size(); ++i) {
    const auto& t = field.texels[i];
    if (!std::isfinite(t.flow) || !std::isfinite(t.direction_x) || !std::isfinite(t.direction_z) || !std::isfinite(t.drop_meters) || !std::isfinite(t.steepness))
      throw std::invalid_argument("runoff texel is not finite");
    encode(i * 4, t.flow);
    const double packed = std::max(t.steepness, 0.08f);   // keep the direction readable on flat ground
    encode(i * 4 + 1, 0.5 + 0.5 * t.direction_x * packed);
    encode(i * 4 + 2, 0.5 + 0.5 * t.direction_z * packed);
    // Falling texels encode their drop in 0 .. 0.85; a plunge pool (a gentle texel with a
    // drop landing on it) sits at 0.9 .. 1 so interpolation toward a crest never reads as one.
    encode(i * 4 + 3, t.impact > 0.0f ? 0.9 + 0.1 * t.impact : 0.85 * std::sqrt(std::max(t.drop_meters, 0.0f) / kRunoffDropScaleMeters));
  }
  return bytes;
}

double freezing_level_meters(double surface_temperature_celsius, double ground_altitude_meters) {
  if (!std::isfinite(surface_temperature_celsius) || !std::isfinite(ground_altitude_meters)) throw std::invalid_argument("freezing level needs finite inputs");
  return ground_altitude_meters + std::max(surface_temperature_celsius, 0.0) / 0.0065;
}

// ---------------------------------------------------------------------------
// Atmosphere block
AtmosphereState neutral_atmosphere() { return AtmosphereState{}; }

ShaderBinding atmosphere_binding(std::uint32_t stages) {
  return {"Atmosphere", ShaderResourceKind::UniformBuffer, kAtmosphereSlot, stages};
}
ShaderBinding orographic_field_binding() {
  return {"OrographicField", ShaderResourceKind::SampledTexture, kOrographicFieldSlot, kFragmentStage};
}
ShaderBinding runoff_field_binding() {
  return {"RunoffField", ShaderResourceKind::SampledTexture, kRunoffFieldSlot, kFragmentStage};
}

namespace {
void validate(const AtmosphereState& s) {
  const auto& c = s.camera;
  if (!finite(c.eye) || !finite(c.forward) || !finite(c.right) || !finite(c.up))
    throw std::invalid_argument("atmosphere camera must be finite");
  if (!(c.forward.length() > 0) || !(c.right.length() > 0) || !(c.up.length() > 0))
    throw std::invalid_argument("atmosphere camera basis must be nonzero");
  if (!within(c.tan_half_fov_x, 1e-3, 100) || !within(c.tan_half_fov_y, 1e-3, 100))
    throw std::invalid_argument("atmosphere field of view is out of range");
  if (!finite(s.sun.light_direction) || !(s.sun.light_direction.length() > 0) || !finite(s.sun.direction))
    throw std::invalid_argument("atmosphere light direction must be a finite nonzero vector");
  if (!unit_color(s.sun.light_color) || !unit_color(s.sun.ambient_color) || !unit_color(s.sun.zenith_color) ||
      !unit_color(s.sun.horizon_color) || !unit_color(s.fog_color))
    throw std::invalid_argument("atmosphere colors must be finite and normalized");
  if (!within(s.sun.light_intensity, 0, 4) || !within(s.sun.ambient_intensity, 0, 4) || !within(s.sun.daylight, 0, 1) ||
      !within(s.sun.star_brightness, 0, 1) || !within(s.sun.altitude_degrees, -90, 90) || !within(s.sun.moon_altitude_degrees, -90, 90))
    throw std::invalid_argument("atmosphere light terms are out of range");
  if (!within(s.visibility_meters, 10, 1e7)) throw std::invalid_argument("visibility must be 10..10000000 meters");
  if (!within(s.reference_elevation_meters, -12000, 12000)) throw std::invalid_argument("reference elevation must be -12000..12000 meters");
  if (!std::isfinite(s.cloud.base_meters) || !std::isfinite(s.cloud.top_meters) || s.cloud.top_meters < s.cloud.base_meters ||
      !within(s.cloud.coverage, 0, 1) || !within(s.cloud.density, 0, 1) || !within(s.cloud.detail, 0, 1) ||
      !within(s.cloud.scale_meters, 50, 100000))
    throw std::invalid_argument("atmosphere cloud layer is out of range");
  if (!std::isfinite(s.drift.x) || !std::isfinite(s.drift.y) || !std::isfinite(s.time_seconds) || s.time_seconds < 0)
    throw std::invalid_argument("atmosphere drift must be finite and time non-negative");
  if (!within(s.wind.speed_mps, 0, 60) || !std::isfinite(s.wind.travel.x) || !std::isfinite(s.wind.travel.y))
    throw std::invalid_argument("atmosphere wind is out of range");
  if (!within(s.precipitation, 0, 1) || !within(s.wetness, 0, 1) || !within(s.lightning, 0, 1))
    throw std::invalid_argument("precipitation, wetness and lightning must be 0..1");
  if (!finite(s.strike_position) || !within(s.strike.intensity, 0, 1) || !within(s.temperature_celsius, -80, 80) ||
      !within(s.relative_humidity, 0, 1) || !within(s.precipitation_mm_h, 0, 500) || !within(s.fire_weather, -1, 1))
    throw std::invalid_argument("strike, temperature, humidity, rainfall and fire weather are out of range");
  if (!finite(s.sun.moon_direction) || !(s.sun.moon_direction.length() > 0) || !within(s.sun.moon_illumination, 0, 1) ||
      !within(s.sun.moon_intensity, 0, 1) || !finite(s.sun.cloud_light_direction) || !(s.sun.cloud_light_direction.length() > 0) ||
      !unit_color(s.sun.cloud_light_color) || !within(s.sun.cloud_light_intensity, 0, 4) || !unit_color(s.sun.horizon_away_color) ||
      !within(s.moon_diameter_degrees, 0.25, 6) || !within(s.rock_relief_meters, 0, 50))
    throw std::invalid_argument("moon, cloud light, horizon and rock relief terms are out of range");
  if (!within(s.freezing_level_meters, -20000, 30000) || !within(s.melting_layer_meters, 10, 3000) || !within(s.runoff, 0, 1) || !within(s.snow_cover, 0, 1))
    throw std::invalid_argument("freezing level, melting layer, runoff and snow cover are out of range");
  if (!within(s.convection, 0, 1) || !within(s.cloud_time_seconds, 0, 1e9) || !within(s.drop_diameter_mm, 0.05, 8) ||
      !within(s.rainfall_mm, 0, 1e6) || !within(s.ponding, 0, 1) || !within(s.flood_rise_meters, 0, 3) || !within(s.rainbow, 0, 1))
    throw std::invalid_argument("convection, cloud time, drop size, rainfall, ponding, flood rise and rainbow are out of range");
  if (!std::isfinite(s.field_x0) || !std::isfinite(s.field_z0) || !within(s.field_width_meters, 1e-3, 1e7) ||
      !within(s.field_height_meters, 1e-3, 1e7))
    throw std::invalid_argument("orographic field mapping is out of range");
  if (!within(s.base_shift_scale_meters, 1, 5000) || !within(s.thickening_strength, 0, 4) || !within(s.top_lift_meters, 0, 5000))
    throw std::invalid_argument("orographic strengths are out of range");
}
}  // namespace

AtmosphereUniform pack_atmosphere(const AtmosphereState& s) {
  validate(s);
  AtmosphereUniform u{};
  const auto put = [&](std::size_t row, math::Vec3 v, double w) {
    u[row * 4] = f(v.x); u[row * 4 + 1] = f(v.y); u[row * 4 + 2] = f(v.z); u[row * 4 + 3] = f(w);
  };
  const auto& c = s.camera;
  put(0, c.forward.normalized(), c.tan_half_fov_x);
  put(1, c.right.normalized(), c.tan_half_fov_y);
  put(2, c.up.normalized(), s.sun.moon_altitude_degrees);
  put(3, c.eye, s.enabled ? 1.0 : 0.0);
  put(4, s.sun.light_direction.normalized(), s.sun.light_intensity);
  put(5, s.sun.light_color, s.sun.ambient_intensity);
  put(6, s.sun.ambient_color, s.sun.daylight);
  put(7, s.sun.zenith_color, s.sun.altitude_degrees);
  put(8, s.sun.horizon_color, s.lightning);
  put(9, s.fog_color, s.visibility_meters);
  put(10, {s.cloud.base_meters, s.cloud.top_meters, s.cloud.coverage}, s.cloud.density);
  put(11, {s.cloud.detail, s.drift.x, s.drift.y}, static_cast<double>(static_cast<int>(s.cloud.type)));
  put(12, {s.field_x0, s.field_z0, 1.0 / s.field_width_meters}, 1.0 / s.field_height_meters);
  put(13, {s.base_shift_scale_meters, s.thickening_strength, s.top_lift_meters}, s.cloud.scale_meters);
  put(14, {s.wind.travel.x, s.wind.travel.y, s.wind.speed_mps}, s.time_seconds);
  put(15, {s.precipitation, s.snow ? 1.0 : 0.0, s.wetness}, s.sun.star_brightness);
  const auto sun = s.sun.direction.length() > 0 ? s.sun.direction.normalized() : math::Vec3{0, 1, 0};
  put(16, sun, s.reference_elevation_meters);
  put(17, s.strike_position, s.strike.active ? s.strike.intensity : 0.0);
  put(18, {s.drop_diameter_mm, s.ponding, s.rainbow}, s.convection);
  put(19, {s.cloud_time_seconds, s.flood_rise_meters, s.moon_diameter_degrees}, s.rock_relief_meters);
  put(20, s.sun.moon_direction.normalized(), s.sun.moon_illumination);
  put(21, s.sun.cloud_light_color, s.sun.cloud_light_intensity);
  put(22, s.sun.cloud_light_direction.normalized(), s.sun.moon_intensity);
  put(23, s.sun.horizon_away_color, 0.0);
  put(24, {s.freezing_level_meters, s.melting_layer_meters, s.runoff}, s.snow_cover);
  return u;
}

math::Vec3 atmosphere_shade(math::Vec3 albedo, math::Vec3 unit_normal, const AtmosphereState& s) {
  validate(s);
  if (!finite(albedo) || !finite(unit_normal) || !(unit_normal.length() > 0))
    throw std::invalid_argument("atmosphere shade needs finite albedo and a nonzero normal");
  const auto n = unit_normal.normalized();
  const auto light = s.sun.light_direction.normalized();
  const double diffuse = std::max(n.dot(light), 0.0);
  const math::Vec3 wet = mix({1, 1, 1}, {0.72, 0.74, 0.78}, s.wetness);
  const math::Vec3 ambient = s.sun.ambient_color * s.sun.ambient_intensity;
  const math::Vec3 direct = s.sun.light_color * (s.sun.light_intensity * diffuse);
  // Sheet lightning only: the channel's point light needs the surface position (GPU-only).
  const math::Vec3 flash = math::Vec3{0.55, 0.62, 0.80} * (0.35 * s.lightning);
  const math::Vec3 lit = ambient + direct + flash;
  return {albedo.x * wet.x * lit.x, albedo.y * wet.y * lit.y, albedo.z * wet.z * lit.z};
}

math::Vec3 atmosphere_haze(math::Vec3 color, math::Vec3 world_position, const AtmosphereState& s) {
  validate(s);
  if (!finite(color) || !finite(world_position)) throw std::invalid_argument("atmosphere haze needs finite inputs");
  const double distance = (world_position - s.camera.eye).length();
  const double haze = 1.0 - std::exp(-distance / s.visibility_meters);
  return mix(color, s.fog_color, haze);
}

math::Vec3 atmosphere_sky_color(math::Vec3 direction, const AtmosphereState& s) {
  validate(s);
  if (!finite(direction) || !(direction.length() > 0)) throw std::invalid_argument("sky direction must be a nonzero vector");
  const auto d = direction.normalized();
  const double up = std::clamp(d.y, -1.0, 1.0);
  const math::Vec3 ground = mix({0.14, 0.15, 0.13}, s.fog_color, 0.25) * (0.15 + 0.7 * s.sun.daylight);
  // The horizon is warm toward the sun and cool opposite it (strongest around sunrise and sunset).
  const auto sun_h = math::Vec2{s.sun.direction.x, s.sun.direction.z};
  const auto dir_h = math::Vec2{d.x, d.z};
  const double toward = sun_h.length() > 1e-6 && dir_h.length() > 1e-6 ? 0.5 + 0.5 * sun_h.normalized().dot(dir_h.normalized()) : 0.5;
  const math::Vec3 horizon = mix(s.sun.horizon_away_color, s.sun.horizon_color, std::pow(toward, 1.5));
  math::Vec3 sky = up >= 0 ? mix(horizon, s.sun.zenith_color, std::pow(up, 0.55))
                           : mix(horizon, ground, clamp01(-up * 6.0));
  const double path = up >= 0 ? 1500.0 / std::max(up, 0.02)
                              : std::max(s.camera.eye.y - s.reference_elevation_meters, 50.0) / std::max(-up, 0.02);
  sky = mix(sky, s.fog_color, 1.0 - std::exp(-std::min(path, 200000.0) / s.visibility_meters));
  const auto sun = s.sun.direction.normalized();
  const double cos_sun = std::clamp(d.dot(sun), -1.0, 1.0);
  const double visible = smoothstep(-1.5, 0.5, s.sun.altitude_degrees);
  const double glow = (0.22 * std::pow(std::max(cos_sun, 0.0), 24.0) + 0.5 * std::pow(std::max(cos_sun, 0.0), 400.0)) * visible *
                      smoothstep(-6.0, 2.0, s.sun.altitude_degrees);
  const double disc = smoothstep(0.99955, 0.99975, cos_sun) * visible;
  // Daytime glow uses the warm sun color; the disc saturates toward white.
  const math::Vec3 sun_light = s.sun.light_direction.dot(sun) > 0.999 ? s.sun.light_color : math::Vec3{1.0, 0.62, 0.30};
  sky = sky + sun_light * glow + mix(sun_light, {1, 1, 1}, 0.5) * (disc * 3.0);
  sky = sky + math::Vec3{0.55, 0.62, 0.80} * (0.45 * s.lightning);
  return {clamp01(sky.x), clamp01(sky.y), clamp01(sky.z)};
}

// ---------------------------------------------------------------------------
// Simulation
WeatherSimulation::WeatherSimulation(WeatherConfig config) : config_(std::move(config)) {
  validate_weather(config_);
}
void WeatherSimulation::set_config(WeatherConfig config) {
  validate_weather(config);
  config_ = std::move(config);
  field_wind_.reset();
  field_ = OrographicField{};
}
void WeatherSimulation::advance(double dt_seconds) {
  if (!std::isfinite(dt_seconds) || dt_seconds < 0 || dt_seconds > 3600) throw std::invalid_argument("weather step must be 0..3600 seconds");
  const auto w = wind();
  // Clouds ride the wind: the pattern is displaced by the integrated wind,
  // sampled at the start of the step (a deterministic explicit integration).
  drift_ = drift_ + w.travel * (w.speed_mps * dt_seconds);
  // Rain accumulates on the ground on the accelerated clock and drains away
  // with a six-hour time constant (puddles after ~10 mm, a rise of standing water
  // of 2 cm per millimetre, capped at 3 m).
  const auto current = layer();
  const double rate = clamp01(current.precipitation * storm_rain_band(time_, current.storm, config_.seed));
  const double accelerated = dt_seconds * clock_acceleration();
  rainfall_mm_ += rate * current.rain_intensity_mm_h / 3600.0 * accelerated;
  rainfall_mm_ *= std::exp(-accelerated / 21600.0);
  rainfall_mm_ = std::min(rainfall_mm_, 1e6);
  time_ += dt_seconds;
}
void WeatherSimulation::set_time(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("weather time must be finite and non-negative");
  time_ = seconds;
}
void WeatherSimulation::reset() { time_ = 0.0; drift_ = {}; rainfall_mm_ = 0.0; }
void WeatherSimulation::set_rainfall_mm(double mm) {
  if (!within(mm, 0, 1e6)) throw std::invalid_argument("rainfall must be 0..1e6 mm");
  rainfall_mm_ = mm;
}
double WeatherSimulation::clock_acceleration() const {
  const double per_hour = config_.daylight.seconds_per_hour;
  return per_hour > 0 ? std::min(3600.0 / per_hour, 60.0) : 1.0;
}
WindConfig WeatherSimulation::effective_wind() const { return storm_wind(config_.wind, layer().storm); }
WindSample WeatherSimulation::wind() const { return wind_at(effective_wind(), time_); }
bool WeatherSimulation::field_stale(const WindSample& wind) const {
  if (!field_wind_) return true;
  double turn = std::abs(wind.direction_degrees - field_wind_->direction_degrees);
  turn = std::min(turn, 360.0 - turn);
  return turn > 3.0 || std::abs(wind.speed_mps - field_wind_->speed_mps) > 0.75;
}
void WeatherSimulation::mark_field(const WindSample& wind, const OrographicField& field) {
  field_wind_ = wind;
  field_ = field;
}

AtmosphereState WeatherSimulation::state(const CameraBasis& camera, double ground_altitude_meters) const {
  AtmosphereState s;
  s.enabled = config_.enabled;
  s.camera = camera;
  s.sun = sun();
  s.cloud = layer();
  s.drift = drift_;
  s.wind = wind();
  s.time_seconds = time_;
  s.reference_elevation_meters = config_.reference_elevation_meters;
  s.visibility_meters = s.cloud.visibility_meters;
  // Haze takes the horizon color by day and darkens with it; heavy cloud greys it.
  const double grey = smoothstep(0.5, 1.0, s.cloud.coverage) * s.cloud.density;
  const math::Vec3 base_fog = mix(s.sun.horizon_color, mix(s.sun.zenith_color, {0.5, 0.5, 0.5}, 0.5) * (0.35 + 0.65 * s.sun.daylight), grey);
  s.fog_color = {clamp01(base_fog.x), clamp01(base_fog.y), clamp01(base_fog.z)};
  // Showers of a thunderstorm come in bands; the rain and the downdraft cool the surface air,
  // which with the humidity gives the fire weather (the v3 contract's weather_term).
  s.precipitation = clamp01(s.cloud.precipitation * storm_rain_band(time_, s.cloud.storm, config_.seed));
  s.precipitation_mm_h = s.precipitation * s.cloud.rain_intensity_mm_h;
  s.temperature_celsius = surface_temperature(config_.temperature_celsius, s.sun.daylight, s.precipitation, s.cloud.storm) -
                          0.0065 * (ground_altitude_meters - config_.reference_elevation_meters);
  s.relative_humidity = relative_humidity_from_dew_point(s.temperature_celsius, s.cloud.dew_point_celsius);
  if (s.precipitation > 0) s.relative_humidity = std::max(s.relative_humidity, 0.7 + 0.25 * s.precipitation);
  s.fire_weather = std::clamp(compute::weather_term({s.temperature_celsius, s.relative_humidity, std::min(s.precipitation_mm_h, 200.0)}), -1.0, 1.0);
  s.snow = s.precipitation > 0 && s.temperature_celsius <= 1.0;
  s.wetness = s.snow ? 0.0 : s.precipitation;
  s.lightning = lightning_at(time_, s.cloud.storm, config_.seed);
  s.strike = lightning_strike_at(time_, s.cloud.storm, config_.seed);
  // Convection feeds cumuliform cells while the sun heats the ground (and the
  // air is warm); without it the cells decay into fuzzy remnants and vanish.
  s.convection = s.cloud.convective * clamp01(0.1 + 0.9 * smoothstep(0.15, 0.7, s.sun.daylight) *
                                              smoothstep(4.0, 22.0, config_.temperature_celsius));
  s.cloud_time_seconds = time_ * clock_acceleration();
  s.drop_diameter_mm = std::clamp(s.cloud.drop_diameter_mm > 0 ? s.cloud.drop_diameter_mm : 1.0, 0.05, 8.0);
  s.rainfall_mm = rainfall_mm_;
  s.ponding = clamp01(rainfall_mm_ / 25.0);
  s.flood_rise_meters = std::min(3.0, 0.02 * rainfall_mm_);
  // A rainbow needs sunshine behind the observer and rain in front: the sun
  // above the horizon and not hidden by an overcast.
  const bool sun_is_light = s.sun.light_direction.dot(s.sun.direction) > 0.999;
  s.rainbow = sun_is_light ? clamp01(s.sun.light_intensity / 0.74) * smoothstep(0.05, 0.4, s.precipitation) *
                             (1.0 - smoothstep(0.5, 0.95, s.cloud.coverage)) * smoothstep(0.5, 3.0, s.sun.altitude_degrees) : 0.0;
  s.moon_diameter_degrees = config_.daylight.moon_diameter_degrees;
  // Snow line of the moment (the 0 C level above the camera's ground), the water running
  // over the ground (rain now plus what is still draining) and the fresh snow cover above it.
  s.freezing_level_meters = std::clamp(freezing_level_meters(s.temperature_celsius, ground_altitude_meters), -20000.0, 30000.0);
  s.runoff = s.snow ? 0.0 : clamp01(1.5 * s.precipitation + 0.8 * s.ponding);  // snow settles, it does not run
  s.snow_cover = clamp01(rainfall_mm_ / 15.0);
  if (field_.width) {
    // Strikes land across the terrain footprint; the channel midpoint lights the scene.
    const double x = field_.x0 + s.strike.u * field_.spacing * (field_.width - 1);
    const double z = field_.z0 + s.strike.v * field_.spacing * (field_.height - 1);
    s.strike_position = {x, 0.5 * (s.cloud.base_meters + ground_altitude_meters), z};
  } else {
    s.strike_position = {camera.eye.x, s.cloud.base_meters, camera.eye.z};
  }
  if (field_.width) {
    s.field_x0 = field_.x0; s.field_z0 = field_.z0;
    s.field_width_meters = std::max(field_.spacing * (field_.width - 1), 1e-3);
    s.field_height_meters = std::max(field_.spacing * (field_.height - 1), 1e-3);
    s.base_shift_scale_meters = field_.base_shift_scale_meters;
  }
  s.thickening_strength = 0.8;
  s.top_lift_meters = std::clamp(0.5 * (s.cloud.top_meters - s.cloud.base_meters), 0.0, 1500.0);
  return s;
}

WaterStyle WeatherSimulation::water_for(const WaterStyle& base) const {
  (void)pack_water_style(base, {}, 0);
  const auto now = wind();
  const double U = now.speed_mps;
  // Fetch-limited sea state: the significant height grows about as U^1.6, the dominant
  // wave lengthens and speeds up with it; a calm leaves a third of the authored waves.
  const double factor = std::clamp(0.35 + std::pow(U / 7.0, 1.6), 0.35, 4.0);
  WaterStyle style = base;
  style.wave_height_meters = std::clamp(base.wave_height_meters * factor, 0.0, 5.0);
  style.wave_length_meters = std::clamp(base.wave_length_meters * std::pow(factor, 0.8), 1.0, 1000.0);
  style.wave_speed_mps = std::clamp(base.wave_speed_mps * std::sqrt(factor), 0.0, 50.0);
  // Waves turn after the wind with a lag: the mean direction plus part of the gust veer.
  const auto mean = effective_wind().direction_degrees;
  const double veer = std::remainder(now.direction_degrees - mean, 360.0);
  style.wave_direction_degrees = std::fmod(std::fmod(mean + 0.4 * veer, 360.0) + 360.0, 360.0);
  style.choppiness = clamp01(0.3 + 0.7 * now.gustiness);
  style.foam_depth_meters = std::clamp(base.foam_depth_meters * (0.8 + 0.5 * factor), 0.0, 50.0);
  return style;
}

// ---------------------------------------------------------------------------
// GPU resources
// Both fields are magnified hundreds of times on screen: they interpolate between texels
// (the shaders ease the fraction as well) and clamp at the terrain's edge.
TextureSampling field_sampling() {
  TextureSampling sampling;
  sampling.min = TextureMinFilter::Linear;
  sampling.mag = TextureMagFilter::Linear;
  sampling.wrap_s = sampling.wrap_t = TextureWrap::ClampToEdge;
  return sampling;
}
AtmosphereGpu::AtmosphereGpu(RenderDevice& device) : device_(device), resources_(device) {
  const auto packed = pack_atmosphere(neutral_atmosphere());
  block_ = resources_.create_buffer({BufferUsage::Uniform, sizeof(packed)}, std::as_bytes(std::span(packed)));
  const auto texels = pack_orographic_texture(neutral_orographic_field());
  TextureDesc desc{1, 1};
  desc.sampling = field_sampling();
  field_ = resources_.create_texture2d(desc, texels);
  desc.format = TextureFormat::Rgba16;
  runoff_ = resources_.create_texture2d(desc, pack_runoff_texture(neutral_runoff_field()));
}
void AtmosphereGpu::update_runoff(const RunoffField& field) {
  const auto texels = pack_runoff_texture(field);
  TextureDesc desc{field.width, field.height};
  desc.sampling = field_sampling();
  desc.format = TextureFormat::Rgba16;
  const auto replacement = resources_.create_texture2d(desc, texels);
  resources_.release_texture(runoff_);
  runoff_ = replacement;
  runoff_width_ = field.width;
}
void AtmosphereGpu::update(const AtmosphereState& state) {
  const auto packed = pack_atmosphere(state);
  device_.update_buffer(block_, 0, std::as_bytes(std::span(packed)));
}
void AtmosphereGpu::update_field(const OrographicField& field) {
  const auto texels = pack_orographic_texture(field);
  TextureDesc desc{field.width, field.height};
  desc.sampling = field_sampling();
  const auto replacement = resources_.create_texture2d(desc, texels);  // admit before retiring the old field
  resources_.release_texture(field_);
  field_ = replacement;
  field_width_ = field.width;
}

// ---------------------------------------------------------------------------
// Sky
ShaderLayout sky_shader_layout() {
  return {8, {{"POSITION", 0, VertexFormat::Float2, 0}},
          {atmosphere_binding(kFragmentStage), orographic_field_binding()}};
}
SkyRenderer::SkyRenderer(RenderDevice& device) : device_(device), resources_(device) {
  // One triangle covering clip space; the vertex stage pins it to the far depth.
  constexpr std::array<float, 6> vertices{-1, -1, 3, -1, -1, 3};
  constexpr std::array<std::uint32_t, 3> indices{0, 1, 2};
  vertices_ = resources_.create_buffer({BufferUsage::Vertex, sizeof(vertices)}, std::as_bytes(std::span(vertices)));
  indices_ = resources_.create_buffer({BufferUsage::Index, sizeof(indices)}, std::as_bytes(std::span(indices)));
  pipeline_ = resources_.create_pipeline({ShaderProgramId::Sky, sky_shader_layout(), true, false, false});
}
void SkyRenderer::draw(const AtmosphereBinding& atmosphere) {
  if (!device_.active_render_pass_id()) throw std::logic_error("sky requires an active pass");
  device_.draw_indexed({pipeline_, vertices_, indices_, 0, 3, {{kAtmosphereSlot, atmosphere.block}}, {},
                        {{kOrographicFieldSlot, atmosphere.field}}});
}

// ---------------------------------------------------------------------------
// Precipitation
ShaderLayout precipitation_shader_layout() {
  return {static_cast<std::uint32_t>(sizeof(PrecipitationVertex)),
          {{"CORNER", 0, VertexFormat::Float2, static_cast<std::uint32_t>(offsetof(PrecipitationVertex, corner))},
           {"PARTICLE", 1, VertexFormat::Float1, static_cast<std::uint32_t>(offsetof(PrecipitationVertex, particle))}},
          {{"Camera", ShaderResourceKind::UniformBuffer, 0, kVertexStage}, atmosphere_binding(kVertexStage | kFragmentStage),
           orographic_field_binding()}};
}
PrecipitationRenderer::PrecipitationRenderer(RenderDevice& device, std::uint32_t particles)
    : device_(device), resources_(device), capacity_(particles) {
  if (particles < 1 || particles > 65536) throw std::invalid_argument("precipitation particles must be 1..65536");
  std::vector<PrecipitationVertex> vertices;
  std::vector<std::uint32_t> indices;
  vertices.reserve(static_cast<std::size_t>(particles) * 4);
  indices.reserve(static_cast<std::size_t>(particles) * 6);
  for (std::uint32_t p = 0; p < particles; ++p) {
    const auto id = static_cast<float>(p);
    const auto base = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({{-0.5f, 0.0f}, id}); vertices.push_back({{0.5f, 0.0f}, id});
    vertices.push_back({{0.5f, 1.0f}, id});  vertices.push_back({{-0.5f, 1.0f}, id});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
  vertices_ = resources_.create_buffer({BufferUsage::Vertex, vertices.size() * sizeof(PrecipitationVertex)}, std::as_bytes(std::span(vertices)));
  indices_ = resources_.create_buffer({BufferUsage::Index, indices.size() * sizeof(std::uint32_t)}, std::as_bytes(std::span(indices)));
  pipeline_ = resources_.create_pipeline({ShaderProgramId::Precipitation, precipitation_shader_layout(), true, true, false});
}
PrecipitationDrawStats PrecipitationRenderer::draw(BufferHandle camera, const AtmosphereBinding& atmosphere, double rate) {
  if (!device_.active_render_pass_id()) throw std::logic_error("precipitation requires an active pass");
  if (!within(rate, 0, 1)) throw std::invalid_argument("precipitation rate must be 0..1");
  PrecipitationDrawStats stats;
  // Light rates still show a visible share of the drops (drizzle is sparse, not invisible).
  const auto count = static_cast<std::uint32_t>(std::ceil(std::pow(rate, 0.6) * capacity_));
  if (!count) return stats;
  device_.draw_indexed({pipeline_, vertices_, indices_, 0, count * 6,
                        {{0, camera}, {kAtmosphereSlot, atmosphere.block}}, {}, {{kOrographicFieldSlot, atmosphere.field}}});
  stats.particles = count; stats.draws = 1;
  return stats;
}

// ---------------------------------------------------------------------------
// Lightning
LightningBolt build_lightning_bolt(const LightningStrike& strike, math::Vec3 top, double ground_y, math::Vec3 eye) {
  if (!finite(top) || !std::isfinite(ground_y) || !finite(eye)) throw std::invalid_argument("lightning bolt needs finite positions");
  LightningBolt bolt;
  if (top.y <= ground_y + 1.0) return bolt;
  const double height = top.y - ground_y;
  std::uint32_t state = strike.seed * 0x9E3779B1U + 0x7F4A7C15U;
  const auto random = [&]() { state = hash_u32(state + 0x9E3779B1U); return static_cast<double>(state & 0xFFFFFFu) / 16777216.0; };
  const auto emit_segment = [&](math::Vec3 a, math::Vec3 b, double width, double glow) {
    const math::Vec3 along = b - a;
    const math::Vec3 to_eye = eye - (a + b) * 0.5;
    math::Vec3 side = along.cross(to_eye);
    if (!(side.length() > 1e-6)) side = {1, 0, 0};
    // The halo is wide enough to read as a glowing channel from the camera's distance.
    const double halo = std::max(width * 14.0, to_eye.length() * 0.012);
    side = side.normalized() * (0.5 * halo);
    const auto base = static_cast<std::uint32_t>(bolt.vertices.size());
    const auto push = [&](math::Vec3 p, double across) { bolt.vertices.push_back({{f(p.x), f(p.y), f(p.z)}, static_cast<float>(across)}); };
    // Two strips: the halo (across -1 .. 1) and an overlapping narrow core strip whose
    // across values stay inside the gaussian core; branches glow less (scaled across).
    const double spread = 1.0 / std::max(glow, 0.2);
    push(a - side, -spread); push(a + side, spread); push(b + side, spread); push(b - side, -spread);
    const double core = 0.12;
    push(a - side * core, -core * spread); push(a + side * core, core * spread);
    push(b + side * core, core * spread); push(b - side * core, -core * spread);
    for (const auto i : {0u, 1u, 2u, 0u, 2u, 3u, 4u, 5u, 6u, 4u, 6u, 7u}) bolt.indices.push_back(base + i);
    ++bolt.segments;
  };
  // Main channel: a random walk downward with lateral jitter that shrinks toward the ground.
  const auto channel = [&](math::Vec3 from, double to_y, double spread, double width, double glow, int depth) {
    math::Vec3 p = from;
    const int steps = std::max(4, static_cast<int>((from.y - to_y) / std::max(height / 28.0, 8.0)));
    const double step_y = (from.y - to_y) / steps;
    for (int i = 0; i < steps; ++i) {
      math::Vec3 q{p.x + (random() - 0.5) * spread, p.y - step_y * (0.7 + 0.6 * random()), p.z + (random() - 0.5) * spread};
      if (i == steps - 1) q.y = to_y;
      emit_segment(p, q, width, glow);
      // Branches fork off sideways and die out before the ground.
      if (depth < 2 && random() < 0.16 && bolt.segments < 4000) {
        ++bolt.branches;
        const math::Vec3 direction{(random() - 0.5) * 2.0, -(0.3 + 0.7 * random()), (random() - 0.5) * 2.0};
        const double length = (0.12 + 0.25 * random()) * height;
        math::Vec3 b = q;
        const int sub = 3 + static_cast<int>(random() * 4);
        for (int k = 0; k < sub; ++k) {
          const math::Vec3 n = b + direction.normalized() * (length / sub) + math::Vec3{(random() - 0.5) * spread * 0.5, 0.0, (random() - 0.5) * spread * 0.5};
          if (n.y <= ground_y) break;
          emit_segment(b, n, width * 0.45, glow * 0.6);
          b = n;
        }
      }
      p = q;
    }
  };
  channel(top, ground_y, 0.14 * height, std::max(2.5, 0.004 * height), 1.0, 0);
  return bolt;
}

ShaderLayout lightning_shader_layout() {
  return {static_cast<std::uint32_t>(sizeof(LightningVertex)),
          {{"POSITION", 0, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(LightningVertex, position))},
           {"GLOW", 1, VertexFormat::Float1, static_cast<std::uint32_t>(offsetof(LightningVertex, glow))}},
          {{"Camera", ShaderResourceKind::UniformBuffer, 0, kVertexStage}, atmosphere_binding(kFragmentStage)}};
}

LightningRenderer::LightningRenderer(RenderDevice& device, std::size_t max_segments)
    : device_(device), resources_(device), capacity_(max_segments) {
  if (max_segments < 1 || max_segments > 8192) throw std::invalid_argument("lightning segments must be 1..8192");
  const std::vector<LightningVertex> vertices(max_segments * 8);
  const std::vector<std::uint32_t> indices(max_segments * 12);
  vertices_ = resources_.create_buffer({BufferUsage::Vertex, vertices.size() * sizeof(LightningVertex)}, std::as_bytes(std::span(vertices)));
  indices_ = resources_.create_buffer({BufferUsage::Index, indices.size() * sizeof(std::uint32_t)}, std::as_bytes(std::span(indices)));
  pipeline_ = resources_.create_pipeline({ShaderProgramId::Lightning, lightning_shader_layout(), true, true, false});
}

LightningDrawStats LightningRenderer::draw(BufferHandle camera, const AtmosphereBinding& atmosphere, const LightningStrike& strike,
                                           math::Vec3 top, double ground_y, math::Vec3 eye) {
  if (!device_.active_render_pass_id()) throw std::logic_error("lightning requires an active pass");
  LightningDrawStats stats;
  if (!strike.active || strike.intensity <= 0.01) return stats;
  if (built_slot_ != strike.slot) {
    auto bolt = build_lightning_bolt(strike, top, ground_y, eye);
    if (bolt.segments > capacity_) {
      bolt.vertices.resize(capacity_ * 8);
      bolt.indices.resize(capacity_ * 12);
      bolt.segments = capacity_;
    }
    if (bolt.segments) {
      device_.update_buffer(vertices_, 0, std::as_bytes(std::span(bolt.vertices)));
      device_.update_buffer(indices_, 0, std::as_bytes(std::span(bolt.indices)));
    }
    indices_count_ = static_cast<std::uint32_t>(bolt.indices.size());
    segments_ = bolt.segments;
    built_slot_ = strike.slot;
  }
  if (!indices_count_) return stats;
  device_.draw_indexed({pipeline_, vertices_, indices_, 0, indices_count_, {{0, camera}, {kAtmosphereSlot, atmosphere.block}}, {}});
  stats.segments = segments_; stats.draws = 1;
  return stats;
}

// ---------------------------------------------------------------------------
// Waterfall spray
ShaderLayout spray_shader_layout() {
  return {static_cast<std::uint32_t>(sizeof(SprayVertex)),
          {{"CORNER", 0, VertexFormat::Float2, static_cast<std::uint32_t>(offsetof(SprayVertex, corner))},
           {"PARTICLE", 1, VertexFormat::Float1, static_cast<std::uint32_t>(offsetof(SprayVertex, particle))},
           {"ORIGIN", 2, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(SprayVertex, origin))},
           {"FLOW", 3, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(SprayVertex, flow))},
           {"FACE", 4, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(SprayVertex, face))},
           {"SHAPE", 5, VertexFormat::Float4, static_cast<std::uint32_t>(offsetof(SprayVertex, shape))}},
          {{"Camera", ShaderResourceKind::UniformBuffer, 0, kVertexStage}, atmosphere_binding(kVertexStage | kFragmentStage),
           orographic_field_binding()}};
}
SprayRenderer::SprayRenderer(RenderDevice& device, std::uint32_t particles_per_emitter, std::size_t max_emitters)
    : device_(device), resources_(device), per_emitter_(particles_per_emitter), capacity_(max_emitters) {
  if (particles_per_emitter < 1 || particles_per_emitter > 1024) throw std::invalid_argument("spray particles per emitter must be 1..1024");
  if (max_emitters < 1 || max_emitters > 512) throw std::invalid_argument("spray emitters must be 1..512");
  const std::size_t quads = capacity_ * per_emitter_;
  const std::vector<SprayVertex> vertices(quads * 4);
  std::vector<std::uint32_t> indices;
  indices.reserve(quads * 6);
  for (std::uint32_t q = 0; q < quads; ++q) {
    const std::uint32_t base = q * 4;
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
  vertices_ = resources_.create_buffer({BufferUsage::Vertex, vertices.size() * sizeof(SprayVertex)}, std::as_bytes(std::span(vertices)));
  indices_ = resources_.create_buffer({BufferUsage::Index, indices.size() * sizeof(std::uint32_t)}, std::as_bytes(std::span(indices)));
  pipeline_ = resources_.create_pipeline({ShaderProgramId::Spray, spray_shader_layout(), true, true, false});
}
void SprayRenderer::set_emitters(const std::vector<SprayEmitter>& emitters) {
  if (device_.active_render_pass_id()) throw std::logic_error("spray emitters change between passes");
  std::vector<SprayVertex> vertices;
  const std::size_t count = std::min(emitters.size(), capacity_);
  vertices.reserve(count * per_emitter_ * 4);
  for (std::size_t e = 0; e < count; ++e) {
    const auto& m = emitters[e];
    if (!finite(m.position) || !finite(m.downhill) || !finite(m.face) || !std::isfinite(m.strength) || !std::isfinite(m.drop_meters) ||
        !std::isfinite(m.width_meters))
      throw std::invalid_argument("spray emitter is not finite");
    if (!within(m.strength, 0, 1) || m.drop_meters < 0 || !(m.width_meters > 0)) throw std::invalid_argument("spray emitter out of range");
    const math::Vec3 down = m.downhill.length() > 1e-6 ? m.downhill.normalized() : math::Vec3{1, 0, 0};
    const math::Vec3 face = m.face.length() > 1e-6 ? m.face.normalized() : math::Vec3{0, 1, 0};
    // Launch speed: the stream's speed over the lip, the fall's speed where it strikes a ledge
    // (a share of sqrt(2 g h), the rest is lost to the rock) and the splash at the pool.
    const double fall_speed = std::sqrt(2.0 * 9.81 * m.drop_meters);
    const double launch = m.kind == SprayKind::Lip ? 1.5 + 2.5 * m.strength
                        : m.kind == SprayKind::Contact ? std::min(0.35 * fall_speed, 9.0)
                        : std::min(0.3 * fall_speed, 8.0);
    for (std::uint32_t p = 0; p < per_emitter_; ++p) {
      const auto push = [&](float cx, float cy) {
        vertices.push_back({{cx, cy}, static_cast<float>(p),
                            {f(m.position.x), f(m.position.y), f(m.position.z)},
                            {f(down.x), f(down.z), static_cast<float>(launch)},
                            {f(face.x), f(face.y), f(face.z)},
                            {static_cast<float>(static_cast<int>(m.kind)), static_cast<float>(m.strength), static_cast<float>(m.drop_meters),
                             static_cast<float>(m.width_meters)}});
      };
      push(-0.5f, 0.0f); push(0.5f, 0.0f); push(0.5f, 1.0f); push(-0.5f, 1.0f);
    }
  }
  if (!vertices.empty()) device_.update_buffer(vertices_, 0, std::as_bytes(std::span(vertices)));
  emitters_ = count;
}
SprayDrawStats SprayRenderer::draw(BufferHandle camera, const AtmosphereBinding& atmosphere, double runoff) {
  if (!device_.active_render_pass_id()) throw std::logic_error("spray requires an active pass");
  if (!within(runoff, 0, 1)) throw std::invalid_argument("runoff must be 0..1");
  SprayDrawStats stats;
  if (!emitters_ || runoff <= 0.0) return stats;
  const auto quads = static_cast<std::uint32_t>(emitters_ * per_emitter_);
  device_.draw_indexed({pipeline_, vertices_, indices_, 0, quads * 6,
                        {{0, camera}, {kAtmosphereSlot, atmosphere.block}}, {}, {{kOrographicFieldSlot, atmosphere.field}}});
  stats.emitters = emitters_; stats.particles = quads; stats.draws = 1;
  return stats;
}

}  // namespace terraflux::graphics
