#include <terraflux/graphics/camera_rig.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;

double wrap_yaw(double yaw) {
  if (!std::isfinite(yaw)) throw std::invalid_argument("camera yaw must be finite");
  if (yaw > -kPi && yaw <= kPi) return yaw;  // untouched when already in range: transitions restore exactly
  yaw = std::fmod(yaw + kPi, 2.0 * kPi);
  if (yaw < 0) yaw += 2.0 * kPi;
  return yaw - kPi;  // (-pi, pi]
}

bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}  // namespace

const char* camera_mode_name(CameraMode mode) {
  return mode == CameraMode::FirstPerson ? "first-person" : "world";
}

math::Vec3 horizontal_forward(double yaw) { return {-std::sin(yaw), 0.0, -std::cos(yaw)}; }
math::Vec3 horizontal_right(double yaw) { return {std::cos(yaw), 0.0, -std::sin(yaw)}; }

math::Vec3 look_direction(double yaw, double pitch) {
  const double c = std::cos(pitch);
  return {-std::sin(yaw) * c, std::sin(pitch), -std::cos(yaw) * c};
}

math::Vec3 OrbitCamera::eye() const {
  return {target.x + distance * std::sin(yaw) * std::cos(pitch),
          target.y + distance * std::sin(pitch),
          target.z + distance * std::cos(yaw) * std::cos(pitch)};
}

math::Vec3 FirstPersonCamera::up() const {
  const auto right = horizontal_right(yaw);
  const double c = std::cos(roll_radians), s = std::sin(roll_radians);
  return {right.x * s, c, right.z * s};
}

void look(FirstPersonCamera& camera, double yaw_delta, double pitch_delta) {
  if (!std::isfinite(yaw_delta) || !std::isfinite(pitch_delta))
    throw std::invalid_argument("camera look deltas must be finite");
  camera.yaw = wrap_yaw(camera.yaw + yaw_delta);
  camera.pitch = std::clamp(camera.pitch + pitch_delta, -kFirstPersonMaxPitch, kFirstPersonMaxPitch);
}

void adjust_walk_speed(FirstPersonCamera& camera, double octaves) {
  if (!std::isfinite(octaves)) throw std::invalid_argument("walk speed change must be finite");
  camera.walk_speed_mps = std::clamp(camera.walk_speed_mps * std::exp2(octaves), kMinWalkSpeed, kMaxWalkSpeed);
}

namespace {

struct Support { double height = 0.0; bool on_obstacle = false; bool on_platform = false; double water_depth = 0.0; bool swimming = false; FloodSample water; };

std::optional<Support> support_at(const WalkEnvironment& env, double x, double z, double feet_y) {
  if (!env.standing_height) throw std::invalid_argument("support needs a standing height function");
  const auto ground = env.standing_height(x, z);
  if (!ground || !std::isfinite(*ground)) return std::nullopt;
  Support support;
  support.height = *ground;
  const double reach = env.body.radius * 0.5;
  for (const auto* field : env.obstacles) {
    if (!field) continue;
    field->for_each_near(x, z, reach + field->max_radius(), [&](const Obstacle& o) {
      if (o.top <= support.height || o.top > feet_y + env.body.step_height) return;  // below the support or out of reach
      if (std::hypot(x - o.x, z - o.z) <= o.radius + reach) { support.height = o.top; support.on_obstacle = true; }
    });
  }
  if (env.platforms) {
    // A road surface or a bridge deck within reach of the feet carries them;
    // a deck high over the ground is never a wall, a surface on the ground keeps the slope rule.
    const auto built = env.platforms(x, z, feet_y);
    if (built && std::isfinite(*built) && *built > support.height && *built <= feet_y + env.body.step_height + 1e-9) {
      if (*built - support.height > env.body.step_height) support.on_obstacle = true;
      support.height = *built;
      support.on_platform = true;
    }
  }
  if (env.water) {
    support.water = env.water(x, z, support.height);
    support.water_depth = std::max(0.0, support.water.surface_meters - support.height);
    if (support.water_depth >= env.water_rules.swim_depth_meters) {
      // Deep enough to float: the feet hang under the surface, the head above it.
      support.swimming = true;
      support.height = support.water.surface_meters - env.water_rules.swim_depth_meters;
      support.on_obstacle = true;  // never a wall: the surface is reached however steep the bed
      support.on_platform = false;
    }
  }
  return support;
}

}  // namespace

