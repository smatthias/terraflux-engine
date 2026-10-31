#pragma once

#include <terraflux/assets/obj.hpp>
#include <terraflux/assets/material.hpp>
#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/graphics/visibility.hpp>
#include <terraflux/math.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace terraflux::graphics {

// ---------------------------------------------------------------------------
// Meshes: unit-height models (base at y = 0, top at y = 1, metres come from
// the instance scale) with vertex colors and a "flex" weight (0 at the
// anchored base .. 1 at the tip) that drives wind sway. Procedural trees and
// grass tufts are generated here; any OBJ can be converted as well, so the
// meshes are configurable and can be swapped while the viewer runs.
struct VegetationVertex {
  float position[3];
  float normal[3];
  float color[3];
  float flex;
  float tintable;  // 1 foliage/blades (takes the instance tint), 0 trunk/wood
};
struct VegetationMesh {
  std::vector<VegetationVertex> vertices;
  std::vector<std::uint32_t> indices;  // CCW triangles; rendered two-sided
  std::string name;
};
enum class TreeShape : std::uint8_t { Conifer, Broadleaf, Shrub };
struct TreeMeshOptions {
  TreeShape shape = TreeShape::Conifer;
  std::uint32_t segments = 6;   // 3 .. 24 radial segments
  std::uint32_t layers = 3;     // 1 .. 6 cone tiers or canopy lobes
  double trunk_height = 0.30;   // 0.05 .. 0.6 of the total height
  double crown_width = 0.55;    // 0.1 .. 1.5 of the total height
  math::Vec3 foliage_color{0.18, 0.38, 0.16};
  math::Vec3 trunk_color{0.30, 0.22, 0.14};
};
VegetationMesh make_tree_mesh(const TreeMeshOptions& options);
// Crossed tapered blades, `blades` (1 .. 8) quads rotated evenly about the axis.
VegetationMesh make_grass_mesh(std::uint32_t blades = 3, math::Vec3 color = {0.45, 0.58, 0.24});
// Normalizes an imported model to unit height with its base at y = 0; vertex
// colors come from the section materials (base color) or the fallback; flex
// is the normalized height. Rejects empty or degenerate (flat) models.
VegetationMesh vegetation_mesh_from_model(const assets::Mesh& model, const assets::MaterialLibrary& materials,
                                          math::Vec3 fallback_color, const std::string& name);
const char* tree_shape_name(TreeShape shape);

// ---------------------------------------------------------------------------
// Style: placement densities, sizes, wind and lighting. Everything validated
// by pack_vegetation_style(); densities and distances are used by placement.
struct VegetationStyle {
  bool trees = true, grass = true;
  double tree_spacing_meters = 12.0;    // 4 .. 200: nominal distance between tree sample points
  double grass_spacing_meters = 2.5;    // 0.5 .. 20
  double grass_distance_meters = 300.0; // 0 .. 5000: grass is drawn (and shrinks away) within this range
  double tree_distance_meters = 6000.0; // 100 .. 50000
  double tree_height_meters = 16.0;     // 1 .. 80: nominal height, scaled per instance 0.7 .. 1.3
  double grass_height_meters = 0.6;     // 0.1 .. 3
  double sway_amplitude = 1.0;          // 0 .. 3: multiplies the wind sway
  double wind_direction_degrees = 90.0; // compass direction the wind blows toward
  double wind_speed_mps = 3.0;          // 0 .. 30
  double color_variation = 0.12;        // 0 .. 0.5: per-instance brightness/hue jitter
  std::uint32_t seed = 0;
  math::Vec3 light_direction{-0.4545, 0.7660, 0.4545};
  double ambient = 0.35;                // 0 .. 1
};
// Fire field addressing for burn states: plants in burned cells char, plants
// in burning cells glow. `present` false disables the lookup (the storage
// buffer must still be bound; one zero record suffices).
struct VegetationFireGrid {
  bool present = false;
  double cell_size_meters = 30.0;
  std::uint32_t width = 1, height = 1;
  std::int64_t origin_offset_x = 0, origin_offset_y = 0;  // terrain origin grid index minus field minimum
};
// Uniform block "VegetationStyle" at slot 13, std140, six vec4s:
//   wind:       unit direction (x, z), speed, time
//   light:      unit direction toward the light, ambient
//   params:     sway amplitude, grass fade start, grass fade end, tree fade end
//   eye:        camera position, 0
//   fireGrid:   cell size, width, height, present (1/0)
//   fireOrigin: origin offset x, origin offset y, 0, 0
inline constexpr std::uint32_t kVegetationStyleSlot = 13;
inline constexpr std::uint32_t kVegetationFireSlot = 5;  // PlantFireState storage buffer
inline constexpr std::size_t kVegetationStyleBytes = 96;
using VegetationStyleUniform = std::array<float, 24>;
VegetationStyleUniform pack_vegetation_style(const VegetationStyle& style, math::Vec3 eye, double time_seconds,
                                             const VegetationFireGrid& fire = {});
// CPU twin of the shader's cell lookup: state (0 unburned, 1 burned, 2 active,
// 3 absent) of the field cell under a world position, 0 when not present or outside.
std::uint32_t vegetation_fire_state(const VegetationFireGrid& fire, const std::vector<std::uint32_t>& states,
                                    double x, double z);
// POSITION 0, NORMAL 1, COLOR 2, FLEX 3, TINT_WEIGHT 4 (44-byte stride); Camera at slot 0,
// VegetationStyle at slot 13 (both stages), InstanceData storage at slot 4, PlantFireState storage at slot 5.
ShaderLayout vegetation_shader_layout();

