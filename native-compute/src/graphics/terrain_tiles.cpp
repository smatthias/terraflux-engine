#include <terraflux/graphics/terrain_tiles.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <algorithm>

namespace terraflux::graphics {
TerrainTileRenderer::TerrainTileRenderer(RenderDevice& device,const terrain::TerrainMesh& source,
    terrain::TerrainTileOptions options):device_(device),resources_(device) {
  const auto tiles=terrain::tile_terrain_mesh(source,options);
  tiles_.reserve(tiles.size());
  for(const auto& tile:tiles) {
    const auto upload=prepare_terrain_upload(tile.mesh);
    const auto vertices=std::as_bytes(std::span(upload.vertices));
    const auto indices=std::as_bytes(std::span(upload.indices));
    const auto vb=resources_.create_buffer({BufferUsage::Vertex,vertices.size()},vertices);
    const auto ib=resources_.create_buffer({BufferUsage::Index,indices.size()},indices);
    // Cull the packed geometry actually submitted to the GPU, not rounded-away
    // double-precision extrema. Frustum tests also retain boundary tolerance.
    const auto& first=upload.vertices.front().position;
    math::Vec3 lo{first[0],first[1],first[2]},hi=lo;
    for(const auto& v:upload.vertices) {
      lo={std::min(lo.x,double(v.position[0])),std::min(lo.y,double(v.position[1])),std::min(lo.z,double(v.position[2]))};
      hi={std::max(hi.x,double(v.position[0])),std::max(hi.y,double(v.position[1])),std::max(hi.z,double(v.position[2]))};
    }
    tiles_.push_back({vb,ib,static_cast<std::uint32_t>(upload.indices.size()),lo,hi});
    geometry_bytes_+=vertices.size()+indices.size();
  }
}
TerrainTileDrawStats TerrainTileRenderer::draw(PipelineHandle pipeline,BufferHandle camera,
    std::optional<BufferHandle> fire,const Frustum& frustum,bool cull) {
  if(!device_.active_render_pass_id())throw std::logic_error("terrain tiles require an active pass");
  TerrainTileDrawStats stats;stats.total=tiles_.size();
  for(const auto& tile:tiles_) {
    if(cull && !frustum.intersects_aabb(tile.lo,tile.hi)){++stats.culled;continue;}
    DrawIndexed command{pipeline,tile.vertices,tile.indices,0,tile.index_count,{{0,camera}}, {}};
    if(fire)command.storage_buffers.push_back({1,*fire});
    device_.draw_indexed(command);
    ++stats.visible;++stats.draws;stats.triangles+=tile.index_count/3;
  }
  return stats;
}
} // namespace terraflux::graphics