namespace {

bool within(double value, double lo, double hi) { return std::isfinite(value) && value >= lo && value <= hi; }

// The support's plan gradient by central differences; zero next to a hole.
math::Vec2 support_gradient(const WalkEnvironment& env, math::Vec2 p, double feet_y) {
  constexpr double kEpsilon = 0.25;
  const auto xp = support_height(env, p.x + kEpsilon, p.y, feet_y), xm = support_height(env, p.x - kEpsilon, p.y, feet_y);
  const auto zp = support_height(env, p.x, p.y + kEpsilon, feet_y), zm = support_height(env, p.x, p.y - kEpsilon, feet_y);
  if (!xp || !xm || !zp || !zm) return {};
  return {(*xp - *xm) / (2 * kEpsilon), (*zp - *zm) / (2 * kEpsilon)};
}

struct SubStep { math::Vec2 position; double support; bool climbed = false; double slope_degrees = 0.0; const char* surface = ""; };

// What stopped a step that was too steep to walk (for the camera's report).
struct StepRefusal { const char* reason = ""; };

// One grounded sub-step from `from` (feet at `height`) by `step`: slid out of
// obstacles, clamped to the bounds, rejected over holes and up walls — unless
// the wall can be climbed: then the step shrinks to the climbing speed along
// the face (`step_seconds` long) and is taken as a climb. The support it
// returns may lie below the feet (a ledge); the caller decides whether that
// is a step down or a fall.
std::optional<SubStep> attempt_step(const WalkEnvironment& env, math::Vec2 from, double height, math::Vec2 step,
                                    double max_slope_tangent, double step_seconds = 0.0, bool may_climb = false,
                                    StepRefusal* refusal = nullptr) {
  math::Vec2 q = from + step;
  q = push_out_of_obstacles(env.obstacles, q, height, env.body);
  q.x = std::clamp(q.x, env.lower.x, env.upper.x);
  q.y = std::clamp(q.y, env.lower.z, env.upper.z);
  const double run = std::hypot(q.x - from.x, q.y - from.y);
  if (run < 1e-9) return SubStep{from, height};
  const auto h = support_at(env, q.x, q.y, height);
  if (!h) return std::nullopt;  // a hole or outside the terrain
  // Ground rising faster than the slope limit is a wall; the top of a rock or
  // ledge within the step height is a step up, however abrupt.
  // Stepping up onto a built surface (a road's edge, a footpath) from the
  // ground beside it is a kerb within the step height, not a wall.
  const auto is_kerb = [&] {
    if (!h->on_platform || h->height - height > env.body.step_height) return false;
    const auto here = support_at(env, from.x, from.y, height);
    return here && (!here->on_platform || here->height > height + 1e-6);  // from the ground, or from below the surface (not yet settled onto it)
  };
  if (!h->on_obstacle && h->height - height > run * max_slope_tangent + 1e-9 && !is_kerb()) {
    if (!env.climb || step_seconds <= 0) return std::nullopt;
    const auto surface = env.climb(q.x, q.y);
    const double slope = std::atan2(h->height - height, run) * 180.0 / kPi;
    if (!surface.climbable || slope > surface.max_slope_degrees + 1e-9) {
      if (refusal) refusal->reason = surface.climbable ? "too steep" : (surface.name[0] ? surface.name : "unclimbable");
      return std::nullopt;
    }
    if (!may_climb) { if (refusal) refusal->reason = "exhausted"; return std::nullopt; }
    // Climb: the horizontal progress is the climbing speed along the face times the cosine of the slope.
    const double horizontal = env.climb_rules.climb_speed_mps * std::cos(slope * kPi / 180.0) * step_seconds;
    const math::Vec2 climb_step = step * std::min(1.0, horizontal / std::max(step.length(), 1e-9));
    math::Vec2 c = from + climb_step;
    c = push_out_of_obstacles(env.obstacles, c, height, env.body);
    c.x = std::clamp(c.x, env.lower.x, env.upper.x);
    c.y = std::clamp(c.y, env.lower.z, env.upper.z);
    const auto hc = support_at(env, c.x, c.y, height);
    if (!hc) return std::nullopt;
    return SubStep{c, hc->height, true, slope, surface.name};
  }
  return SubStep{q, h->height};
}

}  // namespace

void validate_walker_physics(const WalkerPhysics& p) {
  if (!within(p.gravity_mps2, 0.1, 100.0)) throw std::invalid_argument("gravity must be 0.1..100 m/s^2");
  if (!within(p.jump_height_meters, 0.0, 10.0)) throw std::invalid_argument("jump height must be 0..10 meters");
  if (!within(p.air_control, 0.0, 1.0)) throw std::invalid_argument("air control must be 0..1");
  if (!within(p.max_fall_speed_mps, 1.0, 200.0)) throw std::invalid_argument("terminal velocity must be 1..200 m/s");
  if (!within(p.landing_dip_per_mps, 0.0, 0.5)) throw std::invalid_argument("landing dip per m/s must be 0..0.5");
  if (!within(p.landing_dip_max_meters, 0.0, 10.0)) throw std::invalid_argument("landing dip maximum must be 0..10 meters");
  if (!within(p.landing_recovery_per_second, 0.1, 100.0)) throw std::invalid_argument("landing recovery must be 0.1..100 per second");
}

void validate_gait_style(const GaitStyle& g) {
  if (!within(g.step_length_base_meters, 0.1, 2.0)) throw std::invalid_argument("gait step length base must be 0.1..2 meters");
  if (!within(g.step_length_per_mps, 0.0, 1.0)) throw std::invalid_argument("gait step length per m/s must be 0..1");
  if (!within(g.max_cadence_hz, 0.5, 10.0)) throw std::invalid_argument("gait cadence limit must be 0.5..10 steps per second");
  if (!within(g.speed_pulse, 0.0, 0.5)) throw std::invalid_argument("gait speed pulse must be 0..0.5");
  if (!within(g.acceleration_mps2, 1.0, 200.0)) throw std::invalid_argument("gait acceleration must be 1..200 m/s^2");
  if (!within(g.stop_deceleration_mps2, 1.0, 400.0)) throw std::invalid_argument("gait stop deceleration must be 1..400 m/s^2");
  for (const double v : {g.bob_base_meters, g.bob_per_mps, g.bob_max_meters, g.sway_base_meters, g.sway_per_mps, g.sway_max_meters})
    if (!within(v, 0.0, 0.5)) throw std::invalid_argument("gait bob and sway amounts must be 0..0.5");
  if (!within(g.roll_degrees, 0.0, 10.0)) throw std::invalid_argument("gait roll must be 0..10 degrees");
  if (!within(g.footfall_dip_meters, 0.0, 0.2)) throw std::invalid_argument("gait footfall dip must be 0..0.2 meters");
  if (!within(g.footfall_recovery_per_second, 0.1, 100.0)) throw std::invalid_argument("gait footfall recovery must be 0.1..100 per second");
}

double gait_step_length(const GaitStyle& gait, double speed_mps) {
  validate_gait_style(gait);
  if (!std::isfinite(speed_mps) || speed_mps < 0) throw std::invalid_argument("gait speed must be finite and non-negative");
  return std::max(gait.step_length_base_meters + gait.step_length_per_mps * speed_mps, speed_mps / gait.max_cadence_hz);
}

