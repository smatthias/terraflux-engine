#pragma once
#include <terraflux/terrain_tiles.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/visibility.hpp>
#include <memory>

namespace terraflux::graphics {
struct TerrainTileDrawStats {
  std::size_t total=0,visible=0,culled=0,draws=0;
  std::uint64_t triangles=0;
};
inline constexpr std::uint32_t kMaxTerrainRetentionFrames=600;
struct TerrainResidencyChange {
  std::size_t admitted=0,evicted=0,retained=0,trimmed=0;
};
// Retained packed CPU recipes; independently owned, selectively resident GPU
// buffer pairs. Caller owns the pass, pipeline, camera and optional fire field.
class TerrainTileRenderer {
 public:
  TerrainTileRenderer(RenderDevice& device,const terrain::TerrainMesh& source,
                      terrain::TerrainTileOptions options={},bool initially_resident=true);
  TerrainTileRenderer(const TerrainTileRenderer&)=delete;
  TerrainTileRenderer& operator=(const TerrainTileRenderer&)=delete;
  TerrainTileRenderer(TerrainTileRenderer&&)=delete;
  TerrainTileRenderer& operator=(TerrainTileRenderer&&)=delete;
  // Between passes only. Stage every missing tile before committing the set.
  // Rejected candidates preserve current handles/set/bytes. Limit is final-set
  // count; global byte admission also covers old + staged allocations.
  TerrainResidencyChange set_resident_tiles(std::span<const std::size_t> ids);
  TerrainResidencyChange synchronize_visible(const Frustum& frustum,bool cull=true);
  // Hidden GPU tiles may survive this many successful visibility updates.
  // Zero preserves immediate eviction. Required visible tiles always win.
  void set_retention_frames(std::uint32_t frames);
  std::uint32_t retention_frames() const {return retention_frames_;}
  void set_residency_limit(std::size_t count) {residency_limit_=count;}
  std::size_t residency_limit() const {return residency_limit_;}
  std::vector<std::size_t> resident_tiles() const;
  std::size_t resident_tile_count() const;
  std::uint64_t resident_bytes() const;
  BufferHandle resident_vertex_buffer(std::size_t id) const;
  TerrainTileDrawStats draw(PipelineHandle pipeline,BufferHandle camera,
      std::optional<BufferHandle> fire,const Frustum& frustum,bool cull=true,
      std::optional<BufferHandle> layers={});
  std::size_t tile_count() const {return tiles_.size();}
  std::uint64_t geometry_bytes() const {return geometry_bytes_;} // All CPU recipes / fully resident payload.
 private:
  struct Resident {
    RenderResourceOwner resources;
    BufferHandle vertices,indices;
    Resident(RenderDevice& device,const TerrainUpload& recipe);
  };
  struct Tile {
    TerrainUpload recipe;
    math::Vec3 lo,hi;
    std::unique_ptr<Resident> gpu;
    std::uint32_t hidden_frames=0;
  };
  TerrainResidencyChange commit_resident_tiles(std::span<const std::size_t> ids);
  RenderDevice& device_;
  std::vector<Tile> tiles_;
  std::size_t residency_limit_=4096;
  std::uint32_t retention_frames_=0;
  std::uint64_t geometry_bytes_=0;
};
} // namespace terraflux::graphics
