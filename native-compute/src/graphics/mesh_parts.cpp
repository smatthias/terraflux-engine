#include <terraflux/graphics/mesh_parts.hpp>

#include <terraflux/graphics/mesh_upload.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace terraflux::graphics {
namespace {
float f(double v) { return static_cast<float>(v); }
void push_vertex(MeshPart& part, math::Vec3 p, math::Vec3 n) {
  TerrainGpuVertex vert{};
  vert.position[0] = f(p.x); vert.position[1] = f(p.y); vert.position[2] = f(p.z);
  vert.normal[0] = f(n.x); vert.normal[1] = f(n.y); vert.normal[2] = f(n.z);
  part.vertices.push_back(vert);
}
}  // namespace

void add_box(MeshPart& part, math::Vec3 centre, math::Vec3 along, math::Vec3 up, math::Vec3 across, math::Vec3 half) {
  const math::Vec3 axes[3] = {along, up, across};
  const double h[3] = {half.x, half.y, half.z};
  for (int face = 0; face < 6; ++face) {
    const int axis = face / 2;
    const double sign = face % 2 == 0 ? 1.0 : -1.0;
    const math::Vec3 n = axes[axis] * sign;
    const math::Vec3 u = axes[(axis + 1) % 3], v = axes[(axis + 2) % 3];
    const double hu = h[(axis + 1) % 3], hv = h[(axis + 2) % 3];
    const auto base = static_cast<std::uint32_t>(part.vertices.size());
    for (int k = 0; k < 4; ++k) {
      const double su = (k == 1 || k == 2) ? 1.0 : -1.0, sv = k >= 2 ? 1.0 : -1.0;
      push_vertex(part, centre + n * h[axis] + u * (su * hu) + v * (sv * hv), n);
    }
    const bool ccw = (u.cross(v)).dot(n) > 0;
    for (const auto i : (ccw ? std::array<std::uint32_t, 6>{0, 1, 2, 0, 2, 3} : std::array<std::uint32_t, 6>{0, 2, 1, 0, 3, 2}))
      part.indices.push_back(base + i);
  }
}

void add_quad(MeshPart& part, math::Vec3 p0, math::Vec3 p1, math::Vec3 p2, math::Vec3 p3) {
  math::Vec3 n = (p1 - p0).cross(p3 - p0);
  const double len = n.length();
  n = len > 1e-12 ? n / len : math::Vec3{0, 1, 0};
  if (n.y < 0) n = n * -1.0;
  add_quad(part, p0, p1, p2, p3, n);
}

void add_quad(MeshPart& part, math::Vec3 p0, math::Vec3 p1, math::Vec3 p2, math::Vec3 p3, math::Vec3 normal) {
  const auto base = static_cast<std::uint32_t>(part.vertices.size());
  for (const auto& p : {p0, p1, p2, p3}) push_vertex(part, p, normal);
  for (const auto i : {0u, 1u, 2u, 0u, 2u, 3u}) part.indices.push_back(base + i);
}

void add_triangle(MeshPart& part, math::Vec3 p0, math::Vec3 p1, math::Vec3 p2, math::Vec3 normal) {
  const auto base = static_cast<std::uint32_t>(part.vertices.size());
  for (const auto& p : {p0, p1, p2}) push_vertex(part, p, normal);
  for (const auto i : {0u, 1u, 2u}) part.indices.push_back(base + i);
}

MaterialHazard material_hazard(SurfaceMaterial material) {
  // flammable, ignition heat, ignition s, burn s, heat threshold, heat damage s, soak s, impact t, wind m/s, wind s
  switch (material) {
    case SurfaceMaterial::Plain:    return {true, 0.50, 2.0, 30.0, 1.0, 0.0, 600.0, 0.5, 0.0, 0.0};     // fabrics and the like: they burn and they rot
    case SurfaceMaterial::Asphalt:  return {false, 1.0, 0.0, 0.0, 0.60, 120.0, 0.0, 5.0, 0.0, 0.0};     // softens and burns through under a burning trunk
    case SurfaceMaterial::Concrete: return {false, 1.0, 0.0, 0.0, 0.90, 600.0, 0.0, 8.0, 0.0, 0.0};     // spalls only in extreme heat
    case SurfaceMaterial::Thatch:   return {true, 0.25, 1.0, 25.0, 1.0, 0.0, 1200.0, 0.3, 28.0, 60.0};  // catches from a spark, burns fast, blows off
    case SurfaceMaterial::Planks:   return {true, 0.45, 3.0, 60.0, 1.0, 0.0, 900.0, 1.0, 45.0, 120.0};
    case SurfaceMaterial::Stone:    return {false, 1.0, 0.0, 0.0, 0.90, 900.0, 0.0, 6.0, 0.0, 0.0};
    case SurfaceMaterial::Tiles:    return {false, 1.0, 0.0, 0.0, 0.95, 1200.0, 0.0, 0.6, 38.0, 90.0};  // smashed by a trunk, torn off by a gale
    case SurfaceMaterial::Glass:    return {false, 1.0, 0.0, 0.0, 0.50, 8.0, 0.0, 0.2, 40.0, 30.0};     // bursts in the heat, by a branch, by debris
    case SurfaceMaterial::Metal:    return {false, 1.0, 0.0, 0.0, 0.80, 120.0, 0.0, 2.0, 50.0, 120.0};  // buckles
    case SurfaceMaterial::Paint:    return {false, 1.0, 0.0, 0.0, 0.40, 20.0, 0.0, 0.0, 0.0, 0.0};      // blisters
    case SurfaceMaterial::Brick:    return {false, 1.0, 0.0, 0.0, 0.90, 900.0, 0.0, 3.0, 0.0, 0.0};
    case SurfaceMaterial::Timber:   return {true, 0.50, 3.0, 50.0, 1.0, 0.0, 900.0, 1.0, 0.0, 0.0};
    case SurfaceMaterial::Pavement: return {false, 1.0, 0.0, 0.0, 0.90, 900.0, 0.0, 6.0, 0.0, 0.0};
    case SurfaceMaterial::Window:   return {false, 1.0, 0.0, 0.0, 0.50, 8.0, 0.0, 0.2, 40.0, 30.0};
    case SurfaceMaterial::Count: break;
  }
  throw std::invalid_argument("unknown surface material");
}

