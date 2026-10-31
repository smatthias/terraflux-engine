#include <terraflux/graphics/terrain_shading.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Five-stop ramp shared with the GLSL terrainHeightTint(): lowland green,
// meadow, dry grass, upland brown, pale summit. No channel is ever blue- or
// red-dominant, so viewer fire/asset pixel heuristics never match bare ground.
constexpr math::Vec3 kRamp[5] = {
    {0.16, 0.40, 0.22}, {0.42, 0.58, 0.28}, {0.72, 0.64, 0.38},
    {0.58, 0.44, 0.32}, {0.88, 0.86, 0.82}};
constexpr math::Vec3 kRock{0.47, 0.45, 0.44};

math::Vec3 mix(math::Vec3 a, math::Vec3 b, double t) { return a * (1.0 - t) + b * t; }

double smoothstep(double edge0, double edge1, double x) {
  const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

void validate(const TerrainStyle& style) {
  if (!std::isfinite(style.min_elevation_meters) || !std::isfinite(style.max_elevation_meters) ||
      style.min_elevation_meters > style.max_elevation_meters)
    throw std::invalid_argument("terrain style elevation range must be finite and ordered");
  if (!std::isfinite(style.contour_interval_meters) || style.contour_interval_meters < 0.0)
    throw std::invalid_argument("terrain contour interval must be finite and non-negative");
  if (!std::isfinite(style.rock_slope_start_degrees) || !std::isfinite(style.rock_slope_full_degrees) ||
      style.rock_slope_start_degrees < 0.0 || style.rock_slope_full_degrees > 90.0 ||
      !(style.rock_slope_start_degrees < style.rock_slope_full_degrees))
    throw std::invalid_argument("terrain rock slope angles must satisfy 0 <= start < full <= 90");
  if (!finite(style.light_direction) || !(style.light_direction.length() > 0.0))
    throw std::invalid_argument("terrain light direction must be a finite nonzero vector");
  if (!std::isfinite(style.ambient) || style.ambient < 0.0 || style.ambient > 1.0)
    throw std::invalid_argument("terrain ambient shade must be within [0, 1]");
}

double inverse_range(const TerrainStyle& style) {
  const double range = style.max_elevation_meters - style.min_elevation_meters;
  return range > 0.0 && std::isfinite(1.0 / range) ? 1.0 / range : 0.0;
}

}  // namespace

TerrainStyleUniform pack_terrain_style(const TerrainStyle& style) {
  validate(style);
  const auto light = style.light_direction.normalized();
  TerrainStyleUniform packed{};
  packed[0] = static_cast<float>(style.min_elevation_meters);
  packed[1] = static_cast<float>(style.max_elevation_meters);
  packed[2] = static_cast<float>(inverse_range(style));
  packed[3] = static_cast<float>(style.contour_interval_meters);
  packed[4] = static_cast<float>(style.rock_slope_start_degrees);
  packed[5] = static_cast<float>(style.rock_slope_full_degrees);
  packed[8] = static_cast<float>(light.x);
  packed[9] = static_cast<float>(light.y);
  packed[10] = static_cast<float>(light.z);
  packed[11] = static_cast<float>(style.ambient);
  for (const float value : packed)
    if (!std::isfinite(value)) throw std::invalid_argument("terrain style is not representable in float");
  return packed;
}

TerrainStyle terrain_style_for(const terrain::TerrainMesh& mesh, double contour_interval_meters) {
  TerrainStyle style;
  style.min_elevation_meters = mesh.min_elevation_meters;
  style.max_elevation_meters = mesh.max_elevation_meters;
  style.contour_interval_meters = contour_interval_meters;
  validate(style);
  return style;
}

math::Vec3 terrain_height_tint(double normalized_height) {
  const double s = std::clamp(normalized_height, 0.0, 1.0) * 4.0;
  if (s < 1.0) return mix(kRamp[0], kRamp[1], s);
  if (s < 2.0) return mix(kRamp[1], kRamp[2], s - 1.0);
  if (s < 3.0) return mix(kRamp[2], kRamp[3], s - 2.0);
  return mix(kRamp[3], kRamp[4], s - 3.0);
}

double terrain_rock_blend(double slope_degrees, const TerrainStyle& style) {
  validate(style);
  return smoothstep(style.rock_slope_start_degrees, style.rock_slope_full_degrees, slope_degrees);
}

math::Vec3 terrain_surface_color(double elevation_meters, math::Vec3 normal, const TerrainStyle& style) {
  validate(style);
  if (!std::isfinite(elevation_meters) || !finite(normal) || !(normal.length() > 0.0))
    throw std::invalid_argument("terrain surface color requires finite elevation and a nonzero normal");
  const auto n = normal.normalized();
  const double height = (elevation_meters - style.min_elevation_meters) * inverse_range(style);
  math::Vec3 tint = terrain_height_tint(height);
  const double slope = std::acos(std::clamp(n.y, 0.0, 1.0)) * 180.0 / kPi;
  tint = mix(tint, kRock, terrain_rock_blend(slope, style));
  const double diffuse = std::max(n.dot(style.light_direction.normalized()), 0.0);
  const double shade = style.ambient + (1.0 - style.ambient) * diffuse;
  return tint * shade;
}

}  // namespace terraflux::graphics
