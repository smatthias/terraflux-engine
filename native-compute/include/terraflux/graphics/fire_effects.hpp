#pragma once

#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/graphics/visibility.hpp>
#include <terraflux/math.hpp>
#include <terraflux/replay.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraflux::graphics {

// Animated flames and smoke for the simulation's active and burned cells. The
// simulation alone decides which cells burn; these effects only visualise it:
// flames are emitted where the ground actually carries fuel (never on rock,
// water, snow, ice or bare soil inside a burning cell), scale with the fuel
// load, climb into the crowns of trees standing in burning cells, and burned
// cells smoulder. Nothing here feeds back into propagation or replay timing.
struct FireStyle {
  double flame_scale = 1.0;            // 0 .. 3 multiplies flame size
  double smoke_scale = 1.0;            // 0 .. 3 multiplies smoke opacity
  double flames_per_cell = 5.0;        // 1 .. 12 samples per burning cell at full fuel
  double flame_height_meters = 7.0;    // 1 .. 30 ground flame height at full fuel
  double smoke_height_meters = 60.0;   // 5 .. 300 plume height
  double fuel_threshold = 0.05;        // 0 .. 0.5 minimum interpolated fuel for a flame
  double draw_distance_meters = 6000;  // 100 .. 50000
  double wind_direction_degrees = 90;  // compass direction the wind blows toward
  double wind_speed_mps = 3.0;         // 0 .. 30
  std::uint32_t seed = 0;
  math::Vec3 light_direction{-0.4545, 0.7660, 0.4545};
};
// Uniform block "FireStyle" at slot 14, std140, four vec4s:
//   wind:   unit direction (x, z), speed, time
//   eye:    camera position, flame scale
//   params: smoke scale, smoke height, draw distance, 0
//   light:  unit direction toward the light, 0
inline constexpr std::uint32_t kFireStyleSlot = 14;
inline constexpr std::size_t kFireStyleBytes = 64;
using FireStyleUniform = std::array<float, 16>;
FireStyleUniform pack_fire_style(const FireStyle& style, math::Vec3 eye, double time_seconds);

// Billboard quad corner: x in -0.5..0.5, y in 0..1, plus texture coordinates.
struct FireVertex { float position[2]; float uv[2]; };
struct FireMesh { std::vector<FireVertex> vertices; std::vector<std::uint32_t> indices; };
FireMesh make_fire_quad();
// POSITION (0, float2), TEXCOORD (1, float2); Camera at slot 0, FireStyle at
// slot 14 (both stages), InstanceData at slot 4 with 48-byte records.
ShaderLayout fire_shader_layout();

// One flame or plume: std430 three vec4s.
struct FireInstance {
  float position[3];
  float size;       // height in metres
  float phase;      // animation phase
  float intensity;  // 0 .. 1 fuel-driven strength
  float kind;       // 0 flame, 1 smoke plume, 2 smouldering plume
  float seed;       // per-instance noise offset
  float lean[3];    // reserved for future directional lean (zero)
  float pad = 0.0f;
};
inline constexpr std::size_t kFireInstanceBytes = 48;
static_assert(sizeof(FireInstance) == kFireInstanceBytes);

struct FireEmitters {
  std::vector<FireInstance> flames, smoke;
  std::size_t active_cells = 0, burned_cells = 0, fuelless_samples = 0, crown_flames = 0;
};
// Emitters for the current field state. Burning cells get up to
// flames_per_cell x fuel ground flames at seeded sub-cell positions that pass
// the fuel/material test, one to three smoke plumes, and a crown flame for
// every tree (from the optional placement) standing in them; burned cells get
// sparse smouldering plumes. Instances beyond max_instances are dropped
// deterministically (ground flames first).
FireEmitters build_fire_emitters(const replay::FireField& field, std::int32_t origin_grid_x, std::int32_t origin_grid_y,
                                 double cell_size_meters, const TerrainSampler& sampler, const TerrainStyle& terrain,
                                 const VegetationPlacement* vegetation, const FireStyle& style,
                                 std::size_t max_instances = 200'000);
// Fuel/material gate shared with the ground shader: 0 where nothing can burn.
double fire_fuel_factor(double fuel_load, const TerrainMaterial& material, double threshold);

// Continuous front field: the simulation's cell states blurred over a 5x5
// neighbourhood with a radial kernel (1 within 0.45 cells of a centre, 0 beyond
// 1.55 cells), so fronts are no longer cell squares. Adding fire_edge_noise() and
// thresholding at kFireFrontThreshold gives the ragged outline that flames,
// charring and the ground footprint all share (GLSL twins in the shaders).
struct FireFront { double active = 0.0, burned = 0.0; };
inline constexpr double kFireFrontThreshold = 0.6;
FireFront fire_front_at(const replay::FireField& field, std::int32_t origin_grid_x, std::int32_t origin_grid_y,
                        double cell_size_meters, double x, double z);
double fire_edge_noise(double x, double z);  // world-space value noise in about -0.73 .. 0.73
bool fire_front_burning(const FireFront& front, double edge_noise);  // active side of the ragged outline
bool fire_front_burned(const FireFront& front, double edge_noise);   // charred and no longer burning

struct FireDrawStats { std::size_t flames = 0, smoke = 0, draws = 0; };
class FireEffectsRenderer {
 public:
  FireEffectsRenderer(RenderDevice& device, std::size_t capacity);
  FireEffectsRenderer(const FireEffectsRenderer&) = delete;
  FireEffectsRenderer& operator=(const FireEffectsRenderer&) = delete;
  void set_emitters(FireEmitters emitters);  // truncated to capacity
  const FireEmitters& emitters() const { return emitters_; }
  std::size_t capacity() const { return capacity_; }
  // Transparent pass: draws flames then smoke within the draw distance and frustum.
  FireDrawStats draw(PipelineHandle pipeline, BufferHandle camera, BufferHandle style, const Frustum& frustum,
                     math::Vec3 eye, double draw_distance_meters, bool cull = true);
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  std::size_t capacity_;
  BufferHandle vb_, ib_, instances_;
  FireEmitters emitters_;
  std::vector<FireInstance> scratch_;
};

}  // namespace terraflux::graphics
