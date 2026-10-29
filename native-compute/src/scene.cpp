#include <terraflux/scene.hpp>

#include <cmath>
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

Scene::Scene() { nodes_.push_back({root(), "Root", {}}); }

void Scene::check_node(NodeId id) const {
  if (id >= nodes_.size()) throw std::out_of_range("unknown scene node");
}

NodeId Scene::create_node(std::string name, NodeId parent) {
  check_node(parent);
  if (name.empty()) throw std::invalid_argument("scene node name cannot be empty");
  if (nodes_.size() >= std::numeric_limits<NodeId>::max())
    throw std::overflow_error("scene node ID space exhausted");
  const auto id = static_cast<NodeId>(nodes_.size());
  nodes_.push_back({parent, std::move(name), {}});
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
