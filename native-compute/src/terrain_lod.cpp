#include <terraflux/terrain_lod.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraflux::terrain {
std::vector<TerrainLodPatch> analyze_terrain_lod_patches(
    const TerrainMesh& source,TerrainTileOptions options) {
  const auto tiles=tile_terrain_mesh(source,options);
  struct Patch {
    std::array<unsigned,16> mask{};
    std::map<std::pair<std::int32_t,std::int32_t>,std::uint32_t> vertices;
    bool topology=false,coordinates=false;
  };
  std::vector<TerrainLodPatch> result;
  for(std::size_t id=0;id<tiles.size();++id) {
    const auto& tile=tiles[id];
    const auto ox=static_cast<std::int64_t>(source.origin_grid_x)+
                  static_cast<std::int64_t>(tile.x)*options.quads_per_side;
    const auto oy=static_cast<std::int64_t>(source.origin_grid_y)+
                  static_cast<std::int64_t>(tile.y)*options.quads_per_side;
    std::map<std::pair<std::uint32_t,std::uint32_t>,Patch> patches;
    for(std::size_t t=0;t<tile.mesh.indices.size();t+=3) {
      std::array<std::uint32_t,3> indices{tile.mesh.indices[t],tile.mesh.indices[t+1],tile.mesh.indices[t+2]};
      auto min_x=std::numeric_limits<std::int32_t>::max(),min_y=min_x;
      for(auto i:indices) {
        min_x=std::min(min_x,tile.mesh.vertices[i].grid_x);
        min_y=std::min(min_y,tile.mesh.vertices[i].grid_y);
      }
      const auto qx=static_cast<std::uint32_t>(static_cast<std::int64_t>(min_x)-ox);
      const auto qy=static_cast<std::uint32_t>(static_cast<std::int64_t>(min_y)-oy);
      auto& patch=patches[{qy/4,qx/4}];
      std::array<unsigned,3> corners{};
      for(std::size_t j=0;j<3;++j) {
        const auto i=indices[j];const auto& v=tile.mesh.vertices[i];
        corners[j]=static_cast<unsigned>((static_cast<std::int64_t>(v.grid_y)-min_y)*2+
                                        static_cast<std::int64_t>(v.grid_x)-min_x);
        const auto [it,inserted]=patch.vertices.emplace(std::pair{v.grid_y,v.grid_x},tile.source_vertex_indices[i]);
        if(!inserted && it->second!=tile.source_vertex_indices[i])patch.topology=true;
        const double x=static_cast<double>(static_cast<std::int64_t>(v.grid_x)-source.origin_grid_x)*source.cell_size_meters;
        const double z=-static_cast<double>(static_cast<std::int64_t>(v.grid_y)-source.origin_grid_y)*source.cell_size_meters;
        if(v.position.x!=x || v.position.z!=z)patch.coordinates=true;
      }
      const auto cyclic=[&](std::array<unsigned,3> expected) {
        for(std::size_t r=0;r<3;++r)
          if(corners[r]==expected[0] && corners[(r+1)%3]==expected[1] && corners[(r+2)%3]==expected[2])return true;
        return false;
      };
      const unsigned bit=cyclic({0,1,2})?1:cyclic({1,3,2})?2:0;
      auto& mask=patch.mask[(qy%4)*4+qx%4];
      if(!bit || (mask&bit))patch.topology=true;
      mask|=bit;
    }
    for(const auto& [key,p]:patches) {
      const auto status=p.coordinates?TerrainLodPatchStatus::IrregularCoordinates:
                        p.topology?TerrainLodPatchStatus::NoncanonicalTopology:
                        std::all_of(p.mask.begin(),p.mask.end(),[](auto m){return m==3;})?
                          TerrainLodPatchStatus::Eligible:TerrainLodPatchStatus::Incomplete;
      result.push_back({id,key.second,key.first,status});
    }
  }
  return result;
}
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
