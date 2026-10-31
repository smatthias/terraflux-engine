#include <terraflux/terrain_tiles.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace terraflux::terrain {
namespace {
bool finite(math::Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
struct TileBuilder {
  TerrainTile tile;
  std::unordered_map<std::uint32_t,std::uint32_t> local_indices;
};
}
std::vector<TerrainTile> tile_terrain_mesh(const TerrainMesh& source,TerrainTileOptions options) {
  if(!options.quads_per_side || options.quads_per_side>256)
    throw std::invalid_argument("terrain tile size must be 1..256 quads per side");
  if(!std::isfinite(source.cell_size_meters) || source.cell_size_meters<=0 ||
     !std::isfinite(source.min_elevation_meters) || !std::isfinite(source.max_elevation_meters) ||
     source.min_elevation_meters>source.max_elevation_meters ||
     source.vertices.size()>std::numeric_limits<std::uint32_t>::max() || source.indices.size()%3)
    throw std::invalid_argument("invalid source terrain mesh metadata or triangle count");
  if(source.indices.size()>options.max_total_indices)
    throw std::length_error("terrain tile index budget exceeded");
  for(const auto& vertex:source.vertices)
    if(!finite(vertex.position) || !finite(vertex.normal) ||
       vertex.grid_x<source.origin_grid_x || vertex.grid_y<source.origin_grid_y)
      throw std::invalid_argument("invalid source terrain vertex or grid origin");

  // Sparse ordered keys: a distant island never allocates intervening tiles.
  std::map<std::pair<std::uint32_t,std::uint32_t>,TileBuilder> builders;
  const auto max_tile_vertices=static_cast<std::size_t>(options.quads_per_side+1)*
                              (options.quads_per_side+1);
  const auto max_tile_indices=static_cast<std::size_t>(options.quads_per_side)*
                             options.quads_per_side*6;
  std::size_t total_vertices=0;
  for(std::size_t offset=0;offset<source.indices.size();offset+=3) {
    const std::array<std::uint32_t,3> indices{source.indices[offset],source.indices[offset+1],source.indices[offset+2]};
    for(const auto index:indices)
      if(index>=source.vertices.size())throw std::invalid_argument("terrain tile source index out of bounds");
    const auto& a=source.vertices[indices[0]];
    const auto& b=source.vertices[indices[1]];
    const auto& c=source.vertices[indices[2]];
    const auto min_x=std::min({a.grid_x,b.grid_x,c.grid_x});
    const auto min_y=std::min({a.grid_y,b.grid_y,c.grid_y});
    const auto max_x=std::max({a.grid_x,b.grid_x,c.grid_x});
    const auto max_y=std::max({a.grid_y,b.grid_y,c.grid_y});
    const auto same_cell=[](const TerrainVertex& lhs,const TerrainVertex& rhs) {
      return lhs.grid_x==rhs.grid_x && lhs.grid_y==rhs.grid_y;
    };
    if(static_cast<std::int64_t>(max_x)-min_x!=1 ||
       static_cast<std::int64_t>(max_y)-min_y!=1 ||
       same_cell(a,b) || same_cell(a,c) || same_cell(b,c))
      throw std::invalid_argument("terrain tile triangle must occupy three corners of one grid quad");
    const auto x=static_cast<std::uint32_t>((static_cast<std::int64_t>(min_x)-source.origin_grid_x)/options.quads_per_side);
    const auto y=static_cast<std::uint32_t>((static_cast<std::int64_t>(min_y)-source.origin_grid_y)/options.quads_per_side);
    const auto key=std::pair{y,x};
    auto it=builders.find(key);
    if(it==builders.end()) {
      if(builders.size()>=options.max_tiles)throw std::length_error("terrain tile count budget exceeded");
      it=builders.try_emplace(key).first;
      auto& tile=it->second.tile;
      tile.x=x;tile.y=y;
      tile.mesh.origin_grid_x=source.origin_grid_x;tile.mesh.origin_grid_y=source.origin_grid_y;
      tile.mesh.cell_size_meters=source.cell_size_meters;
      // Shading ranges are a property of the whole surface: every tile copies
      // the source range so height colors never shift across tile seams.
      tile.mesh.min_elevation_meters=source.min_elevation_meters;
      tile.mesh.max_elevation_meters=source.max_elevation_meters;
      tile.mesh.refinement=source.refinement;
      tile.mesh.simulation_cell_size_meters=source.simulation_cell_size_meters;
    }
    auto& builder=it->second;
    auto& tile=builder.tile;
    if(tile.mesh.indices.size()>max_tile_indices-3)
      throw std::invalid_argument("source exceeds terrain tile triangle capacity");
    for(const auto index:indices) {
      auto found=builder.local_indices.find(index);
      if(found==builder.local_indices.end()) {
        if(total_vertices>=options.max_total_vertices)
          throw std::length_error("terrain tile vertex budget exceeded");
        if(tile.mesh.vertices.size()>=max_tile_vertices)
          throw std::invalid_argument("source exceeds terrain tile vertex capacity");
        const auto& vertex=source.vertices[index];
        if(tile.mesh.vertices.empty())tile.bounds_min=tile.bounds_max=vertex.position;
        else {
          tile.bounds_min={std::min(tile.bounds_min.x,vertex.position.x),std::min(tile.bounds_min.y,vertex.position.y),std::min(tile.bounds_min.z,vertex.position.z)};
          tile.bounds_max={std::max(tile.bounds_max.x,vertex.position.x),std::max(tile.bounds_max.y,vertex.position.y),std::max(tile.bounds_max.z,vertex.position.z)};
        }
        const auto local=static_cast<std::uint32_t>(tile.mesh.vertices.size());
        tile.mesh.vertices.push_back(vertex);
        tile.source_vertex_indices.push_back(index);
        found=builder.local_indices.emplace(index,local).first;
        ++total_vertices;
      }
      tile.mesh.indices.push_back(found->second);
    }
  }
  std::vector<TerrainTile> result;
  result.reserve(builders.size());
  for(auto& [key,builder]:builders)result.push_back(std::move(builder.tile));
  return result;
}
} // namespace terraflux::terrain
