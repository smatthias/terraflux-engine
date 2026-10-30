#include <terraflux/terrain_lod.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace terraflux;
using namespace terraflux::terrain;
namespace {
void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
template<class Error,class F> void rejects(F f) {
  try {f();}catch(const Error&) {return;}
  throw std::runtime_error("expected rejection");
}
bool equal(math::Vec3 a,math::Vec3 b) {return a.x==b.x && a.y==b.y && a.z==b.z;}
void same_mesh(const TerrainMesh& a,const TerrainMesh& b) {
  require(a.indices==b.indices && a.vertices.size()==b.vertices.size(),"exact topology");
  require(a.origin_grid_x==b.origin_grid_x && a.origin_grid_y==b.origin_grid_y &&
          a.cell_size_meters==b.cell_size_meters,"exact metadata");
  for(std::size_t i=0;i<a.vertices.size();++i) {
    const auto& x=a.vertices[i];const auto& y=b.vertices[i];
    require(equal(x.position,y.position) && equal(x.normal,y.normal) &&
            x.grid_x==y.grid_x && x.grid_y==y.grid_y,"exact vertex and grid identity");
  }
}
void verify(const TerrainMesh& source) {
  TerrainLodOptions options;options.tiles.quads_per_side=2;
  const auto original=source;
  const auto expected=tile_terrain_mesh(source,options.tiles);
  auto actual=build_terrain_lods(source,options);
  require(actual.tiles.size()==expected.size(),"stable tile count");
  std::size_t bytes=0;
  for(std::size_t i=0;i<expected.size();++i) {
    const auto& a=actual.tiles[i];const auto& b=expected[i];
    require(!a.level1 && !a.max_vertical_error,"no invented simplification or error");
    require(a.level0.x==b.x && a.level0.y==b.y,"stable spatial tile IDs");
    same_mesh(a.level0.mesh,b.mesh);
    require(a.level0.source_vertex_indices==b.source_vertex_indices,"exact source map");
    require(equal(a.level0.bounds_min,b.bounds_min) && equal(a.level0.bounds_max,b.bounds_max),"exact bounds");
    const auto pa=graphics::prepare_terrain_upload(a.level0.mesh);
    const auto pb=graphics::prepare_terrain_upload(b.mesh);
    require(pa.indices==pb.indices,"packed indices");
    for(std::size_t v=0;v<pa.vertices.size();++v)for(int axis=0;axis<3;++axis)
      require(pa.vertices[v].position[axis]==pb.vertices[v].position[axis] &&
              pa.vertices[v].normal[axis]==pb.vertices[v].normal[axis],"packed seam parity");
    bytes+=b.mesh.vertices.size()*sizeof(TerrainVertex)+
           (b.mesh.indices.size()+b.source_vertex_indices.size())*sizeof(std::uint32_t);
  }
  require(bytes==actual.recipe_bytes,"logical payload includes boundary copies and source maps");
  options.max_recipe_bytes=bytes;
  require(build_terrain_lods(source,options).recipe_bytes==bytes,"exact byte budget succeeds");
  if(bytes) {
    options.max_recipe_bytes=bytes-1;
    rejects<std::length_error>([&]{build_terrain_lods(source,options);});
    same_mesh(source,original);
    actual.tiles.front().level0.mesh.vertices.front().position.x+=100;
    same_mesh(source,original);
    options.max_recipe_bytes=std::numeric_limits<std::size_t>::max();
    options.tiles.max_total_indices=0;
    rejects<std::length_error>([&]{build_terrain_lods(source,options);});
    options.tiles.max_total_indices=24'000'000;options.tiles.max_tiles=0;
    rejects<std::length_error>([&]{build_terrain_lods(source,options);});
  }
}
void run() {
  std::vector<compute::GridCell> cells;
  for(int y=0;y<7;++y)for(int x=0;x<8;++x) {
    if(x==3 && y==3)continue;
    compute::GridCell c{};c.x=x-20;c.y=y+50;
    c.elevation_meters=0.4*x*x+0.7*y*y;c.fuel_load=1;
    if(x==5 && y==4)c.elevation_meters.reset();
    cells.push_back(c);
  }
  const auto mesh=build_terrain_mesh(compute::Grid(cells),30);
  verify(mesh);
  std::reverse(cells.begin(),cells.end());
  const auto permuted=build_terrain_mesh(compute::Grid(cells),30);
  same_mesh(mesh,permuted);verify(permuted);
  TerrainMesh empty;verify(empty);
  TerrainLodOptions zero;zero.max_recipe_bytes=0;
  require(build_terrain_lods(empty,zero).tiles.empty(),"empty zero cap");
  rejects<std::length_error>([&]{build_terrain_lods(mesh,zero);});
  auto malformed=mesh;malformed.indices[0]=std::numeric_limits<std::uint32_t>::max();
  rejects<std::invalid_argument>([&]{build_terrain_lods(malformed);});
  TerrainLodOptions invalid;invalid.tiles.quads_per_side=0;
  rejects<std::invalid_argument>([&]{build_terrain_lods(mesh,invalid);});
}
}
int main() {
  try {run();std::cout<<"Terrain LOD level-0 tests passed\n";return 0;}
  catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
