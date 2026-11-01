#pragma once

#include <terraflux/contracts.hpp>
#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/water_shading.hpp>
#include <terraflux/math.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace terraflux::graphics {

// ===========================================================================
// Weather: wind, daylight, clouds and their interaction with the terrain.
//
// Everything here is backend-neutral and deterministic. The viewer packs the
// per-frame result into the 256-byte `Atmosphere` uniform block (slot 15) and
// the `OrographicField` texture (slot 1) that every world shader (terrain,
// wildfire, water, vegetation, fire, sky, precipitation) binds; with the block
// in its neutral state the shaders reduce exactly to their weather-free
// formulas, which keeps the existing CPU references valid. Nothing in this
// module feeds back into fire propagation or replay timing.
// ===========================================================================

// ---------------------------------------------------------------------------
// Wind. The speed is never constant: it wanders between two thresholds (the
// lull and the gust) and the direction veers around its mean, both driven by
// seeded, smooth, time-continuous noise so every run is reproducible.
struct WindConfig {
  double direction_degrees = 90.0;  // mean compass direction the wind blows toward (0 north, 90 east), 0..360
  double speed_min_mps = 2.0;       // lower threshold, 0..60
  double speed_max_mps = 6.0;       // upper threshold, speed_min .. 60 (equal: constant speed)
  double swing_degrees = 20.0;      // half-width of the direction variation, 0..90
  double period_seconds = 12.0;     // time scale of gusts and veering, 1..3600
  std::uint32_t seed = 0;
};
struct WindSample {
  double direction_degrees = 90.0;  // instantaneous compass direction the wind blows toward, [0, 360)
  double speed_mps = 0.0;           // always within [speed_min, speed_max]
  math::Vec2 travel{1.0, 0.0};      // unit world xz direction the wind blows toward
  double gustiness = 0.0;           // 0 at the lower threshold .. 1 at the upper
};
void validate_wind(const WindConfig& config);  // throws std::invalid_argument
WindSample wind_at(const WindConfig& config, double time_seconds);
// Smooth seeded value noise in [0, 1] with three octaves; shared by the wind
// and the lightning schedule so tests can reproduce both.
double weather_noise(double t, std::uint32_t seed, std::uint32_t channel);

