#pragma once

#include <terraflux/math.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace terraflux::graphics {

// Backend-neutral description of how the Terrain and Wildfire shaders color
// bare ground, vegetation, rock, ridges, snow and ice. Height uses a relative
// hypsometric ramp across the mesh's own elevation range; slope, convexity and
// the per-vertex fuel load / fuel moisture select materials; a directional
// hillshade lights the result. Optional contour lines mark equal elevation and
// procedural detail (noise, canopy clumps, strata, cracks, sparkle) adds
// texture on the GPU only. Fire/burn overlays and layer visibility are separate.
// What shows through shallow water. Single kinds cover the whole bed; Mixed
// zones the four kinds by depth and bed slope (sand on gentle beds down to
// about 2 m, plants in the sunlit 1.5..6 m band, mud deeper and flat, stones on
// steep beds and in the surf) weighted by the relative weights, and GPU detail
// breaks the zones into seeded patches of `patch_size_meters`.
enum class WaterFloorKind : std::uint8_t { Sand, Vegetation, Mud, Stones, Mixed };
struct WaterFloorStyle {
  WaterFloorKind kind = WaterFloorKind::Mixed;
  double sand = 1.0, vegetation = 1.0, mud = 1.0, stones = 1.0; // relative weights for Mixed, >= 0, sum > 0
  double patch_size_meters = 45.0;                             // 5 .. 500
  std::uint32_t seed = 0;                                      // offsets the patch layout
};
// Weights and seed derived from a seed alone: "random floor" that is still
// reproducible from its printed seed.
WaterFloorStyle random_water_floor(std::uint32_t seed);
const char* water_floor_name(WaterFloorKind kind);
// Parses sand|vegetation|mud|stones|mixed; throws on anything else.
WaterFloorKind parse_water_floor(const char* name);

struct TerrainStyle {
  double min_elevation_meters = 0.0, max_elevation_meters = 0.0; // equal: uniform lowland tint
  double contour_interval_meters = 0.0;                          // 0 disables contour lines
  double rock_slope_start_degrees = 28.0, rock_slope_full_degrees = 48.0;
  math::Vec3 light_direction{-0.4545, 0.7660, 0.4545}; // toward the sun: south-west, 50 degrees up
  double ambient = 0.30;                                 // shade of a surface facing away from the sun
  double snow_line_fraction = 1.0;   // relative height where snow begins; 1 disables snow and ice
  double ridge_curvature = 0.12;     // vertex curvature (slope change per cell) of a fully exposed crest
  double detail_strength = 1.0;      // 0 disables GPU procedural detail (exact CPU reference parity)
  // Visualization-only vertical displacement of rock vertices by ridged crag
  // noise, in meters (0 .. 20). Scaled by detail strength, damped under snow
  // and ice, absent below and just above the water line; measured elevations,
  // fire lookups and the CPU reference are unaffected.
  double rock_relief_meters = 5.0;
  // How much of the forest canopy is painted on the ground (0 .. 1). With 3D
  // trees drawn above, lower values show forest-floor litter instead so the
  // canopy is not drawn twice.
  double canopy_on_ground = 1.0;
  // Ground below this level renders as lake/sea floor instead of land (blending
  // over +-0.3 m around the line). Unset: no water, everything is land.
  std::optional<double> submerged_level_meters;
  WaterFloorStyle floor;
};

// Uniform block "TerrainStyle" at this slot, std140, exactly six vec4s:
//   vec4 elevationRange: min, max, 1/(max-min) or 0 when flat, contour interval
//   vec4 slopeStyle:     rock start degrees, rock full degrees, snow line fraction, ridge curvature
//   vec4 sunLight:       unit direction toward the light, ambient
//   vec4 detail:         detail strength, rock relief meters, canopy on ground, 0
//   vec4 waterFloor:     submerged level (0 when unset), kind index (-1 none, 0..4), patch size, seed offset
//   vec4 floorWeights:   normalized sand, vegetation, mud, stones weights
inline constexpr std::uint32_t kTerrainStyleSlot = 11;
inline constexpr std::size_t kTerrainStyleBytes = 96;
using TerrainStyleUniform = std::array<float, 24>;
static_assert(sizeof(TerrainStyleUniform) == kTerrainStyleBytes);

