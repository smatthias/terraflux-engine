#include <terraflux/graphics/flood.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxGradient = 19.08;  // tan(87 degrees), as the lake mesh

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

}  // namespace

void validate_flood_style(const FloodStyle& s) {
  if (!within(s.storm_rain_mm_h, 1, 500)) throw std::invalid_argument("flood storm rain must be 1..500 mm/h");
  if (!within(s.rise_seconds, 1, 86400) || !within(s.fall_seconds, 1, 86400)) throw std::invalid_argument("flood rise and fall must be 1..86400 seconds");
  if (!within(s.channel_depth_meters, 0, 20)) throw std::invalid_argument("channel depth must be 0..20 meters");
  if (!within(s.flow_exponent, 0.5, 6)) throw std::invalid_argument("flow exponent must be 0.5..6");
  if (!within(s.flow_threshold, 0, 1)) throw std::invalid_argument("flow threshold must be 0..1");
  if (!within(s.manning_n, 0.01, 0.2)) throw std::invalid_argument("Manning's n must be 0.01..0.2");
  if (!within(s.max_current_mps, 0, 30)) throw std::invalid_argument("maximum current must be 0..30 m/s");
  if (!within(s.min_depth_meters, 0, 1)) throw std::invalid_argument("minimum depth must be 0..1 meters");
}

FloodModel::FloodModel(const RunoffField& runoff, const FloodStyle& style) : field_(runoff), style_(style) {
  validate_flood_style(style);
  if (field_.width == 0 || field_.height == 0 || field_.texels.size() != static_cast<std::size_t>(field_.width) * field_.height ||
      !(field_.spacing > 0) || !std::isfinite(field_.spacing))
    throw std::invalid_argument("flood model needs a runoff field with texels and a positive spacing");
}

void FloodModel::set_flash(double flash) {
  if (!within(flash, 0, 1)) throw std::invalid_argument("flash flood level must be 0..1");
  flash_ = flash;
}

void FloodModel::advance(double accelerated_seconds, double rain_mm_h) {
  if (!std::isfinite(accelerated_seconds) || accelerated_seconds < 0) throw std::invalid_argument("flood step must be finite and non-negative");
  if (!within(rain_mm_h, 0, 1e4)) throw std::invalid_argument("rain rate must be 0..10000 mm/h");
  const double target = clamp01(rain_mm_h / style_.storm_rain_mm_h);
  const double tau = target > flash_ ? style_.rise_seconds : style_.fall_seconds;
  flash_ = target + (flash_ - target) * std::exp(-accelerated_seconds / tau);
  flash_ = clamp01(flash_);
}

RunoffTexel FloodModel::interpolate(double x, double z) const {
  const double fx = (x - field_.x0) / field_.spacing, fz = (z - field_.z0) / field_.spacing;
  if (!std::isfinite(fx) || !std::isfinite(fz)) return {};
  const double cx = std::clamp(fx, 0.0, static_cast<double>(field_.width - 1));
  const double cz = std::clamp(fz, 0.0, static_cast<double>(field_.height - 1));
  const auto x0 = static_cast<std::size_t>(std::floor(cx)), z0 = static_cast<std::size_t>(std::floor(cz));
  const auto x1 = std::min<std::size_t>(x0 + 1, field_.width - 1), z1 = std::min<std::size_t>(z0 + 1, field_.height - 1);
  const double tx = cx - static_cast<double>(x0), tz = cz - static_cast<double>(z0);
  const auto at = [&](std::size_t ix, std::size_t iz) -> const RunoffTexel& { return field_.texels[iz * field_.width + ix]; };
  const auto& a = at(x0, z0); const auto& b = at(x1, z0); const auto& c = at(x0, z1); const auto& d = at(x1, z1);
  const auto lerp = [&](float va, float vb, float vc, float vd) {
    return static_cast<float>((va * (1 - tx) + vb * tx) * (1 - tz) + (vc * (1 - tx) + vd * tx) * tz);
  };
  RunoffTexel t;
  t.flow = lerp(a.flow, b.flow, c.flow, d.flow);
  t.slope_degrees = lerp(a.slope_degrees, b.slope_degrees, c.slope_degrees, d.slope_degrees);
  t.waterfall = lerp(a.waterfall, b.waterfall, c.waterfall, d.waterfall);
  t.direction_x = lerp(a.direction_x, b.direction_x, c.direction_x, d.direction_x);
  t.direction_z = lerp(a.direction_z, b.direction_z, c.direction_z, d.direction_z);
  t.steepness = lerp(a.steepness, b.steepness, c.steepness, d.steepness);
  return t;
}