// ---------------------------------------------------------------------------
// Daylight. The sun follows the solar position for a latitude and day of the
// year; the light does not switch off at sunset but fades through civil
// (0 .. -6 degrees), nautical (-6 .. -12) and astronomical (-12 .. -18)
// twilight into night. A full moon opposite the sun provides faint night light.
struct DaylightConfig {
  double latitude_degrees = 47.0;  // -90 .. 90
  double day_of_year = 172.0;      // 1 .. 366 (172: June solstice)
  double start_hour = 10.0;        // local solar time at time zero, 0 .. 24
  double seconds_per_hour = 10.0;  // engine seconds per simulated hour, 0 (frozen) .. 3600 (real time)
  // Lunar phase 0 new .. 0.5 full .. 1 new; unset: from the day of the year (29.53-day synodic
  // month from a new moon on day 1.5). The moon's elongation from the sun follows the phase,
  // so a full moon rises at sunset and a new moon travels with the sun.
  std::optional<double> moon_phase;
  double moon_diameter_degrees = 2.0;  // apparent size of the disc (0.5 is true; larger shows the craters), 0.25 .. 6
};
// Phase of the moon for a day of the year (0 new, 0.5 full).
double moon_phase_for_day(double day_of_year);
// Illuminated fraction of the disc for a phase.
double moon_illumination(double phase);
enum class DaylightPhase : std::uint8_t { Day, CivilTwilight, NauticalTwilight, AstronomicalTwilight, Night };
struct SunState {
  double hour = 12.0;                             // local solar time, [0, 24)
  double altitude_degrees = 0.0;                  // -90 .. 90
  double azimuth_degrees = 0.0;                   // compass degrees, clockwise from north
  math::Vec3 direction{0.0, 1.0, 0.0};            // unit vector toward the sun, also below the horizon
  DaylightPhase phase = DaylightPhase::Day;
  double daylight = 1.0;                          // continuous sky brightness, 0 night .. 1 day
  math::Vec3 light_direction{0.0, 1.0, 0.0};      // sun by day, moon by night (anti-solar)
  math::Vec3 light_color{1.0, 1.0, 1.0};          // direct light color
  double light_intensity = 1.0;                   // direct light strength, 0 .. 1
  math::Vec3 ambient_color{1.0, 1.0, 1.0};        // sky light color
  double ambient_intensity = 0.3;                 // sky light strength, 0 .. 1
  math::Vec3 zenith_color, horizon_color;         // sky gradient
  double star_brightness = 0.0;                   // 0 day .. 1 night
  double moon_altitude_degrees = 0.0, moon_azimuth_degrees = 0.0;
  math::Vec3 moon_direction{0.0, -1.0, 0.0};      // unit vector toward the moon (from the simplified ephemeris)
  double moon_phase = 0.5, moon_illumination = 1.0;  // 0 new .. 0.5 full; lit fraction of the disc
  double moon_intensity = 0.0;                    // moonlight reaching the ground, 0 .. ~0.11
  // Light for the cloud layer: the true sun keeps lighting clouds from the side and below
  // for a few degrees after it has set for the ground, in deepening reds.
  math::Vec3 cloud_light_direction{0.0, 1.0, 0.0}, cloud_light_color{1.0, 1.0, 1.0};
  double cloud_light_intensity = 0.0;
  // The horizon opposite the sun (the Belt of Venus at twilight: cooler, purplish).
  math::Vec3 horizon_away_color;
};
void validate_daylight(const DaylightConfig& config);
double solar_declination_degrees(double day_of_year);
// Altitude and azimuth of the sun for a latitude, day and local solar hour.
SunState sun_for_hour(const DaylightConfig& config, double hour);
SunState sun_at(const DaylightConfig& config, double time_seconds);  // hour = start + t / seconds_per_hour
DaylightPhase daylight_phase(double altitude_degrees);
const char* daylight_phase_name(DaylightPhase phase);

// ---------------------------------------------------------------------------
// Cloud forms. Each genus carries the weather it announces: base height,
// depth, typical coverage, optical density, edge detail, precipitation, storm
// likelihood and surface visibility.
enum class CloudType : std::uint8_t { Clear, Cirrus, Cumulus, Stratocumulus, Stratus, Nimbostratus, Cumulonimbus };
inline constexpr std::size_t kCloudTypeCount = 7;
struct CloudProfile {
  CloudType type;
  const char* name;
  double base_meters_agl;          // typical base above the valley floor
  double thickness_meters;
  double coverage;                 // typical fractional sky cover, 0 .. 1
  double density;                  // optical thickness, 0 .. 1
  double detail;                   // 0 smooth layer (stratus) .. 1 puffy towers (cumulus)
  double precipitation;            // 0 .. 1 expected rate
  double rain_intensity_mm_h;      // rainfall at rate 1 (drizzle 0.5 .. thunderstorm 25)
  double drop_diameter_mm;         // typical drop size: drizzle 0.4 .. thunderstorm 3.0 (sets fall speed and streaks)
  double convective;               // 0 layer cloud .. 1 convective cells with an updraft lifecycle
  double storm;                    // 0 .. 1 lightning likelihood
  double visibility_meters;        // surface visibility in and under the cloud
  const char* expected_weather;    // what an observer expects from this sky
};
const CloudProfile& cloud_profile(CloudType type);
const char* cloud_type_name(CloudType type);
CloudType parse_cloud_type(const char* name);   // clear|cirrus|cumulus|stratocumulus|stratus|nimbostratus|cumulonimbus
// Lifting condensation level above the ground: about 125 m per degree of
// dew-point spread (Espy's rule). Rejects non-finite values; a negative spread
// (saturated air) gives zero, fog on the ground.
double lifting_condensation_level_meters(double temperature_celsius, double dew_point_celsius);
// Air temperature at an altitude from the reference value at the valley floor
// (environmental lapse rate 6.5 degrees per kilometer).
double air_temperature_at(double reference_temperature_celsius, double reference_elevation_meters,
                          double altitude_meters);