void validate_water_rules(const WaterRules& r) {
  if (!within(r.swim_depth_meters, 0.3, 5)) throw std::invalid_argument("swim depth must be 0.3..5 meters");
  if (!within(r.wade_slowdown_per_meter, 0, 2)) throw std::invalid_argument("wade slowdown must be 0..2 per metre");
  if (!within(r.min_wade_factor, 0.05, 1)) throw std::invalid_argument("minimum wade factor must be 0.05..1");
  if (!within(r.swim_speed_mps, 0.1, 5)) throw std::invalid_argument("swim speed must be 0.1..5 m/s");
  if (!within(r.crawl_factor, 1, 4) || !within(r.stroke_seconds, 0.2, 10) || !within(r.stroke_pulse, 0, 0.9))
    throw std::invalid_argument("crawl factor 1..4, stroke 0.2..10 s and pulse 0..0.9");
  if (!within(r.breath_seconds, 1, 600) || !within(r.breath_recovery_seconds, 1, 600) || !within(r.dive_speed_factor, 0.1, 1))
    throw std::invalid_argument("breath 1..600 s, recovery 1..600 s and dive speed 0.1..1");
  if (!within(r.grip_depth_meters, 0, 2) || !within(r.grip_fade_meters, 0.05, 3)) throw std::invalid_argument("grip depth must be 0..2 and its fade 0.05..3 meters");
  if (!within(r.sweep_current_mps, 0.1, 30) || !within(r.sweep_depth_meters, 0, 5) || !within(r.sweep_stun_seconds, 0, 60))
    throw std::invalid_argument("sweep current 0.1..30 m/s, depth 0..5 m and stun 0..60 s");
}

void validate_crouch_rules(const CrouchRules& r) {
  if (!within(r.speed_factor, 0.1, 1) || !within(r.height_factor, 0.2, 1) || !within(r.ease_seconds, 0.01, 5))
    throw std::invalid_argument("crouch speed 0.1..1, height 0.2..1 and ease 0.01..5 s");
}
void validate_grip_rules(const GripRules& r) {
  if (!within(r.slide_slope_degrees, 0, 60) || !within(r.slip_speed_mps, 0.1, 30) || !within(r.slip_stun_seconds, 0, 60) || !within(r.slide_drag, 0, 10))
    throw std::invalid_argument("grip rules out of range");
}
void validate_heading_push(const HeadingPush& h) {
  if (!within(h.threshold_mps, 0, 60) || !within(h.turn_per_mps, 0, 1)) throw std::invalid_argument("heading push out of range");
}

void validate_climb_rules(const ClimbRules& r) {
  if (!within(r.climb_speed_mps, 0.05, 5)) throw std::invalid_argument("climb speed must be 0.05..5 m/s");
  if (!within(r.stamina_seconds, 1, 3600) || !within(r.recovery_seconds, 1, 3600)) throw std::invalid_argument("stamina and recovery must be 1..3600 seconds");
  if (!within(r.min_stamina, 0, 1)) throw std::invalid_argument("minimum stamina must be 0..1");
  if (!within(r.resume_stamina, r.min_stamina, 1)) throw std::invalid_argument("resume stamina must be between the minimum and 1");
}

void validate_wind_push(const WindPush& w) {
  if (!within(w.threshold_mps, 0, 60)) throw std::invalid_argument("wind push threshold must be 0..60 m/s");
  if (!within(w.push_factor, 0, 1)) throw std::invalid_argument("wind push factor must be 0..1");
  if (!within(w.max_drift_mps, 0, 30)) throw std::invalid_argument("wind drift cap must be 0..30 m/s");
  if (!within(w.air_drag, 0, 0.1)) throw std::invalid_argument("air drag must be 0..0.1 per metre");
}

math::Vec2 wind_drift(const WindPush& push, math::Vec2 wind) {
  validate_wind_push(push);
  if (!std::isfinite(wind.x) || !std::isfinite(wind.y)) throw std::invalid_argument("wind velocity must be finite");
  const double speed = wind.length();
  if (speed <= push.threshold_mps || speed <= 0) return {};
  const double over = speed - push.threshold_mps;
  const double drift = std::min(push.max_drift_mps, push.push_factor * over * over);
  return wind * (drift / speed);
}

const char* lightning_hazard_name(LightningHazard hazard) {
  switch (hazard) {
    case LightningHazard::None: return "none";
    case LightningHazard::Close: return "close";
    case LightningHazard::Dangerous: return "dangerous";
    case LightningHazard::Struck: return "struck";
  }
  throw std::invalid_argument("unknown lightning hazard");
}

LightningEffect lightning_effect(double distance_meters) {
  if (!std::isfinite(distance_meters) || distance_meters < 0) throw std::invalid_argument("strike distance must be finite and non-negative");
  LightningEffect e;
  if (distance_meters <= 15.0) { e.hazard = LightningHazard::Struck; e.knock_down_seconds = 4.0; e.crouch_meters = 1.2; e.shake_meters = 0.35; }
  else if (distance_meters <= 50.0) { e.hazard = LightningHazard::Dangerous; e.knock_down_seconds = 1.5; e.crouch_meters = 0.8; e.shake_meters = 0.18; }
  else if (distance_meters <= 200.0) { e.hazard = LightningHazard::Close; e.shake_meters = 0.06 * (1.0 - (distance_meters - 50.0) / 150.0) + 0.01; }
  return e;
}

void knock_down(FirstPersonCamera& camera, double seconds, double crouch_meters) {
  if (!within(seconds, 0, 600) || !within(crouch_meters, 0, 10)) throw std::invalid_argument("knock-down needs sane seconds and crouch");
  camera.stunned_seconds = std::max(camera.stunned_seconds, seconds);
  camera.stun_crouch_meters = std::max(camera.stun_crouch_meters, std::min(crouch_meters, camera.eye_height_meters - 0.2));
  camera.crouch_meters = std::max(camera.crouch_meters, camera.stun_crouch_meters);  // down at once
}

