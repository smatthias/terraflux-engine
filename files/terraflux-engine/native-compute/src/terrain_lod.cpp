#include <terraflux/terrain_lod.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <span>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraflux::terrain {
namespace {
std::vector<TerrainLodPatch> analyze_tiles(
    const TerrainMesh& source,const std::vector<TerrainTile>& tiles,TerrainTileOptions options) {
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
std::optional<TerrainTile> simplify_tile(const TerrainTile& tile,
    std::span<const TerrainLodPatch> reports,std::uint32_t side) {
  using Key=std::pair<std::uint32_t,std::uint32_t>;
  std::map<Key,bool> eligible;
  bool any=false;
  for(const auto& p:reports) {
    const bool ok=p.status==TerrainLodPatchStatus::Eligible;
    eligible[{p.patch_y,p.patch_x}]=ok;any|=ok;
  }
  if(!any)return std::nullopt;
  const auto ox=static_cast<std::int64_t>(tile.mesh.origin_grid_x)+static_cast<std::int64_t>(tile.x)*side;
  const auto oy=static_cast<std::int64_t>(tile.mesh.origin_grid_y)+static_cast<std::int64_t>(tile.y)*side;
  std::map<Key,std::vector<std::size_t>> groups;
  for(std::size_t t=0;t<tile.mesh.indices.size();t+=3) {
    auto x=std::numeric_limits<std::int32_t>::max(),y=x;
    for(std::size_t j=0;j<3;++j) {
      const auto& v=tile.mesh.vertices[tile.mesh.indices[t+j]];
      x=std::min(x,v.grid_x);y=std::min(y,v.grid_y);
    }
    groups[{static_cast<std::uint32_t>((static_cast<std::int64_t>(y)-oy)/4),
            static_cast<std::uint32_t>((static_cast<std::int64_t>(x)-ox)/4)}].push_back(t);
  }
  TerrainTile out;
  out.x=tile.x;out.y=tile.y;out.bounds_min=tile.bounds_min;out.bounds_max=tile.bounds_max;
  out.mesh.origin_grid_x=tile.mesh.origin_grid_x;out.mesh.origin_grid_y=tile.mesh.origin_grid_y;
  out.mesh.cell_size_meters=tile.mesh.cell_size_meters;
  out.mesh.min_elevation_meters=tile.mesh.min_elevation_meters;
  out.mesh.max_elevation_meters=tile.mesh.max_elevation_meters;
  out.mesh.refinement=tile.mesh.refinement;
  out.mesh.simulation_cell_size_meters=tile.mesh.simulation_cell_size_meters;
  std::map<std::uint32_t,std::uint32_t> compact;
  const auto emit=[&](std::uint32_t old) {
    auto [it,inserted]=compact.emplace(old,static_cast<std::uint32_t>(out.mesh.vertices.size()));
    if(inserted) {
      out.mesh.vertices.push_back(tile.mesh.vertices[old]);
      out.source_vertex_indices.push_back(tile.source_vertex_indices[old]);
    }
    out.mesh.indices.push_back(it->second);
  };
  for(const auto& [key,triangles]:groups) {
    if(!eligible.at(key)) {
      for(auto t:triangles)for(std::size_t j=0;j<3;++j)emit(tile.mesh.indices[t+j]);
      continue;
    }
    std::map<std::pair<std::int64_t,std::int64_t>,std::uint32_t> vertices;
    for(auto t:triangles)for(std::size_t j=0;j<3;++j) {
      const auto i=tile.mesh.indices[t+j];const auto& v=tile.mesh.vertices[i];
      vertices[{v.grid_y,v.grid_x}]=i;
    }
    const auto px=ox+static_cast<std::int64_t>(key.second)*4;
    const auto py=oy+static_cast<std::int64_t>(key.first)*4;
    std::array<std::uint32_t,16> ring{};
    for(int i=0;i<4;++i) {
      ring[i]=vertices.at({py,px+i});
      ring[4+i]=vertices.at({py+i,px+4});
      ring[8+i]=vertices.at({py+4,px+4-i});
      ring[12+i]=vertices.at({py+4-i,px});
    }
    const auto center=vertices.at({py+2,px+2});
    for(std::size_t i=0;i<ring.size();++i) {emit(center);emit(ring[i]);emit(ring[(i+1)%ring.size()]);}
  }
  return out;
}
} // namespace
std::vector<TerrainLodPatch> analyze_terrain_lod_patches(
    const TerrainMesh& source,TerrainTileOptions options) {
  return analyze_tiles(source,tile_terrain_mesh(source,options),options);
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
  const auto reports=options.enable_level1?analyze_tiles(source,partition,tile_options):std::vector<TerrainLodPatch>{};
  std::size_t vertices=0,indices=0;
  for(const auto& tile:partition) {vertices+=tile.mesh.vertices.size();indices+=tile.mesh.indices.size();}
  result.tiles.reserve(partition.size());
  std::size_t report=0;
  for(std::size_t id=0;id<partition.size();++id) {
    auto& tile=partition[id];std::optional<TerrainTile> coarse;
    if(options.enable_level1) {
      const auto begin=report;
      while(report<reports.size() && reports[report].tile_index==id)++report;
      coarse=simplify_tile(tile,std::span<const TerrainLodPatch>(reports).subspan(begin,report-begin),tile_options.quads_per_side);
    }
    if(coarse) {
      if(coarse->mesh.vertices.size()>options.tiles.max_total_vertices-vertices ||
         coarse->mesh.indices.size()>options.tiles.max_total_indices-indices)
        throw std::length_error("terrain LOD aggregate element budget exceeded");
      vertices+=coarse->mesh.vertices.size();indices+=coarse->mesh.indices.size();
      charge(coarse->mesh.vertices.size(),sizeof(TerrainVertex));
      charge(coarse->mesh.indices.size(),sizeof(std::uint32_t));
      charge(coarse->source_vertex_indices.size(),sizeof(std::uint32_t));
    }
    result.tiles.push_back({std::move(tile),std::move(coarse),std::nullopt});
  }
  return result;
}
} // namespace terraflux::terrain