// ---------------------------------------------------------------------------
// Full configuration and the derived cloud layer.
struct WeatherConfig {
  bool enabled = true;
  CloudType clouds = CloudType::Cumulus;
  std::optional<double> coverage;            // overrides the profile's sky cover, 0 .. 1
  std::optional<double> cloud_base_meters;   // absolute base altitude override
  double temperature_celsius = 18.0;         // at the reference elevation, -40 .. 50
  std::optional<double> dew_point_celsius;   // default: the profile's typical base through the LCL
  std::optional<double> precipitation_mm_h;  // overrides the form's rainfall (a replay's weather sample), 0 .. 200
  double reference_elevation_meters = 0.0;   // the terrain's valley floor (lowest vertex)
  double stability_hz = 0.010;               // Brunt-Vaisala frequency N for blocking, 0.002 .. 0.05
  WindConfig wind;
  DaylightConfig daylight;
  std::uint32_t seed = 0;
};
void validate_weather(const WeatherConfig& config);
struct CloudLayer {
  CloudType type = CloudType::Clear;
  double base_meters = 0.0, top_meters = 0.0;   // absolute altitudes; top > base unless Clear
  double coverage = 0.0, density = 0.0, detail = 0.0;
  double precipitation = 0.0, storm = 0.0;
  double rain_intensity_mm_h = 0.0;             // rainfall at precipitation 1
  double drop_diameter_mm = 1.0, convective = 0.0;
  double visibility_meters = 50000.0;
  double scale_meters = 1500.0;                 // horizontal wavelength of the cloud pattern
  double dew_point_celsius = 0.0;               // effective value
};
CloudLayer cloud_layer_for(const WeatherConfig& config);
// Snow instead of rain when the air at this altitude is at or below 1 degree.
bool precipitation_is_snow(const WeatherConfig& config, double altitude_meters);
// Flash intensity 0 .. 1 at a time for a storm likelihood: deterministic
// seeded strikes (about one every 6 s at storm 1) with a fast double-pulse decay.
double lightning_at(double time_seconds, double storm, std::uint32_t seed);
// The strike behind a flash: its slot (one candidate every 3 s), age within the slot, a seeded
// position (0..1 across the terrain footprint) and seed; `intensity` is lightning_at().
struct LightningStrike {
  double intensity = 0.0;
  bool active = false;
  std::int64_t slot = 0;
  double age_seconds = 0.0;
  double u = 0.5, v = 0.5;
  std::uint32_t seed = 0;
};
LightningStrike lightning_strike_at(double time_seconds, double storm, std::uint32_t seed);
// Relative humidity (0..1) from temperature and dew point (Magnus formula).
double relative_humidity_from_dew_point(double temperature_celsius, double dew_point_celsius);
double dew_point_from_humidity(double temperature_celsius, double relative_humidity);  // inverse Magnus
// The cloud form a replay's weather sample implies: a downpour is a cumulonimbus, steady rain
// a nimbostratus, drizzle a stratus, saturated air a stratocumulus, otherwise `fair`.
CloudType cloud_type_for_sample(const compute::WeatherSample& sample, CloudType fair = CloudType::Cumulus);
// Surface conditions a thunderstorm imposes: gusts and squalls widen the wind thresholds,
// showers come in bands, the downdraft and rain cool the air.
WindConfig storm_wind(const WindConfig& base, double storm);
double storm_rain_band(double time_seconds, double storm, std::uint32_t seed);  // 0 .. 1 multiplier
double surface_temperature(double reference_celsius, double daylight, double rain_rate, double storm);