void shake(FirstPersonCamera& camera, double amplitude_meters) {
  if (!within(amplitude_meters, 0, 10)) throw std::invalid_argument("shake amplitude must be 0..10 meters");
  camera.shake_meters = std::max(camera.shake_meters, amplitude_meters);
}

double jump_takeoff_speed(const WalkerPhysics& physics) {
  validate_walker_physics(physics);
  return std::sqrt(2.0 * physics.gravity_mps2 * physics.jump_height_meters);
}

double jump_air_time(const WalkerPhysics& physics) {
  validate_walker_physics(physics);
  return 2.0 * std::sqrt(2.0 * physics.jump_height_meters / physics.gravity_mps2);
}

std::optional<double> support_height(const WalkEnvironment& env, double x, double z, double feet_y) {
  const auto support = support_at(env, x, z, feet_y);
  if (!support) return std::nullopt;
  return support->height;
}

double walk(FirstPersonCamera& camera, const WalkInput& given, double dt, const WalkEnvironment& env) {
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("walk time step must be finite and non-negative");
  if (!std::isfinite(given.forward) || !std::isfinite(given.strafe))
    throw std::invalid_argument("walk input must be finite");
  if (!env.standing_height) throw std::invalid_argument("walking needs a standing height function");
  validate_walker_body(env.body);
  validate_walker_physics(env.physics);
  validate_gait_style(env.gait);
  validate_wind_push(env.wind);
  validate_water_rules(env.water_rules);
  validate_climb_rules(env.climb_rules);
  validate_crouch_rules(env.crouch);
  validate_grip_rules(env.grip_rules);
  validate_heading_push(env.heading);
  if (!within(env.swim_stamina_seconds, 1, 3600)) throw std::invalid_argument("swim stamina must be 1..3600 seconds");
  if (!std::isfinite(env.wind_velocity_mps.x) || !std::isfinite(env.wind_velocity_mps.y)) throw std::invalid_argument("wind velocity must be finite");
  if (!std::isfinite(camera.stamina)) throw std::invalid_argument("stamina must be finite");
  camera.swept = false;
  camera.climbing = false;
  camera.climb_refusal = "";
  camera.slipped = false;
  camera.landed = false;
  bool climbed_this_call = false;
  // Crouching eases in and out; crouched, the body is shorter for what it passes under.
  camera.crouched = given.crouch && camera.grounded && !camera.swimming;
  {
    const double target = camera.crouched ? 1.0 : 0.0;
    const double rate = dt / env.crouch.ease_seconds;  // linear: fully down or up within ease_seconds
    if (camera.crouch_blend < target) camera.crouch_blend = std::min(target, camera.crouch_blend + rate);
    else camera.crouch_blend = std::max(target, camera.crouch_blend - rate);
  }
  WalkEnvironment env_here = env;  // the body as it is now
  env_here.body.height = env.body.height * (1.0 - camera.crouch_blend * (1.0 - env.crouch.height_factor));
  env_here.body.step_height = std::min(env.body.step_height, env_here.body.height);
  // A crosswind turns the walker downwind unless it steers against it.
  {
    const auto right = horizontal_right(camera.yaw);
    const double cross = env.wind_velocity_mps.x * right.x + env.wind_velocity_mps.y * right.z;
    const double over = std::max(0.0, std::abs(cross) - env.heading.threshold_mps);
    if (over > 0 && camera.grounded) camera.yaw = camera.yaw + (cross > 0 ? 1.0 : -1.0) * env.heading.turn_per_mps * over * dt;
  }
  if (!finite(camera.feet) || !std::isfinite(camera.vertical_velocity_mps) || !std::isfinite(camera.velocity_mps.x) ||
      !std::isfinite(camera.velocity_mps.y) || !std::isfinite(camera.crouch_meters) || !std::isfinite(camera.gait_speed_mps) ||
      !std::isfinite(camera.gait_phase))
    throw std::invalid_argument("first-person camera must be finite");
  // Knocked down: no walking or jumping until up again; the stun crouch lives in the landing dip.
  WalkInput input = given;
  if (!camera.swimming) {
    camera.diving = false; camera.crawling = false;
    camera.breath = std::min(1.0, camera.breath + dt / env.water_rules.breath_recovery_seconds);
    camera.dive_blend = std::max(0.0, camera.dive_blend - dt / 0.6);
  }
  if (camera.stunned_seconds > 0) {
    camera.stunned_seconds = std::max(0.0, camera.stunned_seconds - dt);
    camera.crouch_meters = std::max(camera.crouch_meters, camera.stun_crouch_meters);
    input = WalkInput{};
    if (camera.stunned_seconds == 0) camera.stun_crouch_meters = 0.0;
  }
  const math::Vec3 direction = horizontal_forward(camera.yaw) * input.forward + horizontal_right(camera.yaw) * input.strafe;
  const double length = direction.length();
  // The surface underfoot: a road hurries the walker, rubble slows it.
  camera.surface_speed_factor = 1.0;
  if (env.surface_speed) {
    const double factor = env.surface_speed(camera.feet.x, camera.feet.z);
    if (!std::isfinite(factor) || factor <= 0) throw std::invalid_argument("surface speed factor must be finite and positive");
    camera.surface_speed_factor = std::clamp(factor, 0.05, 5.0);
  }
  const double speed = std::clamp(camera.walk_speed_mps, kMinWalkSpeed, kMaxWalkSpeed) *
                       (input.run && !camera.crouched ? std::max(1.0, camera.run_multiplier) : 1.0) *
                       (1.0 - camera.crouch_blend * (1.0 - env.crouch.speed_factor)) * camera.surface_speed_factor;
  math::Vec2 drift = wind_drift(env.wind, env.wind_velocity_mps);
  // The ground's grip: on ice the walker slides down any slope and accelerates slowly.
  camera.grip = env.grip ? std::clamp(env.grip(camera.feet.x, camera.feet.z), 0.0, 1.0) : 1.0;
  if (camera.grounded && !camera.swimming) {
    const auto gradient = support_gradient(env_here, {camera.feet.x, camera.feet.z}, camera.feet.y);
    const double g = gradient.length();
    const double slope_deg = std::atan(g) * 180.0 / kPi;
    if (camera.grip < 0.999 && slope_deg > env.grip_rules.slide_slope_degrees && g > 1e-9) {
      const math::Vec2 downhill = gradient * (-1.0 / g);
      const double along = env.physics.gravity_mps2 * std::sin(std::atan(g)) * (1.0 - camera.grip);
      camera.slide_mps = camera.slide_mps + downhill * (along * dt);
    }
    // Friction bleeds the slide off: fully on dry ground, hardly on ice.
    const double drag = env.grip_rules.slide_drag * (0.05 + 0.95 * camera.grip) + 4.0 * camera.grip * camera.grip;
    camera.slide_mps = camera.slide_mps * std::max(0.0, 1.0 - drag * dt);
    if (camera.slide_mps.length() < 0.02) camera.slide_mps = {};
    if (camera.slide_mps.length() >= env.grip_rules.slip_speed_mps && camera.stunned_seconds <= 0 && camera.grip < 0.5) {
      knock_down(camera, env.grip_rules.slip_stun_seconds, 0.6);
      shake(camera, 0.06);
      camera.slipped = true;
      input = WalkInput{};
    }
    drift = drift + camera.slide_mps;
  } else camera.slide_mps = {};
  // Water at the feet: wading slows, the current pushes or carries, deep water floats.
  double wade_factor = 1.0;
  {
    const auto here = support_at(env_here, camera.feet.x, camera.feet.z, camera.feet.y);
    camera.water_depth_meters = here ? here->water_depth : 0.0;
    camera.swimming = here && here->swimming && camera.grounded;  // afloat once the feet have reached the water, not while falling toward it
    camera.on_platform = here && here->on_platform && camera.grounded;
    camera.current_mps = here ? here->water.current_mps : 0.0;
    if (here && here->water_depth > 0) {
      const auto& r = env.water_rules;
      if (camera.swimming) {
        // Strokes: Shift crawls (faster, tiring), Ctrl dives; the speed surges with each stroke.
        camera.crawling = given.run && !camera.diving;
        const double stroke_time = r.stroke_seconds / (camera.crawling ? r.crawl_factor : 1.0);
        const bool stroking = length > 0;
        if (stroking) {
          const double before = camera.swim_phase;
          camera.swim_phase += dt / stroke_time;
          if (camera.swim_phase >= 1.0) { camera.swim_phase -= std::floor(camera.swim_phase); ++camera.strokes; }
          (void)before;
        }
        const double surge = stroking ? 1.0 + r.stroke_pulse * std::sin(2.0 * kPi * camera.swim_phase) : 1.0;
        const double swim = r.swim_speed_mps * (camera.crawling ? r.crawl_factor : 1.0) * (camera.diving ? r.dive_speed_factor : 1.0) * surge;
        wade_factor = swim / std::max(speed, 1e-9);
        drift = drift + here->water.current * here->water.current_mps;
        // Diving: down while the breath lasts, up by itself when it is gone.
        if (given.crouch && camera.breath >= 0.5 && !camera.diving) camera.diving = true;  // a breath caught first
        if (!given.crouch) camera.diving = false;
        if (camera.diving) {
          camera.breath = std::max(0.0, camera.breath - dt / r.breath_seconds);
          if (camera.breath <= 0) camera.diving = false;
        } else camera.breath = std::min(1.0, camera.breath + dt / r.breath_recovery_seconds);
        const double target = camera.diving ? 1.0 : 0.0, rate = dt / 0.6;
        if (camera.dive_blend < target) camera.dive_blend = std::min(target, camera.dive_blend + rate);
        else camera.dive_blend = std::max(target, camera.dive_blend - rate);
        // The stroke moves the head: a small rise with each pull.
        camera.bob_meters = 0.04 * std::max(0.0, std::sin(2.0 * kPi * camera.swim_phase)) * (stroking ? 1.0 : 0.0);
      } else {
        wade_factor = std::max(r.min_wade_factor, 1.0 - r.wade_slowdown_per_meter * here->water_depth);
        const double grip = std::clamp((here->water_depth - r.grip_depth_meters) / r.grip_fade_meters, 0.0, 1.0);
        drift = drift + here->water.current * (here->water.current_mps * grip * grip * (3.0 - 2.0 * grip));
        if (camera.grounded && here->water.current_mps >= r.sweep_current_mps && here->water_depth >= r.sweep_depth_meters &&
            camera.stunned_seconds <= 0) {
          knock_down(camera, r.sweep_stun_seconds, 0.8);
          shake(camera, 0.08);
          camera.swept = true;
          input = WalkInput{};
        }
      }
    }
  }
  // Diagonals are not faster: the wanted velocity is the unit direction times the speed.
  const math::Vec2 wanted = length > 0 ? math::Vec2{direction.x / length * speed * wade_factor, direction.z / length * speed * wade_factor} : math::Vec2{};
  const double max_slope_tangent = std::tan(env.body.max_slope_degrees * kPi / 180.0);
  const double sub_length = std::clamp(env.body.radius * 0.5, 0.05, 0.5);
  constexpr double kAirStep = 1.0 / 120.0;
  math::Vec2 p{camera.feet.x, camera.feet.z};
  double y = camera.feet.y, walked = 0.0, remaining = dt;

  const auto& gait = env.gait;
  const auto rest_gait = [&] {
    camera.gait_speed_mps = 0.0;
    camera.gait_phase = 0.0;
    camera.braking_mps2 = camera.braking_step_length_meters = 0.0;
    camera.bob_meters = camera.sway_meters = camera.roll_radians = 0.0;
  };
  // The step length in use: frozen while a stop finishes the current step.
  const auto current_step_length = [&] {
    return camera.braking_mps2 > 0 ? camera.braking_step_length_meters : gait_step_length(gait, camera.gait_speed_mps);
  };
  // The head over the stance leg: rises mid-step, shifts and rolls toward the
  // stance foot, all fading out below one metre per second.
  const auto pose_head = [&] {
    const double v = camera.gait_speed_mps;
    const double fade = std::min(1.0, v);
    const double bob = std::min(gait.bob_max_meters, gait.bob_base_meters + gait.bob_per_mps * v);
    const double sway = std::min(gait.sway_max_meters, gait.sway_base_meters + gait.sway_per_mps * v);
    const double side = camera.stance_foot == 0 ? -1.0 : 1.0;
    camera.bob_meters = fade * bob * 0.5 * (1.0 - std::cos(2.0 * kPi * camera.gait_phase));
    camera.sway_meters = fade * sway * side * std::sin(kPi * camera.gait_phase);
    camera.roll_radians = fade * gait.roll_degrees * kPi / 180.0 * side * std::sin(kPi * camera.gait_phase);
  };

  if (camera.stamina <= env.climb_rules.min_stamina) camera.exhausted = true;
  else if (camera.stamina >= env.climb_rules.resume_stamina) camera.exhausted = false;
  const bool may_climb = !camera.exhausted && !camera.crouched;
  if (camera.swimming && camera.grounded)  // swimming tires, crawling more so
    camera.stamina = std::max(0.0, camera.stamina - dt * (camera.crawling ? env.water_rules.crawl_factor : 1.0) / env.swim_stamina_seconds);
  // Whatever held the feet has gone (a deck that collapsed, an obstacle that
  // moved): more than the step height above the support — the ground, a
  // deck, or the float height over deep water — the walker falls.
  if (camera.grounded) {
    const auto under = support_height(env_here, camera.feet.x, camera.feet.z, camera.feet.y);
    if (under && camera.feet.y - *under > env_here.body.step_height) {
      camera.grounded = false;
      camera.swimming = false;
      camera.vertical_velocity_mps = 0.0;
      camera.velocity_mps = gait.enabled ? camera.gait_direction * camera.gait_speed_mps : math::Vec2{};
      camera.air_time_seconds = 0.0;
      rest_gait();
    }
  }
  if (camera.grounded && input.jump && env.physics.jump_height_meters > 0 && !camera.swimming && !camera.crouched) {
    camera.grounded = false;
    camera.vertical_velocity_mps = jump_takeoff_speed(env.physics);
    // Running jumps carry the run: the gait's mean speed along its direction.
    camera.velocity_mps = gait.enabled ? camera.gait_direction * camera.gait_speed_mps : wanted;
    camera.air_time_seconds = 0.0;
    rest_gait();
  }

  while (remaining > 1e-12) {
    if (camera.grounded) {
      const double target = wanted.length();
      math::Vec2 step;
      double step_time = remaining;
      bool stopping_at_footfall = false;
      const double drift_speed = drift.length();
      if (!gait.enabled) {
        if (!(target > 0) && !(drift_speed > 0)) break;  // standing still in calm air: nothing moves
        const math::Vec2 total = wanted + drift;
        const double total_speed = std::max(total.length(), 1e-9);
        step_time = std::min(remaining, sub_length / total_speed);
        step = total * step_time;
      } else {
        // Speed control: accelerate toward the wanted speed; with nothing
        // wanted, brake so that the stop lands on a footfall (finishing the
        // step, or the next one when braking harder than allowed).
        const double dt_sub = std::min(remaining, 1.0 / 120.0);
        if (target > 0) {
          camera.gait_direction = wanted / target;
          camera.braking_mps2 = camera.braking_step_length_meters = 0.0;  // a new input cancels a stop
          if (camera.gait_speed_mps < target)
            camera.gait_speed_mps = std::min(target, camera.gait_speed_mps + gait.acceleration_mps2 * camera.grip * dt_sub);
          else camera.gait_speed_mps = std::max(target, camera.gait_speed_mps - gait.stop_deceleration_mps2 * std::min(1.0, 2.5 * camera.grip * camera.grip) * dt_sub);
        } else if (camera.gait_speed_mps > 0) {
          if (!(camera.braking_mps2 > 0)) {
            // Release: brake uniformly so that the speed reaches zero exactly
            // on a footfall (this step's, or a later one when that would take
            // more than the allowed braking), over the step length of the
            // moment, which the stop keeps.
            const double step_length = gait_step_length(gait, camera.gait_speed_mps);
            double distance = (1.0 - camera.gait_phase) * step_length;
            const double v = camera.gait_speed_mps;
            // Braking needs grip: on ice (0.1) about a metre per second squared.
            const double brake_cap = gait.stop_deceleration_mps2 * std::min(1.0, 2.5 * camera.grip * camera.grip);
            while (v * v / (2.0 * std::max(distance, 1e-6)) > std::max(brake_cap, 0.05)) distance += step_length;
            camera.braking_mps2 = v * v / (2.0 * std::max(distance, 1e-6));
            camera.braking_step_length_meters = step_length;
          }
          camera.gait_speed_mps = std::max(0.0, camera.gait_speed_mps - camera.braking_mps2 * dt_sub);
          stopping_at_footfall = true;
        }
        if (!(camera.gait_speed_mps > 0)) {
          if (stopping_at_footfall) { camera.stance_foot ^= 1; ++camera.footfalls; }  // the last foot plants
          rest_gait();
          if (!(drift_speed > 0)) break;
          // Standing in a gale: no steps, but the gust drags you along the ground.
          step_time = std::min(remaining, sub_length / drift_speed);
          step = drift * step_time;
          auto pushed = attempt_step(env_here, p, y, step, max_slope_tangent);
          if (!pushed) break;
          const double moved = std::hypot(pushed->position.x - p.x, pushed->position.y - p.y);
          p = pushed->position; y = y - pushed->support > env.body.step_height ? y : pushed->support;
          walked += moved; remaining -= step_time;
          if (moved < 1e-9) break;
          continue;
        }
        const double pulse = 1.0 - gait.speed_pulse * std::cos(2.0 * kPi * camera.gait_phase);
        const double instant = camera.gait_speed_mps * pulse;
        step_time = std::min(dt_sub, sub_length / std::max(instant + drift_speed, 1e-9));
        step = camera.gait_direction * (instant * step_time) + drift * step_time;
        if (stopping_at_footfall) {
          // Never overshoot the footfall the stop is aimed at (the one the
          // braking reaches zero at; earlier footfalls are passed braking).
          const double to_footfall = (1.0 - camera.gait_phase) * current_step_length();
          const double v = camera.gait_speed_mps;
          const bool last_footfall = v * v <= 2.0 * camera.braking_mps2 * to_footfall * 1.1 + 1e-9;
          const double len = step.length();
          if (last_footfall && len > to_footfall) step = step * (to_footfall / std::max(len, 1e-12));
          stopping_at_footfall = last_footfall;
        }
      }
      StepRefusal refusal;
      auto result = attempt_step(env_here, p, y, step, max_slope_tangent, step_time, may_climb && !camera.swimming, &refusal);
      if (!result) {
        // Blocked by a wall of ground: slide along its contour with what the
        // motion has in that direction.
        const auto gradient = support_gradient(env_here, p, y);
        const double g = gradient.length();
        if (g > 1e-9) {
          const math::Vec2 contour{-gradient.y / g, gradient.x / g};
          const math::Vec2 along = contour * contour.dot(step);
          if (along.length() > 1e-9) result = attempt_step(env_here, p, y, along, max_slope_tangent);
        }
        if (refusal.reason[0]) camera.climb_refusal = refusal.reason;
      }
      if (!result) break;
      const double moved = std::hypot(result->position.x - p.x, result->position.y - p.y);
      if (result->climbed) {
        camera.climbing = true;
        camera.climb_slope_degrees = result->slope_degrees;
        camera.climb_surface = result->surface;
        camera.stamina = std::max(0.0, camera.stamina - step_time / env.climb_rules.stamina_seconds);
        climbed_this_call = true;
      }
      p = result->position;
      walked += moved;
      remaining -= step_time;
      if (gait.enabled && moved > 0 && !camera.swimming) {
        // Advance the step; crossing the footfall swaps the stance foot and
        // jolts the head with the landing foot.
        camera.gait_phase += moved / current_step_length();
        if (camera.gait_phase >= 1.0 - 1e-9) {
          camera.gait_phase = stopping_at_footfall ? 0.0 : camera.gait_phase - std::floor(camera.gait_phase);
          camera.stance_foot ^= 1;
          ++camera.footfalls;
          camera.footfall_dip_meters += gait.footfall_dip_meters * std::clamp(camera.gait_speed_mps / 4.0, 0.25, 3.0);
          if (stopping_at_footfall) camera.gait_speed_mps = 0.0;  // the stop lands on this footfall
        }
        if (camera.gait_speed_mps > 0) pose_head(); else rest_gait();
      } else if (camera.swimming) {
        camera.sway_meters = camera.roll_radians = 0.0;  // no footfalls in the water (the stroke bob is set above)
      }
      if (y - result->support > env.body.step_height) {
        // A ledge: the feet leave the ground at their height and fall from here.
        camera.grounded = false;
        camera.vertical_velocity_mps = 0.0;
        camera.velocity_mps = gait.enabled ? camera.gait_direction * camera.gait_speed_mps : wanted;
        camera.air_time_seconds = 0.0;
        rest_gait();
      } else {
        y = result->support;
        if (moved < 1e-9) break;  // pinned against something: stop instead of spinning
      }
    } else {
      // Airborne: momentum with a little steering, gravity, walls and the landing.
      const double blend = std::min(1.0, env.physics.air_control * std::min(remaining, kAirStep));
      camera.velocity_mps = camera.velocity_mps + (wanted - camera.velocity_mps) * blend;
      {
        // Drag on the body in the air: the wind has you.
        const math::Vec2 relative = env.wind_velocity_mps - camera.velocity_mps;
        camera.velocity_mps = camera.velocity_mps + relative * (env.wind.air_drag * relative.length() * std::min(remaining, kAirStep));
      }
      const double hspeed = camera.velocity_mps.length();
      const double step_time = std::min({remaining, kAirStep, hspeed > 0 ? sub_length / hspeed : remaining});
      if (hspeed > 0) {
        math::Vec2 q = p + camera.velocity_mps * step_time;
        q = push_out_of_obstacles(env_here.obstacles, q, y, env_here.body);
        q.x = std::clamp(q.x, env.lower.x, env.upper.x);
        q.y = std::clamp(q.y, env.lower.z, env.upper.z);
        const auto h = support_height(env_here, q.x, q.y, y);
        if (!h || *h > y + env.body.step_height) camera.velocity_mps = {};  // a hole or a wall in the air: momentum dies
        else { walked += std::hypot(q.x - p.x, q.y - p.y); p = q; }
      }
      camera.vertical_velocity_mps = std::max(camera.vertical_velocity_mps - env.physics.gravity_mps2 * step_time,
                                              -env.physics.max_fall_speed_mps);
      y += camera.vertical_velocity_mps * step_time;
      camera.air_time_seconds += step_time;
      remaining -= step_time;
      const auto support = support_height(env_here, p.x, p.y, y);
      if (support && camera.vertical_velocity_mps <= 0 && y <= *support) {
        const double impact = -camera.vertical_velocity_mps;
        camera.last_impact_mps = impact;
        camera.landed = true;
        y = *support;
        camera.grounded = true;
        camera.vertical_velocity_mps = 0.0;
        camera.velocity_mps = {};
        camera.air_time_seconds = 0.0;
        camera.crouch_meters = std::min(env.physics.landing_dip_max_meters,
                                        camera.crouch_meters + impact * env.physics.landing_dip_per_mps);
        rest_gait();  // the next step starts from rest
      }
    }
  }
  // Resting restores the strength for climbing.
  if (!climbed_this_call && camera.grounded && !camera.swimming) camera.stamina = std::min(1.0, camera.stamina + dt / env.climb_rules.recovery_seconds);
  // The landing dip and the footfall jolt recover exponentially.
  camera.crouch_meters *= std::exp(-env.physics.landing_recovery_per_second * dt);
  if (camera.crouch_meters < 1e-4) camera.crouch_meters = 0.0;
  camera.footfall_dip_meters *= std::exp(-gait.footfall_recovery_per_second * dt);
  if (camera.footfall_dip_meters < 1e-4) camera.footfall_dip_meters = 0.0;
  // The trembling after a shock: a fast irregular jitter that dies away.
  camera.shake_meters *= std::exp(-4.0 * dt);
  if (camera.shake_meters < 1e-4) camera.shake_meters = 0.0;
  if (camera.shake_meters > 0) {
    camera.shake_phase += dt * 37.0;
    const auto right = horizontal_right(camera.yaw);
    const double a = camera.shake_meters;
    camera.shake_offset = right * (a * std::sin(camera.shake_phase) * std::cos(camera.shake_phase * 0.37)) +
                          math::Vec3{0, a * 0.7 * std::sin(camera.shake_phase * 1.31 + 1.0), 0};
  } else {
    camera.shake_offset = {};
    camera.shake_phase = 0.0;
  }
  camera.feet = {p.x, y, p.y};
  return walked;
}

