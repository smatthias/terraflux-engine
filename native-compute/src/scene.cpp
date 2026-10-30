#include <terraflux/scene.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraflux::scene {
namespace {
bool finite(math::Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
}

math::Mat4 Transform::matrix() const {
  if (!finite(translation) || !finite(rotation_radians) || !finite(scale) ||
      scale.x == 0.0 || scale.y == 0.0 || scale.z == 0.0)
    throw std::invalid_argument("invalid node transform");
  return math::Mat4::translation(translation) *
         math::Mat4::rotationZ(rotation_radians.z) *
         math::Mat4::rotationY(rotation_radians.y) *
         math::Mat4::rotationX(rotation_radians.x) *
         math::Mat4::scaling(scale);
}

Scene::Scene() { nodes_.push_back({root(), "Root", {}, false, std::nullopt, std::nullopt, std::nullopt}); }

void Scene::check_node(NodeId id) const {
  if (id >= nodes_.size()) throw std::out_of_range("unknown scene node");
}

NodeId Scene::create_node(std::string name, NodeId parent) {
  check_node(parent);
  if (name.empty()) throw std::invalid_argument("scene node name cannot be empty");
  if (nodes_.size() >= std::numeric_limits<NodeId>::max())
    throw std::overflow_error("scene node ID space exhausted");
  const auto id = static_cast<NodeId>(nodes_.size());
  nodes_.push_back({parent, std::move(name), {}, false, std::nullopt, std::nullopt, std::nullopt});
  return id;
}

void Scene::set_parent(NodeId node, NodeId parent) {
  check_node(node);
  check_node(parent);
  if (node == root()) throw std::invalid_argument("cannot reparent scene root");
  for (NodeId current = parent; current != root(); current = nodes_[current].parent)
    if (current == node) throw std::invalid_argument("scene graph cycle");
  if (node == parent) throw std::invalid_argument("node cannot be its own parent");
  nodes_[node].parent = parent;
}

void Scene::set_transform(NodeId node, const Transform& value) {
  check_node(node);
  (void)value.matrix();  // reject invalid values at assignment, not during draw
  nodes_[node].local = value;
}

void Scene::set_mesh_instance(NodeId node, bool enabled) {
  check_node(node);
  if (node == root() && enabled)
    throw std::invalid_argument("scene root cannot be a drawable mesh instance");
  nodes_[node].mesh_instance = enabled;
  if(!enabled)nodes_[node].mesh_asset.reset();
}

void Scene::declare_mesh_asset(std::string name,std::string path) {
  if(name.empty() || name.size()>128 || name.find('\0')!=std::string::npos)
    throw std::invalid_argument("invalid scene asset name");
  if(mesh_assets_.size()>=128 || mesh_assets_.contains(name))
    throw std::invalid_argument("duplicate scene asset or asset limit exceeded");
  const std::filesystem::path relative(path);
  if(path.empty() || path.size()>1024 || path.find('\0')!=std::string::npos ||
     path.find('\\')!=std::string::npos || path.find(':')!=std::string::npos ||
     relative.has_root_path() || (relative.extension()!=".obj" && relative.extension()!=".gltf" && relative.extension()!=".glb"))
    throw std::invalid_argument("scene asset must be a relative .obj, .gltf or .glb path");
  for(const auto& part:relative)
    if(part=="..")throw std::invalid_argument("scene asset parent traversal forbidden");
  mesh_assets_.emplace(std::move(name),relative.lexically_normal().generic_string());
}

void Scene::set_mesh_asset(NodeId node,const std::string& name) {
  check_node(node);
  if(node==root())throw std::invalid_argument("scene root cannot reference a mesh asset");
  if(!mesh_assets_.contains(name))throw std::invalid_argument("unknown scene mesh asset: "+name);
  nodes_[node].mesh_asset=name;
  nodes_[node].mesh_instance=true;
}

void Scene::set_angular_velocity(NodeId node, std::optional<math::Vec3> velocity) {
  check_node(node);
  if (node == root() && velocity)
    throw std::invalid_argument("scene root cannot have a Spin component");
  if (velocity && (!finite(*velocity) ||
                   std::abs(velocity->x)>1000 || std::abs(velocity->y)>1000 ||
                   std::abs(velocity->z)>1000))
    throw std::invalid_argument("Spin angular velocity must be finite and bounded");
  nodes_[node].angular_velocity=velocity;
}

void Scene::set_linear_velocity(NodeId node, std::optional<math::Vec3> velocity) {
  check_node(node);
  if (node == root() && velocity)
    throw std::invalid_argument("scene root cannot have a Motion component");
  if (velocity && (!finite(*velocity) ||
                   std::abs(velocity->x)>1000 || std::abs(velocity->y)>1000 ||
                   std::abs(velocity->z)>1000))
    throw std::invalid_argument("Motion velocity must be finite and bounded");
  nodes_[node].linear_velocity=velocity;
}

const Node& Scene::get(NodeId node) const {
  check_node(node);
  return nodes_[node];
}

math::Mat4 Scene::world_matrix(NodeId node) const {
  check_node(node);
  std::vector<NodeId> ancestors;
  for (NodeId current = node; current != root(); current = nodes_[current].parent)
    ancestors.push_back(current);
  math::Mat4 world = nodes_[root()].local.matrix();
  for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
    world = world * nodes_[*it].local.matrix();
  return world;
}

}  // namespace terraflux::scene
