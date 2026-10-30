#include <terraflux/terrain_tiles.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/terrain_tiles.hpp>
#include <terraflux/graphics/render_pass.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

using namespace terraflux;
using namespace terraflux::terrain;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class Error=std::invalid_argument,class F> void rejects(F&& f) {
  bool caught=false;try{f();}catch(const Error&){caught=true;}
  require(caught,"expected terrain tile rejection");
}
compute::GridCell cell(int x,int y,std::optional<double> height) {
  compute::GridCell result{};result.x=x;result.y=y;result.elevation_meters=height;
  result.fuel_load=1;result.fuel_moisture=0.15;return result;
}
bool equal(math::Vec3 a,math::Vec3 b) {return a.x==b.x && a.y==b.y && a.z==b.z;}
bool equal(const TerrainVertex& a,const TerrainVertex& b) {
  return equal(a.position,b.position) && equal(a.normal,b.normal) && a.grid_x==b.grid_x && a.grid_y==b.grid_y;
}
std::vector<compute::GridCell> curved_cells() {
  std::vector<compute::GridCell> cells;
  for(int y=0;y<6;++y)for(int x=0;x<7;++x)
    cells.push_back(cell(x-12,y+31,20+0.3*x*x+0.7*y*y+0.2*x*y));
  return cells;
}
void verify(const TerrainMesh& source,const std::vector<TerrainTile>& tiles,std::uint32_t side) {
  std::multiset<std::array<std::uint32_t,3>> expected,actual;
  for(std::size_t i=0;i<source.indices.size();i+=3)
    expected.insert({source.indices[i],source.indices[i+1],source.indices[i+2]});
  const auto packed_source=graphics::prepare_terrain_upload(source);
  std::optional<std::pair<std::uint32_t,std::uint32_t>> previous;
  for(const auto& tile:tiles) {
    const auto key=std::pair{tile.y,tile.x};
    require(!previous || *previous<key,"tiles sorted by row then column");previous=key;
    require(!tile.mesh.indices.empty() && tile.mesh.indices.size()%3==0,"only nonempty triangle tiles emitted");
    require(tile.mesh.vertices.size()<=(side+1)*(side+1) && tile.mesh.indices.size()<=side*side*6,
            "per-tile topology bounded by quad dimensions");
    require(tile.mesh.origin_grid_x==source.origin_grid_x && tile.mesh.origin_grid_y==source.origin_grid_y &&
            tile.mesh.cell_size_meters==source.cell_size_meters,"tiles retain global local-origin metadata");
    require(tile.source_vertex_indices.size()==tile.mesh.vertices.size(),"source map parallels local vertices");
    auto lo=tile.mesh.vertices.front().position,hi=lo;
    const auto packed=graphics::prepare_terrain_upload(tile.mesh);
    for(std::size_t i=0;i<tile.mesh.vertices.size();++i) {
      const auto index=tile.source_vertex_indices[i];
      require(index<source.vertices.size() && equal(tile.mesh.vertices[i],source.vertices[index]),
              "source positions, grid identity and global normals copied exactly");
      const auto& v=tile.mesh.vertices[i];
      const auto x=static_cast<std::int64_t>(v.grid_x)-source.origin_grid_x-static_cast<std::int64_t>(tile.x)*side;
      const auto y=static_cast<std::int64_t>(v.grid_y)-source.origin_grid_y-static_cast<std::int64_t>(tile.y)*side;
      require(x>=0 && x<=side && y>=0 && y<=side,"vertex lies inside inclusive tile grid footprint");
      lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
      hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};
      for(std::size_t axis=0;axis<3;++axis)
        require(packed.vertices[i].position[axis]==packed_source.vertices[index].position[axis] &&
                packed.vertices[i].normal[axis]==packed_source.vertices[index].normal[axis],
                "GPU packed seams exactly match monolithic positions and normals");
    }
    require(equal(lo,tile.bounds_min) && equal(hi,tile.bounds_max),"AABB exactly bounds referenced geometry");
    for(std::size_t i=0;i<tile.mesh.indices.size();i+=3) {
      std::array<std::uint32_t,3> original{};
      for(std::size_t j=0;j<3;++j) {
        const auto local=tile.mesh.indices[i+j];
        require(local<tile.mesh.vertices.size(),"local triangle index stays within tile");
        original[j]=tile.source_vertex_indices[local];
      }
      actual.insert(original);
    }
  }
  require(actual==expected,"every source triangle appears exactly once with original winding");
}
void dense_coverage_and_seams() {
  const compute::Grid grid(curved_cells());
  const auto source=build_terrain_mesh(grid,15);const auto original=source;
  const auto tiles=tile_terrain_mesh(source,{2});
  require(tiles.size()==9,"nondivisible dense surface emits partial edge tiles");
  verify(source,tiles,2);
  std::map<std::uint32_t,std::size_t> copies;
  for(const auto& tile:tiles)for(const auto index:tile.source_vertex_indices)++copies[index];
  require(std::any_of(copies.begin(),copies.end(),[](const auto& item){return item.second==4;}),
          "four-way seams share exact source vertices");
  require(source.indices==original.indices && source.vertices.size()==original.vertices.size(),"partition preserves source topology");
  for(std::size_t i=0;i<source.vertices.size();++i)require(equal(source.vertices[i],original.vertices[i]),"source geometry unchanged");
  require(grid.cells.front().elevation_meters==20 && grid.cells.front().fuel_moisture==0.15,
          "render partition leaves simulation cell data untouched");
  const auto per_quad=tile_terrain_mesh(source,{1});
  require(per_quad.size()==30,"one-quad tile case");verify(source,per_quad,1);
}
void stable_under_permutation() {
  auto cells=curved_cells();const auto a=build_terrain_mesh(compute::Grid(cells),7.5);
  std::reverse(cells.begin(),cells.end());const auto b=build_terrain_mesh(compute::Grid(cells),7.5);
  const auto left=tile_terrain_mesh(a,{2}),right=tile_terrain_mesh(b,{2});
  require(left.size()==right.size(),"input order does not change tile count");
  for(std::size_t i=0;i<left.size();++i) {
    require(left[i].x==right[i].x && left[i].y==right[i].y &&
            left[i].mesh.indices==right[i].mesh.indices &&
            left[i].source_vertex_indices==right[i].source_vertex_indices,"stable tile/vertex/index ordering");
    for(std::size_t j=0;j<left[i].mesh.vertices.size();++j)
      require(equal(left[i].mesh.vertices[j],right[i].mesh.vertices[j]),"stable normal and position copies");
  }
}
void sparse_holes_and_islands() {
  auto cells=curved_cells();
  cells.erase(std::remove_if(cells.begin(),cells.end(),[](const auto& c){return c.x==-10 && c.y==33;}),cells.end());
  cells[0].elevation_meters=std::nullopt;
  cells.push_back(cell(1000000000,31,9));cells.push_back(cell(1000000001,31,9));
  cells.push_back(cell(1000000000,32,9));cells.push_back(cell(1000000001,32,9));
  cells.push_back(cell(50,60,4)); // Isolated measured cell has no drawable tile.
  const auto source=build_terrain_mesh(compute::Grid(cells),1);
  const auto tiles=tile_terrain_mesh(source,{2});verify(source,tiles,2);
  require(tiles.size()<=10,"sparse islands do not allocate intervening tile grid");
  require(std::any_of(tiles.begin(),tiles.end(),[](const auto& t){return t.x>100000000;}),"distant island retained");
  std::size_t copied=0;for(const auto& tile:tiles)for(const auto& v:tile.mesh.vertices)
    if(v.grid_x==50 && v.grid_y==60)++copied;
  require(copied==0,"unreferenced vertices are omitted from draw payload");
}
void count_budgets() {
  const auto source=build_terrain_mesh(compute::Grid(curved_cells()),30);
  const auto tiles=tile_terrain_mesh(source,{2});
  TerrainTileOptions exact{2,tiles.size(),0,source.indices.size()};
  for(const auto& tile:tiles)exact.max_total_vertices+=tile.mesh.vertices.size();
  verify(source,tile_terrain_mesh(source,exact),2);
  auto bad=exact;--bad.max_tiles;rejects<std::length_error>([&]{tile_terrain_mesh(source,bad);});
  bad=exact;--bad.max_total_vertices;rejects<std::length_error>([&]{tile_terrain_mesh(source,bad);});
  bad=exact;--bad.max_total_indices;rejects<std::length_error>([&]{tile_terrain_mesh(source,bad);});
  rejects<std::length_error>([&]{tile_terrain_mesh(source,{2,0,0,0});});
  require(tile_terrain_mesh(TerrainMesh{},{2,0,0,0}).empty(),"zero budgets accept empty draw payload");
  require(source.indices.size()==180,"failed candidates preserve input geometry");
}
void malformed_inputs() {
  const auto source=build_terrain_mesh(compute::Grid(curved_cells()),30);
  rejects([&]{tile_terrain_mesh(source,{0});});rejects([&]{tile_terrain_mesh(source,{257});});
  auto bad=source;bad.indices.pop_back();rejects([&]{tile_terrain_mesh(bad);});
  bad=source;bad.indices[0]=static_cast<std::uint32_t>(bad.vertices.size());rejects([&]{tile_terrain_mesh(bad);});
  bad=source;bad.indices[0]=bad.indices[1];rejects([&]{tile_terrain_mesh(bad);});
  bad=source;bad.indices={0,3,7};rejects([&]{tile_terrain_mesh(bad);});
  bad=source;bad.origin_grid_x=0;rejects([&]{tile_terrain_mesh(bad);});
  bad=source;bad.vertices[0].position.y=std::numeric_limits<double>::infinity();rejects([&]{tile_terrain_mesh(bad);});
  bad=source;bad.vertices[0].normal.z=std::numeric_limits<double>::quiet_NaN();rejects([&]{tile_terrain_mesh(bad);});
  bad=source;bad.cell_size_meters=0;rejects([&]{tile_terrain_mesh(bad);});
}
void gpu_uploads_and_culling() {
  using namespace graphics;
  ValidatingRenderDevice device;
  RenderResourceOwner shared(device);
  const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const auto camera=shared.create_buffer({BufferUsage::Uniform,sizeof(identity)},std::as_bytes(std::span(identity)));
  const auto pipeline=shared.create_pipeline({ShaderProgramId::Terrain,terrain_shader_layout()});
  const auto frustum=Frustum::from_view_projection(math::Mat4::identity());
  require(frustum.intersects_aabb({1,0,0},{2,1,1}),"AABB touching frustum retained");
  require(!frustum.intersects_aabb({1.1,0,0},{2,1,1}),"outside AABB rejected");
  for(const auto offset:std::array<math::Vec3,6>{{{3,0,0},{-3,0,0},{0,3,0},{0,-3,0},{0,0,3},{0,0,-3}}})
    require(!frustum.intersects_aabb(offset-math::Vec3{0.5,0.5,0.5},offset+math::Vec3{0.5,0.5,0.5}),
            "AABB outside each of six frustum planes rejected");
  require(frustum.intersects_aabb({-2,-2,-2},{2,2,2}),"AABB enclosing frustum retained");
  rejects([&]{frustum.intersects_aabb({2,0,0},{1,1,1});});
  rejects([&]{frustum.intersects_aabb({0,0,0},{std::numeric_limits<double>::infinity(),1,1});});
  std::vector<compute::GridCell> cells;
  for(int y=0;y<5;++y)for(int x=0;x<5;++x)cells.push_back(cell(x,y,0));
  const auto source=build_terrain_mesh(compute::Grid(cells),1);
  const auto baseline=device.live_resource_bytes();
  const auto buffer_count=device.live_buffer_count();
  {
    TerrainTileRenderer tiles(device,source,{1});
    require(tiles.tile_count()==16 && tiles.geometry_bytes()==16*(4*28+6*4),"bounded tile uploads accounted exactly");
    rejects<std::logic_error>([&]{tiles.draw(pipeline,camera,{},frustum);});
    RenderPassScope pass(device,{{64,64}});
    const auto culled=tiles.draw(pipeline,camera,{},frustum);
    require(culled.total==16 && culled.visible==4 && culled.culled==12 && culled.draws==4 && culled.triangles==8,
            "frustum skips outside terrain tile draw submissions");
    const auto all=tiles.draw(pipeline,camera,{},frustum,false);
    require(all.visible==16 && !all.culled && all.draws==16 && all.triangles==32,"disabled culling submits complete surface");
    const auto absent=Frustum::from_view_projection(math::Mat4::translation({100,100,100}));
    const auto none=tiles.draw(pipeline,camera,{},absent);
    require(none.culled==16 && !none.draws && !none.triangles,"fully hidden terrain emits no draws");
  }
  require(device.live_resource_bytes()==baseline && device.live_buffer_count()==buffer_count,
          "tile destruction restores shared resource baseline");
  device.set_resource_byte_budget(baseline.total()+160);
  rejects<std::length_error>([&]{TerrainTileRenderer failed(device,source,{1});});
  require(device.live_resource_bytes()==baseline && device.live_buffer_count()==buffer_count,
          "failed multi-tile upload retires all partially created buffers");
}