double walk(FirstPersonCamera& camera, const WalkInput& input, double dt,
            const StandingHeightFn& standing_height, math::Vec3 lower, math::Vec3 upper) {
  WalkEnvironment env;
  env.standing_height = standing_height;
  env.lower = lower; env.upper = upper;
  env.body.max_slope_degrees = 89.0;
  env.gait.enabled = false;
  return walk(camera, input, dt, env);
}

bool settle(FirstPersonCamera& camera, const WalkEnvironment& env) {
  if (!env.standing_height) throw std::invalid_argument("settling needs a standing height function");
  validate_walker_body(env.body);
  if (!finite(camera.feet)) throw std::invalid_argument("first-person camera must be finite");
  math::Vec2 p = push_out_of_obstacles(env.obstacles, {camera.feet.x, camera.feet.z}, camera.feet.y, env.body);
  p.x = std::clamp(p.x, env.lower.x, env.upper.x);
  p.y = std::clamp(p.y, env.lower.z, env.upper.z);
  const auto h = support_height(env, p.x, p.y, camera.feet.y);
  if (!h) return false;
  camera.feet = {p.x, camera.grounded ? *h : camera.feet.y, p.y};
  return true;
}

bool place_on_ground(FirstPersonCamera& camera, double x, double z, const StandingHeightFn& standing_height,
                     math::Vec3 lower, math::Vec3 upper, double step, double max_radius) {
  if (!std::isfinite(x) || !std::isfinite(z)) throw std::invalid_argument("standing position must be finite");
  if (!standing_height) throw std::invalid_argument("placing the camera needs a standing height function");
  if (!(step > 0) || !std::isfinite(step) || !std::isfinite(max_radius) || max_radius < 0)
    throw std::invalid_argument("standing point search needs a positive step and a non-negative radius");
  x = std::clamp(x, lower.x, upper.x);
  z = std::clamp(z, lower.z, upper.z);
  const auto try_point = [&](double px, double pz) {
    if (px < lower.x || px > upper.x || pz < lower.z || pz > upper.z) return false;
    const auto height = standing_height(px, pz);
    if (!height || !std::isfinite(*height)) return false;
    camera.feet = {px, *height, pz};
    camera.grounded = true;
    camera.vertical_velocity_mps = 0.0;
    camera.velocity_mps = {};
    camera.air_time_seconds = 0.0;
    camera.gait_speed_mps = camera.gait_phase = 0.0;
    camera.bob_meters = camera.sway_meters = camera.roll_radians = camera.footfall_dip_meters = 0.0;
    return true;
  };
  if (try_point(x, z)) return true;
  // Rings of growing radius; the nearest ring that has ground wins, its
  // perimeter scanned on the lattice of `step` from the axes outward so the
  // closest point of the ring is preferred.
  for (double radius = step; radius <= max_radius + 1e-9; radius += step) {
    const int n = static_cast<int>(std::lround(radius / step));
    for (int i = 0; i <= n; ++i)
      for (const double offset : {i * step, -i * step}) {
        if (try_point(x + offset, z - radius) || try_point(x + offset, z + radius) ||
            try_point(x - radius, z + offset) || try_point(x + radius, z + offset))
          return true;
        if (i == 0) break;
      }
  }
  return false;
}

bool enter_first_person(FirstPersonCamera& person, const OrbitCamera& orbit, const StandingHeightFn& standing_height,
                        math::Vec3 lower, math::Vec3 upper, double search_step, double search_radius) {
  if (!finite(orbit.target) || !std::isfinite(orbit.yaw)) throw std::invalid_argument("orbit camera must be finite");
  FirstPersonCamera candidate = person;
  if (!place_on_ground(candidate, orbit.target.x, orbit.target.z, standing_height, lower, upper, search_step, search_radius))
    return false;
  candidate.yaw = wrap_yaw(orbit.yaw);
  candidate.pitch = 0.0;  // level with the horizon; the orbit looked down onto this point
  candidate.crouch_meters = 0.0;
  person = candidate;
  return true;
}

void enter_world_view(OrbitCamera& orbit, const FirstPersonCamera& person) {
  if (!finite(person.feet) || !std::isfinite(person.yaw)) throw std::invalid_argument("first-person camera must be finite");
  orbit.target = {person.feet.x, person.feet.y + kOrbitTargetHeight, person.feet.z};
  orbit.yaw = wrap_yaw(person.yaw);
}

}  // namespace terraflux::graphics
