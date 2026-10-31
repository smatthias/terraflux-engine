#include <terraflux/graphics/fire_effects.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float f(double v) {
  const auto value = static_cast<float>(v);
  if (!std::isfinite(value)) throw std::invalid_argument("fire effect value is not representable in float");
  return value;
}
void validate(const FireStyle& s) {
  if (!within(s.flame_scale, 0, 3)) throw std::invalid_argument("flame scale must be 0..3");
  if (!within(s.smoke_scale, 0, 3)) throw std::invalid_argument("smoke scale must be 0..3");
  if (!within(s.flames_per_cell, 1, 12)) throw std::invalid_argument("flames per cell must be 1..12");
  if (!within(s.flame_height_meters, 1, 30)) throw std::invalid_argument("flame height must be 1..30 meters");
  if (!within(s.smoke_height_meters, 5, 300)) throw std::invalid_argument("smoke height must be 5..300 meters");
  if (!within(s.fuel_threshold, 0, 0.5)) throw std::invalid_argument("fuel threshold must be 0..0.5");
  if (!within(s.draw_distance_meters, 100, 50000)) throw std::invalid_argument("fire draw distance must be 100..50000 meters");
  if (!within(s.wind_direction_degrees, 0, 360)) throw std::invalid_argument("wind direction must be 0..360 degrees");
  if (!within(s.wind_speed_mps, 0, 30)) throw std::invalid_argument("wind speed must be 0..30 m/s");
  if (!finite(s.light_direction) || !(s.light_direction.length() > 0)) throw std::invalid_argument("fire light must be nonzero");
}
std::uint32_t hash_u32(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
double hash01(std::int64_t a, std::int64_t b, std::uint32_t salt, std::uint32_t seed) {
  const auto h = hash_u32(static_cast<std::uint32_t>(a) * 0x9E3779B1U ^
                          hash_u32(static_cast<std::uint32_t>(b) + salt * 0x85EBCA77U + seed * 0xC2B2AE3DU));
  return static_cast<double>(h & 0xFFFFFFu) / 16777216.0;
}
double smoothstep(double e0, double e1, double x) {
  const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

}  // namespace

FireStyleUniform pack_fire_style(const FireStyle& style, math::Vec3 eye, double time_seconds) {
  validate(style);
  if (!finite(eye) || !std::isfinite(time_seconds) || time_seconds < 0)
    throw std::invalid_argument("fire eye must be finite and time non-negative");
  const double radians = style.wind_direction_degrees * kPi / 180.0;
  const auto light = style.light_direction.normalized();
  FireStyleUniform u{};
  u[0] = f(std::sin(radians)); u[1] = f(-std::cos(radians)); u[2] = f(style.wind_speed_mps); u[3] = f(time_seconds);
  u[4] = f(eye.x); u[5] = f(eye.y); u[6] = f(eye.z); u[7] = f(style.flame_scale);
  u[8] = f(style.smoke_scale); u[9] = f(style.smoke_height_meters); u[10] = f(style.draw_distance_meters); u[11] = 0.0f;
  u[12] = f(light.x); u[13] = f(light.y); u[14] = f(light.z); u[15] = 0.0f;
  return u;
}

FireMesh make_fire_quad() {
  FireMesh mesh;
  mesh.vertices = {{{-0.5f, 0.0f}, {0.0f, 0.0f}}, {{0.5f, 0.0f}, {1.0f, 0.0f}},
                   {{0.5f, 1.0f}, {1.0f, 1.0f}}, {{-0.5f, 1.0f}, {0.0f, 1.0f}}};
  mesh.indices = {0, 1, 2, 0, 2, 3};
  return mesh;
}

ShaderLayout fire_shader_layout() {
  return {static_cast<std::uint32_t>(sizeof(FireVertex)),
          {{"POSITION", 0, VertexFormat::Float2, static_cast<std::uint32_t>(offsetof(FireVertex, position))},
           {"TEXCOORD", 1, VertexFormat::Float2, static_cast<std::uint32_t>(offsetof(FireVertex, uv))}},
          {{"Camera", ShaderResourceKind::UniformBuffer, 0, kVertexStage},
           {"FireStyle", ShaderResourceKind::UniformBuffer, kFireStyleSlot, kVertexStage | kFragmentStage},
           {"InstanceData", ShaderResourceKind::StorageBuffer, 4, kVertexStage}}};
}

double fire_fuel_factor(double fuel_load, const TerrainMaterial& m, double threshold) {
  if (!std::isfinite(fuel_load) || !within(threshold, 0, 0.5)) throw std::invalid_argument("fire fuel factor needs finite inputs");
  // Mirrors the ground shader: fuel must exceed the threshold and the surface
  // must be soil or vegetation, not rock, water, snow or ice.
  return smoothstep(threshold, threshold + 0.10, fuel_load) * (1.0 - m.rock) * (1.0 - m.submerged) *
         (1.0 - std::max(m.snow, m.ice));
}

namespace {
// Float ports of the GLSL terrainHash/terrainNoise so CPU placement and the
// shaders agree on the ragged outline (bit-exactness is not required).
float hash_f(float x, float y) {
  const auto fract = [](float v) { return v - std::floor(v); };
  float p0 = fract(x * 0.1031f), p1 = fract(y * 0.1031f), p2 = fract(x * 0.1031f);
  const float dot = p0 * (p1 + 33.33f) + p1 * (p2 + 33.33f) + p2 * (p0 + 33.33f);
  p0 += dot; p1 += dot; p2 += dot;
  return fract((p0 + p1) * p2);
}
float noise_f(float x, float y) {
  const float ix = std::floor(x), iy = std::floor(y), fx = x - ix, fy = y - iy;
  const float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
  const float a = hash_f(ix, iy), b = hash_f(ix + 1, iy), c = hash_f(ix, iy + 1), d = hash_f(ix + 1, iy + 1);
  return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}
double front_kernel(double distance_cells) { return 1.0 - smoothstep(0.45, 1.55, distance_cells); }
}  // namespace

FireFront fire_front_at(const replay::FireField& field, std::int32_t origin_grid_x, std::int32_t origin_grid_y,
                        double cell_size_meters, double x, double z) {
  if (!within(cell_size_meters, 1e-3, 1e6) || !std::isfinite(x) || !std::isfinite(z))
    throw std::invalid_argument("fire front needs a valid cell size and finite position");
  if (field.states.size() != static_cast<std::size_t>(field.width) * field.height)
    throw std::invalid_argument("fire field size mismatch");
  const double gx = x / cell_size_meters, gy = -z / cell_size_meters;  // grid units from the origin
  const auto nearest_x = static_cast<std::int64_t>(std::llround(gx)), nearest_y = static_cast<std::int64_t>(std::llround(gy));
  FireFront front;
  for (std::int64_t dy = -2; dy <= 2; ++dy)
    for (std::int64_t dx = -2; dx <= 2; ++dx) {
      const std::int64_t cx = nearest_x + dx, cy = nearest_y + dy;
      const std::int64_t fx = cx + origin_grid_x - field.min_x, fy = cy + origin_grid_y - field.min_y;
      if (fx < 0 || fy < 0 || fx >= field.width || fy >= field.height) continue;
      const auto state = field.states[static_cast<std::size_t>(fy) * field.width + static_cast<std::size_t>(fx)];
      if (state != 1 && state != 2) continue;
      const double distance = std::hypot(gx - static_cast<double>(cx), gy - static_cast<double>(cy));
      const double weight = front_kernel(distance);
      if (state == 2) front.active = std::min(1.0, front.active + weight);
      else front.burned = std::min(1.0, front.burned + weight);
    }
  return front;
}

double fire_edge_noise(double x, double z) {
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("fire edge noise needs a finite position");
  // Local grid metres (x east, north positive) exactly as the ground shader samples them.
  const auto fx = static_cast<float>(x), fz = static_cast<float>(-z);
  // Three scales: broad bulges and dents (~50 m), ragged lobes (~14 m), fine fringe (~4 m).
  return (noise_f(fx * 0.012f + 3.1f, fz * 0.012f + 9.7f) - 0.5) * 0.8 + (noise_f(fx * 0.05f, fz * 0.05f) - 0.5) * 0.5 +
         (noise_f(fx * 0.25f + 7.3f, fz * 0.25f + 1.9f) - 0.5) * 0.16;
}

// Noise only reshapes an outline that exists: without any nearby burning or
// burned cell (field below 0.02) nothing can burn, whatever the noise says.
bool fire_front_burning(const FireFront& front, double edge_noise) {
  return front.active > 0.02 && front.active + edge_noise > kFireFrontThreshold;
}
bool fire_front_burned(const FireFront& front, double edge_noise) {
  return !fire_front_burning(front, edge_noise) && front.burned > 0.02 && front.burned + edge_noise > kFireFrontThreshold;
}

FireEmitters build_fire_emitters(const replay::FireField& field, std::int32_t origin_grid_x, std::int32_t origin_grid_y,
                                 double cell_size_meters, const TerrainSampler& sampler, const TerrainStyle& terrain,
                                 const VegetationPlacement* vegetation, const FireStyle& style, std::size_t max_instances) {
  validate(style);
  (void)pack_terrain_style(terrain);
  if (!within(cell_size_meters, 1e-3, 1e6)) throw std::invalid_argument("fire emitters need a valid cell size");
  if (field.states.size() != static_cast<std::size_t>(field.width) * field.height)
    throw std::invalid_argument("fire field size mismatch");
  FireEmitters out;
  // Trees per burning cell, from the placement (crown flames).
  std::unordered_map<std::uint64_t, std::vector<const VegetationInstance*>> trees_by_cell;
  const auto cell_key = [](std::int64_t x, std::int64_t y) { return (static_cast<std::uint64_t>(x) << 32) ^ static_cast<std::uint32_t>(y); };
  if (vegetation) {
    for (const auto& t : vegetation->trees) {
      const auto cx = origin_grid_x + static_cast<std::int64_t>(std::llround(t.position[0] / cell_size_meters));
      const auto cy = origin_grid_y + static_cast<std::int64_t>(std::llround(-t.position[2] / cell_size_meters));
      trees_by_cell[cell_key(cx, cy)].push_back(&t);
    }
  }
  std::vector<FireInstance> ground, crowns, plumes, smoulder;
  const auto state_of = [&](std::int64_t fx, std::int64_t fy) -> std::uint32_t {
    if (fx < 0 || fy < 0 || fx >= field.width || fy >= field.height) return 0;
    return field.states[static_cast<std::size_t>(fy) * field.width + static_cast<std::size_t>(fx)];
  };
  const auto sample_flame = [&](std::int64_t cx, std::int64_t cy, double x, double z, std::uint32_t salt) -> bool {
    const auto s = sampler.sample(x, z);
    if (!s) return false;
    // The ragged front decides whether this spot burns; the fuel gate whether it can.
    const auto front = fire_front_at(field, origin_grid_x, origin_grid_y, cell_size_meters, x, z);
    if (!fire_front_burning(front, fire_edge_noise(x, z))) return false;
    const auto m = terrain_material({s->position.y, s->normal, s->fuel_load, s->fuel_moisture, s->curvature}, terrain);
    const double factor = fire_fuel_factor(s->fuel_load, m, style.fuel_threshold);
    if (factor <= 0.0) { ++out.fuelless_samples; return false; }
    // Fewer flames where there is less fuel: keep this sample with probability ~ fuel.
    if (hash01(cx * 31 + salt, cy * 17 - salt, 3, style.seed) > std::clamp(s->fuel_load * 1.2, 0.15, 1.0)) return false;
    FireInstance fl{};
    fl.position[0] = f(s->position.x); fl.position[1] = f(s->position.y); fl.position[2] = f(s->position.z);
    fl.size = f(style.flame_height_meters * style.flame_scale * (0.35 + 0.65 * s->fuel_load) *
                (0.7 + 0.6 * hash01(cx, cy, 4 + salt, style.seed)) * factor * (0.6 + 0.4 * front.active));
    fl.phase = f(2 * kPi * hash01(cx, cy, 20 + salt, style.seed));
    fl.intensity = f(std::clamp(s->fuel_load, 0.0, 1.0) * factor);
    fl.kind = 0.0f; fl.seed = f(hash01(cx, cy, 30 + salt, style.seed) * 100.0);
    ground.push_back(fl);
    return true;
  };
  // Flames and smoke: every burning cell, plus its neighbours, sampled on a
  // jittered sub-grid so the ragged front can spill past the cell squares.
  const int side = std::max(2, static_cast<int>(std::ceil(std::sqrt(style.flames_per_cell))));
  for (std::uint32_t iy = 0; iy < field.height; ++iy)
    for (std::uint32_t ix = 0; ix < field.width; ++ix) {
      const auto state = field.states[static_cast<std::size_t>(iy) * field.width + ix];
      const std::int64_t cx = static_cast<std::int64_t>(field.min_x) + ix, cy = static_cast<std::int64_t>(field.min_y) + iy;
      const double center_x = static_cast<double>(cx - origin_grid_x) * cell_size_meters;
      const double center_z = -static_cast<double>(cy - origin_grid_y) * cell_size_meters;
      if (state == 1) {
        ++out.burned_cells;
        // Sparse smouldering: about one cell in three keeps a thin plume.
        if (hash01(cx, cy, 41, style.seed) > 0.33) continue;
        const auto s = sampler.sample(center_x + (hash01(cx, cy, 42, style.seed) - 0.5) * cell_size_meters,
                                      center_z + (hash01(cx, cy, 43, style.seed) - 0.5) * cell_size_meters);
        if (!s) continue;
        const auto m = terrain_material({s->position.y, s->normal, s->fuel_load, s->fuel_moisture, s->curvature}, terrain);
        const double factor = fire_fuel_factor(s->fuel_load, m, style.fuel_threshold);
        if (factor <= 0.0) { ++out.fuelless_samples; continue; }
        FireInstance p{};
        p.position[0] = f(s->position.x); p.position[1] = f(s->position.y); p.position[2] = f(s->position.z);
        p.size = f(style.smoke_height_meters * 0.5); p.phase = f(2 * kPi * hash01(cx, cy, 44, style.seed));
        p.intensity = f(0.35 * factor); p.kind = 2.0f; p.seed = f(hash01(cx, cy, 45, style.seed) * 100.0);
        smoulder.push_back(p);
        continue;
      }
      bool near_active = state == 2;
      for (int dy = -1; dy <= 1 && !near_active; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if (state_of(static_cast<std::int64_t>(ix) + dx, static_cast<std::int64_t>(iy) + dy) == 2) { near_active = true; break; }
      if (!near_active) continue;
      if (state == 2) ++out.active_cells;
      int accepted = 0;
      double cell_fuel = 0.0;
      for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i) {
          const std::uint32_t salt = static_cast<std::uint32_t>(j * side + i);
          const double x = center_x + ((i + hash01(cx * 13 + salt, cy, 1, style.seed)) / side - 0.5) * cell_size_meters;
          const double z = center_z + ((j + hash01(cx, cy * 7 + salt, 2, style.seed)) / side - 0.5) * cell_size_meters;
          if (sample_flame(cx, cy, x, z, salt)) {
            ++accepted;
            cell_fuel = std::max(cell_fuel, static_cast<double>(ground.back().intensity));
          }
        }
      // Crown fires: trees whose own spot lies on the burning side of the front.
      if (const auto found = trees_by_cell.find(cell_key(cx, cy)); found != trees_by_cell.end()) {
        int crowned = 0;
        for (const auto* t : found->second) {
          if (crowned >= 8) break;
          const auto front = fire_front_at(field, origin_grid_x, origin_grid_y, cell_size_meters, t->position[0], t->position[2]);
          if (!fire_front_burning(front, fire_edge_noise(t->position[0], t->position[2]))) continue;
          FireInstance fl{};
          fl.position[0] = t->position[0]; fl.position[1] = f(t->position[1] + t->scale * 0.55); fl.position[2] = t->position[2];
          fl.size = f(t->scale * 1.1 * style.flame_scale);
          fl.phase = t->phase; fl.intensity = 1.0f; fl.kind = 0.0f; fl.seed = f(t->rotation * 10.0);
          crowns.push_back(fl);
          cell_fuel = std::max(cell_fuel, 0.9);
          ++crowned;
        }
        out.crown_flames += static_cast<std::size_t>(crowned);
      }
      if (state != 2 || (accepted == 0 && cell_fuel <= 0.0)) continue;
      const int plume_count = 1 + static_cast<int>(std::floor(cell_fuel * 2.0 + 0.001));
      for (int k = 0; k < plume_count; ++k) {
        const auto s = sampler.sample(center_x + (hash01(cx, cy, 50 + k, style.seed) - 0.5) * cell_size_meters * 0.6,
                                      center_z + (hash01(cx, cy, 60 + k, style.seed) - 0.5) * cell_size_meters * 0.6);
        if (!s) continue;
        const auto m = terrain_material({s->position.y, s->normal, s->fuel_load, s->fuel_moisture, s->curvature}, terrain);
        if (fire_fuel_factor(s->fuel_load, m, style.fuel_threshold) <= 0.0) continue;
        FireInstance p{};
        p.position[0] = f(s->position.x); p.position[1] = f(s->position.y); p.position[2] = f(s->position.z);
        p.size = f(style.smoke_height_meters * (0.6 + 0.4 * cell_fuel)); p.phase = f(2 * kPi * hash01(cx, cy, 70 + k, style.seed));
        p.intensity = f(0.5 + 0.5 * cell_fuel); p.kind = 1.0f; p.seed = f(hash01(cx, cy, 80 + k, style.seed) * 100.0);
        plumes.push_back(p);
      }
    }
  // Budget: crown flames and plumes are kept first, ground flames fill the rest.
  out.flames = std::move(crowns);
  out.smoke = std::move(plumes);
  for (auto& p : smoulder) out.smoke.push_back(p);
  if (out.smoke.size() > max_instances / 2) out.smoke.resize(max_instances / 2);
  const std::size_t flame_budget = max_instances - out.smoke.size();
  if (out.flames.size() > flame_budget) out.flames.resize(flame_budget);
  const std::size_t room = flame_budget - out.flames.size();
  if (ground.size() > room) ground.resize(room);
  out.flames.insert(out.flames.end(), ground.begin(), ground.end());
  return out;
}

FireEffectsRenderer::FireEffectsRenderer(RenderDevice& device, std::size_t capacity)
    : device_(device), resources_(device), capacity_(capacity) {
  if (capacity == 0 || capacity > 2'000'000) throw std::invalid_argument("fire effect capacity must be 1..2000000");
  const auto quad = make_fire_quad();
  const auto vertices = std::as_bytes(std::span(quad.vertices));
  const auto indices = std::as_bytes(std::span(quad.indices));
  vb_ = resources_.create_buffer({BufferUsage::Vertex, vertices.size()}, vertices);
  ib_ = resources_.create_buffer({BufferUsage::Index, indices.size()}, indices);
  std::vector<FireInstance> zero(capacity);
  instances_ = resources_.create_buffer({BufferUsage::Storage, capacity * sizeof(FireInstance)}, std::as_bytes(std::span(zero)));
}

void FireEffectsRenderer::set_emitters(FireEmitters emitters) {
  if (emitters.flames.size() > capacity_) emitters.flames.resize(capacity_);
  if (emitters.smoke.size() > capacity_) emitters.smoke.resize(capacity_);
  emitters_ = std::move(emitters);
}

FireDrawStats FireEffectsRenderer::draw(PipelineHandle pipeline, BufferHandle camera, BufferHandle style,
                                        const Frustum& frustum, math::Vec3 eye, double draw_distance_meters, bool cull) {
  if (!device_.active_render_pass_id()) throw std::logic_error("fire effects require an active pass");
  FireDrawStats stats;
  const auto submit = [&](const std::vector<FireInstance>& list, std::size_t& counted) {
    scratch_.clear();
    for (const auto& i : list) {
      const math::Vec3 p{i.position[0], i.position[1], i.position[2]};
      if ((p - eye).length() > draw_distance_meters) continue;
      const double r = std::max<double>(i.size, 1.0);
      if (cull && !frustum.intersects_aabb({p.x - r, p.y, p.z - r}, {p.x + r, p.y + 2.0 * r, p.z + r})) continue;
      scratch_.push_back(i);
      if (scratch_.size() == capacity_) break;
    }
    counted = scratch_.size();
    std::size_t offset = 0;
    while (offset < scratch_.size()) {
      const auto count = std::min<std::size_t>(scratch_.size() - offset, 65535);
      device_.update_buffer(instances_, 0, std::as_bytes(std::span(scratch_).subspan(offset, count)));
      DrawIndexed command{pipeline, vb_, ib_, 0, 6, {{0, camera}, {kFireStyleSlot, style}}, {{4, instances_}}, {},
                          static_cast<std::uint32_t>(count)};
      device_.draw_indexed(command);
      ++stats.draws;
      offset += count;
    }
  };
  submit(emitters_.smoke, stats.smoke);   // plumes behind the flames
  submit(emitters_.flames, stats.flames);
  return stats;
}

}  // namespace terraflux::graphics
