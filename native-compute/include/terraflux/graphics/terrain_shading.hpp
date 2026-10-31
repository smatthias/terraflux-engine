#pragma once

#include <terraflux/math.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace terraflux::graphics {

// Backend-neutral description of how the Terrain and Wildfire shaders color
// bare ground. Height is shown as a relative hypsometric ramp spanning the
// mesh's own elevation range, slope as a blend toward rock beyond a start
// angle plus a directional hillshade, and optional contour lines mark equal
// elevation. Fire/burn overlays and layer visibility are separate.
struct TerrainStyle {
  double min_elevation_meters = 0.0, max_elevation_meters = 0.0; // equal: uniform lowland tint
  double contour_interval_meters = 0.0;                          // 0 disables contour lines
  double rock_slope_start_degrees = 28.0, rock_slope_full_degrees = 48.0;
  math::Vec3 light_direction{-0.4545, 0.7660, 0.4545}; // toward the sun: south-west, 50 degrees up
  double ambient = 0.30;                                 // shade of a surface facing away from the sun
};

// Uniform block "TerrainStyle" at this slot, std140, exactly three vec4s:
//   vec4 elevationRange: min, max, 1/(max-min) or 0 when flat, contour interval
//   vec4 slopeStyle:     rock start degrees, rock full degrees, 0, 0
//   vec4 sunLight:       unit direction toward the light, ambient
inline constexpr std::uint32_t kTerrainStyleSlot = 11;
inline constexpr std::size_t kTerrainStyleBytes = 48;
using TerrainStyleUniform = std::array<float, 12>;
static_assert(sizeof(TerrainStyleUniform) == kTerrainStyleBytes);

// Rejects non-finite values, an inverted elevation range, negative contour
// spacing, rock angles outside 0 <= start < full <= 90, a zero light vector
// and ambient outside [0, 1]. The light direction is normalized on packing.
TerrainStyleUniform pack_terrain_style(const TerrainStyle& style);

// Style spanning the mesh's recorded elevation range.
TerrainStyle terrain_style_for(const terrain::TerrainMesh& mesh,
                               double contour_interval_meters = 0.0);

// CPU reference implementation of the GLSL ground color, before contour
// lines, fire/burn overlays and 8-bit quantization. Pixel tests compare the
// GPU image against it; the two must change together.
math::Vec3 terrain_height_tint(double normalized_height);        // hypsometric ramp, 0..1
double terrain_rock_blend(double slope_degrees, const TerrainStyle& style); // 0 soil .. 1 rock
math::Vec3 terrain_surface_color(double elevation_meters, math::Vec3 normal,
                                 const TerrainStyle& style);

}  // namespace terraflux::graphics