double FloodModel::channel_depth(double x, double z) const {
  if (flash_ <= 0) return 0.0;
  const auto t = interpolate(x, z);
  const double over = (t.flow - style_.flow_threshold) / std::max(1.0 - style_.flow_threshold, 1e-6);
  if (!(over > 0)) return 0.0;
  const double depth = style_.channel_depth_meters * std::pow(over, style_.flow_exponent) * flash_;
  return depth >= style_.min_depth_meters ? depth : 0.0;
}

double FloodModel::waterfall_at(double x, double z) const {
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("waterfall lookup needs a finite position");
  return std::clamp(static_cast<double>(interpolate(x, z).waterfall), 0.0, 1.0);
}

FloodSample FloodModel::sample(double x, double z, double ground_y, std::optional<double> lake_level) const {
  if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(ground_y)) throw std::invalid_argument("water sample needs a finite position");
  FloodSample w;
  w.surface_meters = ground_y;
  const double channel = channel_depth(x, z);
  const double lake = lake_level && std::isfinite(*lake_level) ? std::max(0.0, *lake_level - ground_y) : 0.0;
  if (channel >= lake && channel > 0) {
    const auto t = interpolate(x, z);
    w.depth_meters = channel;
    w.surface_meters = ground_y + channel;
    w.channel = true;
    w.waterfall = t.waterfall > 0.3f;
    const double slope = std::tan(std::clamp(static_cast<double>(t.slope_degrees), 0.0, 80.0) * kPi / 180.0);
    const double speed = std::pow(channel, 2.0 / 3.0) * std::sqrt(std::max(slope, 0.002)) / style_.manning_n;
    w.current_mps = std::min(style_.max_current_mps, speed);
    const double len = std::hypot(t.direction_x, t.direction_z);
    w.current = len > 1e-6 ? math::Vec2{t.direction_x / len, t.direction_z / len} : math::Vec2{};
    if (w.current.length() == 0) w.current_mps = 0.0;
  } else if (lake > 0) {
    w.depth_meters = lake;
    w.surface_meters = *lake_level;
  }
  return w;
}

terrain::WaterMesh FloodModel::build_mesh(const terrain::TerrainMesh& terrain, std::optional<double> lake_level) const {
  terrain::WaterMesh out;
  if (flash_ <= 0 || terrain.vertices.empty()) return out;
  std::vector<double> depth(terrain.vertices.size(), 0.0);
  for (std::size_t i = 0; i < terrain.vertices.size(); ++i) {
    const auto& v = terrain.vertices[i];
    double d = channel_depth(v.position.x, v.position.z);
    if (lake_level && v.position.y + d <= *lake_level) d = 0.0;  // the lake covers it
    depth[i] = d;
  }
  std::unordered_map<std::uint32_t, std::uint32_t> remap;
  const auto emit = [&](std::uint32_t index) {
    const auto found = remap.find(index);
    if (found != remap.end()) return found->second;
    const auto& v = terrain.vertices[index];
    terrain::WaterVertex w;
    w.position = {v.position.x, v.position.y + depth[index], v.position.z};
    w.bed_height_meters = v.position.y;
    w.source_vertex = index;
    const double ny = v.normal.y;
    if (std::isfinite(ny) && ny > 1e-9 && std::isfinite(v.normal.x) && std::isfinite(v.normal.z)) {
      w.bed_gradient = {-v.normal.x / ny, -v.normal.z / ny};
      const double magnitude = w.bed_gradient.length();
      if (magnitude > kMaxGradient) w.bed_gradient = w.bed_gradient * (kMaxGradient / magnitude);
    }
    const auto id = static_cast<std::uint32_t>(out.vertices.size());
    out.vertices.push_back(w);
    remap.emplace(index, id);
    if (depth[index] > 0) { ++out.submerged_vertices; out.max_depth_meters = std::max(out.max_depth_meters, depth[index]); }
    return id;
  };
  for (std::size_t t = 0; t + 2 < terrain.indices.size(); t += 3) {
    const auto a = terrain.indices[t], b = terrain.indices[t + 1], c = terrain.indices[t + 2];
    if (depth[a] <= 0 && depth[b] <= 0 && depth[c] <= 0) continue;
    out.indices.push_back(emit(a)); out.indices.push_back(emit(b)); out.indices.push_back(emit(c));
  }
  out.level_meters = lake_level.value_or(0.0);
  return out;
}

FloodModel::Stats FloodModel::stats(const terrain::TerrainMesh& terrain, std::optional<double> lake_level) const {
  Stats s;
  for (const auto& v : terrain.vertices) {
    const auto w = sample(v.position.x, v.position.z, v.position.y, lake_level);
    if (!w.channel) continue;
    ++s.wet_vertices;
    s.max_depth_meters = std::max(s.max_depth_meters, w.depth_meters);
    s.max_current_mps = std::max(s.max_current_mps, w.current_mps);
  }
  return s;
}

}  // namespace terraflux::graphics
