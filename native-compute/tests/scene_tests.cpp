#include <terraflux/assets/obj.hpp>
#include <terraflux/scene.hpp>

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using terraflux::math::Mat4;
using terraflux::math::Vec3;
using terraflux::scene::Scene;
using terraflux::scene::Transform;

namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
bool near(double a, double b) { return std::abs(a-b) < 1e-9; }
template <class F> void rejects(F&& f, const char* message) {
  bool thrown = false;
  try { f(); } catch (const std::exception&) { thrown = true; }
  require(thrown, message);
}
void scene_hierarchy() {
  Scene graph;
  auto anchor = graph.create_node("Anchor");
  auto prop = graph.create_node("Prop", anchor);
  graph.set_transform(anchor, Transform{{10,3,5},{0,0,0},{2,2,2}});
  graph.set_transform(prop, Transform{{1,0,0},{0,0,0},{1,1,1}});
  auto world=graph.world_matrix(prop).transformPoint({1,0,0});
  require(near(world.x,14) && near(world.y,3) && near(world.z,5), "parent transform");
  graph.set_parent(prop,Scene::root());
  world=graph.world_matrix(prop).transformPoint({1,0,0});
  require(near(world.x,2) && near(world.y,0), "reparent world transform");
  rejects([&]{ graph.set_parent(anchor,anchor); }, "self-parent");
  graph.set_parent(prop,anchor);
  rejects([&]{ graph.set_parent(anchor,prop); }, "cycle prevention");
  rejects([&]{ graph.set_parent(Scene::root(),prop); }, "root cannot reparent");
  rejects([&]{ graph.get(99); }, "unknown node");
}
void scene_rotation_and_validation() {
  constexpr double halfpi=1.57079632679489661923;
  auto rx=Mat4::rotationX(halfpi).transformDirection({0,1,0});
  auto rz=Mat4::rotationZ(halfpi).transformDirection({1,0,0});
  require(near(rx.z,1) && near(rx.y,0), "X rotation");
  require(near(rz.y,1) && near(rz.x,0), "Z rotation");
  Scene graph;
  auto node=graph.create_node("Rotate");
  graph.set_transform(node, Transform{{1,0,0},{0,0,halfpi},{1,1,1}});
  auto p=graph.world_matrix(node).transformPoint({1,0,0});
  require(near(p.x,1) && near(p.y,1), "rotation and translation order");
  rejects([&]{ graph.set_transform(node, Transform{{},{},{1,0,1}}); }, "invalid scale");
}
void obj_triangulation() {
  std::istringstream file(R"OBJ(# square
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
f 1 2 3 4
)OBJ");
  auto m=terraflux::assets::import_obj(file);
  require(m.vertices.size()==6 && m.indices.size()==6, "quad triangulated");
  require(near(m.vertices[0].normal.z,1) && near(m.vertices[3].normal.z,1), "generated normals");
}
void obj_explicit_normals_and_negative_indices() {
  std::istringstream file(R"OBJ(v 0 0 0
v 1 0 0
v 0 1 0
vn 0 0 -2
f -3//-1 -2//-1 -1//-1
)OBJ");
  const auto mesh=terraflux::assets::import_obj(file);
  require(mesh.indices.size()==3 && near(mesh.vertices[0].normal.z,-1), "explicit normals");
  require(near(mesh.vertices[2].position.y,1), "relative index");
}
void obj_uv_coordinates() {
  std::istringstream in(R"OBJ(v 0 0 0
v 1 0 0
v 0 1 0
vt 0.25 0.75
vt 1.0 0.0
vn 0 0 1
f 1/1/1 2/2/1 3/1/1
)OBJ");
  auto mesh=terraflux::assets::import_obj(in);
  require(near(mesh.vertices[0].uv.x,0.25) && near(mesh.vertices[0].uv.y,0.75),"UV 1");
  require(near(mesh.vertices[1].uv.x,1.0),"UV 2");
  std::istringstream negative(R"OBJ(v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 0 1
vt 1 1
f -3/-3 -2/-2 -1/-1
)OBJ");
  mesh=terraflux::assets::import_obj(negative);
  require(near(mesh.vertices[1].uv.y,1.0),"negative UV indices");
}
void obj_material_groups() {
  std::istringstream input(R"OBJ(mtllib shared.mtl colors.mtl
v 0 0 0
v 1 0 0
v 0 1 0
v 0 0 1
usemtl red
f 1 2 3
usemtl blue
f 1 4 2
usemtl red
f 1 3 4
)OBJ");
  const auto mesh=terraflux::assets::import_obj(input);
  require(mesh.material_libraries.size()==2 && mesh.material_libraries[1]=="colors.mtl",
          "MTL library references parsed");
  require(mesh.indices.size()==9 && mesh.sections.size()==3,"material ranges are contiguous");
  require(mesh.sections[0].material_name=="red" && mesh.sections[0].first_index==0 &&
          mesh.sections[0].index_count==3,"first material range");
  require(mesh.sections[1].material_name=="blue" && mesh.sections[1].first_index==3,
          "material change creates a new draw range");
  require(mesh.sections[2].material_name=="red" && mesh.sections[2].first_index==6,
          "material changes back create another range");
}
void obj_rejection() {
  const auto load=[](const std::string& content) {
    std::istringstream input(content);
    return terraflux::assets::import_obj(input);
  };
  rejects([&]{ (void)load("v 0 0 0\nf 1 2 3\n"); }, "out-of-range index");
  rejects([&]{ (void)load("v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n"); }, "degenerate polygon");
  rejects([&]{ (void)load("v nan 0 0\n"); }, "nonfinite OBJ");
  rejects([&]{ (void)load("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 2 3\n"); }, "OBJ zero index");
}
}

int main() {
  try {
    scene_hierarchy();
    scene_rotation_and_validation();
    obj_triangulation();
    obj_uv_coordinates();
    obj_material_groups();
    obj_explicit_normals_and_negative_indices();
    obj_rejection();
    std::cout << "TerraFlux scene and OBJ importer: 7 tests passed\n";
  } catch(const std::exception& e) {
    std::cerr << "Scene/OBJ test failed: " << e.what() << '\n';
    return 1;
  }
}
