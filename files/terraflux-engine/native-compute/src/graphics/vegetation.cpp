#include <terraflux/graphics/vegetation.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kNoVertex = std::numeric_limits<std::uint32_t>::max();

float f(double v) {
  const auto value = static_cast<float>(v);
  if (!std::isfinite(value)) throw std::invalid_argument("vegetation value is not representable in float");
  return value;
}
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool normalized_color(math::Vec3 c) {
  return finite(c) && c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x <= 1 && c.y <= 1 && c.z <= 1;
}
bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

void push_vertex(VegetationMesh& mesh, math::Vec3 p, math::Vec3 n, math::Vec3 c, double flex, double tintable = 1.0) {
  const auto unit = n.length() > 0 ? n.normalized() : math::Vec3{0, 1, 0};
  mesh.vertices.push_back({{f(p.x), f(p.y), f(p.z)}, {f(unit.x), f(unit.y), f(unit.z)},
                           {f(c.x), f(c.y), f(c.z)}, f(std::clamp(flex, 0.0, 1.0)), f(std::clamp(tintable, 0.0, 1.0))});
}
void push_triangle(VegetationMesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  mesh.indices.insert(mesh.indices.end(), {a, b, c});
}

// Ring of `segments` vertices at height y and radius r; returns the first index.
std::uint32_t push_ring(VegetationMesh& mesh, std::uint32_t segments, double y, double r, double slope_normal_y,
                        math::Vec3 color, double flex, double tintable = 1.0) {
  const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
  for (std::uint32_t i = 0; i < segments; ++i) {
    const double angle = 2.0 * kPi * i / segments;
    const math::Vec3 radial{std::cos(angle), 0.0, std::sin(angle)};
    const math::Vec3 normal = radial * std::sqrt(std::max(0.0, 1.0 - slope_normal_y * slope_normal_y)) +
                              math::Vec3{0, slope_normal_y, 0};
    push_vertex(mesh, {radial.x * r, y, radial.z * r}, normal, color, flex, tintable);
  }
  return first;
}
// Side band between two rings (CCW seen from outside).
void push_band(VegetationMesh& mesh, std::uint32_t lower, std::uint32_t upper, std::uint32_t segments) {
  for (std::uint32_t i = 0; i < segments; ++i) {
    const std::uint32_t j = (i + 1) % segments;
    push_triangle(mesh, lower + i, upper + i, lower + j);
    push_triangle(mesh, lower + j, upper + i, upper + j);
  }
}
// Cone from a ring to an apex vertex.
void push_cone(VegetationMesh& mesh, std::uint32_t ring, std::uint32_t segments, double apex_y, math::Vec3 color,
               double flex, double ring_y, double radius) {
  const double slant = std::hypot(apex_y - ring_y, radius);
  const double ny = slant > 0 ? radius / slant : 1.0;
  const auto apex = static_cast<std::uint32_t>(mesh.vertices.size());
  push_vertex(mesh, {0, apex_y, 0}, {0, 1, 0}, color, flex);
  (void)ny;
  for (std::uint32_t i = 0; i < segments; ++i) {
    const std::uint32_t j = (i + 1) % segments;
    push_triangle(mesh, ring + i, apex, ring + j);
  }
}

void validate_tree(const TreeMeshOptions& o) {
  if (o.segments < 3 || o.segments > 24) throw std::invalid_argument("tree segments must be 3..24");
  if (o.layers < 1 || o.layers > 6) throw std::invalid_argument("tree layers must be 1..6");
  if (!within(o.trunk_height, 0.05, 0.6)) throw std::invalid_argument("trunk height must be 0.05..0.6 of the tree");
  if (!within(o.crown_width, 0.1, 1.5)) throw std::invalid_argument("crown width must be 0.1..1.5 of the tree height");
  if (!normalized_color(o.foliage_color) || !normalized_color(o.trunk_color))
    throw std::invalid_argument("tree colors must be finite and normalized");
}

void validate(const VegetationStyle& s) {
  if (!within(s.tree_spacing_meters, 4, 200)) throw std::invalid_argument("tree spacing must be 4..200 meters");
  if (!within(s.grass_spacing_meters, 0.5, 20)) throw std::invalid_argument("grass spacing must be 0.5..20 meters");
  if (!within(s.grass_distance_meters, 0, 5000)) throw std::invalid_argument("grass distance must be 0..5000 meters");
  if (!within(s.tree_distance_meters, 100, 50000)) throw std::invalid_argument("tree distance must be 100..50000 meters");
  if (!within(s.tree_height_meters, 1, 80)) throw std::invalid_argument("tree height must be 1..80 meters");
  if (!within(s.grass_height_meters, 0.1, 3)) throw std::invalid_argument("grass height must be 0.1..3 meters");
  if (!within(s.sway_amplitude, 0, 3)) throw std::invalid_argument("sway amplitude must be 0..3");
  if (!within(s.wind_direction_degrees, 0, 360)) throw std::invalid_argument("wind direction must be 0..360 degrees");
  if (!within(s.wind_speed_mps, 0, 30)) throw std::invalid_argument("wind speed must be 0..30 m/s");
  if (!within(s.color_variation, 0, 0.5)) throw std::invalid_argument("color variation must be 0..0.5");
  if (!finite(s.light_direction) || !(s.light_direction.length() > 0)) throw std::invalid_argument("vegetation light must be nonzero");
  if (!within(s.ambient, 0, 1)) throw std::invalid_argument("vegetation ambient must be 0..1");
}