// Rejects non-finite values, an inverted elevation range, negative contour
// spacing, rock angles outside 0 <= start < full <= 90, a zero light vector,
// ambient outside [0, 1], a snow line outside [0, 1], a non-positive ridge
// curvature and detail outside [0, 1]. The light direction is normalized.
TerrainStyleUniform pack_terrain_style(const TerrainStyle& style);

// Style spanning the mesh's recorded elevation range. A negative contour
// interval selects auto_contour_interval() for that range; the snow line is
// auto_snow_line() for that range.
TerrainStyle terrain_style_for(const terrain::TerrainMesh& mesh,
                               double contour_interval_meters = -1.0);

// A 1-2-2.5-5 x 10^k spacing yielding roughly 8..16 contour levels across the
// range; zero for a flat or non-positive range.
double auto_contour_interval(double elevation_range_meters);
// Snow and ice appear above 72% of the range once the relief reaches 300 m;
// gentler terrain gets 1.0 (no snow). The relative line keeps summits
// recognizable in any data set; callers with absolute knowledge override it.
inline constexpr double kSnowReliefMeters = 300.0;
double auto_snow_line(double elevation_range_meters);

// One shaded point of the surface. The normal need not be unit length.
struct TerrainSurfacePoint {
  double elevation_meters = 0.0;
  math::Vec3 normal{0.0, 1.0, 0.0};
  double fuel_load = 0.0;      // 0 bare .. 1 dense vegetation
  double fuel_moisture = 0.0;  // 0 dry .. 1 wet
  double curvature = 0.0;      // TerrainVertex::curvature; crests positive
};

// Material weights and the unlit base albedo, mirroring the GLSL
// terrainMaterial() exactly. Weights are blend factors in [0, 1]; vegetation
// moisture classes (dry, semi-dry, wet) sum to one.
struct TerrainMaterial {
  math::Vec3 albedo;
  double height = 0.0, slope_degrees = 0.0;
  double vegetation = 0.0, trees = 0.0, rock = 0.0, ridge = 0.0, snow = 0.0, ice = 0.0;
  double dry = 1.0, semi_dry = 0.0, wet = 0.0;
  // Lake/sea floor: submerged is the blend from land (0) to floor (1); the
  // four floor weights sum to one whenever submerged is above zero.
  double submerged = 0.0, depth_meters = 0.0;
  double floor_sand = 0.0, floor_vegetation = 0.0, floor_mud = 0.0, floor_stones = 0.0;
};
enum class TerrainMaterialClass : std::uint8_t {
  Bare, DryVegetation, SemiDryVegetation, WetVegetation, Forest, Rock, Ridge, Snow, Ice,
  FloorSand, FloorVegetation, FloorMud, FloorStones
};
inline constexpr std::size_t kTerrainMaterialClassCount = 13;
TerrainMaterial terrain_material(const TerrainSurfacePoint& point, const TerrainStyle& style);
TerrainMaterialClass dominant_material(const TerrainMaterial& material);
const char* terrain_material_name(TerrainMaterialClass material);

// CPU reference implementation of the GLSL ground color with detail disabled:
// material albedo times the hillshade, before contour lines, procedural
// detail, fire/burn overlays and 8-bit quantization. Pixel tests compare the
// GPU image against it; the two must change together.
math::Vec3 terrain_height_tint(double normalized_height);                   // hypsometric ramp, 0..1
double terrain_rock_blend(double slope_degrees, const TerrainStyle& style);  // 0 soil .. 1 rock
math::Vec3 terrain_surface_color(const TerrainSurfacePoint& point, const TerrainStyle& style);
// Bare ground (no fuel, flat curvature) convenience overload.
math::Vec3 terrain_surface_color(double elevation_meters, math::Vec3 normal,
                                 const TerrainStyle& style);

}  // namespace terraflux::graphics
