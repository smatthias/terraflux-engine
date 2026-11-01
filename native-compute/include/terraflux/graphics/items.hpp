#pragma once

#include <terraflux/graphics/body_view.hpp>
#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/collision.hpp>
#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/math.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace terraflux::graphics {

// Things in the world a walker can pick up: stones lying where the ground is
// rock, scree or bare (placed deterministically from the terrain), branches
// broken off trees or dropped, and burning branches (which live in the tree
// fire model and are taken from it). Held things are carried in the right
// hand (see body_view.hpp), dropped at the feet or thrown — under the same
// gravity as everything else — and land on the ground as what they are.
struct Stone {
  math::Vec3 position;      // resting on the ground (centre at ground + radius * 0.6)
  double radius = 0.15;
  math::Vec3 size;          // the block drawn (x, y, z), about the radius
  bool present = true;      // false while carried or thrown
};
struct StoneStyle {
  double spacing_meters = 7.0;        // 1 .. 100 sample grid
  double min_radius = 0.06, max_radius = 0.32;  // 0.02 .. 1
  double rock_chance = 0.55, ridge_chance = 0.75, bare_chance = 0.25, soil_chance = 0.03;  // 0 .. 1 per sample by material
  std::uint32_t seed = 0;
  std::size_t max_stones = 200000;    // 1 .. 5000000
};
void validate_stone_style(const StoneStyle& style);
// Stones over the terrain: a jittered grid, kept by the material's chance,
// never on water, snow or ice, sized by a seeded draw. Deterministic.
std::vector<Stone> place_stones(const terrain::TerrainMesh& terrain, const TerrainStyle& terrain_style, const TerrainSampler& sampler,
                                const StoneStyle& style, std::optional<double> water_level = std::nullopt);
// Present stones as walking obstacles (low: stepped over when small).
std::vector<Obstacle> stone_obstacles(std::span<const Stone> stones);

// A branch lying on the ground (not burning; burning ones are the tree model's).
struct DroppedBranch {
  math::Vec3 position;
  double length = 0.8;
  double yaw = 0.0;         // which way it lies
  bool present = true;
};

// What the hand holds.
struct HeldItem {
  HeldKind kind = HeldKind::Nothing;
  double size = 0.0;              // stone radius or branch length
  double torch_seconds = 0.0;     // a torch burns this much longer
  math::Vec3 stone_size;          // the stone's block
  std::uint32_t source = 0;       // the stone index it came from (to put it back in place)
};
inline constexpr double kTorchSeconds = 90.0;

// Something in flight after a throw.
struct Projectile {
  HeldItem item;
  math::Vec3 position, velocity;
  double seconds = 0.0;
  bool landed = false;
};
// Launch velocity: `speed` along the look direction lifted by `lift_degrees`.
math::Vec3 throw_velocity(math::Vec3 forward, double speed_mps, double lift_degrees);
// Integrates the flight by dt under gravity until the ground (the standing
// height; beyond the terrain it keeps falling), sub-stepped; sets `landed`.
void advance_projectile(Projectile& p, double dt, double gravity_mps2, const StandingHeightFn& ground, math::Vec3 lower, math::Vec3 upper);

// Reach: the nearest thing within `reach` metres of the eye that lies within
// `cone_degrees` of the look direction (or, for `nearest_trunk`, any trunk
// within reach of the feet). Nullopt when nothing is in reach.
std::optional<std::size_t> stone_in_reach(std::span<const Stone> stones, math::Vec3 eye, math::Vec3 forward, double reach, double cone_degrees);
std::optional<std::size_t> branch_in_reach(std::span<const DroppedBranch> branches, math::Vec3 eye, math::Vec3 forward, double reach, double cone_degrees);
std::optional<std::size_t> trunk_in_reach(const ObstacleField& trunks, math::Vec3 feet, double reach);

// A few boxes drawn with the Object program: stones and branches near the walker.
struct Prop { math::Mat4 transform; math::Vec3 size; math::Vec3 color; };
class PropRenderer {
 public:
  PropRenderer(RenderDevice& device, std::size_t max_props = 256);
  PropRenderer(const PropRenderer&) = delete;
  PropRenderer& operator=(const PropRenderer&) = delete;
  std::size_t draw(BufferHandle camera, std::span<const Prop> props);  // truncates to max_props
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  std::size_t max_props_;
  BufferHandle vb_, ib_;
  std::vector<BufferHandle> uniforms_;
  PipelineHandle pipeline_;
  std::uint32_t indices_ = 0;
};
// The props of the stones and branches within `radius` of `eye`, nearest first.
std::vector<Prop> props_near(std::span<const Stone> stones, std::span<const DroppedBranch> branches, math::Vec3 eye, double radius, std::size_t max_props);

}  // namespace terraflux::graphics
