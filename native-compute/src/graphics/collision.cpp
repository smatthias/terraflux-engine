#include <terraflux/graphics/collision.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace terraflux::graphics {
namespace {

bool within(double value, double lo, double hi) { return std::isfinite(value) && value >= lo && value <= hi; }

void validate_obstacle(const Obstacle& o) {
  if (!std::isfinite(o.x) || !std::isfinite(o.z) || !std::isfinite(o.radius) || o.radius < 0 ||
      !std::isfinite(o.bottom) || !std::isfinite(o.top) || o.top < o.bottom)
    throw std::invalid_argument("obstacle needs a finite axis, a non-negative radius and bottom <= top");
}

}  // namespace

void validate_walker_body(const WalkerBody& body) {
  if (!within(body.radius, 0.05, 5.0)) throw std::invalid_argument("walker radius must be 0.05..5 meters");
  if (!within(body.height, 0.2, 10.0)) throw std::invalid_argument("walker height must be 0.2..10 meters");
  if (!within(body.step_height, 0.0, body.height)) throw std::invalid_argument("walker step height must be 0..height");
  if (!within(body.max_slope_degrees, 1.0, 89.0)) throw std::invalid_argument("walkable slope must be 1..89 degrees");
}

bool obstacle_blocks(const Obstacle& obstacle, double feet_y, const WalkerBody& body) {
  return obstacle.bottom < feet_y + body.height && obstacle.top > feet_y + body.step_height;
}

ObstacleField::ObstacleField(std::vector<Obstacle> obstacles, double cell_size_meters)
    : obstacles_(std::move(obstacles)), cell_(cell_size_meters) {
  if (!std::isfinite(cell_) || cell_ <= 0) throw std::invalid_argument("obstacle field cell size must be positive");
  if (obstacles_.size() > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("too many obstacles");
  for (std::uint32_t i = 0; i < obstacles_.size(); ++i) {
    const auto& o = obstacles_[i];
    validate_obstacle(o);
    max_radius_ = std::max(max_radius_, o.radius);
    const auto lo_x = cell_index(o.x - o.radius), hi_x = cell_index(o.x + o.radius);
    const auto lo_z = cell_index(o.z - o.radius), hi_z = cell_index(o.z + o.radius);
    for (auto cz = lo_z; cz <= hi_z; ++cz)
      for (auto cx = lo_x; cx <= hi_x; ++cx) cells_[key(cx, cz)].push_back(i);
  }
}

std::int64_t ObstacleField::cell_index(double v) const { return static_cast<std::int64_t>(std::floor(v / cell_)); }

std::uint64_t ObstacleField::key(std::int64_t cx, std::int64_t cz) {
  return (static_cast<std::uint64_t>(cx) << 32) ^ (static_cast<std::uint64_t>(cz) & 0xFFFFFFFFull);
}

std::optional<double> ObstacleField::clearance(double x, double z, double radius) const {
  std::optional<double> best;
  for_each_near(x, z, radius, [&](const Obstacle& o) {
    const double d = std::hypot(x - o.x, z - o.z) - o.radius;
    if (d <= radius && (!best || d < *best)) best = d;
  });
  return best;
}

double mesh_footprint_radius(const VegetationMesh& mesh, double up_to_height, double minimum) {
  if (!std::isfinite(up_to_height) || up_to_height <= 0 || !std::isfinite(minimum) || minimum < 0)
    throw std::invalid_argument("footprint height must be positive and the minimum non-negative");
  double radius = minimum;
  for (const auto& v : mesh.vertices)
    if (v.position[1] <= up_to_height)
      radius = std::max(radius, std::hypot(static_cast<double>(v.position[0]), static_cast<double>(v.position[2])));
  return radius;
}

std::vector<Obstacle> tree_obstacles(const VegetationPlacement& placement, std::span<const VegetationMesh> tree_meshes) {
  std::vector<Obstacle> out;
  if (tree_meshes.empty() || placement.trees.empty()) return out;
  std::vector<double> footprints;
  for (const auto& mesh : tree_meshes) footprints.push_back(mesh_footprint_radius(mesh));
  out.reserve(placement.trees.size());
  for (const auto& t : placement.trees) {
    const std::size_t slot = std::min<std::size_t>(t.kind > 0.5f ? 1 : 0, footprints.size() - 1);
    Obstacle o;
    o.x = t.position[0]; o.z = t.position[2];
    o.radius = footprints[slot] * static_cast<double>(t.scale);
    // Rooted: the footprint reaches down a footprint-radius below the base (a
    // 45-degree hillside), so a wide footprint is solid all round on a slope
    // instead of hanging above the downhill ground.
    o.bottom = t.position[1] - std::max(0.5, o.radius); o.top = t.position[1] + static_cast<double>(t.scale);
    out.push_back(o);
  }
  return out;
}

Obstacle asset_obstacle(const BoundingSphere& bounds) {
  Obstacle o;
  o.x = bounds.center.x; o.z = bounds.center.z;
  o.radius = std::max(0.0, bounds.radius);
  o.bottom = bounds.center.y - o.radius; o.top = bounds.center.y + o.radius;
  return o;
}

math::Vec2 push_out_of_obstacles(std::span<const ObstacleField* const> fields, math::Vec2 position, double feet_y,
                                 const WalkerBody& body) {
  validate_walker_body(body);
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(feet_y))
    throw std::invalid_argument("push-out needs a finite position");
  constexpr int kPasses = 6;
  for (int pass = 0; pass < kPasses; ++pass) {
    bool moved = false;
    for (const auto* field : fields) {
      if (!field) continue;
      field->for_each_near(position.x, position.y, body.radius + field->max_radius(), [&](const Obstacle& o) {
        if (!obstacle_blocks(o, feet_y, body)) return;
        const double keep = o.radius + body.radius;
        const double dx = position.x - o.x, dz = position.y - o.z;
        const double dist = std::hypot(dx, dz);
        if (dist >= keep) return;
        if (dist < 1e-9) position = {o.x + keep, o.z};  // standing on the axis: step out east
        else position = {o.x + dx / dist * keep, o.z + dz / dist * keep};
        moved = true;
      });
    }
    if (!moved) break;
  }
  return position;
}

}  // namespace terraflux::graphics