// ---------------------------------------------------------------------------
// Terrain interaction. A regular height field is sampled from the render mesh
// (holes and the outside take the valley floor), and the orographic field
// describes, per texel, what the wind does to the cloud layer there:
//
//  * thickening   windward slopes: air forced uphill cools adiabatically and
//                 condenses, the cloud deepens, darkens and rains more
//                 ("stau"); also the pile-up in front of a barrier the air
//                 cannot cross (blocking, Froude number U / (N H) below one)
//  * dissipation  lee slopes: air that crossed a crest descends and warms
//                 (foehn), evaporating the cloud for a few kilometres; with
//                 blocked flow the low cloud cannot cross the crest at all
//  * base_shift   the cloud base rises in the lee (drier air) and drops a
//                 little on the windward side
//  * precipitation 0.5 neutral, above on the windward side, below in the rain
//                 shadow
//
// The coupling fades out when the cloud base lies far above the crests: a
// cirrus sheet at 7 km does not notice a 300 m hill. Where the terrain itself
// rises into the layer the shaders put the ground inside the cloud (cap cloud,
// summit fog) because both evaluate the same density function.
struct HeightField {
  double x0 = 0.0, z0 = 0.0, spacing = 30.0;   // world xz of texel (0, 0) and texel size in meters
  std::uint32_t width = 0, height = 0;
  double reference_meters = 0.0;               // valley floor used outside the data and in holes
  std::vector<float> heights;                  // row-major, NaN = hole
  double sample(double x, double z) const;     // bilinear, holes/outside -> reference
};
HeightField sample_height_field(const terrain::TerrainMesh& mesh, std::uint32_t max_texels = 128);
struct OrographicTexel { float thickening = 0, dissipation = 0, base_shift_meters = 0, precipitation = 0.5f; };
struct OrographicField {
  std::uint32_t width = 0, height = 0;
  double x0 = 0.0, z0 = 0.0, spacing = 30.0;
  double base_shift_scale_meters = 600.0;      // encoding range of base_shift (+-)
  std::vector<OrographicTexel> texels;
  // Diagnostics
  double max_thickening = 0, max_dissipation = 0, mean_thickening = 0, mean_dissipation = 0;
  std::size_t windward_texels = 0, lee_texels = 0, blocked_texels = 0, in_cloud_texels = 0;
  double coupling = 0.0;                        // 0 .. 1 how strongly this layer feels the terrain
  double froude = 0.0;                          // U / (N H) for the highest crest
};
OrographicField compute_orographic_field(const HeightField& terrain, const WindSample& wind, const CloudLayer& layer,
                                         double stability_hz, double fetch_meters = 6000.0);
// RGBA8 texels: thickening, dissipation, base shift (0.5 neutral) and precipitation.
std::vector<std::byte> pack_orographic_texture(const OrographicField& field);
OrographicField neutral_orographic_field();   // one neutral texel

// Runoff. Rain that falls below the freezing level runs downhill: the field routes every
// texel's drainage to its steepest lower neighbour (D8) and accumulates the contributing
// area, so channels form where the water gathers; the local slope decides whether it seeps
// slowly over gentle ground, streams down steep rock or shoots over a cliff as a waterfall
// (slopes beyond ~45 degrees with enough catchment), where part of the water detaches into
// spray. Computed once per terrain; the weather decides how much water is running.
// Each cascade is traced from its lip (the first cliff texel a stream reaches) down the
// drainage: drop_meters is the height the water has fallen since the lip, so the shader can
// stretch and whiten the fall with its speed (sqrt(2 g h)); the gentle texel the cascade
// finally lands on is its plunge pool (impact > 0, with the drop it arrived with).
struct RunoffTexel { float flow = 0, slope_degrees = 0, waterfall = 0, drop_meters = 0, impact = 0, direction_x = 0, direction_z = 0, steepness = 0; };
struct RunoffField {
  std::uint32_t width = 0, height = 0;
  double x0 = 0.0, z0 = 0.0, spacing = 30.0;
  std::vector<RunoffTexel> texels;
  double max_accumulation_cells = 0.0;          // largest catchment (texels)
  double max_drop_meters = 0.0;                 // tallest cascade
  std::size_t channel_texels = 0, waterfall_texels = 0, lip_texels = 0, plunge_texels = 0;
};
RunoffField compute_runoff_field(const HeightField& terrain);
// RGBA16 (little-endian): log-scaled flow accumulation, drainage direction x and z scaled
// by the steepness (slope / 60 degrees, at least 0.08; 0.5 neutral), 0.85 sqrt(drop / 250 m)
// on a falling texel or 0.9 + 0.1 impact on a plunge pool.
inline constexpr double kRunoffDropScaleMeters = 250.0;
std::vector<std::byte> pack_runoff_texture(const RunoffField& field);
RunoffField neutral_runoff_field();

