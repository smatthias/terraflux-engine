#include <terraflux/graphics/tree_fire.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTick = 1.0 / 60.0;
constexpr double kGroundAngle = 88.0 * kPi / 180.0;  // a trunk lying on the ground

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
float f(double v) { return static_cast<float>(v); }

std::uint32_t hash_u32(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352DU; x ^= x >> 15; x *= 0x846CA68BU; x ^= x >> 16;
  return x;
}
double hash01(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  return static_cast<double>(hash_u32(a * 0x9E3779B1U ^ hash_u32(b + 0x85EBCA77U * c)) & 0xFFFFFFu) / 16777216.0;
}

}  // namespace

const char* tree_fire_stage_name(TreeFireStage stage) {
  switch (stage) {
    case TreeFireStage::Unburned: return "unburned";
    case TreeFireStage::Ignited: return "ignited";
    case TreeFireStage::Burning: return "burning";
    case TreeFireStage::Falling: return "falling";
    case TreeFireStage::Fallen: return "fallen";
    case TreeFireStage::BurnedOut: return "burned-out";
  }
  throw std::invalid_argument("unknown tree fire stage");
}

const char* ignition_source_name(IgnitionSource source) {
  switch (source) {
    case IgnitionSource::None: return "none";
    case IgnitionSource::Cell: return "cell";
    case IgnitionSource::Contact: return "contact";
    case IgnitionSource::Radiation: return "radiation";
    case IgnitionSource::Ember: return "ember";
    case IgnitionSource::Collapse: return "collapse";
    case IgnitionSource::Lightning: return "lightning";
  }
  throw std::invalid_argument("unknown ignition source");
}

void validate_tree_fire_style(const TreeFireStyle& s) {
  if (!within(s.consume_seconds, 1, 600)) throw std::invalid_argument("tree consume time must be 1..600 seconds");
  if (!within(s.reference_height_meters, 1, 100)) throw std::invalid_argument("tree reference height must be 1..100 meters");
  if (!within(s.size_exponent, 0, 2)) throw std::invalid_argument("tree size exponent must be 0..2");
  if (!within(s.moisture_slowdown, 0, 5)) throw std::invalid_argument("tree moisture slowdown must be 0..5");
  if (!within(s.wind_burn_factor, 0, 1)) throw std::invalid_argument("tree wind burn factor must be 0..1 per m/s");
  if (!within(s.ignition_delay_seconds, 0, 60)) throw std::invalid_argument("tree ignition delay must be 0..60 seconds");
  if (!within(s.branch_drop_rate, 0, 20)) throw std::invalid_argument("branch drop rate must be 0..20 per second");
  if (!within(s.branch_burn_seconds, 0.1, 600)) throw std::invalid_argument("branch burn time must be 0.1..600 seconds");
  if (!within(s.ember_ignition_radius_meters, 0, 50)) throw std::invalid_argument("ember ignition radius must be 0..50 meters");
  if (!within(s.radiant_reach_meters, 0, 200)) throw std::invalid_argument("radiant reach must be 0..200 meters");
  if (!within(s.radiant_wind_factor, 0, 10)) throw std::invalid_argument("radiant wind factor must be 0..10 per m/s");
  if (!within(s.radiant_rate, 0, 100)) throw std::invalid_argument("radiant rate must be 0..100 per second");
  if (!within(s.contact_ignition_seconds, 0, 600)) throw std::invalid_argument("contact ignition time must be 0..600 seconds");
  if (!within(s.lean_degrees_max, 0, 30)) throw std::invalid_argument("standing lean must be 0..30 degrees");
  if (!within(s.burn_shift, 0, 2)) throw std::invalid_argument("burn shift must be 0..2 radians");
  if (!within(s.gravity_mps2, 0.1, 100)) throw std::invalid_argument("gravity must be 0.1..100 m/s^2");
  if (!within(s.air_density, 0.01, 20) || !within(s.drag_coefficient, 0, 5)) throw std::invalid_argument("air density and drag must be sane");
  for (const double d : {s.conifer_density, s.broadleaf_density})
    if (!within(d, 100, 1500)) throw std::invalid_argument("wood density must be 100..1500 kg/m^3");
  for (const double m : {s.conifer_rupture_pa, s.broadleaf_rupture_pa})
    if (!within(m, 1e6, 1e9)) throw std::invalid_argument("modulus of rupture must be 1e6..1e9 Pa");
  if (!within(s.variation, 0, 0.9)) throw std::invalid_argument("tree variation must be 0..0.9");
  if (!within(s.anchor_factor, 0.005, 1)) throw std::invalid_argument("anchor factor must be 0.005..1");
  if (!within(s.strike_radius_meters, 0, 200) || !within(s.strike_flash_radius_meters, 0, 500) || !within(s.strike_flash_chance, 0, 1))
    throw std::invalid_argument("strike radii must be 0..200 / 0..500 meters and the flash chance 0..1");
}

math::Vec2 compass_vector(double degrees) {
  const double a = degrees * kPi / 180.0;
  return {std::sin(a), -std::cos(a)};  // 0 north (-z), 90 east (+x)
}

