#include <terraflux/graphics/terrain_shading.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Palette shared with the GLSL in opengl_device.cpp (kTerrainStyleGlsl). No
// entry is blue- or red-dominant, so the viewer's fire/asset pixel heuristics
// never classify bare ground, vegetation, rock, snow or ice as fire or assets.
constexpr math::Vec3 kRamp[5] = {
    {0.16, 0.40, 0.22}, {0.42, 0.58, 0.28}, {0.72, 0.64, 0.38},
    {0.58, 0.44, 0.32}, {0.88, 0.86, 0.82}};
constexpr math::Vec3 kGrassDry{0.76, 0.66, 0.36}, kGrassSemi{0.55, 0.58, 0.28}, kGrassWet{0.18, 0.47, 0.20};
constexpr math::Vec3 kTreeDry{0.42, 0.40, 0.19}, kTreeSemi{0.22, 0.38, 0.15}, kTreeWet{0.11, 0.33, 0.13};
constexpr math::Vec3 kRockLow{0.40, 0.38, 0.36}, kRockHigh{0.60, 0.58, 0.56}, kScree{0.66, 0.63, 0.58};
constexpr math::Vec3 kSnow{0.93, 0.94, 0.96}, kIce{0.70, 0.85, 0.94};
// Lake/sea floor seen through water: sand, aquatic plants, mud, stones.
constexpr math::Vec3 kFloorSand{0.74, 0.68, 0.50}, kFloorVegetation{0.12, 0.32, 0.16};
constexpr math::Vec3 kFloorMud{0.32, 0.28, 0.22}, kFloorStones{0.46, 0.44, 0.40};

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
  if (!std::isfinite(style.snow_line_fraction) || style.snow_line_fraction < 0.0 || style.snow_line_fraction > 1.0)
    throw std::invalid_argument("terrain snow line must be a fraction within [0, 1]");
  if (!std::isfinite(style.ridge_curvature) || !(style.ridge_curvature > 0.0))
    throw std::invalid_argument("terrain ridge curvature must be positive");
  if (!std::isfinite(style.detail_strength) || style.detail_strength < 0.0 || style.detail_strength > 1.0)
    throw std::invalid_argument("terrain detail strength must be within [0, 1]");
  if (!std::isfinite(style.rock_relief_meters) || style.rock_relief_meters < 0.0 || style.rock_relief_meters > 20.0)
    throw std::invalid_argument("rock relief must be 0..20 meters");
  if (!std::isfinite(style.canopy_on_ground) || style.canopy_on_ground < 0.0 || style.canopy_on_ground > 1.0)
    throw std::invalid_argument("canopy on ground must be 0..1");
  if (style.submerged_level_meters && !std::isfinite(*style.submerged_level_meters))
    throw std::invalid_argument("submerged level must be finite");
  const auto& f = style.floor;
  if (static_cast<unsigned>(f.kind) > static_cast<unsigned>(WaterFloorKind::Mixed))
    throw std::invalid_argument("unknown water floor kind");
  const double weights[4] = {f.sand, f.vegetation, f.mud, f.stones};
  double sum = 0.0;
  for (const double w : weights) {
    if (!std::isfinite(w) || w < 0.0) throw std::invalid_argument("water floor weights must be finite and non-negative");
    sum += w;
  }
  if (!(sum > 0.0)) throw std::invalid_argument("water floor weights must not all be zero");
  if (!std::isfinite(f.patch_size_meters) || f.patch_size_meters < 5.0 || f.patch_size_meters > 500.0)
    throw std::invalid_argument("water floor patch size must be 5..500 meters");
}