// Deterministic hashes for placement.
std::uint32_t hash_u32(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
double hash01(std::int64_t ix, std::int64_t iy, std::uint32_t salt, std::uint32_t seed) {
  const auto h = hash_u32(static_cast<std::uint32_t>(ix) * 0x9E3779B1U ^ hash_u32(static_cast<std::uint32_t>(iy) + salt * 0x85EBCA77U + seed * 0xC2B2AE3DU));
  return static_cast<double>(h & 0xFFFFFFu) / 16777216.0;
}

}  // namespace

// ---------------------------------------------------------------------------
const char* tree_shape_name(TreeShape shape) {
  switch (shape) {
    case TreeShape::Conifer: return "conifer";
    case TreeShape::Broadleaf: return "broadleaf";
    case TreeShape::Shrub: return "shrub";
  }
  throw std::invalid_argument("unknown tree shape");
}

VegetationMesh make_tree_mesh(const TreeMeshOptions& o) {
  validate_tree(o);
  VegetationMesh mesh;
  mesh.name = tree_shape_name(o.shape);
  const std::uint32_t n = o.segments;
  const double trunk_top = o.shape == TreeShape::Shrub ? 0.12 : o.trunk_height;
  const double trunk_radius = o.shape == TreeShape::Shrub ? 0.03 : 0.035 + 0.02 * o.crown_width;
  // Trunk: tapered tube with a closed top.
  const auto base = push_ring(mesh, n, 0.0, trunk_radius, 0.0, o.trunk_color, 0.0, 0.0);
  const auto top = push_ring(mesh, n, trunk_top + 0.02, trunk_radius * 0.7, 0.0, o.trunk_color, trunk_top, 0.0);
  push_band(mesh, base, top, n);
  if (o.shape == TreeShape::Conifer) {
    // Stacked cones, each tier overlapping the one below, narrowing upward.
    const double span = 1.0 - trunk_top;
    for (std::uint32_t layer = 0; layer < o.layers; ++layer) {
      const double t0 = static_cast<double>(layer) / o.layers, t1 = static_cast<double>(layer + 1) / o.layers;
      const double y0 = trunk_top + span * t0 * 0.85;
      const double y1 = layer + 1 == o.layers ? 1.0 : trunk_top + span * (t1 * 0.85 + 0.15);
      const double radius = o.crown_width * 0.5 * (1.0 - 0.75 * t0);
      const math::Vec3 color = o.foliage_color * (0.85 + 0.15 * t0);
      const double flex0 = std::pow(y0, 1.5), flex1 = std::pow(y1, 1.5);
      const double slant = std::hypot(y1 - y0, radius);
      const auto ring = push_ring(mesh, n, y0, radius, slant > 0 ? radius / slant : 0.5, color, flex0);
      push_cone(mesh, ring, n, y1, color, flex1, y0, radius);
      // Underside disc so the tier has no hole when seen from below.
      const auto center = static_cast<std::uint32_t>(mesh.vertices.size());
      push_vertex(mesh, {0, y0, 0}, {0, -1, 0}, color * 0.7, flex0);
      for (std::uint32_t i = 0; i < n; ++i) push_triangle(mesh, ring + (i + 1) % n, center, ring + i);
    }
  } else {
    // Broadleaf/shrub: lobed canopy built from stacked rings (a low-poly
    // ellipsoid), the lobes offset slightly per layer for irregularity.
    const double crown_base = o.shape == TreeShape::Shrub ? 0.05 : trunk_top * 0.8;
    const double crown_height = 1.0 - crown_base;
    const std::uint32_t rings = std::max(2U, o.layers + 1);
    std::vector<std::uint32_t> ring_index;
    const auto bottom = static_cast<std::uint32_t>(mesh.vertices.size());
    push_vertex(mesh, {0, crown_base, 0}, {0, -1, 0}, o.foliage_color * 0.7, std::pow(crown_base, 1.5));
    for (std::uint32_t r = 0; r < rings; ++r) {
      const double t = (r + 1.0) / (rings + 1.0);  // 0 .. 1 exclusive
      const double y = crown_base + crown_height * t;
      const double radius = o.crown_width * 0.5 * std::sqrt(std::max(0.0, 1.0 - std::pow(2.0 * t - 1.0, 2)));
      const double lobe = 1.0 + 0.08 * std::sin(r * 2.3);
      const math::Vec3 color = o.foliage_color * (0.8 + 0.25 * t);
      const auto ring = static_cast<std::uint32_t>(mesh.vertices.size());
      for (std::uint32_t i = 0; i < n; ++i) {
        const double angle = 2.0 * kPi * i / n + 0.3 * r;
        const double wobble = lobe * (1.0 + 0.10 * std::sin(3.0 * angle + r));
        const math::Vec3 radial{std::cos(angle), 0.0, std::sin(angle)};
        const math::Vec3 normal = (radial * std::sqrt(1.0 - std::pow(2.0 * t - 1.0, 2)) + math::Vec3{0, 2.0 * t - 1.0, 0});
        push_vertex(mesh, {radial.x * radius * wobble, y, radial.z * radius * wobble}, normal, color, std::pow(y, 1.5));
      }
      ring_index.push_back(ring);
      if (r == 0) for (std::uint32_t i = 0; i < n; ++i) push_triangle(mesh, ring + (i + 1) % n, bottom, ring + i);
      else push_band(mesh, ring_index[r - 1], ring, n);
    }
    push_cone(mesh, ring_index.back(), n, 1.0, o.foliage_color * 1.05, 1.0, crown_base + crown_height * rings / (rings + 1.0), 0.1);
  }
  return mesh;
}