const char* surface_material_name(SurfaceMaterial material) {
  switch (material) {
    case SurfaceMaterial::Plain: return "plain";
    case SurfaceMaterial::Asphalt: return "asphalt";
    case SurfaceMaterial::Concrete: return "concrete";
    case SurfaceMaterial::Thatch: return "thatch";
    case SurfaceMaterial::Planks: return "planks";
    case SurfaceMaterial::Stone: return "stone";
    case SurfaceMaterial::Tiles: return "tiles";
    case SurfaceMaterial::Glass: return "glass";
    case SurfaceMaterial::Metal: return "metal";
    case SurfaceMaterial::Paint: return "paint";
    case SurfaceMaterial::Brick: return "brick";
    case SurfaceMaterial::Timber: return "timber";
    case SurfaceMaterial::Pavement: return "pavement";
    case SurfaceMaterial::Window: return "window";
    case SurfaceMaterial::Count: break;
  }
  throw std::invalid_argument("unknown surface material");
}

std::size_t part_triangles(std::span<const MeshPart> parts) {
  std::size_t n = 0;
  for (const auto& p : parts) n += p.indices.size() / 3;
  return n;
}

MeshPartRenderer::MeshPartRenderer(RenderDevice& device) : device_(device), resources_(device) {
  pipeline_ = resources_.create_pipeline({ShaderProgramId::Structure, object_shader_layout(), true, false, true});
}

void MeshPartRenderer::set_parts(std::span<const MeshPart> parts) {
  auto fresh = std::make_unique<RenderResourceOwner>(device_);
  std::vector<Part> uploaded;
  std::size_t triangles = 0;
  for (const auto& p : parts) {
    if (p.vertices.empty() || p.indices.empty()) continue;
    if (p.indices.size() % 3 != 0) throw std::invalid_argument("mesh part has a partial triangle");
    for (const auto i : p.indices) if (i >= p.vertices.size()) throw std::invalid_argument("mesh part index out of range");
    Part part;
    part.vb = fresh->create_buffer({BufferUsage::Vertex, p.vertices.size() * sizeof(TerrainGpuVertex)}, std::as_bytes(std::span(p.vertices)));
    part.ib = fresh->create_buffer({BufferUsage::Index, p.indices.size() * sizeof(std::uint32_t)}, std::as_bytes(std::span(p.indices)));
    auto uniform = make_object_uniform(math::Mat4::identity(), {std::clamp(p.color.x, 0.0, 1.0), std::clamp(p.color.y, 0.0, 1.0), std::clamp(p.color.z, 0.0, 1.0)});
    (void)surface_material_name(p.material);
    uniform.tint[3] = static_cast<float>(p.material);  // the Structure program reads the material from the tint's alpha
    part.uniform = fresh->create_buffer({BufferUsage::Uniform, sizeof(uniform)}, std::as_bytes(std::span(&uniform, 1)));
    part.indices = static_cast<std::uint32_t>(p.indices.size());
    triangles += p.indices.size() / 3;
    uploaded.push_back(part);
  }
  parts_ = std::move(uploaded);
  triangles_ = triangles;
  part_resources_ = std::move(fresh);  // the previous parts are released here
}

std::size_t MeshPartRenderer::draw(BufferHandle camera) {
  if (!device_.active_render_pass_id()) throw std::logic_error("mesh parts require an active pass");
  for (const auto& p : parts_) device_.draw_indexed({pipeline_, p.vb, p.ib, 0, p.indices, {{0, camera}, {2, p.uniform}}, {}, {}});
  return parts_.size();
}

}  // namespace terraflux::graphics