TreeBody tree_body(const VegetationInstance& tree, double footprint_radius_fraction, double moisture, const TreeFireStyle& style,
                   std::uint32_t index) {
  validate_tree_fire_style(style);
  if (!within(footprint_radius_fraction, 0, 2) || !(tree.scale > 0) || !std::isfinite(tree.scale))
    throw std::invalid_argument("tree body needs a positive height and a sane footprint");
  moisture = std::clamp(std::isfinite(moisture) ? moisture : 0.0, 0.0, 1.0);
  TreeBody b;
  b.kind = tree.kind > 0.5f ? 1 : 0;
  b.height_meters = tree.scale;
  b.trunk_radius_meters = std::max(0.02, footprint_radius_fraction * tree.scale);
  const double u_density = hash01(index, 1, style.seed), u_strength = hash01(index, 2, style.seed);
  const double density = (b.kind ? style.broadleaf_density : style.conifer_density) * (1.0 + style.variation * (2.0 * u_density - 1.0));
  const double rupture = (b.kind ? style.broadleaf_rupture_pa : style.conifer_rupture_pa) * (1.0 + style.variation * (2.0 * u_strength - 1.0));
  const double r = b.trunk_radius_meters, h = b.height_meters;
  const double trunk_volume = kPi * r * r * h * 0.4;   // tapered
  b.mass_kg = 1.3 * trunk_volume * density;            // plus the crown
  b.breaking_moment_nm = rupture * kPi * r * r * r / 4.0;
  const double u_anchor = hash01(index, 5, style.seed);
  b.anchor_moment_nm = style.anchor_factor * (1.0 + style.variation * (2.0 * u_anchor - 1.0)) * b.mass_kg * style.gravity_mps2 * h;
  b.moisture = moisture;
  b.consume_seconds = style.consume_seconds * std::pow(h / style.reference_height_meters, style.size_exponent) *
                      (1.0 + style.moisture_slowdown * moisture);
  b.lean_radians = style.lean_degrees_max * kPi / 180.0 * hash01(index, 3, style.seed);
  const double a = 2.0 * kPi * hash01(index, 4, style.seed);
  b.lean_direction = {std::cos(a), std::sin(a)};
  return b;
}

double wind_moment_nm(const TreeBody& body, double wind_speed_mps, const TreeFireStyle& style) {
  const double h = body.height_meters;
  const double crown_area = (body.kind ? 0.28 : 0.19) * h * h * 0.5;  // half porous
  const double force = 0.5 * style.air_density * style.drag_coefficient * crown_area * wind_speed_mps * wind_speed_mps;
  return force * 0.65 * h;
}

double charred_strength_nm(const TreeBody& body, double consumed) {
  const double sound = std::clamp(1.0 - consumed, 0.0, 1.0);
  return body.breaking_moment_nm * sound * sound * sound;
}

TreeLoad standing_load(const TreeBody& body, double consumed, double wind_speed_mps, math::Vec2 wind_direction,
                       const TreeFireStyle& style) {
  consumed = std::clamp(consumed, 0.0, 1.0);
  const double wl = wind_direction.length();
  const math::Vec2 wdir = wl > 0 ? wind_direction / wl : math::Vec2{};
  // The flames wrap round the lee of the trunk, so the burn eats the downwind
  // side first and the weight shifts that way; in calm air it follows the lean.
  const double windward = wl > 0 ? std::min(1.0, wind_speed_mps / 6.0) : 0.0;
  const math::Vec2 shift_dir = (body.lean_direction * (1.0 - windward) + wdir * windward);
  const double shift_len = shift_dir.length();
  const math::Vec2 lean_vec = body.lean_direction * body.lean_radians +
                              (shift_len > 1e-9 ? shift_dir / shift_len : body.lean_direction) * (style.burn_shift * consumed);
  const double lean = lean_vec.length();
  const double gravity = body.mass_kg * style.gravity_mps2 * 0.5 * body.height_meters * std::sin(std::min(lean, kPi / 2));
  const double wind = wind_moment_nm(body, wind_speed_mps, style) * (1.0 - 0.7 * consumed);
  const math::Vec2 total = (lean > 1e-9 ? lean_vec / lean : body.lean_direction) * gravity + wdir * wind;
  TreeLoad load;
  load.moment_nm = total.length();
  load.direction = load.moment_nm > 1e-9 ? total / load.moment_nm : body.lean_direction;
  return load;
}

// ---------------------------------------------------------------------------
TreeFireModel::TreeFireModel(const VegetationPlacement& placement, std::span<const VegetationMesh> tree_meshes,
                             const TreeFireStyle& style, StandingHeightFn ground, std::function<double(double, double)> moisture)
    : style_(style), ground_(std::move(ground)) {
  validate_tree_fire_style(style);
  std::vector<double> footprints;
  for (const auto& mesh : tree_meshes) footprints.push_back(mesh_footprint_radius(mesh));
  if (footprints.empty()) footprints.push_back(0.05);
  bodies_.reserve(placement.trees.size());
  positions_.reserve(placement.trees.size());
  for (std::uint32_t i = 0; i < placement.trees.size(); ++i) {
    const auto& t = placement.trees[i];
    const std::size_t slot = std::min<std::size_t>(t.kind > 0.5f ? 1 : 0, footprints.size() - 1);
    const double m = moisture ? moisture(t.position[0], t.position[2]) : 0.0;
    bodies_.push_back(tree_body(t, footprints[slot], m, style, i));
    positions_.push_back({t.position[0], t.position[1], t.position[2]});
  }
  states_.assign(bodies_.size(), TreeFireState{});
  windthrown_flag_.assign(bodies_.size(), 0);
  std::vector<Obstacle> trunks;
  trunks.reserve(bodies_.size());
  for (std::size_t i = 0; i < bodies_.size(); ++i) {
    Obstacle o;
    o.x = positions_[i].x; o.z = positions_[i].z; o.radius = bodies_[i].trunk_radius_meters;
    o.bottom = positions_[i].y; o.top = positions_[i].y + bodies_[i].height_meters;
    trunks.push_back(o);
  }
  index_ = ObstacleField(std::move(trunks), 24.0);
}

template <class Visit>
void TreeFireModel::for_each_near(double x, double z, double radius, Visit&& visit) const {
  const auto* base = index_.obstacles().data();
  index_.for_each_near(x, z, radius, [&](const Obstacle& o) {
    const auto i = static_cast<std::uint32_t>(&o - base);
    const double d = std::hypot(o.x - x, o.z - z);
    if (d <= radius) visit(i, d);
  });
}

double TreeFireModel::rand01(std::uint32_t tree, std::uint32_t salt) const {
  return hash01(tree, salt + static_cast<std::uint32_t>(tick_ * 7919u), style_.seed);
}

double TreeFireModel::ground_at(double x, double z, double fallback) const {
  if (!ground_) return fallback;
  const auto h = ground_(x, z);
  return h && std::isfinite(*h) ? *h : fallback;
}