VegetationMesh make_grass_mesh(std::uint32_t blades, math::Vec3 color) {
  if (blades < 1 || blades > 8) throw std::invalid_argument("grass blades must be 1..8");
  if (!normalized_color(color)) throw std::invalid_argument("grass color must be finite and normalized");
  VegetationMesh mesh;
  mesh.name = "grass";
  for (std::uint32_t b = 0; b < blades; ++b) {
    const double angle = kPi * b / blades;
    const math::Vec3 along{std::cos(angle), 0, std::sin(angle)}, normal{-std::sin(angle), 0, std::cos(angle)};
    // A tapered blade: wide base, pinched tip, slight lean.
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    const math::Vec3 dark = color * 0.55;
    push_vertex(mesh, along * -0.28, normal, dark, 0.0);
    push_vertex(mesh, along * 0.28, normal, dark, 0.0);
    push_vertex(mesh, along * 0.20 + math::Vec3{0, 0.55, 0}, normal, color * 0.85, 0.55);
    push_vertex(mesh, along * -0.20 + math::Vec3{0, 0.55, 0}, normal, color * 0.85, 0.55);
    push_vertex(mesh, along * 0.03 + math::Vec3{0.06 * normal.x, 1.0, 0.06 * normal.z}, normal, color, 1.0);
    push_triangle(mesh, first, first + 1, first + 2);
    push_triangle(mesh, first, first + 2, first + 3);
    push_triangle(mesh, first + 3, first + 2, first + 4);
  }
  return mesh;
}

VegetationMesh vegetation_mesh_from_model(const assets::Mesh& model, const assets::MaterialLibrary& materials,
                                          math::Vec3 fallback_color, const std::string& name) {
  if (model.vertices.empty() || model.indices.size() < 3 || model.indices.size() % 3 != 0)
    throw std::invalid_argument("vegetation model needs at least one triangle");
  if (!normalized_color(fallback_color)) throw std::invalid_argument("vegetation fallback color must be normalized");
  double min_y = std::numeric_limits<double>::infinity(), max_y = -min_y, radius = 0;
  for (const auto& v : model.vertices) {
    if (!finite(v.position)) throw std::invalid_argument("vegetation model position must be finite");
    min_y = std::min(min_y, v.position.y); max_y = std::max(max_y, v.position.y);
  }
  const double height = max_y - min_y;
  if (!(height > 1e-9)) throw std::invalid_argument("vegetation model must have height");
  for (const auto& v : model.vertices) radius = std::max(radius, std::hypot(v.position.x, v.position.z) / height);
  if (radius > 50.0) throw std::invalid_argument("vegetation model is too wide for its height");
  // Per-vertex color from the section material; a vertex shared by sections takes the first.
  std::vector<math::Vec3> colors(model.vertices.size(), fallback_color);
  std::vector<bool> colored(model.vertices.size(), false);
  for (const auto& section : model.sections) {
    math::Vec3 color = fallback_color;
    const auto found = materials.find(section.material_name);
    if (found != materials.end() && normalized_color(found->second.base_color)) color = found->second.base_color;
    for (std::uint32_t i = section.first_index; i < section.first_index + section.index_count && i < model.indices.size(); ++i) {
      const auto index = model.indices[i];
      if (index >= model.vertices.size()) throw std::invalid_argument("vegetation model index out of range");
      if (!colored[index]) { colors[index] = color; colored[index] = true; }
    }
  }
  VegetationMesh mesh;
  mesh.name = name;
  mesh.vertices.reserve(model.vertices.size());
  for (std::size_t i = 0; i < model.vertices.size(); ++i) {
    const auto& v = model.vertices[i];
    const math::Vec3 p{v.position.x / height, (v.position.y - min_y) / height, v.position.z / height};
    // Greenish materials take the species tint; brown/grey wood keeps its own color.
    const bool foliage = colors[i].y >= colors[i].x && colors[i].y >= colors[i].z;
    push_vertex(mesh, p, finite(v.normal) ? v.normal : math::Vec3{0, 1, 0}, colors[i], p.y, foliage ? 1.0 : 0.0);
  }
  for (const auto index : model.indices) {
    if (index >= model.vertices.size()) throw std::invalid_argument("vegetation model index out of range");
    mesh.indices.push_back(index);
  }
  return mesh;
}

