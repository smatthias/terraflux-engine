#include <terraflux/graphics/items.hpp>

#include <terraflux/graphics/mesh_upload.hpp>
#include <terraflux/graphics/terrain_upload.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

std::uint32_t hash_u32(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352DU; x ^= x >> 15; x *= 0x846CA68BU; x ^= x >> 16;
  return x;
}
double hash01(std::int64_t ix, std::int64_t iz, std::uint32_t salt, std::uint32_t seed) {
  const auto h = hash_u32(static_cast<std::uint32_t>(ix) * 0x9E3779B1U ^ hash_u32(static_cast<std::uint32_t>(iz) + salt * 0x85EBCA77U + seed * 0xC2B2AE3DU));
  return static_cast<double>(h & 0xFFFFFFu) / 16777216.0;
}

math::Mat4 frame_at(math::Vec3 centre, double yaw) {
  math::Mat4 m = math::Mat4::rotationY(yaw);
  m.at(0, 3) = centre.x; m.at(1, 3) = centre.y; m.at(2, 3) = centre.z;
  return m;
}

}  // namespace

void validate_stone_style(const StoneStyle& s) {
  if (!within(s.spacing_meters, 1, 100) || !within(s.min_radius, 0.02, 1) || !within(s.max_radius, s.min_radius, 1) ||
      !within(s.rock_chance, 0, 1) || !within(s.ridge_chance, 0, 1) || !within(s.bare_chance, 0, 1) || !within(s.soil_chance, 0, 1) ||
      s.max_stones == 0 || s.max_stones > 5000000)
    throw std::invalid_argument("stone style out of range");
}

std::vector<Stone> place_stones(const terrain::TerrainMesh& terrain, const TerrainStyle& terrain_style, const TerrainSampler& sampler,
                                const StoneStyle& style, std::optional<double> water_level) {
  validate_stone_style(style);
  (void)terrain;
  std::vector<Stone> out;
  const auto lo = sampler.lower_bound(), hi = sampler.upper_bound();
  const double spacing = style.spacing_meters;
  const auto ix0 = static_cast<std::int64_t>(std::floor(lo.x / spacing)), ix1 = static_cast<std::int64_t>(std::ceil(hi.x / spacing));
  const auto iz0 = static_cast<std::int64_t>(std::floor(lo.z / spacing)), iz1 = static_cast<std::int64_t>(std::ceil(hi.z / spacing));
  for (auto iz = iz0; iz <= iz1 && out.size() < style.max_stones; ++iz)
    for (auto ix = ix0; ix <= ix1 && out.size() < style.max_stones; ++ix) {
      const double x = (ix + hash01(ix, iz, 1, style.seed)) * spacing, z = (iz + hash01(ix, iz, 2, style.seed)) * spacing;
      const auto g = sampler.sample(x, z);
      if (!g) continue;
      if (water_level && g->position.y <= *water_level) continue;
      TerrainSurfacePoint point;
      point.elevation_meters = g->position.y; point.normal = g->normal;
      point.fuel_load = g->fuel_load; point.fuel_moisture = g->fuel_moisture; point.curvature = g->curvature;
      const auto m = terrain_material(point, terrain_style);
      if (m.snow >= 0.5 || m.ice >= 0.5 || m.submerged > 0.5) continue;
      double chance = style.soil_chance;
      if (m.ridge >= 0.4) chance = style.ridge_chance;
      else if (m.rock >= 0.4) chance = style.rock_chance;
      else if (m.vegetation < 0.15 && m.trees < 0.15) chance = style.bare_chance;
      if (hash01(ix, iz, 3, style.seed) >= chance) continue;
      Stone s;
      const double u = hash01(ix, iz, 4, style.seed);
      s.radius = style.min_radius + (style.max_radius - style.min_radius) * u * u;  // many small, few big
      s.size = {s.radius * (1.6 + 0.8 * hash01(ix, iz, 5, style.seed)), s.radius * (1.0 + 0.5 * hash01(ix, iz, 6, style.seed)),
                s.radius * (1.4 + 0.8 * hash01(ix, iz, 7, style.seed))};
      s.position = {x, g->position.y + s.size.y * 0.4, z};
      out.push_back(s);
    }
  return out;
}

