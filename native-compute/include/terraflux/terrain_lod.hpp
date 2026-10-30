#pragma once
#include <terraflux/terrain_tiles.hpp>
#include <optional>

namespace terraflux::terrain {
// Level 1 is reserved for a later simplification increment. Its absence means
// exact level-0 fallback, not a second allocation or a zero-error measurement.
struct TerrainTileLod {
  TerrainTile level0;
  std::optional<TerrainTile> level1;
  std::optional<double> max_vertical_error;
};
struct TerrainLodOptions {
  TerrainTileOptions tiles{};
  // Used vertex, index and source-map elements only; excludes capacities,
  // containers, source mesh, temporary builders and future packed GPU recipes.
  std::size_t max_recipe_bytes=512ULL*1024*1024;
};
struct TerrainLodSet {
  std::vector<TerrainTileLod> tiles;
  std::size_t recipe_bytes=0;
};
// Exact partition order, geometry, metadata and source mappings. Output owns
// its data; source is untouched. No simplification or GPU work occurs yet.
// Invalid source/options use the tile partitioner's invalid_argument contract;
// aggregate payload/count overflow throws length_error before publication.
TerrainLodSet build_terrain_lods(const TerrainMesh& source,
                               TerrainLodOptions options={});
} // namespace terraflux::terrain
