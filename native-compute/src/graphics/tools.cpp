#include <terraflux/graphics/tools.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// A box frame whose z axis runs along `direction`, centred at `centre`.
math::Mat4 frame_along(math::Vec3 centre, math::Vec3 direction, math::Vec3 up_hint) {
  const double len = direction.length();
  const math::Vec3 z = len > 1e-9 ? direction / len : math::Vec3{0, 0, 1};
  math::Vec3 x = up_hint.cross(z);
  if (x.length() < 1e-6) x = math::Vec3{1, 0, 0}.cross(z);
  if (x.length() < 1e-6) x = math::Vec3{0, 0, 1}.cross(z);
  x = x.normalized();
  const math::Vec3 y = z.cross(x).normalized();
  math::Mat4 m = math::Mat4::identity();
  m.at(0, 0) = x.x; m.at(1, 0) = x.y; m.at(2, 0) = x.z;
  m.at(0, 1) = y.x; m.at(1, 1) = y.y; m.at(2, 1) = y.z;
  m.at(0, 2) = z.x; m.at(1, 2) = z.y; m.at(2, 2) = z.z;
  m.at(0, 3) = centre.x; m.at(1, 3) = centre.y; m.at(2, 3) = centre.z;
  return m;
}

// A line of boxes from a to b (at most `budget`), appended.
void line_props(std::vector<Prop>& out, math::Vec3 a, math::Vec3 b, double thickness, math::Vec3 color, std::size_t budget) {
  const math::Vec3 d = b - a;
  const double len = d.length();
  if (len < 1e-6 || budget == 0) return;
  const std::size_t pieces = std::min(budget, static_cast<std::size_t>(std::max(1.0, std::ceil(len / 4.0))));
  for (std::size_t k = 0; k < pieces; ++k) {
    const double t0 = static_cast<double>(k) / static_cast<double>(pieces), t1 = static_cast<double>(k + 1) / static_cast<double>(pieces);
    const math::Vec3 p0 = a + d * t0, p1 = a + d * t1;
    out.push_back({frame_along((p0 + p1) * 0.5, p1 - p0, {0, 1, 0}), {thickness, thickness, (p1 - p0).length()}, color});
  }
}

constexpr math::Vec3 kRopeColor{0.72, 0.60, 0.40};

}  // namespace

std::optional<ToolKind> tool_kind_from_name(std::string_view name) {
  if (name == "none") return ToolKind::None;
  if (name == "rope") return ToolKind::Rope;
  if (name == "axe") return ToolKind::Axe;
  if (name == "chainsaw") return ToolKind::Chainsaw;
  return std::nullopt;
}

void validate_axe_rules(const AxeRules& r) {
  if (!within(r.chop_depth_meters, 0.005, 1) || !within(r.swing_seconds, 0.1, 5) || !within(r.reach_meters, 0.3, 5))
    throw std::invalid_argument("axe rules out of range");
}
void validate_chainsaw_rules(const ChainsawRules& r) {
  if (!within(r.cut_rate_mps, 0.001, 5) || !within(r.fuel_seconds, 1, 36000) || !within(r.branch_seconds, 0.05, 60) || !within(r.reach_meters, 0.3, 5))
    throw std::invalid_argument("chainsaw rules out of range");
}
int chops_to_fell(double trunk_radius_meters, const AxeRules& rules) {
  validate_axe_rules(rules);
  if (!within(trunk_radius_meters, 0, 50)) throw std::invalid_argument("trunk radius out of range");
  return std::max(1, static_cast<int>(std::ceil(2.0 * trunk_radius_meters * kFellCutFraction / rules.chop_depth_meters - 1e-9)));
}
double seconds_to_saw(double trunk_radius_meters, const ChainsawRules& rules) {
  validate_chainsaw_rules(rules);
  if (!within(trunk_radius_meters, 0, 50)) throw std::invalid_argument("trunk radius out of range");
  return 2.0 * trunk_radius_meters * kFellCutFraction / rules.cut_rate_mps;
}
bool branch_can_be_cut(double length_meters) { return std::isfinite(length_meters) && length_meters * 0.5 >= kMinBranchPiece; }