std::vector<Obstacle> stone_obstacles(std::span<const Stone> stones) {
  std::vector<Obstacle> out;
  for (const auto& s : stones) {
    if (!s.present) continue;
    Obstacle o;
    o.x = s.position.x; o.z = s.position.z;
    o.radius = std::max(s.size.x, s.size.z) * 0.5;
    o.bottom = s.position.y - s.size.y * 0.5; o.top = s.position.y + s.size.y * 0.5;
    out.push_back(o);
  }
  return out;
}

math::Vec3 throw_velocity(math::Vec3 forward, double speed_mps, double lift_degrees) {
  if (!finite(forward) || !within(speed_mps, 0, 100) || !within(lift_degrees, -89, 89)) throw std::invalid_argument("throw needs a finite direction, speed 0..100 and lift -89..89");
  math::Vec3 f = forward;
  const double l = f.length();
  if (l < 1e-9) f = {0, 0, -1}; else f = f / l;
  // Lift: rotate the direction up by the angle within its vertical plane.
  const double horizontal = std::hypot(f.x, f.z);
  const double pitch = std::atan2(f.y, std::max(horizontal, 1e-9)) + lift_degrees * kPi / 180.0;
  const math::Vec3 h = horizontal > 1e-9 ? math::Vec3{f.x / horizontal, 0, f.z / horizontal} : math::Vec3{0, 0, -1};
  return (h * std::cos(pitch) + math::Vec3{0, 1, 0} * std::sin(pitch)) * speed_mps;
}

void advance_projectile(Projectile& p, double dt, double gravity_mps2, const StandingHeightFn& ground, math::Vec3 lower, math::Vec3 upper) {
  if (!std::isfinite(dt) || dt < 0 || !within(gravity_mps2, 0, 100)) throw std::invalid_argument("projectile step needs finite time and gravity 0..100");
  if (!finite(p.position) || !finite(p.velocity)) throw std::invalid_argument("projectile must be finite");
  if (p.landed) return;
  double remaining = dt;
  while (remaining > 1e-9 && !p.landed) {
    const double step = std::min(remaining, 1.0 / 120.0);
    remaining -= step;
    p.velocity.y -= gravity_mps2 * step;
    p.position = p.position + p.velocity * step;
    p.seconds += step;
    p.position.x = std::clamp(p.position.x, lower.x, upper.x);
    p.position.z = std::clamp(p.position.z, lower.z, upper.z);
    const auto h = ground ? ground(p.position.x, p.position.z) : std::nullopt;
    if (h && p.position.y <= *h) { p.position.y = *h; p.velocity = {}; p.landed = true; }
    else if (!h && p.position.y < lower.y - 1000.0) { p.landed = true; p.velocity = {}; }  // lost down a hole
  }
}

namespace {
template <class T, class Pos>
std::optional<std::size_t> in_reach(std::span<const T> items, math::Vec3 eye, math::Vec3 forward, double reach, double cone_degrees, Pos pos) {
  if (!finite(eye) || !finite(forward) || !within(reach, 0, 100) || !within(cone_degrees, 0, 180)) throw std::invalid_argument("reach query out of range");
  const double fl = forward.length();
  const math::Vec3 f = fl > 1e-9 ? forward / fl : math::Vec3{0, 0, -1};
  const double cos_cone = std::cos(cone_degrees * kPi / 180.0);
  std::optional<std::size_t> best;
  double best_d = reach;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (!items[i].present) continue;
    const math::Vec3 to = pos(items[i]) - eye;
    const double d = to.length();
    if (d > best_d || d < 1e-9) continue;
    if (to.dot(f) / d < cos_cone) continue;
    best = i; best_d = d;
  }
  return best;
}
}  // namespace