// ---------------------------------------------------------------------------
VegetationStyleUniform pack_vegetation_style(const VegetationStyle& style, math::Vec3 eye, double time_seconds,
                                             const VegetationFireGrid& fire) {
  validate(style);
  if (!finite(eye) || !std::isfinite(time_seconds) || time_seconds < 0)
    throw std::invalid_argument("vegetation eye must be finite and time non-negative");
  if (!within(fire.cell_size_meters, 1e-3, 1e6) || fire.width == 0 || fire.height == 0 ||
      std::abs(fire.origin_offset_x) > (1 << 24) || std::abs(fire.origin_offset_y) > (1 << 24))
    throw std::invalid_argument("vegetation fire grid must have a valid cell size, size and offsets");
  const double radians = style.wind_direction_degrees * kPi / 180.0;
  const auto light = style.light_direction.normalized();
  VegetationStyleUniform u{};
  u[0] = f(std::sin(radians)); u[1] = f(-std::cos(radians)); u[2] = f(style.wind_speed_mps); u[3] = f(time_seconds);
  u[4] = f(light.x); u[5] = f(light.y); u[6] = f(light.z); u[7] = f(style.ambient);
  u[8] = f(style.sway_amplitude); u[9] = f(style.grass_distance_meters * 0.6); u[10] = f(style.grass_distance_meters);
  u[11] = f(style.tree_distance_meters);
  u[12] = f(eye.x); u[13] = f(eye.y); u[14] = f(eye.z); u[15] = 0.0f;
  u[16] = f(fire.cell_size_meters); u[17] = f(fire.width); u[18] = f(fire.height); u[19] = fire.present ? 1.0f : 0.0f;
  u[20] = f(static_cast<double>(fire.origin_offset_x)); u[21] = f(static_cast<double>(fire.origin_offset_y));
  return u;
}

std::uint32_t vegetation_fire_state(const VegetationFireGrid& fire, const std::vector<std::uint32_t>& states,
                                    double x, double z) {
  if (!fire.present || !std::isfinite(x) || !std::isfinite(z)) return 0;
  const auto ix = static_cast<std::int64_t>(std::llround(x / fire.cell_size_meters)) + fire.origin_offset_x;
  const auto iy = static_cast<std::int64_t>(std::llround(-z / fire.cell_size_meters)) + fire.origin_offset_y;
  if (ix < 0 || iy < 0 || ix >= fire.width || iy >= fire.height) return 0;
  const auto index = static_cast<std::size_t>(iy) * fire.width + static_cast<std::size_t>(ix);
  return index < states.size() ? states[index] : 0;
}

ShaderLayout vegetation_shader_layout() {
  return {static_cast<std::uint32_t>(sizeof(VegetationVertex)),
          {{"POSITION", 0, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(VegetationVertex, position))},
           {"NORMAL", 1, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(VegetationVertex, normal))},
           {"COLOR", 2, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(VegetationVertex, color))},
           {"FLEX", 3, VertexFormat::Float1, static_cast<std::uint32_t>(offsetof(VegetationVertex, flex))},
           {"TINT_WEIGHT", 4, VertexFormat::Float1, static_cast<std::uint32_t>(offsetof(VegetationVertex, tintable))}},
          {{"Camera", ShaderResourceKind::UniformBuffer, 0, kVertexStage},
           {"VegetationStyle", ShaderResourceKind::UniformBuffer, kVegetationStyleSlot, kVertexStage | kFragmentStage},
           {"InstanceData", ShaderResourceKind::StorageBuffer, 4, kVertexStage},
           {"PlantFireState", ShaderResourceKind::StorageBuffer, kVegetationFireSlot, kVertexStage}}};
}

// ---------------------------------------------------------------------------
TerrainSampler::TerrainSampler(const terrain::TerrainMesh& mesh) : mesh_(mesh) {
  if (mesh.vertices.empty()) throw std::invalid_argument("terrain sampler needs vertices");
  if (!(mesh.cell_size_meters > 0) || !std::isfinite(mesh.cell_size_meters))
    throw std::invalid_argument("terrain sampler needs a valid cell size");
  std::int64_t max_x = std::numeric_limits<std::int64_t>::min(), max_y = max_x;
  min_x_ = std::numeric_limits<std::int64_t>::max(); min_y_ = min_x_;
  for (const auto& v : mesh.vertices) {
    min_x_ = std::min<std::int64_t>(min_x_, v.grid_x); max_x = std::max<std::int64_t>(max_x, v.grid_x);
    min_y_ = std::min<std::int64_t>(min_y_, v.grid_y); max_y = std::max<std::int64_t>(max_y, v.grid_y);
  }
  const auto width = static_cast<std::uint64_t>(max_x - min_x_ + 1), height = static_cast<std::uint64_t>(max_y - min_y_ + 1);
  if (width > 1u << 16 || height > 1u << 16 || width * height > 64'000'000ULL)
    throw std::invalid_argument("terrain sampler lattice is too large");
  width_ = static_cast<std::size_t>(width); height_ = static_cast<std::size_t>(height);
  lattice_.assign(width_ * height_, kNoVertex);
  quad_.assign(width_ * height_, false);
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    const auto& v = mesh.vertices[i];
    auto& slot = lattice_[static_cast<std::size_t>(v.grid_y - min_y_) * width_ + static_cast<std::size_t>(v.grid_x - min_x_)];
    if (slot != kNoVertex) throw std::invalid_argument("terrain sampler requires unique lattice vertices");
    slot = static_cast<std::uint32_t>(i);
  }
  lo_ = hi_ = mesh.vertices.front().position;
  for (const auto& v : mesh.vertices) {
    lo_ = {std::min(lo_.x, v.position.x), std::min(lo_.y, v.position.y), std::min(lo_.z, v.position.z)};
    hi_ = {std::max(hi_.x, v.position.x), std::max(hi_.y, v.position.y), std::max(hi_.z, v.position.z)};
  }
  for (std::size_t t = 0; t + 5 < mesh.indices.size(); t += 6) {
    const auto a = mesh.indices[t];
    if (a >= mesh.vertices.size()) throw std::invalid_argument("terrain sampler index out of range");
    const auto& v = mesh.vertices[a];
    quad_[static_cast<std::size_t>(v.grid_y - min_y_) * width_ + static_cast<std::size_t>(v.grid_x - min_x_)] = true;
  }
}