void TreeFireModel::reset() {
  states_.assign(bodies_.size(), TreeFireState{});
  windthrown_flag_.assign(bodies_.size(), 0);
  windthrown_ = struck_ = 0;
  branches_.clear();
  events_.clear();
  carry_ = seconds_ = 0.0;
  tick_ = 0;
  ++obstacle_version_;
}

std::size_t TreeFireModel::ignite(std::uint32_t tree, IgnitionSource source) {
  if (tree >= states_.size()) throw std::out_of_range("tree index out of range");
  auto& s = states_[tree];
  if (s.stage != TreeFireStage::Unburned) return 0;
  s.stage = TreeFireStage::Ignited;
  s.source = source;
  s.burn_seconds = 0.0;
  events_.push_back({TreeFireEvent::Kind::Ignited, tree, -1, source});
  return 1;
}

std::size_t TreeFireModel::ignite_from_field(const replay::FireField& field, std::int32_t origin_grid_x, std::int32_t origin_grid_y,
                                             double cell_size_meters, bool settle_burned) {
  if (!within(cell_size_meters, 1e-3, 1e6)) throw std::invalid_argument("tree fire needs a valid cell size");
  if (field.states.size() != static_cast<std::size_t>(field.width) * field.height) throw std::invalid_argument("fire field size mismatch");
  std::size_t ignited = 0;
  bool fell = false;
  for (std::uint32_t i = 0; i < bodies_.size(); ++i) {
    if (states_[i].stage != TreeFireStage::Unburned) continue;
    const auto cx = origin_grid_x + static_cast<std::int64_t>(std::llround(positions_[i].x / cell_size_meters));
    const auto cy = origin_grid_y + static_cast<std::int64_t>(std::llround(-positions_[i].z / cell_size_meters));
    const auto fx = cx - field.min_x, fy = cy - field.min_y;
    if (fx < 0 || fy < 0 || fx >= field.width || fy >= field.height) continue;
    const auto state = field.states[static_cast<std::size_t>(fy) * field.width + static_cast<std::size_t>(fx)];
    if (state == 2) ignited += ignite(i, IgnitionSource::Cell);
    else if (state == 1 && settle_burned) {
      auto& s = states_[i];
      s.stage = TreeFireStage::BurnedOut;
      s.source = IgnitionSource::Cell;
      s.consumed = 1.0;
      s.burn_seconds = bodies_[i].consume_seconds;
      if (hash01(i, 11, style_.seed) < 0.5) {
        s.fall_angle = kGroundAngle;
        const double a = 2.0 * kPi * hash01(i, 12, style_.seed);
        s.fall_direction = {std::cos(a), std::sin(a)};
        fell = true;
      }
    }
  }
  if (fell) ++obstacle_version_;
  return ignited;
}

std::optional<double> TreeFireModel::take_branch(double x, double z, double radius) {
  if (!std::isfinite(x) || !std::isfinite(z) || !within(radius, 0, 100)) throw std::invalid_argument("taking a branch needs a finite position and radius");
  std::optional<std::size_t> best;
  double best_d = radius;
  for (std::size_t i = 0; i < branches_.size(); ++i) {
    const auto& b = branches_[i];
    if (!b.landed) continue;
    const double d = std::hypot(b.position.x - x, b.position.z - z);
    if (d <= best_d) { best_d = d; best = i; }
  }
  if (!best) return std::nullopt;
  const double left = branches_[*best].seconds_left;
  branches_.erase(branches_.begin() + static_cast<std::ptrdiff_t>(*best));
  return left;
}

void TreeFireModel::add_burning_branch(math::Vec3 position, double seconds_left, double size_meters) {
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !within(seconds_left, 0, 3600) || !within(size_meters, 0.05, 5))
    throw std::invalid_argument("a burning branch needs a finite position, 0..3600 seconds and a sane size");
  BurningBranch br;
  br.position = position;
  br.ground_y = ground_at(position.x, position.z, position.y);
  br.position.y = std::max(br.position.y, br.ground_y);
  br.landed = true;
  br.seconds_left = seconds_left;
  br.size_meters = size_meters;
  br.tree = 0;
  branches_.push_back(br);
  // Lights the trunks it lies beside, like any landed branch.
  if (style_.spread && style_.ember_ignition_radius_meters > 0)
    for_each_near(br.position.x, br.position.z, style_.ember_ignition_radius_meters + index_.max_radius(), [&](std::uint32_t j, double d) {
      if (states_[j].stage != TreeFireStage::Unburned || d > style_.ember_ignition_radius_meters + bodies_[j].trunk_radius_meters) return;
      if (hash01(j, 71u + static_cast<std::uint32_t>(tick_), style_.seed) < 0.6) ignite(j, IgnitionSource::Ember);
    });
}

bool TreeFireModel::split_branch(double x, double z, double radius, double min_piece_meters) {
  if (!std::isfinite(x) || !std::isfinite(z) || !within(radius, 0, 100) || !within(min_piece_meters, 0.05, 5))
    throw std::invalid_argument("splitting a branch needs a finite position, radius and piece length");
  std::optional<std::size_t> best;
  double best_d = radius;
  for (std::size_t i = 0; i < branches_.size(); ++i) {
    const auto& b = branches_[i];
    if (!b.landed || b.size_meters * 0.5 < min_piece_meters) continue;
    const double d = std::hypot(b.position.x - x, b.position.z - z);
    if (d <= best_d) { best_d = d; best = i; }
  }
  if (!best) return false;
  BurningBranch half = branches_[*best];
  half.size_meters *= 0.5;
  branches_[*best].size_meters = half.size_meters;
  // The halves lie a little apart.
  const double a = hash01(static_cast<std::uint32_t>(*best), 91u + static_cast<std::uint32_t>(tick_), style_.seed) * 2.0 * kPi;
  half.position.x += std::cos(a) * half.size_meters * 0.7;
  half.position.z += std::sin(a) * half.size_meters * 0.7;
  half.ground_y = ground_at(half.position.x, half.position.z, half.ground_y);
  half.position.y = half.ground_y;
  branches_.push_back(half);
  return true;
}