void selective_gpu_residency() {
  using namespace graphics;
  ValidatingRenderDevice device;RenderResourceOwner shared(device);
  const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const auto camera=shared.create_buffer({BufferUsage::Uniform,sizeof(identity)},std::as_bytes(std::span(identity)));
  const auto pipeline=shared.create_pipeline({ShaderProgramId::Terrain,terrain_shader_layout()});
  const auto baseline=device.live_resource_bytes();const auto base_count=device.live_buffer_count();
  std::vector<compute::GridCell> cells;
  for(int y=0;y<5;++y)for(int x=0;x<5;++x)cells.push_back(cell(x,y,0));
  const auto source=build_terrain_mesh(compute::Grid(cells),1);
  const auto frustum=Frustum::from_view_projection(math::Mat4::identity());
  {
    TerrainTileRenderer tiles(device,source,{1},false);
    require(!tiles.resident_tile_count() && tiles.geometry_bytes()==16*136 && device.live_resource_bytes()==baseline,
            "inactive construction retains CPU recipes without GPU admission");
    const std::array<std::size_t,2> initial{0,1};
    auto change=tiles.set_resident_tiles(initial);
    const auto shared_vertex=tiles.resident_vertex_buffer(1),old_vertex=tiles.resident_vertex_buffer(0);
    require(change.admitted==2 && !change.evicted && tiles.resident_bytes()==272,"selective admission counts payload");
    const std::array<std::size_t,2> replacement{1,2};change=tiles.set_resident_tiles(replacement);
    require(change.admitted==1 && change.evicted==1 && tiles.resident_vertex_buffer(1)==shared_vertex &&
            !tiles.resident_vertex_buffer(0),"overlap preserves resident handles and retires hidden geometry");
    rejects([&]{device.destroy_buffer(old_vertex);});
    const std::array<std::size_t,2> duplicate{1,1};
    rejects([&]{tiles.set_resident_tiles(duplicate);});
    const std::array<std::size_t,1> unknown{16};rejects([&]{tiles.set_resident_tiles(unknown);});
    tiles.set_residency_limit(1);
    require(tiles.resident_tile_count()==2,"lower count limit preserves existing storage");
    rejects<std::length_error>([&]{tiles.set_resident_tiles(replacement);});
    const std::array<std::size_t,1> one{1};tiles.set_resident_tiles(one);
    tiles.set_residency_limit(16);
    const auto before=device.live_resource_bytes();const auto before_count=device.live_buffer_count();
    device.set_resource_byte_budget(before.total()+160); // Stage one tile, fail on the next.
    const std::array<std::size_t,3> expanded{1,2,3};
    rejects<std::length_error>([&]{tiles.set_resident_tiles(expanded);});
    require(tiles.resident_tiles()==std::vector<std::size_t>{1} && tiles.resident_vertex_buffer(1)==shared_vertex &&
            device.live_resource_bytes()==before && device.live_buffer_count()==before_count,
            "failed staged set preserves all current handles/bytes and releases candidates");
    device.set_resource_byte_budget(std::numeric_limits<std::uint64_t>::max());
    change=tiles.set_resident_tiles(expanded);
    require(change.admitted==2 && !change.evicted && tiles.resident_tile_count()==3,"same set recovers after cap increase");
    tiles.set_resident_tiles({});
    require(!tiles.resident_bytes() && device.live_resource_bytes()==baseline,"full eviction keeps CPU recipe only");
    tiles.set_resident_tiles(std::array<std::size_t,1>{0});
    const auto current=tiles.resident_vertex_buffer(0);
    change=tiles.set_resident_tiles(std::array<std::size_t,1>{0});
    require(!change.admitted && !change.evicted && tiles.resident_vertex_buffer(0)==current,"unchanged residency reuses handles");
    {
      RenderPassScope pass(device,{{64,64}});const auto draws=device.draw_count();
      rejects<std::logic_error>([&]{tiles.draw(pipeline,camera,{},frustum);});
      require(device.draw_count()==draws,"missing visible geometry rejects before any partial draw");
      rejects<std::logic_error>([&]{tiles.set_resident_tiles({});});
    }
    change=tiles.synchronize_visible(frustum);
    require(tiles.resident_tile_count()==4 && change.admitted==3,"visible set cold-loads only required tiles");
    {RenderPassScope pass(device,{{64,64}});require(tiles.draw(pipeline,camera,{},frustum).draws==4,"rehydrated visible set draws");}
    const auto absent=Frustum::from_view_projection(math::Mat4::translation({100,100,100}));
    tiles.synchronize_visible(absent);
    require(!tiles.resident_tile_count() && device.live_resource_bytes()==baseline,"fully hidden view releases all tile GPU storage");
    tiles.synchronize_visible(frustum,false);
    require(tiles.resident_tile_count()==16,"disabled culling requires all tiles resident");
  }
  require(device.live_resource_bytes()==baseline && device.live_buffer_count()==base_count,
          "resident renderer destruction returns shared resource baseline");
}

