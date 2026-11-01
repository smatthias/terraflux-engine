#pragma once

#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/graphics/visibility.hpp>
#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace terraflux::graphics {

// Walking collision for the first-person camera. Everything a walker can bump
// into is a vertical cylinder in world space: a tree trunk (its footprint on
// the ground times the tree's height), an imported asset (its bounding sphere)
// or anything else a caller adds. The walker is a capsule-like body with a
// radius, a height and a step height: an obstacle whose top is below the step
// height is stepped over (grass is never an obstacle at all, pebbles and low
// rocks would be), one whose bottom is above the body is passed under, the
// rest is slid around. Steep ground is handled by the walker itself (see
// WalkerBody::max_slope_degrees and walk() in camera_rig.hpp): an ascent
// steeper than the limit is a wall, so cliffs and crags stop you while you
// still slide along their foot. Units: meters, degrees.
struct Obstacle {
  double x = 0.0, z = 0.0;          // axis in plan
  double radius = 0.0;              // footprint radius (>= 0)
  double bottom = 0.0, top = 0.0;   // vertical extent (bottom <= top)
};

struct WalkerBody {
  double radius = 0.35;             // 0.05 .. 5
  double height = 1.7;              // eye height; 0.2 .. 10
  double step_height = 0.5;         // obstacles lower than this above the feet are stepped over; 0 .. height
  double max_slope_degrees = 45.0;  // steepest ascent that can be walked; 1 .. 89 (89: no limit in practice)
};
void validate_walker_body(const WalkerBody& body);  // throws std::invalid_argument

// Whether an obstacle is in the walker's way at a given feet height: its
// vertical extent overlaps [feet + step height, feet + body height].
bool obstacle_blocks(const Obstacle& obstacle, double feet_y, const WalkerBody& body);

// Uniform plan-space hash of obstacles for neighbourhood queries. Immutable
// after construction; rebuild it when the obstacles change (vegetation
// re-placement, scene animation).
class ObstacleField {
 public:
  ObstacleField() = default;
  explicit ObstacleField(std::vector<Obstacle> obstacles, double cell_size_meters = 16.0);
  std::size_t size() const { return obstacles_.size(); }
  bool empty() const { return obstacles_.empty(); }
  const std::vector<Obstacle>& obstacles() const { return obstacles_; }
  double cell_size() const { return cell_; }
  double max_radius() const { return max_radius_; }
  // Calls visit(const Obstacle&) for every obstacle whose footprint may reach
  // within `radius` of (x, z) (a superset: the cells the disc touches).
  template <class Visit>
  void for_each_near(double x, double z, double radius, Visit&& visit) const {
    if (obstacles_.empty()) return;
    const auto lo_x = cell_index(x - radius), hi_x = cell_index(x + radius);
    const auto lo_z = cell_index(z - radius), hi_z = cell_index(z + radius);
    for (auto cz = lo_z; cz <= hi_z; ++cz)
      for (auto cx = lo_x; cx <= hi_x; ++cx) {
        const auto found = cells_.find(key(cx, cz));
        if (found == cells_.end()) continue;
        for (const auto index : found->second) visit(obstacles_[index]);
      }
  }
  // Nearest obstacle axis distance minus its radius (negative inside), or
  // nullopt when nothing lies within `radius`.
  std::optional<double> clearance(double x, double z, double radius) const;

 private:
  std::int64_t cell_index(double v) const;
  static std::uint64_t key(std::int64_t cx, std::int64_t cz);
  std::vector<Obstacle> obstacles_;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells_;
  double cell_ = 16.0, max_radius_ = 0.0;
};

// Largest plan radius of the vertices of a unit-height mesh up to `up_to_height`
// (default the lowest 15 %): the trunk of a tree, the base of a custom model.
// Never below `minimum`.
double mesh_footprint_radius(const VegetationMesh& mesh, double up_to_height = 0.15, double minimum = 0.02);
// One obstacle per placed tree: footprint of its mesh slot (kind k uses
// min(k, n-1), as the renderer does) scaled by the instance height, standing
// from its ground point to its top. Grass is walkable and contributes nothing.
std::vector<Obstacle> tree_obstacles(const VegetationPlacement& placement, std::span<const VegetationMesh> tree_meshes);
// An imported asset as the cylinder around its world bounding sphere.
Obstacle asset_obstacle(const BoundingSphere& bounds);

// Pushes a plan position out of every blocking obstacle in the fields
// (iterating so that leaving one does not enter another), returning the
// resolved position. Exact contact is allowed (the body touches the trunk).
math::Vec2 push_out_of_obstacles(std::span<const ObstacleField* const> fields, math::Vec2 position, double feet_y,
                                 const WalkerBody& body);

}  // namespace terraflux::graphics