bool TreeFireModel::standing(std::uint32_t tree) const {
  if (tree >= states_.size()) throw std::out_of_range("tree index");
  const auto& s = states_[tree];
  return s.stage != TreeFireStage::Falling && s.fall_angle < 0.3 && s.leaning_on < 0;
}

double TreeFireModel::cut(std::uint32_t tree, double depth_meters, math::Vec2 toward) {
  if (tree >= states_.size()) throw std::out_of_range("tree index");
  if (!within(depth_meters, 0, 10) || !std::isfinite(toward.x) || !std::isfinite(toward.y)) throw std::invalid_argument("a cut needs a finite depth and direction");
  auto& s = states_[tree];
  if (!standing(tree)) return s.cut;
  const double diameter = std::max(2.0 * bodies_[tree].trunk_radius_meters, 1e-3);
  const double l = std::hypot(toward.x, toward.y);
  if (l > 1e-9) s.cut_direction = {toward.x / l, toward.y / l};
  s.cut = std::min(1.0, s.cut + depth_meters / diameter);
  if (s.cut >= kFellCut - 1e-9) {
    s.cut = 1.0;
    s.felled = true;
    s.leaning_on = -1;
    s.stage = TreeFireStage::Falling;
    s.fall_direction = s.cut_direction;
    s.fall_angle = std::max(s.fall_angle, 0.03);
    s.fall_rate = 0.15;
    events_.push_back({TreeFireEvent::Kind::Felled, tree, -1, s.source});
  }
  return s.cut;
}

int TreeFireModel::strike(double x, double z, double wind_speed_mps, double wind_direction_degrees) {
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("strike position must be finite");
  if (!within(wind_speed_mps, 0, 100) || !std::isfinite(wind_direction_degrees)) throw std::invalid_argument("strike wind must be sane");
  events_.clear();
  const auto wind_dir = compass_vector(wind_direction_degrees);
  int nearest = -1;
  double best = style_.strike_radius_meters;
  const double reach = std::max(style_.strike_radius_meters, style_.strike_flash_radius_meters);
  std::vector<std::uint32_t> flashed;
  for_each_near(x, z, reach, [&](std::uint32_t j, double d) {
    const auto& s = states_[j];
    if (s.fall_angle < kGroundAngle && s.stage != TreeFireStage::Falling && d <= best) { best = d; nearest = static_cast<int>(j); }
    if (d <= style_.strike_flash_radius_meters && s.stage == TreeFireStage::Unburned) flashed.push_back(j);
  });
  if (nearest >= 0) {
    const auto i = static_cast<std::uint32_t>(nearest);
    auto& s = states_[i];
    if (s.stage == TreeFireStage::Unburned || s.stage == TreeFireStage::Ignited) {
      if (s.stage == TreeFireStage::Unburned) ignite(i, IgnitionSource::Lightning);
      s.stage = TreeFireStage::Burning;
      s.burn_seconds = std::max(s.burn_seconds, style_.ignition_delay_seconds);
      s.intensity = 1.0;
      events_.push_back({TreeFireEvent::Kind::Flames, i, -1, s.source});
    }
    if (s.stage == TreeFireStage::Burning) {
      // Split down the trunk: the tree comes down at once, away from the bolt's side.
      s.leaning_on = -1;
      begin_fall(i, wind_speed_mps, wind_dir, 1.0);
      const double dx = positions_[i].x - x, dz = positions_[i].z - z, dl = std::hypot(dx, dz);
      if (dl > 1e-6 && std::hypot(wind_dir.x, wind_dir.y) * wind_speed_mps < 6.0) s.fall_direction = {dx / dl, dz / dl};
      s.fall_rate = 0.6;
    }
    ++struck_;
    events_.push_back({TreeFireEvent::Kind::Struck, i, -1, IgnitionSource::Lightning});
  }
  for (const auto j : flashed) {
    if (static_cast<int>(j) == nearest || states_[j].stage != TreeFireStage::Unburned) continue;
    const double dry = 1.0 - 0.7 * bodies_[j].moisture;
    if (hash01(j, 61u + static_cast<std::uint32_t>(tick_), style_.seed) < style_.strike_flash_chance * dry) ignite(j, IgnitionSource::Lightning);
  }
  return nearest;
}

void TreeFireModel::begin_fall(std::uint32_t i, double wind_speed, math::Vec2 wind_dir, double lean_weight) {
  auto& s = states_[i];
  const auto load = standing_load(bodies_[i], s.consumed, wind_speed, wind_dir, style_);
  s.stage = TreeFireStage::Falling;
  s.fall_direction = lean_weight > 0 ? load.direction : s.fall_direction;
  s.fall_angle = std::max(s.fall_angle, std::max(std::min(bodies_[i].lean_radians + style_.burn_shift * s.consumed, 0.5), 0.02));
  s.fall_rate = 0.0;
  s.leaning_on = -1;
  events_.push_back({TreeFireEvent::Kind::Toppled, i, -1, s.source});
}

void TreeFireModel::advance(double dt, double wind_speed_mps, double wind_direction_degrees) {
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("tree fire time step must be finite and non-negative");
  if (!within(wind_speed_mps, 0, 100) || !std::isfinite(wind_direction_degrees)) throw std::invalid_argument("tree fire wind must be sane");
  events_.clear();
  carry_ += dt;
  const auto wind_dir = compass_vector(wind_direction_degrees);
  while (carry_ >= kTick - 1e-12) {
    carry_ -= kTick;
    step(kTick, wind_speed_mps, wind_dir);
    seconds_ += kTick;
    ++tick_;
  }
}