ToolKind ToolBelt::select(ToolKind kind) {
  selected = kind == selected ? ToolKind::None : kind;
  swing = 0.0;
  sawing = false;
  return selected;
}

const char* rope_state_name(RopeState state) {
  switch (state) {
    case RopeState::Coiled: return "coiled";
    case RopeState::Flying: return "flying";
    case RopeState::Laid: return "laid";
    case RopeState::Hung: return "hung";
  }
  throw std::invalid_argument("unknown rope state");
}

void validate_rope_rules(const RopeRules& r) {
  if (!within(r.length_meters, 2, 200) || !within(r.throw_speed_mps, 1, 60) || !within(r.lift_degrees, -60, 60) || !within(r.hang_drop_meters, 0, 50) ||
      !within(r.climb_speed_mps, 0.05, 5) || !within(r.stamina_seconds, 1, 3600) || !within(r.slide_speed_mps, 0.1, 20) ||
      !within(r.grab_reach_meters, 0.1, 5) || !within(r.step_off_meters, 0, 3) || !within(r.pull_seconds_per_meter, 0, 10) ||
      !within(r.face_slope_tangent, 0.1, 50))
    throw std::invalid_argument("rope rules out of range");
}

double Rope::span_meters() const { return (end - anchor).length(); }

math::Vec3 Rope::point_at(double t_meters) const {
  const double span = span_meters();
  if (span < 1e-9) return anchor;
  const double t = std::clamp(t_meters, 0.0, span);
  return anchor + (end - anchor) * (t / span);
}

void throw_rope(Rope& rope, math::Vec3 feet, math::Vec3 eye, math::Vec3 forward, const RopeRules& rules) {
  validate_rope_rules(rules);
  if (!finite(feet) || !finite(eye) || !finite(forward)) throw std::invalid_argument("rope throw needs finite positions");
  if (rope.state != RopeState::Coiled) throw std::logic_error("only a coiled rope can be thrown");
  math::Vec3 f = forward;
  if (f.length() < 1e-9) f = {0, 0, -1};
  f = f.normalized();
  rope = Rope{};
  rope.state = RopeState::Flying;
  rope.thrower_feet = feet;
  rope.anchor = feet;
  rope.flight.position = eye + f * 0.5;
  rope.flight.velocity = throw_velocity(f, rules.throw_speed_mps, rules.lift_degrees);
  rope.end = rope.flight.position;
}

