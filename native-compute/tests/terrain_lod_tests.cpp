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
TerrainMesh patch_mesh(int width=5,int height=5,int offset=0) {
  std::vector<compute::GridCell> cells;
  for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
    compute::GridCell c{};c.x=offset+x;c.y=offset+y;
    c.elevation_meters=x*x+y*y;c.fuel_load=1;cells.push_back(c);
  }
  return build_terrain_mesh(compute::Grid(cells),30);
}
void patch_eligibility() {
  using S=TerrainLodPatchStatus;
  const auto check=[](const TerrainMesh& m,S status) {
    const auto r=analyze_terrain_lod_patches(m);
    require(r.size()==1 && r[0].status==status,"patch classification");
  };
  const auto full=patch_mesh();const auto original=full;
  check(full,S::Eligible);same_mesh(full,original);
  auto cyclic=full;
  for(std::size_t i=0;i<cyclic.indices.size();i+=3)
    std::rotate(cyclic.indices.begin()+i,cyclic.indices.begin()+i+1,cyclic.indices.begin()+i+3);
  check(cyclic,S::Eligible);
  auto reversed=full;std::swap(reversed.indices[0],reversed.indices[1]);
  check(reversed,S::NoncanonicalTopology);
  auto missing=full;missing.indices.erase(missing.indices.begin(),missing.indices.begin()+3);
  check(missing,S::Incomplete);
  auto hole=full;hole.indices.clear();
  for(std::size_t i=0;i<full.indices.size();i+=3)
    if(full.indices[i]!=12 && full.indices[i+1]!=12 && full.indices[i+2]!=12)
      hole.indices.insert(hole.indices.end(),full.indices.begin()+i,full.indices.begin()+i+3);
  check(hole,S::Incomplete);
  auto reordered=full;
  for(std::size_t i=0;i<full.indices.size()/2;i+=3)
    for(std::size_t j=0;j<3;++j)
      std::swap(reordered.indices[i+j],reordered.indices[full.indices.size()-i-3+j]);
  check(reordered,S::Eligible);
  auto duplicate=full;std::copy_n(duplicate.indices.begin(),3,duplicate.indices.begin()+3);
  check(duplicate,S::NoncanonicalTopology);
  auto diagonal=full;
  diagonal.indices[0]=0;diagonal.indices[1]=1;diagonal.indices[2]=6;
  diagonal.indices[3]=0;diagonal.indices[4]=6;diagonal.indices[5]=5;
  check(diagonal,S::NoncanonicalTopology);
  auto irregular=full;irregular.vertices[12].position.x+=0.01;
  check(irregular,S::IrregularCoordinates);
  auto alias=full;alias.vertices.push_back(alias.vertices[1]);
  alias.indices[1]=static_cast<std::uint32_t>(alias.vertices.size()-1);
  check(alias,S::NoncanonicalTopology);
  check(patch_mesh(4,5),S::Incomplete);
  check(patch_mesh(5,5,std::numeric_limits<std::int32_t>::max()-4),S::Eligible);
  check(patch_mesh(5,5,std::numeric_limits<std::int32_t>::min()),S::Eligible);
  const auto multiple=analyze_terrain_lod_patches(patch_mesh(10,6),TerrainTileOptions{5});
  require(multiple.size()==6,"nonempty partial patch reports only");
  std::size_t eligible=0;
  for(std::size_t i=0;i<multiple.size();++i) {
    eligible+=multiple[i].status==S::Eligible;
    if(i)require(multiple[i-1].tile_index<multiple[i].tile_index ||
      (multiple[i-1].tile_index==multiple[i].tile_index &&
       std::pair{multiple[i-1].patch_y,multiple[i-1].patch_x}<
       std::pair{multiple[i].patch_y,multiple[i].patch_x}),"stable report order");
  }
  require(eligible==2,"eligible patches remain independent of partial edges");
  require(analyze_terrain_lod_patches(TerrainMesh{}).empty(),"no records for empty space");
  const auto tiny=analyze_terrain_lod_patches(full,TerrainTileOptions{1});
  require(tiny.size()==16 && std::all_of(tiny.begin(),tiny.end(),[](auto p){return p.status==S::Incomplete;}),"one-quad tiles fall back");
  TerrainTileOptions cap;cap.max_tiles=0;
  rejects<std::length_error>([&]{analyze_terrain_lod_patches(full,cap);});
}
void run() {
  patch_eligibility();
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