void TreeFireModel::step(double dt, double wind_speed, math::Vec2 wind_dir) {
  const double burn_boost = 1.0 + style_.wind_burn_factor * wind_speed;
  const std::size_t n = bodies_.size();
  // 1. Burning: flames, consumption, strength against load, branch drops.
  for (std::uint32_t i = 0; i < n; ++i) {
    auto& s = states_[i];
    const auto& b = bodies_[i];
    if (s.stage == TreeFireStage::Unburned || s.stage == TreeFireStage::BurnedOut) continue;
    if (s.source == IgnitionSource::None) continue;  // falling or fallen without ever having been lit (windthrow, crushed)
    s.burn_seconds += dt;
    if (s.stage == TreeFireStage::Ignited) {
      if (s.burn_seconds >= style_.ignition_delay_seconds) {
        s.stage = TreeFireStage::Burning;
        events_.push_back({TreeFireEvent::Kind::Flames, i, -1, s.source});
      } else continue;
    }
    const double rate = burn_boost / std::max(b.consume_seconds, 1e-3);
    if (s.stage != TreeFireStage::Falling) s.consumed = std::min(1.0, s.consumed + rate * dt);  // a fall takes the trunk down first
    const double flaring = std::min(1.0, (s.burn_seconds - style_.ignition_delay_seconds) / 1.0 + 0.05);
    const double fuel_left = 1.0 - s.consumed;
    s.intensity = std::clamp(flaring * std::min(1.0, 4.0 * fuel_left), 0.0, 1.0);
    if (s.consumed >= 1.0) {
      s.intensity = 0.0;
      if (s.stage != TreeFireStage::Falling) {  // a falling trunk finishes its fall first
        s.stage = TreeFireStage::BurnedOut;
        events_.push_back({TreeFireEvent::Kind::BurnedOut, i, -1, s.source});
      }
      continue;
    }
    // Burning branches drop from the crown while there is a crown to drop them from.
    if (s.stage == TreeFireStage::Burning && style_.branch_drop_rate > 0 && s.consumed > 0.08 && s.consumed < 0.92) {
      if (rand01(i, 21) < style_.branch_drop_rate * s.intensity * dt) {
        BurningBranch br;
        const double along = (0.5 + 0.45 * rand01(i, 22)) * b.height_meters;
        const math::Vec3 axis{s.fall_direction.x * std::sin(s.fall_angle), std::cos(s.fall_angle), s.fall_direction.y * std::sin(s.fall_angle)};
        const double side_angle = 2.0 * kPi * rand01(i, 23);
        const double side = 0.3 * b.height_meters * rand01(i, 24);
        br.position = positions_[i] + axis * along + math::Vec3{std::cos(side_angle) * side, 0.0, std::sin(side_angle) * side};
        br.velocity = {std::cos(side_angle) * 0.8 + wind_dir.x * 0.3 * wind_speed, 0.0, std::sin(side_angle) * 0.8 + wind_dir.y * 0.3 * wind_speed};
        br.size_meters = 0.4 + 0.04 * b.height_meters * rand01(i, 25);
        br.tree = i;
        br.ground_y = ground_at(br.position.x, br.position.z, positions_[i].y);
        branches_.push_back(br);
        ++s.branches_dropped;
        events_.push_back({TreeFireEvent::Kind::BranchDropped, i, -1, s.source});
      }
    }
    // Standing and charred (and maybe cut into): does the trunk still hold?
    if (s.stage == TreeFireStage::Burning) {
      const auto load = standing_load(b, s.consumed, wind_speed, wind_dir, style_);
      const double cut_left = (1.0 - s.cut) * (1.0 - s.cut);
      if (load.moment_nm > charred_strength_nm(b, s.consumed) * cut_left) {
        begin_fall(i, wind_speed, wind_dir, 1.0);
        if (s.cut > 0) {  // it breaks at the cut, the cut's way
          s.fall_direction = s.cut_direction;
          s.felled = true;
          s.cut = 1.0;
          events_.push_back({TreeFireEvent::Kind::Felled, i, -1, s.source});
        }
      }
    }
  }
  // 1a. Windthrow: a gust past what the roots hold uproots a standing tree, sound or charred.
  if (wind_speed > 0) {
    for (std::uint32_t i = 0; i < n; ++i) {
      auto& s = states_[i];
      if (s.stage == TreeFireStage::Falling || s.fall_angle > 0.05 || s.leaning_on >= 0) continue;
      if (s.stage == TreeFireStage::Fallen) continue;
      const auto& b = bodies_[i];
      // The roots see the wind on the crown and the standing lean; the burn's own
      // weight shift is the trunk's problem (handled above by its charred strength).
      const auto load = standing_load(b, 0.0, wind_speed, wind_dir, style_);
      // A trunk cut into breaks at the cut before the roots give.
      const double cut_left = (1.0 - s.cut) * (1.0 - s.cut);
      const bool breaks_at_cut = s.cut > 0 && load.moment_nm > b.breaking_moment_nm * cut_left;
      if (load.moment_nm > b.anchor_moment_nm || breaks_at_cut) {
        begin_fall(i, wind_speed, wind_dir, 1.0);
        s.fall_direction = breaks_at_cut ? s.cut_direction : load.direction;
        if (breaks_at_cut) { s.felled = true; s.cut = 1.0; events_.push_back({TreeFireEvent::Kind::Felled, i, -1, s.source}); continue; }
        if (!windthrown_flag_[i]) { windthrown_flag_[i] = 1; ++windthrown_; }
        events_.push_back({TreeFireEvent::Kind::Windthrown, i, -1, s.source});
      }
    }
  }
  // 1b. Leaners: the prop burning through, breaking or falling drops them the rest of the way.
  for (std::uint32_t i = 0; i < n; ++i) {
    auto& s = states_[i];
    if (s.leaning_on < 0 || s.stage == TreeFireStage::Falling) continue;
    const auto j = static_cast<std::size_t>(s.leaning_on);
    const auto& prop = states_[j];
    const double load_on_prop = 0.5 * bodies_[i].mass_kg * style_.gravity_mps2 * bodies_[j].height_meters * std::sin(s.fall_angle);
    if (prop.stage == TreeFireStage::Falling || prop.fall_angle >= kGroundAngle || prop.leaning_on >= 0 ||
        charred_strength_nm(bodies_[j], prop.consumed) < load_on_prop) {
      events_.push_back({TreeFireEvent::Kind::Toppled, i, s.leaning_on, s.source});
      s.stage = TreeFireStage::Falling;
      s.leaning_on = -1;
      s.fall_rate = 0.0;
    }
  }
  // 2. Falling: a rigid rod about its base under gravity and the wind; neighbours in the way.
  for (std::uint32_t i = 0; i < n; ++i) {
    auto& s = states_[i];
    if (s.stage != TreeFireStage::Falling) continue;
    const auto& b = bodies_[i];
    const double h = b.height_meters;
    const double alignment = std::max(0.0, s.fall_direction.dot(wind_dir));
    const double alpha = 1.5 * style_.gravity_mps2 / h * std::sin(s.fall_angle) +
                         3.0 * wind_moment_nm(b, wind_speed, style_) * alignment * std::cos(s.fall_angle) / std::max(b.mass_kg * h * h, 1e-6);
    s.fall_rate += alpha * dt;
    const double previous = s.fall_angle;
    s.fall_angle = std::min(kGroundAngle, s.fall_angle + s.fall_rate * dt);
    // The first standing neighbour the trunk sweeps into between the two angles.
    int caught = -1;
    double catch_angle = kGroundAngle;
    for_each_near(positions_[i].x, positions_[i].z, h + 2.0, [&](std::uint32_t j, double d) {
      if (j == i || d < 1e-6) return;
      const auto& sj = states_[j];
      if (sj.stage == TreeFireStage::Falling || (sj.fall_angle >= kGroundAngle)) return;  // nothing standing there
      if (sj.stage == TreeFireStage::Fallen && sj.leaning_on >= 0) return;                 // already propped on something
      const math::Vec2 to{(positions_[j].x - positions_[i].x) / d, (positions_[j].z - positions_[i].z) / d};
      const double along = d * to.dot(s.fall_direction);
      if (along <= b.trunk_radius_meters) return;  // behind or beside the base
      const double lateral = std::abs(d * (to.x * s.fall_direction.y - to.y * s.fall_direction.x));
      if (lateral > b.trunk_radius_meters + bodies_[j].trunk_radius_meters + 0.5) return;
      if (along > h) return;  // out of reach
      const double angle = std::atan2(along, bodies_[j].height_meters);  // the trunk touches its top here
      if (angle > s.fall_angle || angle < previous - 1e-9) return;
      if (angle < catch_angle) { catch_angle = angle; caught = static_cast<int>(j); }
    });
    if (caught >= 0) {
      const auto j = static_cast<std::uint32_t>(caught);
      auto& sj = states_[j];
      const double contact_height = bodies_[j].height_meters;
      const double load_on_prop = 0.5 * b.mass_kg * style_.gravity_mps2 * contact_height * std::sin(catch_angle);
      const bool burning = s.intensity > 0.0;
      if (charred_strength_nm(bodies_[j], sj.consumed) >= load_on_prop) {
        s.stage = TreeFireStage::Fallen;
        s.fall_angle = catch_angle;
        s.fall_rate = 0.0;
        s.leaning_on = caught;
        ++obstacle_version_;
        events_.push_back({TreeFireEvent::Kind::Leaning, i, caught, s.source});
        if (burning && style_.spread && sj.stage == TreeFireStage::Unburned) sj.contact_seconds += style_.contact_ignition_seconds * 0.5;
      } else {
        // The prop breaks: it falls the same way, alight if what hit it was burning.
        s.fall_angle = catch_angle;
        if (sj.stage == TreeFireStage::Unburned && burning && style_.spread) {
          ignite(j, IgnitionSource::Collapse);
          states_[j].stage = TreeFireStage::Burning;
          states_[j].burn_seconds = style_.ignition_delay_seconds;
        }
        states_[j].fall_direction = s.fall_direction;
        states_[j].fall_angle = std::max(states_[j].fall_angle, 0.05);
        states_[j].stage = TreeFireStage::Falling;
        states_[j].fall_rate = s.fall_rate * 0.5;
        states_[j].leaning_on = -1;
        events_.push_back({TreeFireEvent::Kind::Collapsed, i, caught, s.source});
      }
    } else if (s.fall_angle >= kGroundAngle) {
      s.stage = s.consumed >= 1.0 ? TreeFireStage::BurnedOut : TreeFireStage::Fallen;
      s.fall_rate = 0.0;
      s.leaning_on = -1;
      ++obstacle_version_;
      events_.push_back({TreeFireEvent::Kind::Grounded, i, -1, s.source});
    }
  }
  // 3. Spread: contact along burning trunks, radiant heat downwind, embers.
  if (style_.spread) {
    for (std::uint32_t i = 0; i < n; ++i) {
      const auto& s = states_[i];
      if (s.intensity <= 0.0) continue;
      const auto& b = bodies_[i];
      const double reach = style_.radiant_reach_meters * (b.height_meters / style_.reference_height_meters) * s.intensity;
      const double max_reach = reach * (1.0 + style_.radiant_wind_factor * wind_speed) + b.height_meters;
      for_each_near(positions_[i].x, positions_[i].z, max_reach, [&](std::uint32_t j, double d) {
        if (j == i || d < 1e-6) return;
        auto& sj = states_[j];
        if (sj.stage != TreeFireStage::Unburned) return;
        const math::Vec2 to{(positions_[j].x - positions_[i].x) / d, (positions_[j].z - positions_[i].z) / d};
        // Contact: a fallen trunk lying against the neighbour, or a leaner on it.
        if (s.stage == TreeFireStage::Fallen) {
          const double along = d * to.dot(s.fall_direction);
          const double lateral = std::abs(d * (to.x * s.fall_direction.y - to.y * s.fall_direction.x));
          const double touching = b.trunk_radius_meters + bodies_[j].trunk_radius_meters + 0.5;
          const bool leaner = s.leaning_on == static_cast<int>(j);
          if (leaner || (along > 0 && along <= b.height_meters * std::sin(s.fall_angle) + touching && lateral <= touching)) {
            sj.contact_seconds += dt;
            if (sj.contact_seconds >= style_.contact_ignition_seconds) ignite(j, IgnitionSource::Contact);
            return;
          }
        }
        // Radiant heat: stronger downwind, fading with distance.
        const double r = reach * (1.0 + style_.radiant_wind_factor * wind_speed * std::max(0.0, to.dot(wind_dir)));
        if (d >= r || r <= 0) return;
        const double p = style_.radiant_rate * s.intensity * (1.0 - d / r) * dt;
        if (hash01(i * 131u + j, 31u + static_cast<std::uint32_t>(tick_), style_.seed) < p) ignite(j, IgnitionSource::Radiation);
      });
    }
  }
  // 4. Branches: fall, land, burn, ignite what they land beside.
  for (auto& br : branches_) {
    if (!br.landed) {
      br.velocity.y -= style_.gravity_mps2 * dt;
      br.position = br.position + br.velocity * dt;
      br.ground_y = ground_at(br.position.x, br.position.z, br.ground_y);
      if (br.position.y <= br.ground_y) {
        br.position.y = br.ground_y;
        br.velocity = {};
        br.landed = true;
        br.seconds_left = style_.branch_burn_seconds;
        if (style_.spread && style_.ember_ignition_radius_meters > 0)
          for_each_near(br.position.x, br.position.z, style_.ember_ignition_radius_meters + index_.max_radius(), [&](std::uint32_t j, double d) {
            if (states_[j].stage != TreeFireStage::Unburned || d > style_.ember_ignition_radius_meters + bodies_[j].trunk_radius_meters) return;
            if (hash01(j, 41u + static_cast<std::uint32_t>(tick_), style_.seed) < 0.6) ignite(j, IgnitionSource::Ember);
          });
      }
    } else {
      br.seconds_left -= dt;
    }
  }
  branches_.erase(std::remove_if(branches_.begin(), branches_.end(), [](const BurningBranch& b) { return b.landed && b.seconds_left <= 0; }),
                  branches_.end());
}