std::optional<std::size_t> stone_in_reach(std::span<const Stone> stones, math::Vec3 eye, math::Vec3 forward, double reach, double cone_degrees) {
  return in_reach(stones, eye, forward, reach, cone_degrees, [](const Stone& s) { return s.position; });
}
std::optional<std::size_t> branch_in_reach(std::span<const DroppedBranch> branches, math::Vec3 eye, math::Vec3 forward, double reach, double cone_degrees) {
  return in_reach(branches, eye, forward, reach, cone_degrees, [](const DroppedBranch& b) { return b.position; });
}
std::optional<std::size_t> trunk_in_reach(const ObstacleField& trunks, math::Vec3 feet, double reach) {
  if (!finite(feet) || !within(reach, 0, 100)) throw std::invalid_argument("trunk reach out of range");
  std::optional<std::size_t> best;
  double best_d = reach;
  const auto* base = trunks.obstacles().data();
  trunks.for_each_near(feet.x, feet.z, reach + trunks.max_radius(), [&](const Obstacle& o) {
    if (o.top - o.bottom < 2.0) return;  // a log or a stone, not a standing trunk
    const double d = std::max(0.0, std::hypot(o.x - feet.x, o.z - feet.z) - o.radius);
    if (d < best_d) { best_d = d; best = static_cast<std::size_t>(&o - base); }
  });
  return best;
}

PropRenderer::PropRenderer(RenderDevice& device, std::size_t max_props) : device_(device), resources_(device), max_props_(max_props) {
  if (max_props == 0 || max_props > 4096) throw std::invalid_argument("prop renderer needs 1..4096 props");
  const auto box = make_unit_box();
  const auto vertices = std::as_bytes(std::span(box.vertices));
  const auto indices = std::as_bytes(std::span(box.indices));
  vb_ = resources_.create_buffer({BufferUsage::Vertex, vertices.size()}, vertices);
  ib_ = resources_.create_buffer({BufferUsage::Index, indices.size()}, indices);
  indices_ = static_cast<std::uint32_t>(box.indices.size());
  const auto zero = make_object_uniform(math::Mat4::identity(), {0.5, 0.5, 0.5});
  uniforms_.reserve(max_props);
  for (std::size_t i = 0; i < max_props; ++i) uniforms_.push_back(resources_.create_buffer({BufferUsage::Uniform, sizeof(zero)}, std::as_bytes(std::span(&zero, 1))));
  pipeline_ = resources_.create_pipeline({ShaderProgramId::Object, object_shader_layout(), true, false, true});
}

std::size_t PropRenderer::draw(BufferHandle camera, std::span<const Prop> props) {
  if (!device_.active_render_pass_id()) throw std::logic_error("props require an active pass");
  const std::size_t count = std::min(props.size(), max_props_);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& p = props[i];
    const auto model = p.transform * math::Mat4::scaling(p.size);
    const auto uniform = make_object_uniform(model, {std::clamp(p.color.x, 0.0, 1.0), std::clamp(p.color.y, 0.0, 1.0), std::clamp(p.color.z, 0.0, 1.0)});
    device_.update_buffer(uniforms_[i], 0, std::as_bytes(std::span(&uniform, 1)));
    device_.draw_indexed({pipeline_, vb_, ib_, 0, indices_, {{0, camera}, {2, uniforms_[i]}}, {}, {}});
  }
  return count;
}

std::vector<Prop> props_near(std::span<const Stone> stones, std::span<const DroppedBranch> branches, math::Vec3 eye, double radius, std::size_t max_props) {
  if (!finite(eye) || !within(radius, 0, 100000)) throw std::invalid_argument("prop query out of range");
  struct Candidate { double d; Prop prop; };
  std::vector<Candidate> candidates;
  for (std::size_t i = 0; i < stones.size(); ++i) {
    const auto& s = stones[i];
    if (!s.present) continue;
    const double d = (s.position - eye).length();
    if (d > radius) continue;
    const double yaw = hash01(static_cast<std::int64_t>(i), 0, 9, 0) * kPi;
    const double shade = 0.34 + 0.16 * hash01(static_cast<std::int64_t>(i), 0, 10, 0);
    candidates.push_back({d, {frame_at(s.position, yaw), s.size, {shade, shade * 0.97, shade * 0.9}}});
  }
  for (const auto& b : branches) {
    if (!b.present) continue;
    const double d = (b.position - eye).length();
    if (d > radius) continue;
    candidates.push_back({d, {frame_at(b.position, b.yaw), {0.04, 0.04, b.length}, {0.36, 0.27, 0.16}}});
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.d < b.d; });
  std::vector<Prop> out;
  for (std::size_t i = 0; i < candidates.size() && i < max_props; ++i) out.push_back(candidates[i].prop);
  return out;
}

}  // namespace terraflux::graphics