void advance_rope(Rope& rope, double dt, double gravity_mps2, const StandingHeightFn& ground, math::Vec3 lower, math::Vec3 upper,
                  const BranchCatchFn& branches, const RopeRules& rules) {
  validate_rope_rules(rules);
  if (rope.state != RopeState::Flying) return;
  const math::Vec3 hand = rope.thrower_feet + math::Vec3{0, 1.5, 0};
  double remaining = dt;
  while (remaining > 1e-9 && rope.state == RopeState::Flying) {
    const double step = std::min(remaining, 1.0 / 60.0);
    remaining -= step;
    const math::Vec3 from = rope.flight.position;
    advance_projectile(rope.flight, step, gravity_mps2, ground, lower, upper);
    // Taut: the coil can go no further than the rope; it drops from there.
    math::Vec3 rel = rope.flight.position - hand;
    if (rel.length() > rules.length_meters) {
      const math::Vec3 n = rel.normalized();
      rope.flight.position = hand + n * rules.length_meters;
      const double outward = rope.flight.velocity.dot(n);
      if (outward > 0) rope.flight.velocity = rope.flight.velocity - n * outward;
      const auto h = ground ? ground(rope.flight.position.x, rope.flight.position.z) : std::nullopt;
      if (h && rope.flight.position.y <= *h) { rope.flight.position.y = *h; rope.flight.velocity = {}; rope.flight.landed = true; }
    }
    rope.paid_out_meters = std::min(rules.length_meters, (rope.flight.position - hand).length());
    rope.end = rope.flight.position;
    // A crown on the way: the rope wraps round a branch and hangs from it.
    if (!rope.flight.landed && branches) {
      if (const auto caught = branches(from, rope.flight.position)) {
        rope.state = RopeState::Hung;
        rope.anchor = caught->point;
        rope.branch_tree = caught->tree;
        const auto h = ground ? ground(rope.anchor.x, rope.anchor.z) : std::nullopt;
        const double ground_y = h ? *h : rope.thrower_feet.y;
        rope.anchor_height_meters = rope.anchor.y - ground_y;
        // The end hangs straight down: to the ground, or as far as the rope goes.
        const double below = std::min(rules.length_meters, std::max(0.0, rope.anchor.y - ground_y));
        rope.end = {rope.anchor.x, rope.anchor.y - below, rope.anchor.z};
        return;
      }
    }
    if (rope.flight.landed) {
      rope.branch_tree = -1;
      rope.landing = rope.flight.position;
      rope.anchor = rope.thrower_feet;
      rope.end = rope.landing;
      rope.state = RopeState::Laid;
      rope.anchor_height_meters = 0.0;
      if (!ground || rope.landing.y < lower.y - 500.0) { rope = Rope{}; return; }  // lost down a hole: pulled back in
      // Where does the ground fall away between the feet and the landing point?
      // The rope lies to the lip, hangs down the face (the steep run of the
      // ground, steeper than a walker's slope) to its foot, and lies on from there.
      const math::Vec3 a = rope.thrower_feet, b = rope.landing;
      const double dx = b.x - a.x, dz = b.z - a.z, horizontal = std::hypot(dx, dz);
      if (horizontal < 1e-6) return;
      const int n = std::max(1, static_cast<int>(std::ceil(horizontal / 0.25)));
      const double step = horizontal / n;
      std::vector<math::Vec3> samples;
      samples.reserve(static_cast<std::size_t>(n) + 1);
      for (int k = 0; k <= n; ++k) {
        const double f = static_cast<double>(k) / n;
        const double x = a.x + dx * f, z = a.z + dz * f;
        const auto h = ground(x, z);
        if (!h) break;
        samples.push_back({x, *h, z});
      }
      const auto steep_down = [&](std::size_t k) { return samples[k].y - samples[k + 1].y > step * rules.face_slope_tangent; };
      std::optional<std::size_t> lip;
      for (std::size_t k = 0; k + 1 < samples.size(); ++k) if (steep_down(k)) { lip = k; break; }
      if (!lip) return;
      std::size_t foot = *lip + 1;
      while (foot + 1 < samples.size() && steep_down(foot)) ++foot;
      const double drop = samples[*lip].y - samples[foot].y;
      if (drop < rules.hang_drop_meters) return;
      rope.state = RopeState::Hung;
      rope.anchor = samples[*lip];
      rope.end = samples[foot];
      rope.anchor_height_meters = drop;
      return;
    }
  }
}

void coil_rope(Rope& rope) { rope = Rope{}; }

std::optional<double> rope_grab_point(const Rope& rope, math::Vec3 feet, double shoulder_height, const RopeRules& rules) {
  validate_rope_rules(rules);
  if (!finite(feet) || !within(shoulder_height, 0, 5)) throw std::invalid_argument("rope grab query out of range");
  if (rope.state != RopeState::Hung) return std::nullopt;
  const double span = rope.span_meters();
  // From the edge: the anchor within reach of the feet.
  std::optional<double> best;
  double best_d = rules.grab_reach_meters;
  if ((rope.anchor - feet).length() <= best_d && rope.branch_tree < 0) { best = 0.0; best_d = (rope.anchor - feet).length(); }
  // Along the line: the nearest point to the hands.
  if (span > 1e-9) {
    const math::Vec3 hands = feet + math::Vec3{0, shoulder_height, 0};
    const math::Vec3 d = (rope.end - rope.anchor) / span;
    const double t = std::clamp((hands - rope.anchor).dot(d), 0.0, span);
    const double dist = (rope.anchor + d * t - hands).length();
    if (dist <= best_d) { best = t; best_d = dist; }
    // The lower end within reach of the feet (standing below a dangling rope).
    const double end_d = (rope.end - feet).length();
    if (end_d <= best_d) { best = span; best_d = end_d; }
  }
  return best;
}