TreeFireStats TreeFireModel::stats() const {
  TreeFireStats st;
  st.trees = bodies_.size();
  st.seconds = seconds_;
  st.lightest_kg = st.weakest_nm = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < bodies_.size(); ++i) {
    const auto& s = states_[i];
    const auto& b = bodies_[i];
    switch (s.stage) {
      case TreeFireStage::Unburned: ++st.unburned; break;
      case TreeFireStage::Ignited: ++st.ignited; break;
      case TreeFireStage::Burning: ++st.burning; break;
      case TreeFireStage::Falling: ++st.falling; break;
      case TreeFireStage::Fallen: ++st.fallen; break;
      case TreeFireStage::BurnedOut: ++st.burned_out; break;
    }
    switch (s.source) {
      case IgnitionSource::None: break;
      case IgnitionSource::Cell: ++st.ignited_by_cell; break;
      case IgnitionSource::Contact: ++st.ignited_by_contact; break;
      case IgnitionSource::Radiation: ++st.ignited_by_radiation; break;
      case IgnitionSource::Ember: ++st.ignited_by_ember; break;
      case IgnitionSource::Collapse: ++st.ignited_by_collapse; break;
      case IgnitionSource::Lightning: ++st.ignited_by_lightning; break;
    }
    if (s.leaning_on >= 0) ++st.leaning;
    if (s.fall_angle >= kGroundAngle) ++st.grounded;
    if (s.felled) ++st.felled;
    else if (s.cut > 0) ++st.cut;
    st.branches_dropped += s.branches_dropped;
    st.heaviest_kg = std::max(st.heaviest_kg, b.mass_kg);
    st.lightest_kg = std::min(st.lightest_kg, b.mass_kg);
    st.strongest_nm = std::max(st.strongest_nm, b.breaking_moment_nm);
    st.weakest_nm = std::min(st.weakest_nm, b.breaking_moment_nm);
  }
  if (bodies_.empty()) st.lightest_kg = st.weakest_nm = 0.0;
  st.windthrown = windthrown_;
  st.struck = struck_;
  st.branches_burning = branches_.size();
  for (const auto& e : events_) if (e.kind == TreeFireEvent::Kind::Collapsed) ++st.collapses;
  return st;
}

