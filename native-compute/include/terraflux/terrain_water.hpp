#pragma once

#include <terraflux/math.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraflux::terrain {

// One vertex of the still-water surface at a constant level above a terrain
// mesh. The bed height and its horizontal gradient come from the terrain
// vertex directly beneath, so a renderer interpolating them across the same
// triangles recovers the exact piecewise-linear bed: depth = level - bed.
struct WaterVertex {
  math::Vec3 position;          // (x, level, z) in the terrain's local frame
  double bed_height_meters = 0; // terrain elevation beneath this vertex
  math::Vec2 bed_gradient;      // d(bed)/dX, d(bed)/dZ in meters per meter; points toward rising ground
  std::uint32_t source_vertex = 0; // index into the terrain mesh
};

struct WaterMesh {
  std::vector<WaterVertex> vertices;
  std::vector<std::uint32_t> indices; // CCW from above, same winding as the terrain
  double level_meters = 0.0;
  double max_depth_meters = 0.0;      // over included vertices; 0 when nothing is submerged
  std::size_t submerged_vertices = 0; // included vertices whose bed lies below the level
};

// Still-water plane at `level_meters` clipped to the terrain: every terrain
// triangle with at least one corner below the level is copied (triangles
// straddling the shoreline keep the land corners, whose bed height is above
// the level, so the renderer can fade or discard them per pixel). Holes in the
// terrain remain holes; nothing is invented beyond the measured surface. The
// gradient is derived from the terrain's smooth normal and capped at a slope
// of 87 degrees. Cell coordinates, normals and the terrain itself are untouched.
// Rejects a non-finite level. A level at or below the lowest vertex yields an
// empty mesh.
WaterMesh build_water_mesh(const TerrainMesh& terrain, double level_meters);

}  // namespace terraflux::terrain
