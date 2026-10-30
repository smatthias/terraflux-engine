#include <terraflux/graphics/terrain_tiles.hpp>
#include <algorithm>
#include <numeric>

namespace terraflux::graphics {
TerrainTileRenderer::Resident::Resident(RenderDevice& device,const TerrainUpload& recipe):resources(device) {
  const auto vertex_data=std::as_bytes(std::span(recipe.vertices));
  const auto index_data=std::as_bytes(std::span(recipe.indices));
  vertices=resources.create_buffer({BufferUsage::Vertex,vertex_data.size()},vertex_data);
  indices=resources.create_buffer({BufferUsage::Index,index_data.size()},index_data);
}
TerrainTileRenderer::TerrainTileRenderer(RenderDevice& device,const terrain::TerrainMesh& source,
    terrain::TerrainTileOptions options,bool initially_resident):device_(device),residency_limit_(options.max_tiles) {
  const auto tiles=terrain::tile_terrain_mesh(source,options);
  tiles_.reserve(tiles.size());
  for(const auto& tile:tiles) {
    auto upload=prepare_terrain_upload(tile.mesh);
    const auto& first=upload.vertices.front().position;
    math::Vec3 lo{first[0],first[1],first[2]},hi=lo;
    for(const auto& v:upload.vertices) {
      lo={std::min(lo.x,double(v.position[0])),std::min(lo.y,double(v.position[1])),std::min(lo.z,double(v.position[2]))};
      hi={std::max(hi.x,double(v.position[0])),std::max(hi.y,double(v.position[1])),std::max(hi.z,double(v.position[2]))};
    }
    geometry_bytes_+=upload.vertices.size()*sizeof(TerrainGpuVertex)+upload.indices.size()*sizeof(std::uint32_t);
    tiles_.push_back({std::move(upload),lo,hi,nullptr});
  }
  if(initially_resident) {
    std::vector<std::size_t> ids(tiles_.size());std::iota(ids.begin(),ids.end(),0);
    set_resident_tiles(ids);
  }
}
std::vector<std::size_t> TerrainTileRenderer::resident_tiles() const {
  std::vector<std::size_t> ids;
  for(std::size_t i=0;i<tiles_.size();++i)if(tiles_[i].gpu)ids.push_back(i);
  return ids;
}
std::size_t TerrainTileRenderer::resident_tile_count() const {
  return std::count_if(tiles_.begin(),tiles_.end(),[](const auto& tile){return bool(tile.gpu);});
}
std::uint64_t TerrainTileRenderer::resident_bytes() const {
  std::uint64_t bytes=0;
  for(const auto& tile:tiles_)if(tile.gpu)
    bytes+=tile.recipe.vertices.size()*sizeof(TerrainGpuVertex)+tile.recipe.indices.size()*sizeof(std::uint32_t);
  return bytes;
}
BufferHandle TerrainTileRenderer::resident_vertex_buffer(std::size_t id) const {
  const auto& tile=tiles_.at(id);return tile.gpu?tile.gpu->vertices:BufferHandle{};
}
TerrainResidencyChange TerrainTileRenderer::set_resident_tiles(std::span<const std::size_t> ids) {
  if(device_.active_render_pass_id())throw std::logic_error("terrain residency changes require ended passes");
  if(ids.size()>residency_limit_)throw std::length_error("terrain resident tile count limit exceeded");
  std::vector<bool> wanted(tiles_.size(),false);
  for(const auto id:ids) {
    if(id>=tiles_.size() || wanted[id])throw std::invalid_argument("invalid or duplicate terrain tile residency ID");
    wanted[id]=true;
  }
  std::vector<std::pair<std::size_t,std::unique_ptr<Resident>>> staged;
  staged.reserve(ids.size());
  for(const auto id:ids)if(!tiles_[id].gpu)
    staged.emplace_back(id,std::make_unique<Resident>(device_,tiles_[id].recipe));
  TerrainResidencyChange change{staged.size(),0};
  for(auto& [id,gpu]:staged)tiles_[id].gpu=std::move(gpu);
  for(std::size_t i=0;i<tiles_.size();++i)if(!wanted[i] && tiles_[i].gpu) {
    tiles_[i].gpu.reset();++change.evicted;
  }
  return change;
}
TerrainResidencyChange TerrainTileRenderer::synchronize_visible(const Frustum& frustum,bool cull) {
  std::vector<std::size_t> wanted;
  for(std::size_t i=0;i<tiles_.size();++i)
    if(!cull || frustum.intersects_aabb(tiles_[i].lo,tiles_[i].hi))wanted.push_back(i);
  return set_resident_tiles(wanted);
}
TerrainTileDrawStats TerrainTileRenderer::draw(PipelineHandle pipeline,BufferHandle camera,
    std::optional<BufferHandle> fire,const Frustum& frustum,bool cull) {
  if(!device_.active_render_pass_id())throw std::logic_error("terrain tiles require an active pass");
  // Reject an incomplete visible set before submitting any part of the surface.
  for(const auto& tile:tiles_)
    if(!tile.gpu && (!cull || frustum.intersects_aabb(tile.lo,tile.hi)))
      throw std::logic_error("visible terrain tile is not GPU resident");
  TerrainTileDrawStats stats;stats.total=tiles_.size();
  for(const auto& tile:tiles_) {
    if(cull && !frustum.intersects_aabb(tile.lo,tile.hi)){++stats.culled;continue;}
    DrawIndexed command{pipeline,tile.gpu->vertices,tile.gpu->indices,0,
        static_cast<std::uint32_t>(tile.recipe.indices.size()),{{0,camera}}, {}};
    if(fire)command.storage_buffers.push_back({1,*fire});
    device_.draw_indexed(command);
    ++stats.visible;++stats.draws;stats.triangles+=tile.recipe.indices.size()/3;
  }
  return stats;
}
} // namespace terraflux::graphics
