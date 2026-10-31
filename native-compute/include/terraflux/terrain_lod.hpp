#pragma once
#include <terraflux/terrain_tiles.hpp>
#include <optional>

namespace terraflux::terrain {
enum class TerrainLodPatchStatus { Eligible, Incomplete, NoncanonicalTopology, IrregularCoordinates };
struct TerrainLodPatch {
  std::size_t tile_index=0; // Stable coordinate-sorted partition index.
  std::uint32_t patch_x=0,patch_y=0; // 4-quad patch coordinates within the tile.
  TerrainLodPatchStatus status=TerrainLodPatchStatus::Incomplete;
};
// Reports only patches with source triangles, sorted by tile then patch (Y,X).
// Empty space creates no records. Strict canonical X/Z coordinates and unique
// source vertices are required. Unsupported valid topology falls back; malformed
// tile input still throws. Source and output geometry remain unchanged.
std::vector<TerrainLodPatch> analyze_terrain_lod_patches(
    const TerrainMesh& source,TerrainTileOptions options={});
// Level 1 is opt-in. Its absence means
// exact level-0 fallback, not a second allocation or a zero-error measurement.
struct TerrainTileLod {
  TerrainTile level0;
  std::optional<TerrainTile> level1;
  std::optional<double> max_vertical_error;
};
struct TerrainLodOptions {
  TerrainTileOptions tiles{};
  bool enable_level1=false; // Complete 4x4 patches only; no GPU integration.
  // Used vertex, index and source-map elements only; excludes capacities,
  // containers, source mesh, temporary builders and future packed GPU recipes.
  std::size_t max_recipe_bytes=512ULL*1024*1024;
};
struct TerrainLodSet {
  std::vector<TerrainTileLod> tiles;
  std::size_t recipe_bytes=0;
};
// Exact partition order, geometry, metadata and source mappings. Output owns
// its data; source is untouched. Optional level 1 preserves all patch boundaries.
// Error measurement remains absent; this is not ready for automatic selection.
// Element/byte caps include both stored levels. No GPU work occurs.
// Invalid source/options use the tile partitioner's invalid_argument contract;
// aggregate payload/count overflow throws length_error before publication.
TerrainLodSet build_terrain_lods(const TerrainMesh& source,
                               TerrainLodOptions options={});
} // namespace terraflux::terrain