std::optional<TerrainSample> TerrainSampler::sample(double x, double z) const {
  if (!std::isfinite(x) || !std::isfinite(z)) return std::nullopt;
  const double cell = mesh_.cell_size_meters;
  const double fx = x / cell + static_cast<double>(mesh_.origin_grid_x) - static_cast<double>(min_x_);
  const double fy = -z / cell + static_cast<double>(mesh_.origin_grid_y) - static_cast<double>(min_y_);
  if (fx < 0 || fy < 0 || fx >= static_cast<double>(width_ - 1) + 1e-9 || fy >= static_cast<double>(height_ - 1) + 1e-9) return std::nullopt;
  auto ix = static_cast<std::size_t>(std::floor(fx)), iy = static_cast<std::size_t>(std::floor(fy));
  ix = std::min(ix, width_ - 2); iy = std::min(iy, height_ - 2);
  if (!quad_[iy * width_ + ix]) return std::nullopt;
  const double u = std::clamp(fx - static_cast<double>(ix), 0.0, 1.0), v = std::clamp(fy - static_cast<double>(iy), 0.0, 1.0);
  const auto& a = mesh_.vertices[lattice_[iy * width_ + ix]];
  const auto& b = mesh_.vertices[lattice_[iy * width_ + ix + 1]];
  const auto& c = mesh_.vertices[lattice_[(iy + 1) * width_ + ix]];
  const auto& d = mesh_.vertices[lattice_[(iy + 1) * width_ + ix + 1]];
  // Triangles (a, b, c) and (b, d, c): the b-c diagonal is u + v = 1.
  double wa, wb, wc, wd;
  if (u + v <= 1.0) { wa = 1 - u - v; wb = u; wc = v; wd = 0; }
  else { wa = 0; wb = 1 - v; wc = 1 - u; wd = u + v - 1; }
  const auto blend = [&](auto get) { return get(a) * wa + get(b) * wb + get(c) * wc + get(d) * wd; };
  TerrainSample s;
  s.position = blend([](const terrain::TerrainVertex& t) { return t.position; });
  s.normal = blend([](const terrain::TerrainVertex& t) { return t.normal; });
  s.normal = s.normal.length() > 0 ? s.normal.normalized() : math::Vec3{0, 1, 0};
  s.fuel_load = blend([](const terrain::TerrainVertex& t) { return math::Vec3{t.fuel_load, 0, 0}; }).x;
  s.fuel_moisture = blend([](const terrain::TerrainVertex& t) { return math::Vec3{t.fuel_moisture, 0, 0}; }).x;
  s.curvature = blend([](const terrain::TerrainVertex& t) { return math::Vec3{t.curvature, 0, 0}; }).x;
  return s;
}