void grab_rope(FirstPersonCamera& camera, const Rope& rope, double t_meters) {
  if (rope.state != RopeState::Hung) throw std::logic_error("only a hung rope can be taken hold of");
  if (!std::isfinite(t_meters)) throw std::invalid_argument("rope point must be finite");
  camera.on_rope = true;
  camera.rope_t_meters = std::clamp(t_meters, 0.0, rope.span_meters());
  camera.rope_ground_meters = 0.0;
  camera.feet = rope.point_at(camera.rope_t_meters);
  camera.grounded = false;
  camera.vertical_velocity_mps = 0.0;
  camera.velocity_mps = {};
  camera.gait_speed_mps = 0.0;
  camera.braking_mps2 = 0.0;
  camera.air_time_seconds = 0.0;
  camera.swimming = false;
  camera.diving = false;
  camera.crawling = false;
  camera.climbing = true;
  camera.climb_surface = "rope";
  camera.climb_slope_degrees = 90.0;
  camera.climb_refusal = "";
}

RopeClimbResult climb_rope(FirstPersonCamera& camera, const Rope& rope, const WalkInput& input, double dt, const WalkEnvironment& environment,
                           const RopeRules& rules) {
  validate_rope_rules(rules);
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("rope step must be finite and non-negative");
  RopeClimbResult result;
  if (!camera.on_rope) return result;
  if (rope.state != RopeState::Hung) {  // the rope is gone from under the hands
    camera.on_rope = false; camera.climbing = false; camera.grounded = false;
    result.let_go = true;
    return result;
  }
  const auto& climb = environment.climb_rules;
  const auto release = [&] {
    camera.on_rope = false;
    camera.climbing = false;
    camera.grounded = false;
    camera.vertical_velocity_mps = 0.0;
    camera.velocity_mps = {};
    result.let_go = true;
  };
  if (input.jump) { release(); return result; }
  // Strength: climbing up spends it, hanging a third as fast, going down nothing.
  if (camera.stamina <= climb.min_stamina) camera.exhausted = true;
  double rate = 0.0;  // + down the rope
  if (camera.exhausted) {
    rate = rules.slide_speed_mps;
    result.sliding = true;
  } else if (input.forward > 0) {
    rate = -rules.climb_speed_mps;
    camera.stamina = std::max(0.0, camera.stamina - dt / rules.stamina_seconds);
  } else if (input.forward < 0) {
    rate = rules.climb_speed_mps;
  } else {
    camera.stamina = std::max(0.0, camera.stamina - dt / (3.0 * rules.stamina_seconds));
  }
  const double span = rope.span_meters();
  const double t_old = camera.rope_t_meters;
  const double t_new = std::clamp(t_old + rate * dt, 0.0, span);
  result.moved_meters = t_new - t_old;
  camera.rope_t_meters = t_new;
  camera.rope_phase += std::abs(result.moved_meters) / 0.6;
  camera.rope_phase -= std::floor(camera.rope_phase);
  const math::Vec3 point = rope.point_at(t_new);
  const auto& sample = environment.standing_height;
  const auto ground = sample ? sample(point.x, point.z) : std::nullopt;
  camera.feet = point;
  if (ground && *ground > camera.feet.y) camera.feet.y = *ground;  // the rope runs over the ground here: the feet on it
  // Ground to stand on: within the step-off distance below and no steeper than the walker can walk.
  const auto walkable_ground = [&]() -> bool {
    if (!ground || point.y - *ground > rules.step_off_meters + 1e-9) return false;
    const double limit = std::tan(environment.body.max_slope_degrees * kPi / 180.0);
    for (const auto& [ox, oz] : {std::pair{0.5, 0.0}, std::pair{-0.5, 0.0}, std::pair{0.0, 0.5}, std::pair{0.0, -0.5}}) {
      const auto h = sample(point.x + ox, point.z + oz);
      if (h && std::abs(*h - *ground) / 0.5 > limit) return false;
    }
    return true;
  };
  camera.climbing = true;
  camera.climb_surface = "rope";
  camera.climb_slope_degrees = 90.0;
  camera.climb_refusal = "";
  camera.gait_speed_mps = 0.0;
  camera.bob_meters *= std::max(0.0, 1.0 - 5.0 * dt);
  camera.sway_meters *= std::max(0.0, 1.0 - 5.0 * dt);
  camera.roll_radians *= std::max(0.0, 1.0 - 5.0 * dt);
  if (t_new <= 1e-9) result.at_top = true;
  // Ground within the step-off distance below (the bottom, or a ledge the rope crosses): standing again.
  const bool at_bottom = t_new >= span - 1e-9;
  const bool ground_close = ground && point.y - *ground <= rules.step_off_meters + 1e-9;
  // A ledge the rope crosses: walkable ground underfoot for a metre of rope (a bump is less).
  if (result.moved_meters > 0 && t_new > 1.5 && walkable_ground()) camera.rope_ground_meters += result.moved_meters;
  else if (!ground_close || t_new <= 1.5) camera.rope_ground_meters = 0.0;
  if (result.moved_meters > 0 && t_new > 1.0 && ((at_bottom && ground_close) || camera.rope_ground_meters >= 1.0)) {
    camera.feet.y = *ground;
    camera.on_rope = false;
    camera.climbing = false;
    camera.grounded = true;
    camera.vertical_velocity_mps = 0.0;
    result.stepped_off = true;
    if (result.sliding) { camera.last_impact_mps = rules.slide_speed_mps; camera.landed = true; }
  }
  // Sliding off the end of a dangling rope: falls.
  if (result.sliding && at_bottom && camera.on_rope) release();
  return result;
}

