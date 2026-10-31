#pragma once

#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <map>
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
  bool mesh_instance = false; // Draw a named asset, or the legacy primary asset.
  std::optional<std::string> mesh_asset;
  std::optional<math::Vec3> angular_velocity; // rad/s, runtime Spin component
  std::optional<math::Vec3> linear_velocity; // local units/s, runtime Motion component
};

class Scene {
 public:
  Scene();
  static constexpr NodeId root() { return 0; }
  NodeId create_node(std::string name, NodeId parent = root());
  void set_parent(NodeId node, NodeId parent);
  void set_transform(NodeId node, const Transform& value);
  void set_mesh_instance(NodeId node, bool enabled);
  void declare_mesh_asset(std::string name, std::string relative_obj_path);
  void set_mesh_asset(NodeId node, const std::string& asset_name);
  const std::map<std::string,std::string>& mesh_assets() const { return mesh_assets_; }
  void set_angular_velocity(NodeId node, std::optional<math::Vec3> radians_per_second);
  void set_linear_velocity(NodeId node, std::optional<math::Vec3> units_per_second);
  const Node& get(NodeId node) const;
  std::size_t size() const { return nodes_.size(); }
  math::Mat4 world_matrix(NodeId node) const;

 private:
  std::vector<Node> nodes_;
  std::map<std::string,std::string> mesh_assets_;
  void check_node(NodeId node) const;
};

}  // namespace terraflux::scene
