#include <terraflux/terrain_lod.hpp>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace terraflux::terrain {
TerrainLodSet build_terrain_lods(const TerrainMesh& source,TerrainLodOptions options) {
  constexpr auto vertex_bytes=sizeof(TerrainVertex)+sizeof(std::uint32_t);
  // Necessary per-category limits reject oversized output during partitioning.
  // The combined cap is checked below before the completed result is returned.
  auto tile_options=options.tiles;
  tile_options.max_total_vertices=std::min(tile_options.max_total_vertices,
                                         options.max_recipe_bytes/vertex_bytes);
  tile_options.max_total_indices=std::min(tile_options.max_total_indices,
                                        options.max_recipe_bytes/sizeof(std::uint32_t));
  auto partition=tile_terrain_mesh(source,tile_options);
  TerrainLodSet result;
  const auto charge=[&](std::size_t count,std::size_t width) {
    const auto remaining=options.max_recipe_bytes-result.recipe_bytes;
    if(count>remaining/width)throw std::length_error("terrain LOD recipe byte budget exceeded");
    result.recipe_bytes+=count*width;
  };
  for(const auto& tile:partition) {
    charge(tile.mesh.vertices.size(),sizeof(TerrainVertex));
    charge(tile.mesh.indices.size(),sizeof(std::uint32_t));
    charge(tile.source_vertex_indices.size(),sizeof(std::uint32_t));
  }
  result.tiles.reserve(partition.size());
  for(auto& tile:partition)
    result.tiles.push_back({std::move(tile),std::nullopt,std::nullopt});
  return result;
}
} // namespace terraflux::terrain