void TreeFireModel::apply_to_placement(VegetationPlacement& placement) const {
  if (placement.trees.size() != bodies_.size()) throw std::invalid_argument("placement does not match the tree fire model");
  for (std::size_t i = 0; i < bodies_.size(); ++i) {
    auto& t = placement.trees[i];
    const auto& s = states_[i];
    t.fall_direction[0] = f(s.fall_direction.x); t.fall_direction[1] = f(s.fall_direction.y);
    t.fall_angle = f(s.fall_angle);
    t.consumed = f(s.consumed);
    t.char_amount = f(std::min(1.0, s.consumed * 1.5 + (s.stage == TreeFireStage::BurnedOut ? 1.0 : 0.0)));
    t.glow = f(s.intensity);
  }
}

void TreeFireModel::emit(std::vector<FireInstance>& flames, std::vector<FireInstance>& smoke, const FireStyle& style) const {
  for (std::size_t i = 0; i < bodies_.size(); ++i) {
    const auto& s = states_[i];
    const auto& b = bodies_[i];
    const auto& p = positions_[i];
    const math::Vec3 axis{s.fall_direction.x * std::sin(s.fall_angle), std::cos(s.fall_angle), s.fall_direction.y * std::sin(s.fall_angle)};
    if (s.intensity > 0.0) {
      const bool lying = s.fall_angle >= kGroundAngle;
      const int count = lying ? 3 : (s.consumed > 0.3 ? 2 : 1);
      for (int k = 0; k < count; ++k) {
        const double along = lying ? (0.2 + 0.3 * k) * b.height_meters : (k == 0 ? 0.55 : 0.15) * b.height_meters;
        const auto at = p + axis * along;
        FireInstance fl{};
        fl.position[0] = f(at.x); fl.position[1] = f(at.y); fl.position[2] = f(at.z);
        fl.size = f((lying ? 0.3 : (k == 0 ? 1.1 : 0.5)) * b.height_meters * style.flame_scale * (0.4 + 0.6 * s.intensity));
        fl.phase = f(2.0 * kPi * hash01(static_cast<std::uint32_t>(i), 51u + k, style.seed));
        fl.intensity = f(s.intensity); fl.kind = 0.0f; fl.seed = f(hash01(static_cast<std::uint32_t>(i), 52u + k, style.seed) * 100.0);
        flames.push_back(fl);
      }
      FireInstance pl{};
      const auto top = p + axis * (b.height_meters * (lying ? 0.5 : 0.9));
      pl.position[0] = f(top.x); pl.position[1] = f(top.y); pl.position[2] = f(top.z);
      pl.size = f(style.smoke_height_meters * 0.6 * s.intensity); pl.phase = f(2.0 * kPi * hash01(static_cast<std::uint32_t>(i), 53, style.seed));
      pl.intensity = f(0.4 + 0.6 * s.intensity); pl.kind = 1.0f; pl.seed = f(hash01(static_cast<std::uint32_t>(i), 54, style.seed) * 100.0);
      smoke.push_back(pl);
    } else if (s.stage == TreeFireStage::BurnedOut && hash01(static_cast<std::uint32_t>(i), 55, style.seed) < 0.3) {
      FireInstance pl{};
      const auto at = p + axis * (b.height_meters * 0.3);
      pl.position[0] = f(at.x); pl.position[1] = f(at.y); pl.position[2] = f(at.z);
      pl.size = f(style.smoke_height_meters * 0.25); pl.phase = f(2.0 * kPi * hash01(static_cast<std::uint32_t>(i), 56, style.seed));
      pl.intensity = f(0.25); pl.kind = 2.0f; pl.seed = f(hash01(static_cast<std::uint32_t>(i), 57, style.seed) * 100.0);
      smoke.push_back(pl);
    }
  }
  for (const auto& br : branches_) {
    FireInstance fl{};
    fl.position[0] = f(br.position.x); fl.position[1] = f(br.position.y); fl.position[2] = f(br.position.z);
    const double life = br.landed ? std::clamp(br.seconds_left / style_.branch_burn_seconds, 0.0, 1.0) : 1.0;
    fl.size = f(br.size_meters * (br.landed ? 1.6 : 1.0) * style.flame_scale);
    fl.phase = f(br.tree * 0.37); fl.intensity = f(0.5 + 0.5 * life);
    fl.kind = br.landed ? 0.0f : 3.0f;  // a flame on the ground, an ember in the air
    fl.seed = f(br.tree * 1.3);
    flames.push_back(fl);
  }
}