// Waterfall spray. Where a cascade shoots over its lip, the fine fraction of the water
// separates into a drizzle; wherever the falling water strikes the rock on the way down it
// splashes off the ledges; at the plunge pool it throws up a plume. Each such place is a
// spray emitter: the particles are derived on the GPU from the emitter, the clock and the
// wind, lit drop by drop (forward-scattered sun or moon, shadowed by the drops around them
// and by the cliff) with a fragmented bow that follows the cascade down.
enum class SprayKind : std::uint8_t { Lip = 0, Contact = 1, Plunge = 2 };
struct SprayEmitter {
  math::Vec3 position;          // world, on the rock
  math::Vec3 downhill;          // unit, horizontal direction the water leaves in
  math::Vec3 face;              // unit normal of the rock face at the emitter
  SprayKind kind = SprayKind::Lip;
  double strength = 0.0;        // 0..1 from the flow (and the drop for contact/plunge)
  double drop_meters = 0.0;     // height fallen so far (lip: 0)
  double width_meters = 30.0;   // lateral extent of the pouring water (the texel)
};
// Strongest emitters first, at most max_emitters.
std::vector<SprayEmitter> waterfall_spray_emitters(const RunoffField& field, const HeightField& terrain, std::size_t max_emitters = 96);
// Altitude of the 0 C level for a surface temperature at a ground altitude (6.5 K/km); at or
// below the ground when the surface itself is freezing.
double freezing_level_meters(double surface_temperature_celsius, double ground_altitude_meters);