// ---------------------------------------------------------------------------
VegetationPlacement place_vegetation(const terrain::TerrainMesh& mesh, const TerrainStyle& terrain,
                                     const VegetationStyle& style, std::size_t max_instances) {
  validate(style);
  (void)pack_terrain_style(terrain);
  VegetationPlacement out;
  if (mesh.vertices.empty() || mesh.indices.empty()) return out;
  const TerrainSampler sampler(mesh);
  const auto lo = sampler.lower_bound(), hi = sampler.upper_bound();
  const double chunk = out.chunk_size_meters;
  const auto chunks_x = static_cast<std::size_t>(std::floor((hi.x - lo.x) / chunk)) + 1;
  const auto chunks_z = static_cast<std::size_t>(std::floor((hi.z - lo.z) / chunk)) + 1;
  if (chunks_x * chunks_z > 1'000'000) throw std::length_error("vegetation chunk grid is too large");
  struct Bucket { std::vector<VegetationInstance> trees, grass; };
  std::vector<Bucket> buckets(chunks_x * chunks_z);
  const auto bucket_of = [&](double x, double z) {
    const auto cx = std::min(chunks_x - 1, static_cast<std::size_t>(std::max(0.0, (x - lo.x) / chunk)));
    const auto cz = std::min(chunks_z - 1, static_cast<std::size_t>(std::max(0.0, (z - lo.z) / chunk)));
    return cz * chunks_x + cx;
  };
  std::size_t total = 0;
  // Species palettes by moisture class; kind 0 is conifer-like, 1 broadleaf-like.
  // Species tints are multipliers on the mesh's foliage color: semi-dry ground
  // keeps the authored color, dry ground turns olive-yellow, wet ground lush.
  const math::Vec3 dry{1.55, 1.05, 0.95}, semi{1.0, 1.0, 1.0}, wet{0.82, 1.08, 1.12};
  const math::Vec3 grass_dry{1.45, 1.10, 1.05}, grass_semi{1.0, 1.0, 1.0}, grass_wet{0.70, 0.95, 0.95};

  const auto jitter = [&](double spacing, std::uint32_t salt, auto&& visit) {
    const auto nx = static_cast<std::int64_t>(std::ceil((hi.x - lo.x) / spacing));
    const auto nz = static_cast<std::int64_t>(std::ceil((hi.z - lo.z) / spacing));
    if (nx <= 0 || nz <= 0) return;
    if (static_cast<double>(nx) * static_cast<double>(nz) > 40'000'000.0)
      throw std::length_error("vegetation sample grid is too large; increase the spacing");
    for (std::int64_t iz = 0; iz < nz; ++iz)
      for (std::int64_t ix = 0; ix < nx; ++ix) {
        const double x = lo.x + (static_cast<double>(ix) + hash01(ix, iz, salt, style.seed)) * spacing;
        const double z = lo.z + (static_cast<double>(iz) + hash01(ix, iz, salt + 1, style.seed)) * spacing;
        const auto s = sampler.sample(x, z);
        if (!s) continue;
        const auto material = terrain_material({s->position.y, s->normal, s->fuel_load, s->fuel_moisture, s->curvature}, terrain);
        visit(ix, iz, *s, material);
      }
  };
  const auto tinted = [&](math::Vec3 base, std::int64_t ix, std::int64_t iz, std::uint32_t salt) {
    const double value = 1.0 + style.color_variation * (2.0 * hash01(ix, iz, salt + 7, style.seed) - 1.0);
    const double hue = style.color_variation * 0.6 * (2.0 * hash01(ix, iz, salt + 8, style.seed) - 1.0);
    math::Vec3 c{base.x * value * (1.0 + hue), base.y * value, base.z * value * (1.0 - hue)};
    return math::Vec3{std::clamp(c.x, 0.0, 2.0), std::clamp(c.y, 0.0, 2.0), std::clamp(c.z, 0.0, 2.0)};
  };

  if (style.trees) {
    jitter(style.tree_spacing_meters, 11, [&](std::int64_t ix, std::int64_t iz, const TerrainSample& s, const TerrainMaterial& m) {
      const double cover = m.trees * m.vegetation * (1.0 - m.snow) * (1.0 - m.ice) * (1.0 - m.submerged) * (1.0 - m.rock);
      if (cover < 0.05 || hash01(ix, iz, 3, style.seed) > cover) return;
      if (++total > max_instances) throw std::length_error("vegetation instance budget exceeded");
      VegetationInstance t{};
      t.position[0] = f(s.position.x); t.position[1] = f(s.position.y); t.position[2] = f(s.position.z);
      const double size = 0.7 + 0.6 * hash01(ix, iz, 4, style.seed);
      t.scale = f(style.tree_height_meters * size * (0.85 + 0.15 * m.wet));
      t.rotation = f(2.0 * kPi * hash01(ix, iz, 5, style.seed));
      t.phase = f(2.0 * kPi * hash01(ix, iz, 6, style.seed));
      t.bend = f(std::clamp(m.slope_degrees / 60.0, 0.0, 1.0));
      // Wet ground grows broadleaf trees, dry and semi-dry ground conifers, with some mixing.
      const double broadleaf = m.wet * 0.85 + m.semi_dry * 0.25 + m.dry * 0.05;
      t.kind = hash01(ix, iz, 9, style.seed) < broadleaf ? 1.0f : 0.0f;
      const math::Vec3 base = dry * m.dry + semi * m.semi_dry + wet * m.wet;
      const auto tint = tinted(base, ix, iz, 10);
      t.tint[0] = f(tint.x); t.tint[1] = f(tint.y); t.tint[2] = f(tint.z);
      buckets[bucket_of(s.position.x, s.position.z)].trees.push_back(t);
    });
  }
  if (style.grass && style.grass_distance_meters > 0) {
    jitter(style.grass_spacing_meters, 23, [&](std::int64_t ix, std::int64_t iz, const TerrainSample& s, const TerrainMaterial& m) {
      const double cover = m.vegetation * (1.0 - 0.7 * m.trees) * (1.0 - m.snow) * (1.0 - m.ice) * (1.0 - m.submerged) * (1.0 - m.rock);
      if (cover < 0.08 || hash01(ix, iz, 13, style.seed) > cover) return;
      if (++total > max_instances) throw std::length_error("vegetation instance budget exceeded");
      VegetationInstance g{};
      g.position[0] = f(s.position.x); g.position[1] = f(s.position.y); g.position[2] = f(s.position.z);
      g.scale = f(style.grass_height_meters * (0.6 + 0.8 * hash01(ix, iz, 14, style.seed)) * (0.7 + 0.5 * m.wet));
      g.rotation = f(2.0 * kPi * hash01(ix, iz, 15, style.seed));
      g.phase = f(2.0 * kPi * hash01(ix, iz, 16, style.seed));
      g.bend = 0.0f; g.kind = 0.0f;
      const auto tint = tinted(grass_dry * m.dry + grass_semi * m.semi_dry + grass_wet * m.wet, ix, iz, 17);
      g.tint[0] = f(tint.x); g.tint[1] = f(tint.y); g.tint[2] = f(tint.z);
      buckets[bucket_of(s.position.x, s.position.z)].grass.push_back(g);
    });
  }
  for (auto& bucket : buckets) {
    if (bucket.trees.empty() && bucket.grass.empty()) continue;
    VegetationChunk c;
    c.tree_first = static_cast<std::uint32_t>(out.trees.size()); c.tree_count = static_cast<std::uint32_t>(bucket.trees.size());
    c.grass_first = static_cast<std::uint32_t>(out.grass.size()); c.grass_count = static_cast<std::uint32_t>(bucket.grass.size());
    bool first = true;
    const auto extend = [&](const VegetationInstance& i, double width) {
      const math::Vec3 p{i.position[0], i.position[1], i.position[2]};
      const math::Vec3 lo_i{p.x - width, p.y, p.z - width}, hi_i{p.x + width, p.y + i.scale, p.z + width};
      if (first) { c.lo = lo_i; c.hi = hi_i; first = false; return; }
      c.lo = {std::min(c.lo.x, lo_i.x), std::min(c.lo.y, lo_i.y), std::min(c.lo.z, lo_i.z)};
      c.hi = {std::max(c.hi.x, hi_i.x), std::max(c.hi.y, hi_i.y), std::max(c.hi.z, hi_i.z)};
    };
    for (const auto& t : bucket.trees) { extend(t, t.scale * 0.8); out.trees.push_back(t); out.trees_per_kind[t.kind > 0.5f ? 1 : 0]++; }
    for (const auto& g : bucket.grass) { extend(g, g.scale); out.grass.push_back(g); }
    out.chunks.push_back(c);
  }
  return out;
}

