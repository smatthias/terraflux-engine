#pragma once
#include <terraflux/terrain_mesh.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraflux::terrain {
struct TerrainTileOptions {
  std::uint32_t quads_per_side=32; // 1..256; boundary vertices are shared by copying.
  std::size_t max_tiles=4096;
  std::size_t max_total_vertices=4'000'000; // Includes copies at tile boundaries.
  std::size_t max_total_indices=24'000'000;
};
struct TerrainTile {
  std::uint32_t x=0,y=0; // Tile coordinates relative to the source grid origin.
  TerrainMesh mesh; // Same origin/coordinate system and normals as source.
  std::vector<std::uint32_t> source_vertex_indices; // Parallel to mesh.vertices.
  math::Vec3 bounds_min,bounds_max; // Inclusive bounds of referenced geometry.
};

// Partition a build_terrain_mesh surface into nonempty tiles, sorted by (y,x).
// Triangles must occupy one grid quad. Every triangle is copied exactly once;
// only referenced vertices are copied, with local uint32 indices. No gap fill,
// decimation, normal recomputation, GPU upload, or mutation of source occurs.
// Output count budgets bound payload elements, not vector capacity/process RSS.
// The complete source mesh is still required and retained by the caller.
std::vector<TerrainTile> tile_terrain_mesh(const TerrainMesh& source,
                                         TerrainTileOptions options={});
} // namespace terraflux::terrain
