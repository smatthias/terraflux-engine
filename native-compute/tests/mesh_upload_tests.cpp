#include <terraflux/assets/obj.hpp>
#include <terraflux/graphics/mesh_upload.hpp>
#include <terraflux/scene.hpp>

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}
bool near(double a,double b){return std::abs(a-b)<1e-5;}
void upload_nonuniform_world_transform() {
  std::istringstream in("v 0 0 0\nv 1 0 0\nv 0 1 1\nf 1 2 3\n");
  const auto mesh=terraflux::assets::import_obj(in);
  terraflux::scene::Scene scene;
  auto parent=scene.create_node("Objects");
  auto child=scene.create_node("Model",parent);
  scene.set_transform(parent,{{10,5,-8},{0,0,0},{2,3,4}});
  auto uploaded=terraflux::graphics::prepare_asset_upload(mesh,scene.world_matrix(child));
  require(uploaded.vertices.size()==3 && uploaded.indices.size()==3,"mesh uploaded");
  require(near(uploaded.vertices[1].position[0],12),"parent scale/position");
  const auto& n=uploaded.vertices[0].normal;
  require(near(std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]),1),"unit normal");
  const double dot=n[0]*0.0+n[1]*3.0+n[2]*4.0;
  require(near(dot,0),"correct inverse transpose normal against transformed edge");
}
void object_uniform_and_local_geometry() {
  std::istringstream in("v 0 0 0\\nv 1 0 0\\nv 0 1 1\\nf 1 2 3\\n");
  const auto mesh=terraflux::assets::import_obj(in);
  const auto upload=terraflux::graphics::prepare_asset_upload(mesh);
  require(upload.indices.size()==3 && near(upload.vertices[1].position[0],1),
          "asset GPU vertices stay in model space");
  const auto world=terraflux::math::Mat4::translation({10,5,-8}) *
                   terraflux::math::Mat4::scaling({2,3,4});
  const auto data=terraflux::graphics::make_object_uniform(world,{0.2,0.4,0.9});
  require(near(data.model[12],10) && near(data.model[0],2),
          "GPU model matrix has world transform");
  require(near(data.normal_matrix[0],0.5) &&
          near(data.normal_matrix[5],1.0/3.0) &&
          near(data.normal_matrix[10],0.25),
          "inverse transpose normal matrix matches nonuniform scale");
  require(near(data.tint[2],0.9) && near(data.tint[3],1.0),"object material tint");
  bool bad=false;
  try{(void)terraflux::graphics::make_object_uniform(world,{1.2,0,0});}
  catch(const std::invalid_argument&){bad=true;}
  require(bad,"out-of-range material tint rejected");
}
void reject_singular_world_matrix(){
  std::istringstream in("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  auto mesh=terraflux::assets::import_obj(in);
  auto singular=terraflux::math::Mat4::scaling({1,0,1});
  bool threw=false;
  try{(void)terraflux::graphics::prepare_asset_upload(mesh,singular);}
  catch(const std::invalid_argument&){threw=true;}
  require(threw,"singular transform rejected");
}
}  // namespace
int main(){
  try{
    upload_nonuniform_world_transform();
    object_uniform_and_local_geometry();
    reject_singular_world_matrix();
    std::cout<<"TerraFlux OBJ upload: 3 tests passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