// ---------------------------------------------------------------------------
VegetationRenderer::VegetationRenderer(RenderDevice& device, VegetationPlacement placement,
                                       std::vector<VegetationMesh> tree_meshes, VegetationMesh grass_mesh,
                                       double grass_distance_meters, double grass_spacing_meters)
    : device_(device), resources_(device), placement_(std::move(placement)),
      grass_radius_hint_(grass_distance_meters), grass_spacing_hint_(grass_spacing_meters) {
  set_tree_meshes(std::move(tree_meshes));
  set_grass_mesh(std::move(grass_mesh));
  allocate_instance_buffers();
}

VegetationRenderer::MeshGpu VegetationRenderer::upload(const VegetationMesh& mesh) {
  if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3)
    throw std::invalid_argument("vegetation mesh needs whole triangles");
  for (const auto i : mesh.indices) if (i >= mesh.vertices.size()) throw std::invalid_argument("vegetation mesh index out of range");
  MeshGpu gpu;
  const auto vertices = std::as_bytes(std::span(mesh.vertices));
  const auto indices = std::as_bytes(std::span(mesh.indices));
  gpu.vb = resources_.create_buffer({BufferUsage::Vertex, vertices.size()}, vertices);
  try { gpu.ib = resources_.create_buffer({BufferUsage::Index, indices.size()}, indices); }
  catch (...) { resources_.release_buffer(gpu.vb); throw; }
  gpu.indices = static_cast<std::uint32_t>(mesh.indices.size());
  return gpu;
}

void VegetationRenderer::set_tree_meshes(std::vector<VegetationMesh> meshes) {
  if (meshes.empty() || meshes.size() > 2) throw std::invalid_argument("vegetation needs one or two tree meshes");
  std::vector<MeshGpu> staged;
  try { for (const auto& m : meshes) staged.push_back(upload(m)); }
  catch (...) { for (auto& g : staged) { resources_.release_buffer(g.ib); resources_.release_buffer(g.vb); } throw; }
  for (auto& g : tree_gpu_) { resources_.release_buffer(g.ib); resources_.release_buffer(g.vb); }
  tree_gpu_ = std::move(staged);
  tree_meshes_ = std::move(meshes);
}

void VegetationRenderer::set_grass_mesh(VegetationMesh mesh) {
  MeshGpu staged = upload(mesh);
  if (grass_gpu_.vb) { resources_.release_buffer(grass_gpu_.ib); resources_.release_buffer(grass_gpu_.vb); }
  grass_gpu_ = staged;
  grass_mesh_ = std::move(mesh);
}

