#include <terraflux/graphics/water_shading.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool normalized_color(math::Vec3 c) {
  return finite(c) && c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x <= 1 && c.y <= 1 && c.z <= 1;
}
bool within(double value, double lo, double hi) { return std::isfinite(value) && value >= lo && value <= hi; }

void validate(const WaterStyle& s) {
  if (!std::isfinite(s.level_meters)) throw std::invalid_argument("water level must be finite");
  if (!within(s.wave_height_meters, 0, 5)) throw std::invalid_argument("wave height must be 0..5 meters");
  if (!within(s.wave_length_meters, 1, 1000)) throw std::invalid_argument("wave length must be 1..1000 meters");
  if (!within(s.wave_speed_mps, 0, 50)) throw std::invalid_argument("wave speed must be 0..50 m/s");
  if (!within(s.wave_direction_degrees, 0, 360)) throw std::invalid_argument("wave direction must be 0..360 compass degrees");
  if (!within(s.choppiness, 0, 1)) throw std::invalid_argument("choppiness must be 0..1");
  if (!within(s.foam_depth_meters, 0, 50)) throw std::invalid_argument("foam depth must be 0..50 meters");
  if (!normalized_color(s.shallow_color) || !normalized_color(s.deep_color) ||
      !normalized_color(s.horizon_color) || !normalized_color(s.zenith_color))
    throw std::invalid_argument("water colors must be finite and normalized");
  if (!std::isfinite(s.absorption_meters) || !(s.absorption_meters > 0) || s.absorption_meters > 200 ||
      !within(s.clarity_meters, 0.5, 50))
    throw std::invalid_argument("absorption depth must be 0..200 and clarity 0.5..50 meters");
  if (!within(s.reflectivity, 0, 1)) throw std::invalid_argument("reflectivity must be 0..1");
  if (!finite(s.light_direction) || !(s.light_direction.length() > 0))
    throw std::invalid_argument("water light direction must be a finite nonzero vector");
  if (!within(s.specular_power, 1, 4096) || !within(s.specular_intensity, 0, 4))
    throw std::invalid_argument("specular power must be 1..4096 and intensity 0..4");
}

float checked(double value) {
  const auto f = static_cast<float>(value);
  if (!std::isfinite(f)) throw std::invalid_argument("water style is not representable in float");
  return f;
}

}  // namespace

static_assert(sizeof(WaterGpuVertex) == 6 * sizeof(float));
static_assert(std::is_standard_layout_v<WaterGpuVertex>);

math::Vec2 wave_travel_vector(double compass_degrees) {
  if (!std::isfinite(compass_degrees)) throw std::invalid_argument("wave direction must be finite");
  const double radians = compass_degrees * kPi / 180.0;
  return {std::sin(radians), -std::cos(radians)};  // east is +X, north is -Z
}

WaterStyleUniform pack_water_style(const WaterStyle& style, math::Vec3 eye, double time_seconds) {
  validate(style);
  if (!finite(eye)) throw std::invalid_argument("water eye position must be finite");
  if (!std::isfinite(time_seconds) || time_seconds < 0) throw std::invalid_argument("water time must be finite and non-negative");
  const auto travel = wave_travel_vector(style.wave_direction_degrees);
  const auto sun = style.light_direction.normalized();
  WaterStyleUniform u{};
  u[0] = checked(style.level_meters); u[1] = checked(style.wave_height_meters);
  u[2] = checked(style.wave_length_meters); u[3] = checked(style.wave_speed_mps);
  u[4] = checked(travel.x); u[5] = checked(travel.y); u[6] = checked(time_seconds); u[7] = checked(style.foam_depth_meters);
  u[8] = checked(style.shallow_color.x); u[9] = checked(style.shallow_color.y); u[10] = checked(style.shallow_color.z);
  u[11] = checked(style.absorption_meters);
  u[12] = checked(style.deep_color.x); u[13] = checked(style.deep_color.y); u[14] = checked(style.deep_color.z);
  u[15] = checked(style.clarity_meters);
  u[16] = checked(eye.x); u[17] = checked(eye.y); u[18] = checked(eye.z); u[19] = checked(style.choppiness);
  u[20] = checked(style.horizon_color.x); u[21] = checked(style.horizon_color.y); u[22] = checked(style.horizon_color.z);
  u[23] = checked(style.specular_power);
  u[24] = checked(style.zenith_color.x); u[25] = checked(style.zenith_color.y); u[26] = checked(style.zenith_color.z);
  u[27] = checked(style.reflectivity);
  u[28] = checked(sun.x); u[29] = checked(sun.y); u[30] = checked(sun.z); u[31] = checked(style.specular_intensity);
  return u;
}

ShaderLayout water_shader_layout() {
  return {
      static_cast<std::uint32_t>(sizeof(WaterGpuVertex)),
      {{"POSITION", 0, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(WaterGpuVertex, position))},
       {"BED", 1, VertexFormat::Float1, static_cast<std::uint32_t>(offsetof(WaterGpuVertex, bed_height))},
       {"BED_GRADIENT", 2, VertexFormat::Float2, static_cast<std::uint32_t>(offsetof(WaterGpuVertex, bed_gradient))}},
      {{"Camera", ShaderResourceKind::UniformBuffer, 0, kVertexStage},
       {"WaterStyle", ShaderResourceKind::UniformBuffer, kWaterStyleSlot, kVertexStage | kFragmentStage}}};
}