bool step_off_rope_top(FirstPersonCamera& camera, const Rope& rope, const WalkEnvironment& environment, const RopeRules& rules) {
  validate_rope_rules(rules);
  if (rope.state != RopeState::Hung || !environment.standing_height) return false;
  math::Vec3 inward{rope.anchor.x - rope.end.x, 0.0, rope.anchor.z - rope.end.z};
  if (inward.length() < 1e-6) inward = horizontal_forward(camera.yaw) * -1.0;
  inward = inward.normalized();
  for (const double d : {0.6, 1.0, 1.5}) {
    const math::Vec3 p = rope.anchor + inward * d;
    const auto g = environment.standing_height(p.x, p.z);
    if (!g || std::abs(*g - rope.anchor.y) > rules.step_off_meters + 0.3) continue;
    camera.feet = {p.x, *g, p.z};
    camera.on_rope = false;
    camera.climbing = false;
    camera.grounded = true;
    camera.vertical_velocity_mps = 0.0;
    camera.velocity_mps = {};
    return true;
  }
  return false;
}

std::vector<Prop> rope_props(const Rope& rope, math::Vec3 hand, std::size_t max_props) {
  std::vector<Prop> out;
  if (max_props == 0) return out;
  switch (rope.state) {
    case RopeState::Coiled: break;
    case RopeState::Flying: {
      line_props(out, hand, rope.flight.position, 0.03, kRopeColor, max_props > 1 ? max_props - 1 : 0);
      out.push_back({frame_along(rope.flight.position, {0, 1, 0}, {0, 0, 1}), {0.22, 0.22, 0.08}, kRopeColor});
      break;
    }
    case RopeState::Laid:
      line_props(out, rope.anchor + math::Vec3{0, 0.03, 0}, rope.end + math::Vec3{0, 0.03, 0}, 0.03, kRopeColor, max_props);
      break;
    case RopeState::Hung: {
      // The tail lying to the lip, the hanging run, and what lies on past the foot.
      const std::size_t third = std::max<std::size_t>(1, max_props / 3);
      if (rope.branch_tree < 0 && (rope.anchor - rope.thrower_feet).length() > 0.05)
        line_props(out, rope.thrower_feet + math::Vec3{0, 0.03, 0}, rope.anchor + math::Vec3{0, 0.03, 0}, 0.03, kRopeColor, third);
      line_props(out, rope.anchor, rope.end, 0.035, kRopeColor, max_props > out.size() ? max_props - out.size() : 0);
      if (rope.branch_tree < 0 && (rope.landing - rope.end).length() > 0.05 && out.size() < max_props)
        line_props(out, rope.end + math::Vec3{0, 0.03, 0}, rope.landing + math::Vec3{0, 0.03, 0}, 0.03, kRopeColor, max_props - out.size());
      break;
    }
  }
  return out;
}

}  // namespace terraflux::graphics