// Zoned floor weights shared with the GLSL floorWeights(): one-hot for single
// kinds; for Mixed the configured weights are modulated by depth and bed slope.
std::array<double, 4> floor_weights(double depth, double slope_degrees, const WaterFloorStyle& floor) {
  std::array<double, 4> w{};
  switch (floor.kind) {
    case WaterFloorKind::Sand: w[0] = 1; return w;
    case WaterFloorKind::Vegetation: w[1] = 1; return w;
    case WaterFloorKind::Mud: w[2] = 1; return w;
    case WaterFloorKind::Stones: w[3] = 1; return w;
    case WaterFloorKind::Mixed: break;
  }
  const double gentle = 1.0 - smoothstep(12.0, 25.0, slope_degrees);
  w[0] = floor.sand * ((1.0 - smoothstep(2.0, 6.0, depth)) * gentle + 0.15);
  w[1] = floor.vegetation * 1.2 * smoothstep(0.5, 1.5, depth) * (1.0 - smoothstep(6.0, 12.0, depth)) *
         (1.0 - smoothstep(14.0, 26.0, slope_degrees));
  w[2] = floor.mud * (smoothstep(3.0, 8.0, depth) * (1.0 - smoothstep(6.0, 14.0, slope_degrees)) + 0.05);
  w[3] = floor.stones * (smoothstep(8.0, 20.0, slope_degrees) + 0.10 * (1.0 - smoothstep(1.5, 4.0, depth)));
  const double sum = w[0] + w[1] + w[2] + w[3];
  if (!(sum > 1e-9)) return {1.0, 0.0, 0.0, 0.0};
  for (auto& value : w) value /= sum;
  return w;
}

double inverse_range(const TerrainStyle& style) {
  const double range = style.max_elevation_meters - style.min_elevation_meters;
  return range > 0.0 && std::isfinite(1.0 / range) ? 1.0 / range : 0.0;
}

}  // namespace

WaterFloorStyle random_water_floor(std::uint32_t seed) {
  // Small integer hash: reproducible weights from the seed alone.
  std::uint32_t h = seed * 2654435761u + 0x9E3779B9u;
  const auto next = [&] {
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return 0.2 + 1.3 * (static_cast<double>(h & 0xFFFFFFu) / 16777216.0);  // 0.2 .. 1.5
  };
  WaterFloorStyle floor;
  floor.kind = WaterFloorKind::Mixed;
  floor.seed = seed;
  floor.sand = next(); floor.vegetation = next(); floor.mud = next(); floor.stones = next();
  floor.patch_size_meters = 25.0 + 50.0 * (next() - 0.2) / 1.3;  // 25 .. 75
  return floor;
}

const char* water_floor_name(WaterFloorKind kind) {
  switch (kind) {
    case WaterFloorKind::Sand: return "sand";
    case WaterFloorKind::Vegetation: return "vegetation";
    case WaterFloorKind::Mud: return "mud";
    case WaterFloorKind::Stones: return "stones";
    case WaterFloorKind::Mixed: return "mixed";
  }
  throw std::invalid_argument("unknown water floor kind");
}

WaterFloorKind parse_water_floor(const char* name) {
  const std::string text = name ? name : "";
  for (const auto kind : {WaterFloorKind::Sand, WaterFloorKind::Vegetation, WaterFloorKind::Mud,
                          WaterFloorKind::Stones, WaterFloorKind::Mixed})
    if (text == water_floor_name(kind)) return kind;
  throw std::invalid_argument("water floor must be sand, vegetation, mud, stones or mixed");
}

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
  packed[6] = static_cast<float>(style.snow_line_fraction);
  packed[7] = static_cast<float>(style.ridge_curvature);
  packed[8] = static_cast<float>(light.x);
  packed[9] = static_cast<float>(light.y);
  packed[10] = static_cast<float>(light.z);
  packed[11] = static_cast<float>(style.ambient);
  packed[12] = static_cast<float>(style.detail_strength);
  packed[13] = static_cast<float>(style.rock_relief_meters);
  packed[14] = static_cast<float>(style.canopy_on_ground);
  packed[16] = static_cast<float>(style.submerged_level_meters.value_or(0.0));
  packed[17] = style.submerged_level_meters ? static_cast<float>(static_cast<int>(style.floor.kind)) : -1.0f;
  packed[18] = static_cast<float>(style.floor.patch_size_meters);
  packed[19] = static_cast<float>((style.floor.seed % 1024u) * 37.31);  // world-space patch offset
  const double total = style.floor.sand + style.floor.vegetation + style.floor.mud + style.floor.stones;
  packed[20] = static_cast<float>(style.floor.sand / total);
  packed[21] = static_cast<float>(style.floor.vegetation / total);
  packed[22] = static_cast<float>(style.floor.mud / total);
  packed[23] = static_cast<float>(style.floor.stones / total);
  for (const float value : packed)
    if (!std::isfinite(value)) throw std::invalid_argument("terrain style is not representable in float");
  return packed;
}

