#pragma once

#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace terraflux::scene {

// Stable IDs for append-only scene nodes. Root is always node 0. Deletion and
// component registration are deliberately deferred to a later engine phase.
using NodeId = std::uint32_t;

struct Transform {
  math::Vec3 translation{};
  math::Vec3 rotation_radians{};  // Euler X then Y then Z
  math::Vec3 scale{1.0, 1.0, 1.0};

  // Column vectors, Y-up, right-handed. T * Rz * Ry * Rx * S.
  math::Mat4 matrix() const;
};

struct Node {
  NodeId parent = 0;
  std::string name;
  Transform local{};
  bool mesh_instance = false; // Draw the scene's primary asset at this node.
};

class Scene {
 public:
  Scene();
  static constexpr NodeId root() { return 0; }
  NodeId create_node(std::string name, NodeId parent = root());
  void set_parent(NodeId node, NodeId parent);
  void set_transform(NodeId node, const Transform& value);
  void set_mesh_instance(NodeId node, bool enabled);
  const Node& get(NodeId node) const;
  std::size_t size() const { return nodes_.size(); }
  math::Mat4 world_matrix(NodeId node) const;

 private:
  std::vector<Node> nodes_;
  void check_node(NodeId node) const;
};

}  // namespace terraflux::scene
