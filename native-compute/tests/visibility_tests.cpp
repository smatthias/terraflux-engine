#include <terraflux/graphics/visibility.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/resource_owner.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>

namespace {
using namespace terraflux;
void check(bool value,const char* msg){if(!value)throw std::runtime_error(msg);}
bool close(double a,double b){return std::abs(a-b)<1e-7;}
template<class F>void rejected(F&& f,const char* msg){
  bool thrown=false;try{f();}catch(const std::exception&){thrown=true;}
  check(thrown,msg);
}
void frustum_and_scaled_bounds(){
  assets::Mesh mesh;
  mesh.vertices={{{-1,-1,-1},{0,1,0},{}},
                 {{1,1,1},{0,1,0},{}}};
  const auto bounds=graphics::mesh_bounds(mesh);
  check(close(bounds.center.x,0) && close(bounds.radius,std::sqrt(3.0)),
        "conservative mesh sphere");
  const auto view=math::Mat4::lookAtRH({0,0,10},{0,0,0},{0,1,0});
  const auto proj=math::Mat4::perspectiveRH_NO(1.5707963267948966,1,1,100);
  const auto frustum=graphics::Frustum::from_view_projection(proj*view);
  check(frustum.intersects(graphics::world_bounds(bounds,math::Mat4::identity())),
        "origin inside frustum");
  check(!frustum.intersects(graphics::world_bounds(
       bounds,math::Mat4::translation({500,0,0}))),"far outside on X");
  check(!frustum.intersects(graphics::world_bounds(
       bounds,math::Mat4::translation({0,0,20}))),"behind camera");
  check(!frustum.intersects(graphics::world_bounds(
       bounds,math::Mat4::translation({0,0,-500}))),"past far clip");
  check(frustum.intersects(graphics::world_bounds(
       bounds,math::Mat4::translation({0,0,9}))),"near plane overlap retained");
  // Nonuniform scale and shear must not shrink the conservative sphere.
  auto scale=math::Mat4::scaling({2,4,1});
  auto transformed=graphics::world_bounds(bounds,scale);
  check(close(transformed.radius,bounds.radius*4),"nonuniform scale");
  scale.at(0,1)=6;
  transformed=graphics::world_bounds(bounds,scale);
  check(transformed.radius>bounds.radius*6,"shear conservative bound");
  rejected([&]{(void)graphics::mesh_bounds({});},"empty mesh rejected");
}
void batch_order_and_draw_validation(){
  graphics::ValidatingRenderDevice device;
  graphics::RenderResourceOwner owner(device);
  std::array<std::byte,32*3> vertices{};
  std::array<std::byte,6*4> indices{};
  std::array<std::byte,144> uniform{};
  std::array<std::byte,64> camera{};
  std::array<std::byte,4> white{};
  const auto vb=owner.create_buffer({graphics::BufferUsage::Vertex,vertices.size()},vertices);
  const auto ib=owner.create_buffer({graphics::BufferUsage::Index,indices.size()},indices);
  const auto cb=owner.create_buffer({graphics::BufferUsage::Uniform,camera.size()},camera);
  const auto ob1=owner.create_buffer({graphics::BufferUsage::Uniform,uniform.size()},uniform);
  const auto ob2=owner.create_buffer({graphics::BufferUsage::Uniform,uniform.size()},uniform);
  const auto tex1=owner.create_texture2d({1,1},white);
  const auto tex2=owner.create_texture2d({1,1},white);
  const auto pipeline=owner.create_pipeline({graphics::textured_object_shader_layout(),true});
  graphics::OpaqueRenderQueue queue;
  auto prepare=[&](graphics::TextureHandle tex,graphics::BufferHandle object){
    graphics::PreparedDraw out;
    out.command={pipeline,vb,ib,0,6,{{0,cb},{2,object}},{},{{3,tex}}};
    out.object_uniform=object;
    out.material=graphics::make_object_uniform(math::Mat4::identity(),{1,1,1});
    return out;
  };
  queue.add(prepare(tex2,ob1));
  queue.add(prepare(tex1,ob1));
  queue.add(prepare(tex2,ob2));
  queue.add(prepare(tex1,ob2));
  check(queue.size()==4,"queued four opaque draws");
  const auto stats=queue.submit(device);
  check(stats.submitted==4 && stats.state_groups==2,"identical material keys grouped");
  check(device.draw_count()==4 && queue.size()==0,"submitted and cleared queue");
  check(queue.submit(device).submitted==0,"empty queue");
  rejected([&]{queue.add({{}, {}, {}});},"object UBO required");
}
}
int main(){
  try{frustum_and_scaled_bounds();batch_order_and_draw_validation();
    std::cout<<"TerraFlux visibility: 2 test groups passed\n";
  }catch(const std::exception&e){std::cerr<<"visibility tests: "<<e.what()<<'\n';return 1;}
}