// ---------------------------------------------------------------------------
// Atmosphere block: everything the shaders need for one frame.
struct CameraBasis {
  math::Vec3 eye, forward{0, 0, -1}, right{1, 0, 0}, up{0, 1, 0};
  double tan_half_fov_x = 1.0, tan_half_fov_y = 0.577;
};
struct AtmosphereState {
  bool enabled = false;              // false: every shader uses its weather-free formula
  CameraBasis camera;
  SunState sun;
  CloudLayer cloud;
  math::Vec2 drift;                  // accumulated cloud displacement (meters) along the wind
  WindSample wind;
  double time_seconds = 0.0;
  double reference_elevation_meters = 0.0;   // valley floor: the sky's ground tone below the horizon starts here
  double visibility_meters = 50000.0;
  math::Vec3 fog_color{0.70, 0.80, 0.92};
  double precipitation = 0.0, wetness = 0.0, lightning = 0.0;
  bool snow = false;
  LightningStrike strike;                          // position filled by the viewer (world xz from u, v)
  math::Vec3 strike_position;                      // channel midpoint in world space (when active)
  double temperature_celsius = 20.0, relative_humidity = 0.5, precipitation_mm_h = 0.0;
  double fire_weather = 0.0;                       // compute::weather_term of the surface conditions
  // Convection (0 .. 1): how strongly updrafts feed cumuliform cells; cells are crisp and
  // dense while fed and decay into fuzzy, thinning remnants when the updraft dies (night,
  // cold ground). cloud_time runs the cell lifecycle on the accelerated weather clock.
  double convection = 0.0, cloud_time_seconds = 0.0;
  double drop_diameter_mm = 1.0;                   // mean raindrop size of the shower
  double rainfall_mm = 0.0;                        // accumulated rain on the ground (drains with time)
  double ponding = 0.0;                            // 0 .. 1 puddles on flat ground
  double flood_rise_meters = 0.0;                  // rise of standing water after heavy rain, 0 .. 3
  double rainbow = 0.0;                            // 0 .. 1: sun behind the observer while it rains
  double moon_diameter_degrees = 2.0;
  double rock_relief_meters = 0.0;                 // amplitude of the visual crag displacement the spray must clear, 0 .. 50
  // Snow and runoff: rain above the freezing level falls as snow (slowly, swaying, carried by
  // a strong wind) and melts back to rain through a layer below it; what falls below runs
  // downhill; what falls above accumulates as snow cover.
  double freezing_level_meters = 2000.0, melting_layer_meters = 300.0;
  double runoff = 0.0;                             // 0 .. 1 water running over the ground now
  double snow_cover = 0.0;                         // 0 .. 1 fresh snow above the freezing level
  // Orographic texture mapping and strengths.
  double field_x0 = 0.0, field_z0 = 0.0, field_width_meters = 1.0, field_height_meters = 1.0;
  double base_shift_scale_meters = 600.0, thickening_strength = 0.8, top_lift_meters = 500.0;
};
// Uniform block "Atmosphere" at slot 15, std140, exactly twenty-five vec4s:
//   camForward/camRight/camUp: basis, w = tan half fov x, tan half fov y, moon altitude degrees
//   eye:          camera position, w enabled (1) or neutral (0)
//   sun:          unit direction toward the light, w direct intensity
//   sunColor:     rgb, w ambient intensity
//   ambientColor: rgb, w daylight
//   zenith:       rgb, w sun altitude degrees
//   horizon:      rgb, w lightning flash
//   fog:          rgb, w visibility meters
//   cloud:        base, top, coverage, density
//   cloudShape:   detail, drift x, drift z, type index
//   fieldOrigin:  x0, z0, 1/width, 1/height (world xz -> texture uv)
//   fieldScale:   base shift scale m, thickening strength, top lift m, pattern scale m
//   wind:         unit direction x, z, speed, time
//   weather:      precipitation, snow (0/1), wetness, star brightness
//   sunDisc:      true sun direction (also below the horizon), w valley floor altitude
//   strike:       lightning channel midpoint xyz, w flash intensity (0 without a strike)
//   rain:         drop diameter mm, ponding, rainbow, convection
//   life:         cloud lifecycle time, flood rise m, moon diameter degrees, 0
//   moon:         unit direction toward the moon, w illuminated fraction
//   cloudSun:     rgb cloud light color, w intensity (true sun direction in cloudLight)
//   cloudLight:   unit direction of the cloud light, w moonlight intensity
//   horizonAway:  rgb horizon color opposite the sun, w 0
//   snow:         freezing level m, melting layer m, runoff, snow cover
inline constexpr std::uint32_t kAtmosphereSlot = 15;
inline constexpr std::uint32_t kOrographicFieldSlot = 1;  // sampler2D in every fragment stage that uses the block
inline constexpr std::uint32_t kRunoffFieldSlot = 2;      // sampler2D in the terrain and wildfire fragment stages
inline constexpr std::size_t kAtmosphereBytes = 400;
using AtmosphereUniform = std::array<float, 100>;
static_assert(sizeof(AtmosphereUniform) == kAtmosphereBytes);
AtmosphereUniform pack_atmosphere(const AtmosphereState& state);   // validates, throws std::invalid_argument
AtmosphereState neutral_atmosphere();
ShaderBinding atmosphere_binding(std::uint32_t stages = kVertexStage | kFragmentStage);
ShaderBinding orographic_field_binding();
ShaderBinding runoff_field_binding();

// CPU twins mirroring the GLSL for the parts without clouds (coverage zero):
//  - the lit surface color: albedo (ambientColor ambient + lightColor intensity diffuse + sheet-lightning flash)
//    with wet darkening; the cloud shadow and a strike's point light are GPU-only
math::Vec3 atmosphere_shade(math::Vec3 albedo, math::Vec3 unit_normal, const AtmosphereState& state);
//  - distance haze toward the fog color: mix(color, fog, 1 - exp(-d / visibility))
math::Vec3 atmosphere_haze(math::Vec3 color, math::Vec3 world_position, const AtmosphereState& state);
//  - the clear-sky color for a view direction (gradient, sun glow and disc, no stars/clouds)
math::Vec3 atmosphere_sky_color(math::Vec3 unit_direction, const AtmosphereState& state);