// One placed plant: std430 layout of three vec4s (48 bytes), consumed by the
// Vegetation shader through the InstanceData storage buffer.
struct VegetationInstance {
  float position[3];
  float scale;      // metres of height
  float rotation;   // radians about +Y
  float phase;      // sway phase
  float bend;       // 0 .. 1 extra lean (steep ground, exposure)
  float kind;       // tree mesh slot (0 conifer-like, 1 broadleaf-like); grass 0
  float tint[3];   // multiplier on tintable vertex colors (species and per-plant variation)
  float pad = 0.0f;
};
inline constexpr std::size_t kVegetationInstanceBytes = 48;
static_assert(sizeof(VegetationInstance) == kVegetationInstanceBytes);

// ---------------------------------------------------------------------------
// Exact surface lookup on a terrain mesh lattice: barycentric interpolation on
// the rendered triangle (b-c diagonal), so plants stand on the drawn surface.
struct TerrainSample {
  math::Vec3 position, normal;
  double fuel_load = 0, fuel_moisture = 0, curvature = 0;
};
class TerrainSampler {
 public:
  explicit TerrainSampler(const terrain::TerrainMesh& mesh);
  std::optional<TerrainSample> sample(double x, double z) const;  // nullopt outside or over holes
  math::Vec3 lower_bound() const { return lo_; }
  math::Vec3 upper_bound() const { return hi_; }
 private:
  const terrain::TerrainMesh& mesh_;
  std::int64_t min_x_ = 0, min_y_ = 0;
  std::size_t width_ = 0, height_ = 0;
  std::vector<std::uint32_t> lattice_;  // vertex index or UINT32_MAX
  std::vector<bool> quad_;              // triangulated quad at (x, y)
  math::Vec3 lo_, hi_;
};

// ---------------------------------------------------------------------------
// Placement: deterministic (seeded) jittered-grid sampling of the terrain.
// Trees follow the material model's canopy cover (fuel load, tree line, slope,
// no snow, no water); grass follows vegetation cover outside dense canopy.
// Species kind and tint follow the moisture class with per-plant variation.
struct VegetationChunk {
  math::Vec3 lo, hi;                               // instance bounds including height
  std::uint32_t tree_first = 0, tree_count = 0;    // contiguous ranges in the instance arrays
  std::uint32_t grass_first = 0, grass_count = 0;
};
struct VegetationPlacement {
  std::vector<VegetationInstance> trees, grass;    // sorted by chunk
  std::vector<VegetationChunk> chunks;
  double chunk_size_meters = 120.0;
  std::array<std::size_t, 2> trees_per_kind{};
};
VegetationPlacement place_vegetation(const terrain::TerrainMesh& mesh, const TerrainStyle& terrain,
                                     const VegetationStyle& style, std::size_t max_instances = 3'000'000);

// ---------------------------------------------------------------------------
// Renderer: static mesh buffers per slot (two tree kinds, grass) and one
// instance storage buffer per slot refilled each frame with the chunks that
// survive frustum and distance culling. Meshes can be replaced at run time;
// instances can be replaced (re-placement) at run time. Caller owns the pass,
// pipeline, camera and style buffers.
struct VegetationDrawStats {
  std::size_t chunks_visible = 0, chunks_total = 0, tree_instances = 0, grass_instances = 0, draws = 0;
  std::uint64_t triangles = 0;
};
class VegetationRenderer {
 public:
  // The grass distance/spacing size the per-frame grass buffer (densest disc
  // of that radius); placement is drawn nearest chunk first.
  VegetationRenderer(RenderDevice& device, VegetationPlacement placement,
                     std::vector<VegetationMesh> tree_meshes, VegetationMesh grass_mesh,
                     double grass_distance_meters, double grass_spacing_meters);
  VegetationRenderer(const VegetationRenderer&) = delete;
  VegetationRenderer& operator=(const VegetationRenderer&) = delete;
  void set_tree_meshes(std::vector<VegetationMesh> meshes);  // 1 .. 2 meshes; kind k uses min(k, n-1)
  void set_grass_mesh(VegetationMesh mesh);
  void set_placement(VegetationPlacement placement, double grass_distance_meters, double grass_spacing_meters);
  std::size_t grass_capacity() const { return grass_capacity_; }
  const VegetationPlacement& placement() const { return placement_; }
  const std::vector<VegetationMesh>& tree_meshes() const { return tree_meshes_; }
  VegetationDrawStats draw(PipelineHandle pipeline, BufferHandle camera, BufferHandle style, BufferHandle fire_states,
                           const Frustum& frustum, math::Vec3 eye, const VegetationStyle& ranges,
                           bool cull = true);
  std::uint64_t geometry_bytes() const;
 private:
  struct MeshGpu { BufferHandle vb, ib; std::uint32_t indices = 0; };
  MeshGpu upload(const VegetationMesh& mesh);
  void allocate_instance_buffers();
  RenderDevice& device_;
  RenderResourceOwner resources_;
  VegetationPlacement placement_;
  std::vector<VegetationMesh> tree_meshes_;
  VegetationMesh grass_mesh_;
  std::vector<MeshGpu> tree_gpu_;
  MeshGpu grass_gpu_;
  std::array<BufferHandle, 2> tree_instances_{};
  BufferHandle grass_instances_{};
  std::array<std::size_t, 2> tree_capacity_{};
  std::size_t grass_capacity_ = 0;
  double grass_radius_hint_ = 0.0, grass_spacing_hint_ = 1.0;
  std::vector<VegetationInstance> scratch_;
};

}  // namespace terraflux::graphics