void VegetationRenderer::allocate_instance_buffers() {
  const auto release = [&](BufferHandle& h) { if (h) { resources_.release_buffer(h); h = {}; } };
  release(tree_instances_[0]); release(tree_instances_[1]); release(grass_instances_);
  tree_capacity_ = placement_.trees_per_kind;
  // Grass is refilled per frame with the chunks inside the draw distance, so
  // the buffer only needs the densest disc of that radius (plus the chunk
  // margin) at the placement spacing; chunks are submitted nearest first, so an
  // overflow drops the farthest grass rather than random patches.
  const double radius = grass_radius_hint_ + 1.5 * placement_.chunk_size_meters;
  const double disc = kPi * radius * radius / std::max(grass_spacing_hint_ * grass_spacing_hint_, 0.25);
  grass_capacity_ = std::min(placement_.grass.size(), static_cast<std::size_t>(std::ceil(disc * 0.6)) + 1024);
  const auto make = [&](std::size_t capacity) -> BufferHandle {
    if (!capacity) return {};
    std::vector<VegetationInstance> zero(capacity);
    return resources_.create_buffer({BufferUsage::Storage, capacity * sizeof(VegetationInstance)}, std::as_bytes(std::span(zero)));
  };
  tree_instances_[0] = make(tree_capacity_[0]);
  tree_instances_[1] = make(tree_capacity_[1]);
  grass_instances_ = make(grass_capacity_);
}

void VegetationRenderer::set_placement(VegetationPlacement placement, double grass_distance_meters,
                                       double grass_spacing_meters) {
  placement_ = std::move(placement);
  grass_radius_hint_ = grass_distance_meters;
  grass_spacing_hint_ = grass_spacing_meters;
  allocate_instance_buffers();
}

std::uint64_t VegetationRenderer::geometry_bytes() const {
  std::uint64_t bytes = (tree_capacity_[0] + tree_capacity_[1] + grass_capacity_) * sizeof(VegetationInstance);
  for (const auto& m : tree_meshes_) bytes += m.vertices.size() * sizeof(VegetationVertex) + m.indices.size() * 4;
  bytes += grass_mesh_.vertices.size() * sizeof(VegetationVertex) + grass_mesh_.indices.size() * 4;
  return bytes;
}

VegetationDrawStats VegetationRenderer::draw(PipelineHandle pipeline, BufferHandle camera, BufferHandle style,
                                             BufferHandle fire_states, const Frustum& frustum, math::Vec3 eye,
                                             const VegetationStyle& ranges, bool cull) {
  if (!device_.active_render_pass_id()) throw std::logic_error("vegetation requires an active pass");
  VegetationDrawStats stats;
  stats.chunks_total = placement_.chunks.size();
  const auto nearest_distance = [&](const VegetationChunk& c) {
    const double dx = std::max({c.lo.x - eye.x, 0.0, eye.x - c.hi.x});
    const double dy = std::max({c.lo.y - eye.y, 0.0, eye.y - c.hi.y});
    const double dz = std::max({c.lo.z - eye.z, 0.0, eye.z - c.hi.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };
  std::array<std::vector<VegetationInstance>, 2> trees;
  std::vector<VegetationInstance>& grass = scratch_;
  grass.clear();
  std::vector<std::pair<double, const VegetationChunk*>> visible;
  for (const auto& c : placement_.chunks) {
    if (cull && !frustum.intersects_aabb(c.lo, c.hi)) continue;
    const double distance = nearest_distance(c);
    if (distance > ranges.tree_distance_meters) continue;
    visible.emplace_back(distance, &c);
  }
  std::sort(visible.begin(), visible.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [distance, chunk] : visible) {
    const auto& c = *chunk;
    ++stats.chunks_visible;
    for (std::uint32_t i = c.tree_first; i < c.tree_first + c.tree_count; ++i) {
      const auto& t = placement_.trees[i];
      trees[t.kind > 0.5f ? 1 : 0].push_back(t);
    }
    if (distance <= ranges.grass_distance_meters)
      for (std::uint32_t i = c.grass_first; i < c.grass_first + c.grass_count; ++i) grass.push_back(placement_.grass[i]);
  }
  const auto submit = [&](BufferHandle instances, std::size_t capacity, std::vector<VegetationInstance>& list, const MeshGpu& mesh) {
    if (list.empty() || !instances || !mesh.vb) return;
    if (list.size() > capacity) list.resize(capacity);
    std::size_t offset = 0;
    // Draws are limited to 65535 instances each; the buffer is refilled per batch.
    while (offset < list.size()) {
      const auto count = std::min<std::size_t>(list.size() - offset, std::min<std::size_t>(capacity, 65535));
      device_.update_buffer(instances, 0, std::as_bytes(std::span(list).subspan(offset, count)));
      DrawIndexed command{pipeline, mesh.vb, mesh.ib, 0, mesh.indices,
                          {{0, camera}, {kVegetationStyleSlot, style}}, {{4, instances}, {kVegetationFireSlot, fire_states}}, {},
                          static_cast<std::uint32_t>(count)};
      device_.draw_indexed(command);
      ++stats.draws;
      stats.triangles += static_cast<std::uint64_t>(mesh.indices / 3) * count;
      offset += count;
    }
  };
  for (std::size_t k = 0; k < 2; ++k) {
    stats.tree_instances += trees[k].size();
    const auto& mesh = tree_gpu_[std::min(k, tree_gpu_.size() - 1)];
    submit(tree_instances_[k], tree_capacity_[k], trees[k], mesh);
  }
  stats.grass_instances = grass.size();
  submit(grass_instances_, grass_capacity_, grass, grass_gpu_);
  return stats;
}

}  // namespace terraflux::graphics