double auto_contour_interval(double elevation_range_meters) {
  if (!std::isfinite(elevation_range_meters) || !(elevation_range_meters > 0.0)) return 0.0;
  const double raw = elevation_range_meters / 10.0;
  const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
  const double mantissa = raw / magnitude;
  const double nice = mantissa < 1.5 ? 1.0 : mantissa < 2.25 ? 2.0 : mantissa < 3.5 ? 2.5 : mantissa < 7.5 ? 5.0 : 10.0;
  return nice * magnitude;
}

double auto_snow_line(double elevation_range_meters) {
  return std::isfinite(elevation_range_meters) && elevation_range_meters >= kSnowReliefMeters ? 0.72 : 1.0;
}

TerrainStyle terrain_style_for(const terrain::TerrainMesh& mesh, double contour_interval_meters) {
  TerrainStyle style;
  style.min_elevation_meters = mesh.min_elevation_meters;
  style.max_elevation_meters = mesh.max_elevation_meters;
  const double range = mesh.max_elevation_meters - mesh.min_elevation_meters;
  style.contour_interval_meters = contour_interval_meters < 0.0 ? auto_contour_interval(range) : contour_interval_meters;
  style.snow_line_fraction = auto_snow_line(range);
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

TerrainMaterial terrain_material(const TerrainSurfacePoint& point, const TerrainStyle& style) {
  validate(style);
  if (!std::isfinite(point.elevation_meters) || !finite(point.normal) || !(point.normal.length() > 0.0) ||
      !std::isfinite(point.fuel_load) || !std::isfinite(point.fuel_moisture) || !std::isfinite(point.curvature))
    throw std::invalid_argument("terrain surface point requires finite attributes and a nonzero normal");
  const auto n = point.normal.normalized();
  TerrainMaterial m;
  m.height = std::clamp((point.elevation_meters - style.min_elevation_meters) * inverse_range(style), 0.0, 1.0);
  m.slope_degrees = std::acos(std::clamp(n.y, 0.0, 1.0)) * 180.0 / kPi;
  const double fuel = std::clamp(point.fuel_load, 0.0, 1.0);
  const double moisture = std::clamp(point.fuel_moisture, 0.0, 1.0);
  const double convexity = std::clamp(point.curvature / style.ridge_curvature, -1.0, 1.0);

  // Vegetation: moisture classes and canopy cover from fuel load, thinning
  // above the tree line and on steep ground. Bare ground keeps the height ramp.
  const double toward_semi = smoothstep(0.10, 0.20, moisture);
  const double toward_wet = smoothstep(0.26, 0.40, moisture);
  m.wet = toward_wet;
  m.semi_dry = toward_semi * (1.0 - toward_wet);
  m.dry = (1.0 - toward_semi) * (1.0 - toward_wet);
  const math::Vec3 grass = mix(mix(kGrassDry, kGrassSemi, toward_semi), kGrassWet, toward_wet);
  // With 3D trees above, the ground under the canopy shows litter (darkened grass).
  const math::Vec3 canopy = mix(grass * 0.55, mix(mix(kTreeDry, kTreeSemi, toward_semi), kTreeWet, toward_wet),
                                style.canopy_on_ground);
  m.trees = smoothstep(0.45, 0.80, fuel) * (1.0 - smoothstep(0.62, 0.78, m.height)) *
            (1.0 - smoothstep(30.0, 42.0, m.slope_degrees));
  m.vegetation = smoothstep(0.04, 0.25, fuel);
  const math::Vec3 ground = mix(terrain_height_tint(m.height), grass, m.vegetation);

  // Rock on steep slopes; exposed crests become scree/rock even when gentle.
  const double steep = smoothstep(style.rock_slope_start_degrees, style.rock_slope_full_degrees, m.slope_degrees);
  m.ridge = smoothstep(0.20, 0.70, convexity) * smoothstep(0.30, 0.60, m.height);
  m.rock = std::max(steep, 0.85 * m.ridge);
  const math::Vec3 stone = mix(mix(kRockLow, kRockHigh, m.height), kScree, m.ridge * (1.0 - steep));
  // Composition order is shared with the GLSL terrainCompose().
  math::Vec3 albedo = mix(mix(ground, canopy, m.trees * m.vegetation), stone, m.rock);

  // Snow above the snow line, blown off crests and absent on cliffs; ice in
  // flat high basins and on flat summit caps.
  const double snow_height = m.height - 0.12 * m.ridge;
  m.snow = smoothstep(style.snow_line_fraction - 0.05, style.snow_line_fraction + 0.05, snow_height) *
           (1.0 - smoothstep(42.0, 60.0, m.slope_degrees));
  if (style.snow_line_fraction >= 1.0) m.snow = 0.0;
  const double flat = 1.0 - smoothstep(6.0, 14.0, m.slope_degrees);
  const double basin = 1.0 - smoothstep(-0.35, -0.05, convexity);
  m.ice = m.snow * flat * std::max(basin, 0.6 * smoothstep(0.93, 0.99, m.height));
  albedo = mix(albedo, kSnow, m.snow);
  albedo = mix(albedo, kIce, m.ice);

  // Lake/sea floor replaces every land material below the water line.
  if (style.submerged_level_meters) {
    const double level = *style.submerged_level_meters;
    m.depth_meters = std::max(level - point.elevation_meters, 0.0);
    m.submerged = 1.0 - smoothstep(level - 0.3, level + 0.3, point.elevation_meters);
    const auto w = floor_weights(m.depth_meters, m.slope_degrees, style.floor);
    m.floor_sand = w[0]; m.floor_vegetation = w[1]; m.floor_mud = w[2]; m.floor_stones = w[3];
    const math::Vec3 floor = kFloorSand * w[0] + kFloorVegetation * w[1] + kFloorMud * w[2] + kFloorStones * w[3];
    albedo = mix(albedo, floor, m.submerged);
  }
  m.albedo = albedo;
  return m;
}

TerrainMaterialClass dominant_material(const TerrainMaterial& m) {
  if (m.submerged > 0.5) {
    const double w[4] = {m.floor_sand, m.floor_vegetation, m.floor_mud, m.floor_stones};
    const auto best = static_cast<std::size_t>(std::max_element(w, w + 4) - w);
    return static_cast<TerrainMaterialClass>(static_cast<std::size_t>(TerrainMaterialClass::FloorSand) + best);
  }
  if (m.ice > 0.5) return TerrainMaterialClass::Ice;
  if (m.snow > 0.5) return TerrainMaterialClass::Snow;
  if (m.rock > 0.5) return m.ridge > 0.5 ? TerrainMaterialClass::Ridge : TerrainMaterialClass::Rock;
  if (m.vegetation <= 0.5) return TerrainMaterialClass::Bare;
  if (m.trees > 0.5) return TerrainMaterialClass::Forest;
  if (m.wet >= m.semi_dry && m.wet >= m.dry) return TerrainMaterialClass::WetVegetation;
  if (m.semi_dry >= m.dry) return TerrainMaterialClass::SemiDryVegetation;
  return TerrainMaterialClass::DryVegetation;
}

const char* terrain_material_name(TerrainMaterialClass material) {
  switch (material) {
    case TerrainMaterialClass::Bare: return "bare";
    case TerrainMaterialClass::DryVegetation: return "dry_vegetation";
    case TerrainMaterialClass::SemiDryVegetation: return "semi_dry_vegetation";
    case TerrainMaterialClass::WetVegetation: return "wet_vegetation";
    case TerrainMaterialClass::Forest: return "forest";
    case TerrainMaterialClass::Rock: return "rock";
    case TerrainMaterialClass::Ridge: return "ridge";
    case TerrainMaterialClass::Snow: return "snow";
    case TerrainMaterialClass::Ice: return "ice";
    case TerrainMaterialClass::FloorSand: return "floor_sand";
    case TerrainMaterialClass::FloorVegetation: return "floor_vegetation";
    case TerrainMaterialClass::FloorMud: return "floor_mud";
    case TerrainMaterialClass::FloorStones: return "floor_stones";
  }
  throw std::invalid_argument("unknown terrain material class");
}

math::Vec3 terrain_surface_color(const TerrainSurfacePoint& point, const TerrainStyle& style) {
  const auto material = terrain_material(point, style);
  const auto n = point.normal.normalized();
  const double diffuse = std::max(n.dot(style.light_direction.normalized()), 0.0);
  const double shade = style.ambient + (1.0 - style.ambient) * diffuse;
  return material.albedo * shade;
}

math::Vec3 terrain_surface_color(double elevation_meters, math::Vec3 normal, const TerrainStyle& style) {
  return terrain_surface_color(TerrainSurfacePoint{elevation_meters, normal}, style);
}

}  // namespace terraflux::graphics