TreeFireModel::Exposure TreeFireModel::exposure_at(double x, double z, double wind_direction_degrees, double heat_radius_meters,
                                                   double smoke_radius_meters) const {
  if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(wind_direction_degrees)) throw std::invalid_argument("exposure needs a finite position");
  if (!within(heat_radius_meters, 0.1, 1000) || !within(smoke_radius_meters, 0.1, 10000)) throw std::invalid_argument("exposure radii out of range");
  Exposure e;
  const auto wind = compass_vector(wind_direction_degrees);
  for_each_near(x, z, std::max(heat_radius_meters, smoke_radius_meters) + 40.0, [&](std::uint32_t i, double d_base) {
    const auto& s = states_[i];
    if (s.intensity <= 0.0) return;
    const auto& b = bodies_[i];
    // Distance to the trunk: the base for a standing tree, the nearest point of the line for a lying one.
    double d = d_base;
    if (s.fall_angle > 0.3) {
      const double len = b.height_meters * std::sin(s.fall_angle);
      const double ax = x - positions_[i].x, az = z - positions_[i].z;
      const double t = std::clamp(ax * s.fall_direction.x + az * s.fall_direction.y, 0.0, len);
      d = std::hypot(ax - s.fall_direction.x * t, az - s.fall_direction.y * t);
    }
    d = std::max(0.0, d - b.trunk_radius_meters);
    const double heat = s.intensity * (1.0 - std::clamp((d - 3.0) / std::max(heat_radius_meters - 3.0, 0.1), 0.0, 1.0));
    e.heat = std::max(e.heat, heat);
    if (d < smoke_radius_meters) {
      const double dl = std::max(d_base, 1e-6);
      const double downwind = std::max(0.0, ((x - positions_[i].x) / dl) * wind.x + ((z - positions_[i].z) / dl) * wind.y);
      e.smoke += s.intensity * (0.15 + 0.85 * downwind) * (1.0 - d / smoke_radius_meters) * 0.4;
    }
  });
  e.heat = std::clamp(e.heat, 0.0, 1.0);
  e.smoke = std::clamp(e.smoke, 0.0, 1.0);
  return e;
}

std::vector<Obstacle> TreeFireModel::obstacles(std::span<const Obstacle> standing) const {
  if (standing.size() != bodies_.size()) throw std::invalid_argument("standing obstacles do not match the tree fire model");
  std::vector<Obstacle> out;
  out.reserve(standing.size());
  for (std::size_t i = 0; i < bodies_.size(); ++i) {
    const auto& s = states_[i];
    if (s.fall_angle < kGroundAngle) { out.push_back(standing[i]); continue; }
    // A trunk on the ground: a chain of its diameter along its length.
    const auto& b = bodies_[i];
    const double r = b.trunk_radius_meters;
    const double spacing = std::max(2.0 * r, 0.5);
    const int count = std::max(1, static_cast<int>(std::ceil(b.height_meters / spacing)));
    for (int k = 0; k < count; ++k) {
      const double along = std::min(b.height_meters, (k + 0.5) * spacing);
      Obstacle o;
      o.x = positions_[i].x + s.fall_direction.x * along;
      o.z = positions_[i].z + s.fall_direction.y * along;
      o.radius = r;
      const double ground = ground_at(o.x, o.z, positions_[i].y);
      o.bottom = ground; o.top = ground + 2.0 * r;
      out.push_back(o);
    }
  }
  return out;
}

}  // namespace terraflux::graphics