// ---------------------------------------------------------------------------
// Simulation: integrates time and cloud drift, samples wind and sun, and
// decides when the orographic field must be recomputed (the wind veered by
// more than 3 degrees or changed by more than 0.75 m/s since the last field).
class WeatherSimulation {
 public:
  explicit WeatherSimulation(WeatherConfig config);
  const WeatherConfig& config() const { return config_; }
  void set_config(WeatherConfig config);      // keeps the clock and drift
  void advance(double dt_seconds);            // integrates drift with the wind at the current time
  void set_time(double seconds);              // jumps the clock (drift continues from here)
  void reset();                               // time, drift and rainfall back to zero
  void set_rainfall_mm(double mm);            // ground already wet from earlier rain (puddles, runoff, flood)
  // Weather clock acceleration: the solar clock's hours per engine second, capped at 60x,
  // so cell lifecycles and rainfall totals are visible within minutes.
  double clock_acceleration() const;
  double rainfall_mm() const { return rainfall_mm_; }
  double time() const { return time_; }
  // The wind of the moment, including the squalls of a thunderstorm (storm_wind).
  WindSample wind() const;
  WindConfig effective_wind() const;
  SunState sun() const { return sun_at(config_.daylight, time_); }
  CloudLayer layer() const { return cloud_layer_for(config_); }
  math::Vec2 drift() const { return drift_; }
  bool field_stale(const WindSample& wind) const;
  void mark_field(const WindSample& wind, const OrographicField& field);
  const OrographicField* field() const { return field_.width ? &field_ : nullptr; }
  AtmosphereState state(const CameraBasis& camera, double ground_altitude_meters) const;
  // Wind-driven sea state for a base water style: height, length, speed, direction lag,
  // choppiness and foam follow the wind (storms raise the sea, calm flattens it).
  WaterStyle water_for(const WaterStyle& base) const;
 private:
  WeatherConfig config_;
  double time_ = 0.0;
  math::Vec2 drift_{};
  double rainfall_mm_ = 0.0;
  std::optional<WindSample> field_wind_;
  OrographicField field_;
};

// ---------------------------------------------------------------------------
// GPU resources. The block and the field texture are created neutral and
// updated by the viewer; every world draw binds both through `binding()`.
struct AtmosphereBinding {
  BufferHandle block;
  TextureHandle field;
  TextureHandle runoff;   // bound at slot 2 by the terrain and wildfire draws only
};
class AtmosphereGpu {
 public:
  explicit AtmosphereGpu(RenderDevice& device);
  AtmosphereGpu(const AtmosphereGpu&) = delete;
  AtmosphereGpu& operator=(const AtmosphereGpu&) = delete;
  void update(const AtmosphereState& state);
  void update_field(const OrographicField& field);   // replaces the texture (between passes)
  void update_runoff(const RunoffField& field);      // replaces the runoff texture (between passes)
  AtmosphereBinding binding() const { return {block_, field_, runoff_}; }
  std::uint32_t field_width() const { return field_width_; }
  std::uint32_t runoff_width() const { return runoff_width_; }
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  BufferHandle block_;
  TextureHandle field_, runoff_;
  std::uint32_t field_width_ = 1, runoff_width_ = 1;
};

// Sky dome: a fullscreen triangle drawn after the opaque terrain at the far
// depth, so it only fills pixels no geometry covered. The fragment stage
// evaluates the gradient, sun and moon discs, stars, lightning and ray-marches
// the cloud layer. POSITION (0, float2); Atmosphere block at slot 15 in both
// stages; OrographicField at texture slot 1.
ShaderLayout sky_shader_layout();
class SkyRenderer {
 public:
  explicit SkyRenderer(RenderDevice& device);
  SkyRenderer(const SkyRenderer&) = delete;
  SkyRenderer& operator=(const SkyRenderer&) = delete;
  void draw(const AtmosphereBinding& atmosphere);
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  PipelineHandle pipeline_;
  BufferHandle vertices_, indices_;
};