WaterUpload prepare_water_upload(const terrain::WaterMesh& mesh) {
  WaterUpload upload;
  upload.vertices.reserve(mesh.vertices.size());
  for (const auto& v : mesh.vertices) {
    const double values[6] = {v.position.x, v.position.y, v.position.z, v.bed_height_meters,
                              v.bed_gradient.x, v.bed_gradient.y};
    for (const double value : values)
      if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw std::invalid_argument("water mesh contains non-finite or unrepresentable GPU values");
    upload.vertices.push_back({{static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2])},
                               static_cast<float>(values[3]),
                               {static_cast<float>(values[4]), static_cast<float>(values[5])}});
  }
  upload.indices = mesh.indices;
  for (const auto index : upload.indices)
    if (index >= upload.vertices.size()) throw std::invalid_argument("water mesh contains an out-of-range triangle index");
  return upload;
}

double wave_shallowness(double depth_meters, double wave_length_meters) {
  if (!std::isfinite(depth_meters) || !within(wave_length_meters, 1, 1000))
    throw std::invalid_argument("wave shallowness requires a finite depth and a valid wave length");
  const double k = kTwoPi / wave_length_meters;
  return std::clamp(std::sqrt(std::tanh(k * std::max(depth_meters, 0.02))), 0.3, 1.0);
}

math::Vec2 wave_travel_direction(math::Vec2 travel, math::Vec2 bed_gradient, double depth_meters,
                                 double wave_length_meters) {
  if (!std::isfinite(travel.x) || !std::isfinite(travel.y) || !(travel.length() > 0) ||
      !std::isfinite(bed_gradient.x) || !std::isfinite(bed_gradient.y))
    throw std::invalid_argument("wave refraction requires a nonzero travel direction and finite gradient");
  const auto unit_travel = travel.normalized();
  const double slope = bed_gradient.length();
  if (slope < 1e-4) return unit_travel;
  // Waves slow down over rising ground, so their fronts swing toward the
  // shore: blend the travel direction toward the bed gradient as the
  // phase-speed ratio drops. Mirrors the GLSL refractedTravel().
  const double bend = 0.75 * (1.0 - wave_shallowness(depth_meters, wave_length_meters)) *
                      std::clamp(slope * 20.0, 0.0, 1.0);
  const math::Vec2 shore = bed_gradient / slope;
  const math::Vec2 bent = unit_travel * (1.0 - bend) + shore * bend;
  return bent.length() > 1e-3 ? bent.normalized() : unit_travel;
}

WaveShoaling wave_shoaling(double depth_meters, const WaterStyle& style) {
  validate(style);
  if (!std::isfinite(depth_meters)) throw std::invalid_argument("wave shoaling requires a finite depth");
  const double depth = std::max(depth_meters, 0.0);
  const double shallowness = wave_shallowness(depth, style.wave_length_meters);
  const double shoal = std::clamp(1.0 / std::sqrt(shallowness), 1.0, 1.8);  // Green's law growth
  const double limit = 0.6 * depth;                                          // depth-limited height
  WaveShoaling result;
  result.wave_length_meters = style.wave_length_meters * shallowness;        // shorter in the shallows
  result.amplitude_meters = std::min(style.wave_height_meters * shoal, limit);
  const double ratio = style.wave_height_meters * shoal / std::max(limit, 1e-3);
  const double t = std::clamp((ratio - 0.85) / 0.15, 0.0, 1.0);
  result.breaking = style.wave_height_meters > 0 ? t * t * (3 - 2 * t) : 0.0;
  return result;
}

WaterSample water_surface_sample(math::Vec3 surface_position, double depth_meters, math::Vec3 eye,
                                 const WaterStyle& style) {
  validate(style);
  if (!finite(surface_position) || !finite(eye) || !std::isfinite(depth_meters) || !(depth_meters > 0))
    throw std::invalid_argument("water sample requires finite positions and a positive depth");
  const math::Vec3 n{0, 1, 0};
  const auto view = (eye - surface_position).normalized();
  const double cos_theta = std::max(n.dot(view), 0.0);
  const double fresnel = 0.02 + 0.98 * std::pow(1.0 - cos_theta, 5.0);
  const double absorbed = 1.0 - std::exp(-depth_meters / style.absorption_meters);
  const math::Vec3 body = style.shallow_color * (1.0 - absorbed) + style.deep_color * absorbed;
  const math::Vec3 reflected = view * -1.0 + n * (2.0 * n.dot(view));
  const double up = std::clamp(reflected.y, 0.0, 1.0);
  const math::Vec3 sky = style.horizon_color * (1.0 - up) + style.zenith_color * up;
  const auto sun = style.light_direction.normalized();
  const double specular = std::pow(std::max(reflected.dot(sun), 0.0), style.specular_power) * style.specular_intensity;
  const double lit = 0.75 + 0.25 * std::max(n.dot(sun), 0.0);
  const double reflection = fresnel * style.reflectivity;
  WaterSample sample;
  sample.color = body * lit * (1.0 - reflection) + sky * reflection + math::Vec3{specular, specular, specular};
  sample.alpha = std::max(1.0 - std::exp(-depth_meters / style.clarity_meters), reflection);
  return sample;
}

}  // namespace terraflux::graphics
