#pragma once
#include <terraflux/terrain_tiles.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/visibility.hpp>

namespace terraflux::graphics {
struct TerrainTileDrawStats {
  std::size_t total=0,visible=0,culled=0,draws=0;
  std::uint64_t triangles=0;
};
// Eager immutable full-resolution uploads, owned independently of scene assets.
// Caller owns the active pass, terrain/wildfire pipeline, camera and fire field.
class TerrainTileRenderer {
 public:
  TerrainTileRenderer(RenderDevice& device,const terrain::TerrainMesh& source,
                      terrain::TerrainTileOptions options={});
  TerrainTileDrawStats draw(PipelineHandle pipeline,BufferHandle camera,
      std::optional<BufferHandle> fire,const Frustum& frustum,bool cull=true);
  std::size_t tile_count() const {return tiles_.size();}
  std::uint64_t geometry_bytes() const {return geometry_bytes_;}
 private:
  struct Tile {
    BufferHandle vertices,indices;
    std::uint32_t index_count=0;
    math::Vec3 lo,hi;
  };
  RenderDevice& device_;
  RenderResourceOwner resources_;
  std::vector<Tile> tiles_;
  std::uint64_t geometry_bytes_=0;
};
} // namespace terraflux::graphics