// Precipitation: a fixed set of streak quads whose positions the vertex stage
// derives from the particle index, the time and the wind, wrapped in a box
// around the camera; rain falls fast and leans with the wind, snow drifts.
// CORNER (0, float2), PARTICLE (1, float1); Camera at slot 0, Atmosphere at
// slot 15 (both stages), OrographicField at slot 1 (rain shadow lookup). The
// pipeline must blend, depth-test and not write depth.
struct PrecipitationVertex { float corner[2]; float particle; };
ShaderLayout precipitation_shader_layout();
struct PrecipitationDrawStats { std::size_t particles = 0, draws = 0; };
class PrecipitationRenderer {
 public:
  PrecipitationRenderer(RenderDevice& device, std::uint32_t particles = 16000);
  PrecipitationRenderer(const PrecipitationRenderer&) = delete;
  PrecipitationRenderer& operator=(const PrecipitationRenderer&) = delete;
  std::uint32_t capacity() const { return capacity_; }
  // Draws ceil(capacity x rate^0.6) particles; rate 0 draws nothing.
  PrecipitationDrawStats draw(BufferHandle camera, const AtmosphereBinding& atmosphere, double rate);
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  PipelineHandle pipeline_;
  BufferHandle vertices_, indices_;
  std::uint32_t capacity_;
};

// Lightning bolts: a seeded branching channel from the cloud base to the ground, tessellated
// into camera-facing quads with a bright core and a wide soft glow. POSITION (0, float3),
// GLOW (1, float1); Camera at slot 0, Atmosphere at slot 15 (fragment, for the flash
// intensity). Emissive: alpha blend, depth test, no depth write.
struct LightningVertex { float position[3]; float glow; };
struct LightningBolt { std::vector<LightningVertex> vertices; std::vector<std::uint32_t> indices; std::size_t segments = 0, branches = 0; };
LightningBolt build_lightning_bolt(const LightningStrike& strike, math::Vec3 top, double ground_y, math::Vec3 eye);
ShaderLayout lightning_shader_layout();
struct LightningDrawStats { std::size_t segments = 0, draws = 0; };
class LightningRenderer {
 public:
  explicit LightningRenderer(RenderDevice& device, std::size_t max_segments = 4096);
  LightningRenderer(const LightningRenderer&) = delete;
  LightningRenderer& operator=(const LightningRenderer&) = delete;
  // Rebuilds the bolt when the strike slot changes and draws it with the flash intensity.
  LightningDrawStats draw(BufferHandle camera, const AtmosphereBinding& atmosphere, const LightningStrike& strike,
                          math::Vec3 top, double ground_y, math::Vec3 eye);
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  PipelineHandle pipeline_;
  BufferHandle vertices_, indices_;
  std::size_t capacity_;
  std::int64_t built_slot_ = -1;
  std::uint32_t indices_count_ = 0;
  std::size_t segments_ = 0;
};

// Waterfall spray: a fixed number of drop quads per emitter whose motion the vertex stage
// derives from the emitter, the particle index, the clock and the wind (ballistic launch
// decaying to the drop's terminal velocity). CORNER (0, float2), PARTICLE (1, float1),
// ORIGIN (2, float3), FLOW (3, float3: downhill xz, launch speed), FACE (4, float3),
// SHAPE (5, float4: kind, strength, drop metres, width metres); Camera at slot 0,
// Atmosphere at slot 15 (both stages), OrographicField at slot 1. Blend, depth test, no
// depth write.
struct SprayVertex { float corner[2]; float particle; float origin[3]; float flow[3]; float face[3]; float shape[4]; };
ShaderLayout spray_shader_layout();
struct SprayDrawStats { std::size_t emitters = 0, particles = 0, draws = 0; };
class SprayRenderer {
 public:
  SprayRenderer(RenderDevice& device, std::uint32_t particles_per_emitter = 224, std::size_t max_emitters = 96);
  SprayRenderer(const SprayRenderer&) = delete;
  SprayRenderer& operator=(const SprayRenderer&) = delete;
  // Replaces the emitters (between passes); more than the capacity keeps the strongest.
  void set_emitters(const std::vector<SprayEmitter>& emitters);
  std::size_t emitter_count() const { return emitters_; }
  std::uint32_t particles_per_emitter() const { return per_emitter_; }
  // Draws every emitter's particles while water is running (runoff > 0); the shader fades
  // them with the runoff and the freezing level.
  SprayDrawStats draw(BufferHandle camera, const AtmosphereBinding& atmosphere, double runoff);
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  PipelineHandle pipeline_;
  BufferHandle vertices_, indices_;
  std::uint32_t per_emitter_;
  std::size_t capacity_, emitters_ = 0;
};

}  // namespace terraflux::graphics
