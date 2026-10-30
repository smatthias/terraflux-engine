#pragma once

#include <terraflux/grid.hpp>
#include <terraflux/math.hpp>

#include <cstdint>
#include <vector>

namespace terraflux::terrain {

// GPU-backend-independent triangulated surface. Coordinates use a local origin:
// +X east, +Y elevation (meters), -Z north. Grid cell indices remain available
// for associating a vertex with a simulation's wildfire state.
struct TerrainVertex {
  math::Vec3 position;
  math::Vec3 normal;
  std::int32_t grid_x = 0, grid_y = 0;
};

struct TerrainMesh {
  std::vector<TerrainVertex> vertices;
  std::vector<std::uint32_t> indices;  // CCW triangles when viewed from above
  std::int32_t origin_grid_x = 0, origin_grid_y = 0;
  double cell_size_meters = 30.0;
};

// Tessellate each complete 2x2 block of grid cells into two triangles.
// Cells with unavailable elevations have no vertex; missing cells and voids
// prevent the corresponding quad from being triangulated. No mesh triangles
// may bridge gaps. Pre-fill elevations with compute::fill_elevation_voids when
// interpolation is desired; measured elevations are never changed here.
// Vertex/triangle order is coordinate-stable regardless of original cell order.
// This is geometry only: it does not affect fire propagation or create a
// rendering context, GPU resources, or proprietary platform dependencies.
TerrainMesh build_terrain_mesh(const compute::Grid& grid, double cell_size_meters);

}  // namespace terraflux::terrain