void integer_edges_and_maximum_tile() {
  const auto high=std::numeric_limits<std::int32_t>::max();
  const auto mesh=build_terrain_mesh(compute::Grid({cell(high-1,high-1,1),cell(high,high-1,1),
      cell(high-1,high,1),cell(high,high,1)}),1);
  verify(mesh,tile_terrain_mesh(mesh,{1}),1);
  auto shifted=mesh;shifted.origin_grid_x=std::numeric_limits<std::int32_t>::min();
  for(auto& v:shifted.vertices)v.position.x=static_cast<double>(static_cast<std::int64_t>(v.grid_x)-shifted.origin_grid_x);
  const auto tiles=tile_terrain_mesh(shifted,{1});
  require(tiles.size()==1 && tiles[0].x==std::numeric_limits<std::uint32_t>::max()-1,"wide relative tile coordinate does not overflow signed arithmetic");
  verify(shifted,tiles,1);
  std::vector<compute::GridCell> cells;
  for(int y=0;y<2;++y)for(int x=0;x<258;++x)cells.push_back(cell(x,y,1));
  const auto strip=build_terrain_mesh(compute::Grid(cells),1);
  const auto wide=tile_terrain_mesh(strip,{256});
  require(wide.size()==2,"maximum supported tile side has a partial neighbor");verify(strip,wide,256);
}
}
int main() {
  try {
    dense_coverage_and_seams();stable_under_permutation();sparse_holes_and_islands();
    count_budgets();malformed_inputs();integer_edges_and_maximum_tile();gpu_uploads_and_culling();selective_gpu_residency();
    std::cout<<"Terrain tiles: 8 test groups passed\n";
  }catch(const std::exception& error){std::cerr<<"Terrain tile test failed: "<<error.what()<<'\n';return 1;}
}
