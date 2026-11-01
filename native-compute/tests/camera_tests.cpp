#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/collision.hpp>

#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

using namespace terraflux::graphics;
using terraflux::math::Vec3;

namespace {
constexpr double kPi = 3.14159265358979323846;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template <class F> void rejects(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const std::invalid_argument&) { caught = true; }
  require(caught, message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) < tolerance; }
bool near(Vec3 a, Vec3 b, double tolerance = 1e-9) {
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

// A 1 km square whose ground rises 0.1 m per meter east, with a 100 m hole
// (a river) across z = 300..400 and a lake surface at 20 m on the west.
const Vec3 kLower{0, 0, 0}, kUpper{1000, 100, 1000};
std::optional<double> ground(double x, double z) {
  if (x < 0 || x > 1000 || z < 0 || z > 1000) return std::nullopt;
  if (z >= 300 && z <= 400) return std::nullopt;
  return std::max(0.1 * x, 20.0);
}

void test_directions() {
  require(near(horizontal_forward(0), {0, 0, -1}) && near(horizontal_right(0), {1, 0, 0}),
          "yaw zero looks north with east on the right");
  require(near(horizontal_forward(kPi / 2), {-1, 0, 0}) && near(horizontal_right(kPi / 2), {0, 0, -1}),
          "a quarter turn clockwise from above looks west");
  require(near(look_direction(0, 0), {0, 0, -1}) && near(look_direction(0, kPi / 2), {0, 1, 0}),
          "look direction tilts from the horizon to the zenith");
  for (double yaw : {0.0, 0.45, 2.0, -2.5})
    for (double pitch : {-1.0, 0.0, 0.7}) {
      const auto d = look_direction(yaw, pitch);
      require(near(d.length(), 1.0) && near(Vec3{d.x, 0, d.z}.normalized(), horizontal_forward(yaw)),
              "look direction is a unit vector above the horizontal forward");
    }
  OrbitCamera orbit;
  orbit.target = {100, 10, 200}; orbit.yaw = 0.45; orbit.pitch = 0.75; orbit.distance = 500;
  const auto eye = orbit.eye();
  require(near(eye, {100 + 500 * std::sin(0.45) * std::cos(0.75), 10 + 500 * std::sin(0.75), 200 + 500 * std::cos(0.45) * std::cos(0.75)}),
          "the orbit eye sits behind the target against its forward direction");
  require(near((orbit.target - eye).normalized().dot(look_direction(0.45, -0.75)), 1.0),
          "the orbit looks along its yaw, pitched down onto the target");
  require(std::string(camera_mode_name(CameraMode::World)) == "world" &&
          std::string(camera_mode_name(CameraMode::FirstPerson)) == "first-person", "mode names");
}

void test_look_and_speed() {
  FirstPersonCamera person;
  look(person, 0.3, 0.2);
  require(near(person.yaw, 0.3) && near(person.pitch, 0.2), "look turns yaw and pitch");
  look(person, 0, 3.0);
  require(near(person.pitch, kFirstPersonMaxPitch), "pitch is clamped to 85 degrees up");
  look(person, 0, -9.0);
  require(near(person.pitch, -kFirstPersonMaxPitch), "pitch is clamped to 85 degrees down");
  look(person, 2 * kPi, 0);
  require(near(person.yaw, 0.3), "a full turn wraps the yaw back");
  look(person, 3.5, 0);
  require(person.yaw > -kPi && person.yaw <= kPi && near(std::sin(person.yaw), std::sin(3.8)), "yaw wraps into (-pi, pi]");
  rejects([&] { look(person, std::nan(""), 0); }, "non-finite look deltas are rejected");
  person.walk_speed_mps = 8.0;
  adjust_walk_speed(person, 1.0);
  require(near(person.walk_speed_mps, 16.0), "one octave doubles the walking speed");
  adjust_walk_speed(person, 10.0);
  require(near(person.walk_speed_mps, kMaxWalkSpeed), "walking speed is capped");
  adjust_walk_speed(person, -20.0);
  require(near(person.walk_speed_mps, kMinWalkSpeed), "walking speed has a floor");
}

void test_walking() {
  FirstPersonCamera person;
  require(place_on_ground(person, 500, 100, ground, kLower, kUpper, 10, 100), "the centre of the field is ground");
  require(near(person.feet, {500, 50, 100}), "feet stand on the ground height");
  person.yaw = 0; person.walk_speed_mps = 8.0;
  double walked = walk(person, {1, 0, false}, 0.5, ground, kLower, kUpper);
  require(near(walked, 4.0) && near(person.feet, {500, 50, 96}), "W walks north at the walking speed");
  walked = walk(person, {0, 1, false}, 1.0, ground, kLower, kUpper);
  require(near(walked, 8.0) && near(person.feet, {508, 50.8, 96}), "D strafes east and the feet follow the rising ground");
  walked = walk(person, {1, 1, false}, 1.0, ground, kLower, kUpper);
  require(near(walked, 8.0), "diagonal movement is not faster");
  walked = walk(person, {-1, 0, true}, 1.0, ground, kLower, kUpper);
  require(near(walked, 24.0), "running multiplies the speed (three times)");
  require(near(walk(person, {0, 0, true}, 1.0, ground, kLower, kUpper), 0.0), "no input, no movement");
  require(near(walk(person, {1, 0, false}, 0.0, ground, kLower, kUpper), 0.0), "a zero time step does not move");
  require(near(person.eye(), person.feet + Vec3{0, 1.7, 0}), "the eye is 1.7 m above the feet");
  require(near(person.target() - person.eye(), look_direction(person.yaw, person.pitch)), "the target is one look direction ahead");

  // The river hole: the walker advances to its edge and no further.
  require(place_on_ground(person, 200, 295, ground, kLower, kUpper, 10, 100), "river bank");
  person.yaw = kPi;  // looking south (+z)
  const double to_the_edge = walk(person, {1, 0, false}, 1.0, ground, kLower, kUpper);
  require(to_the_edge < 5.0 + 1e-9 && to_the_edge > 4.5 && person.feet.z < 300 && near(person.feet.x, 200) && near(person.feet.y, 20),
          "a step onto a hole is rejected at the edge of the ground");
  require(near(walk(person, {1, 0, false}, 1.0, ground, kLower, kUpper), 0.0), "at the edge, no further");
  require(walk(person, {0, 1, false}, 1.0, ground, kLower, kUpper) > 0, "but walking along the bank works");

  // The edge: the plan position is clamped to the terrain bounds.
  require(place_on_ground(person, 995, 500, ground, kLower, kUpper, 10, 100), "near the east edge");
  person.yaw = -kPi / 2;  // looking east
  walked = walk(person, {1, 0, true}, 10.0, ground, kLower, kUpper);
  require(near(person.feet.x, 1000) && near(walked, 5.0), "walking stops at the terrain edge");

  // Placement over a hole snaps to the nearest ring with ground.
  require(place_on_ground(person, 600, 350, ground, kLower, kUpper, 10, 100) && (person.feet.z < 300 || person.feet.z > 400) &&
          near(std::abs(person.feet.z - 350), 60.0, 1e-9) && near(person.feet.x, 600), "a hole finds the nearest bank");
  require(!place_on_ground(person, 600, 350, ground, kLower, kUpper, 10, 40), "the search radius limits the snap");
  rejects([&] { place_on_ground(person, 600, 350, ground, kLower, kUpper, 0, 40); }, "a zero search step is rejected");
  rejects([&] { walk(person, {1, 0, false}, -1.0, ground, kLower, kUpper); }, "negative time steps are rejected");
  rejects([&] { walk(person, {1, 0, false}, 1.0, nullptr, kLower, kUpper); }, "walking needs a standing height function");
}

void test_transitions() {
  OrbitCamera orbit;
  orbit.target = {400, 60, 700}; orbit.yaw = 0.45; orbit.pitch = 0.75; orbit.distance = 900;
  const auto before = orbit;
  FirstPersonCamera person;
  require(enter_first_person(person, orbit, ground, kLower, kUpper, 10, 100), "entering first person under the orbit target");
  require(near(person.feet, {400, 40, 700}) && near(person.yaw, 0.45) && near(person.pitch, 0.0),
          "first person stands under the orbit target, level, looking the orbit's way");
  require((orbit.eye() - person.eye()).normalized().dot(horizontal_forward(0.45)) < -0.5,
          "the orbit eye was behind the new viewpoint");
  enter_world_view(orbit, person);
  require(orbit.target.x == before.target.x && orbit.target.z == before.target.z &&
          orbit.target.y == 40 + kOrbitTargetHeight && orbit.yaw == before.yaw &&
          orbit.pitch == before.pitch && orbit.distance == before.distance,
          "returning orbits the standing point with the previous pitch, distance and heading, exactly");

  // Walk, turn, and return: the world view follows.
  person.walk_speed_mps = 10;
  walk(person, {1, 0, false}, 3.0, ground, kLower, kUpper);
  look(person, 1.0, -0.5);
  enter_world_view(orbit, person);
  require(near(orbit.target, {person.feet.x, person.feet.y + kOrbitTargetHeight, person.feet.z}) && near(orbit.yaw, person.yaw + 0.0) &&
          orbit.pitch == before.pitch, "the orbit target follows the walker and keeps its pitch");

  // Over a hole the transition finds the nearest bank; with nothing within reach it is refused.
  orbit.target = {600, 0, 350};
  require(enter_first_person(person, orbit, ground, kLower, kUpper, 10, 100) && (person.feet.z < 300 || person.feet.z > 400),
          "entering over a river stands on its bank");
  const auto unchanged = person;
  require(!enter_first_person(person, orbit, ground, kLower, kUpper, 10, 30), "entering with no ground in reach is refused");
  require(near(person.feet, unchanged.feet) && person.yaw == unchanged.yaw, "a refused transition leaves the camera untouched");
  orbit.target = {std::nan(""), 0, 0};
  rejects([&] { enter_first_person(person, orbit, ground, kLower, kUpper, 10, 100); }, "non-finite orbit targets are rejected");
}
// Flat ground at 10 m with a 2 m ledge east of x = 600 (ground at 8 m beyond).
std::optional<double> ledge(double x, double z) {
  if (x < 0 || x > 1000 || z < 0 || z > 1000) return std::nullopt;
  return x > 600 ? 8.0 : 10.0;
}
Obstacle block(double x, double z, double radius, double bottom, double top) {
  Obstacle o; o.x = x; o.z = z; o.radius = radius; o.bottom = bottom; o.top = top; return o;
}
WalkEnvironment flat_env(const ObstacleField* field = nullptr, bool gait = false) {
  WalkEnvironment env;
  env.standing_height = ledge;
  env.lower = kLower; env.upper = kUpper;
  env.gait.enabled = gait;
  if (field) env.obstacles.push_back(field);
  return env;
}
// Steps the walker at 60 Hz until it lands again (or the time runs out),
// returning the apex height above the take-off and the air time.
struct Flight { double apex = 0, air_time = 0, landed_x = 0, landed_z = 0, dip = 0; bool landed = false; };
Flight fly(FirstPersonCamera& person, const WalkEnvironment& env, const WalkInput& in_air, double max_seconds = 5.0) {
  Flight f;
  const double start_y = 10.0;  // the take-off ground of every flight below
  f.apex = person.feet.y - start_y;
  for (double t = 0; t < max_seconds; t += 1.0 / 60.0) {
    const double airborne_so_far = person.air_time_seconds;  // the rig's own clock, reset on landing
    walk(person, in_air, 1.0 / 60.0, env);
    f.apex = std::max(f.apex, person.feet.y - start_y);
    if (person.grounded) { f.air_time = airborne_so_far + 1.0 / 60.0; f.landed = true; f.landed_x = person.feet.x; f.landed_z = person.feet.z; f.dip = person.crouch_meters; break; }
  }
  return f;
}

void test_gravity_and_jumping() {
  const auto env = flat_env();
  require(near(jump_takeoff_speed(env.physics), std::sqrt(2 * 9.81 * 1.0)) && near(jump_air_time(env.physics), 2 * std::sqrt(2 / 9.81)),
          "take-off speed and air time follow sqrt(2 g h)");
  rejects([] { WalkerPhysics p; p.gravity_mps2 = 0; validate_walker_physics(p); }, "zero gravity rejected");
  rejects([] { WalkerPhysics p; p.air_control = 2; validate_walker_physics(p); }, "air control above one rejected");

  // A standing jump: leaves the ground, peaks at the jump height, lands after the analytic air time with a dip.
  FirstPersonCamera person;
  person.feet = {500, 10, 500}; person.yaw = 0;
  walk(person, {0, 0, false, true}, 1.0 / 60.0, env);
  require(!person.grounded && person.vertical_velocity_mps > 0 && person.feet.y > 10, "the jump leaves the ground");
  require(near(walk(person, {0, 0, false, true}, 1.0 / 60.0, env), 0.0) && !person.grounded, "no double jump in the air");
  const auto standing = fly(person, env, {});
  require(standing.landed && near(standing.apex, 1.0, 0.04), "a standing jump peaks at the jump height");
  require(near(standing.air_time, jump_air_time(env.physics), 1.0 / 60.0 + 1e-9), "and lands after the Earth-gravity air time");
  require(near(person.feet.y, 10) && person.grounded && near(person.feet.x, 500) && near(person.feet.z, 500), "landing restores the feet to the ground where it took off");
  require(standing.dip > 0.2 && standing.dip < 0.5 && person.eye().y < 11.7, "the landing dips the eye");
  for (int i = 0; i < 120; ++i) walk(person, {}, 1.0 / 60.0, env);
  require(person.crouch_meters == 0 && near(person.eye().y, 11.7), "the dip recovers within two seconds");

  // Standing still on the ground nothing moves and nothing drifts.
  const auto before = person.feet;
  require(near(walk(person, {}, 0.5, env), 0.0) && near(person.feet, before), "standing still is exactly still");

  // A running jump carries the run: about speed x air time further east.
  person.feet = {500, 10, 500}; person.yaw = -kPi / 2; person.walk_speed_mps = 8;
  walk(person, {1, 0, false}, 0.1, env);
  walk(person, {1, 0, false, true}, 1.0 / 60.0, env);
  const auto running = fly(person, env, {});
  require(running.landed && running.landed_x > 500.8 + 8 * running.air_time * 0.85 && running.landed_x < 500.8 + 8 * running.air_time * 1.05,
          "a running jump keeps its momentum through the air");
  // Air control is limited: releasing the keys mid-air does not stop you, reversing them barely slows you.
  person.feet = {500, 10, 500}; person.yaw = -kPi / 2;
  walk(person, {1, 0, false, true}, 1.0 / 60.0, env);
  const auto reversed = fly(person, env, {-1, 0, false});
  require(reversed.landed_x > 500 + 8 * reversed.air_time * 0.5, "reversing in the air only trims the jump");

  // Walking off a ledge: the feet leave the ground at its height and fall onto the lower ground.
  person.feet = {599, 10, 500}; person.yaw = -kPi / 2;
  walk(person, {1, 0, false}, 0.2, env);
  require(!person.grounded && person.feet.x > 600 && person.feet.y > 8 && person.feet.y <= 10, "stepping over the edge starts a fall");
  const auto fall = fly(person, env, {1, 0, false});
  require(fall.landed && near(person.feet.y, 8) && near(fall.air_time, std::sqrt(2 * 2 / 9.81), 0.1), "the fall lands on the lower ground after sqrt(2 h / g)");
  // A step of 0.4 m down is walked, not fallen.
  WalkEnvironment step_env = env;
  step_env.standing_height = [](double x, double z) -> std::optional<double> { if (x < 0 || x > 1000 || z < 0 || z > 1000) return std::nullopt; return x > 600 ? 9.6 : 10.0; };
  person.feet = {599, 10, 500}; person.grounded = true; person.vertical_velocity_mps = 0;
  walk(person, {1, 0, false}, 0.3, step_env);
  require(person.grounded && near(person.feet.y, 9.6), "a small drop is a step down");

  // Jumping over a 1.2 m rock that blocks walking, and onto a wide one.
  const ObstacleField rock({block(503, 500, 0.4, 10, 11.2)});
  const auto rocky = flat_env(&rock);
  person.feet = {500, 10, 500}; person.yaw = -kPi / 2; person.grounded = true;
  walk(person, {1, 0, false}, 1.0, rocky);
  require(near(person.feet.x, 503 - 0.75, 1e-6), "on the ground the rock is a wall");
  person.feet = {501.5, 10, 500};
  walk(person, {1, 0, false, true}, 1.0 / 60.0, rocky);
  const auto over = fly(person, rocky, {1, 0, false});
  require(over.landed && person.feet.x > 503.4 + 0.35 && near(person.feet.y, 10), "a running jump clears the rock");
  const ObstacleField table({block(505, 500, 1.5, 10, 11.2)});
  const auto tabled = flat_env(&table);
  person.feet = {502.5, 10, 500}; person.grounded = true;
  walk(person, {1, 0, false, true}, 1.0 / 60.0, tabled);
  const auto onto = fly(person, tabled, {1, 0, false});
  require(onto.landed && near(person.feet.y, 11.2) && std::hypot(person.feet.x - 505, person.feet.z - 500) < 1.5 + 0.175,
          "a jump onto a wide rock lands on its top");
  walk(person, {1, 0, true}, 0.3, tabled);
  require(!person.grounded || person.feet.y < 11.2, "running off the rock drops off it");
  fly(person, tabled, {});
  require(person.grounded && near(person.feet.y, 10), "and lands on the ground beside it");
  // The support is reported for the rig's callers too.
  require(near(*support_height(tabled, 505, 500, 11.0), 11.2) && near(*support_height(tabled, 505, 500, 10.0), 10.0) &&
          !support_height(tabled, -5, 500, 10.0), "support: the rock top within reach, the ground when too high, nothing outside");

  // Lower gravity: the same jump flies higher and longer.
  WalkEnvironment moon = env; moon.physics.gravity_mps2 = 1.62;
  person.feet = {500, 10, 500}; person.grounded = true; person.crouch_meters = 0;
  walk(person, {0, 0, false, true}, 1.0 / 60.0, moon);
  const auto lunar = fly(person, moon, {}, 10.0);
  require(lunar.landed && near(lunar.apex, 1.0, 0.04) && near(lunar.air_time, jump_air_time(moon.physics), 1.0 / 60.0 + 1e-9), "lower gravity lengthens the air time for the same height");
  WalkEnvironment grounded_only = env; grounded_only.physics.jump_height_meters = 0;
  walk(person, {0, 0, false, true}, 1.0 / 60.0, grounded_only);
  require(person.grounded, "a zero jump height disables jumping");

  // settle keeps an airborne walker in the air.
  person.feet = {500, 12, 500}; person.grounded = false; person.vertical_velocity_mps = 0;
  require(settle(person, env) && near(person.feet.y, 12), "settling does not pull a jumper down");
  person.grounded = true;
  require(settle(person, env) && near(person.feet.y, 10), "settling a grounded walker re-samples the support");
}
void test_gait() {
  const auto env = flat_env(nullptr, true);
  const auto& gait = env.gait;
  rejects([] { GaitStyle g; g.speed_pulse = 0.9; validate_gait_style(g); }, "a speed pulse above a half is rejected");
  rejects([] { GaitStyle g; g.max_cadence_hz = 0; validate_gait_style(g); }, "a zero cadence limit is rejected");
  require(near(gait_step_length(gait, 1.4), 0.45 + 0.22 * 1.4) && near(gait_step_length(gait, 8.0), 0.45 + 0.22 * 8.0) &&
          near(gait_step_length(gait, 32.0), std::max(0.45 + 0.22 * 32.0, 32.0 / 4.5)) &&
          gait_step_length(gait, 60.0) >= 60.0 / 4.5 - 1e-9, "step length grows with speed and the cadence limit caps the rate");
  constexpr double kTick = 1.0 / 60.0;

  // Starting: the first steps accelerate from rest; after a second the mean speed is the walking speed.
  FirstPersonCamera person;
  person.feet = {500, 10, 500}; person.yaw = -kPi / 2; person.walk_speed_mps = 8;
  double first_tenth = 0;
  for (int i = 0; i < 6; ++i) first_tenth += walk(person, {1, 0, false}, kTick, env);
  require(first_tenth > 0 && first_tenth < 0.8 * 0.5 && person.gait_speed_mps < 8, "the walker accelerates from rest instead of starting at full speed");
  for (int i = 6; i < 60; ++i) walk(person, {1, 0, false}, kTick, env);
  require(near(person.gait_speed_mps, 8.0), "the walking speed is reached within a second");
  const double x_at_1s = person.feet.x;
  double slowest = 1e9, fastest = 0;
  std::uint64_t footfalls_at_1s = person.footfalls;
  double max_bob = 0, max_sway = 0, min_sway = 0, max_roll = 0;
  int sway_sign_changes = 0; double last_sway = 0;
  for (int i = 0; i < 60; ++i) {
    const double d = walk(person, {1, 0, false}, kTick, env);
    slowest = std::min(slowest, d / kTick); fastest = std::max(fastest, d / kTick);
    max_bob = std::max(max_bob, person.bob_meters);
    max_sway = std::max(max_sway, person.sway_meters); min_sway = std::min(min_sway, person.sway_meters);
    max_roll = std::max(max_roll, std::abs(person.roll_radians));
    if (last_sway * person.sway_meters < 0) ++sway_sign_changes;
    if (person.sway_meters != 0) last_sway = person.sway_meters;
  }
  const double second = person.feet.x - x_at_1s;
  require(second > 8.0 * 0.95 && second < 8.0 * 1.05, "over a whole second the walker covers the walking speed");
  require(slowest < 8.0 * (1 - gait.speed_pulse * 0.6) && fastest > 8.0 * (1 + gait.speed_pulse * 0.6),
          "within a step the speed pulses: slow at the footfall, fast at push-off");
  const auto steps = person.footfalls - footfalls_at_1s;
  const double expected_steps = 8.0 / gait_step_length(gait, 8.0);
  require(steps >= std::floor(expected_steps) - 1 && steps <= std::ceil(expected_steps) + 1, "footfalls follow the step length");
  require(max_bob > 0.5 * std::min(gait.bob_max_meters, gait.bob_base_meters + gait.bob_per_mps * 8) && max_bob <= gait.bob_max_meters + 1e-9,
          "the head rises over the stance leg");
  require(max_sway > 0 && min_sway < 0 && sway_sign_changes >= 2, "the head sways to alternate sides as the feet alternate");
  require(max_roll > 0 && max_roll <= gait.roll_degrees * kPi / 180 + 1e-9, "and rolls toward the stance foot");
  require(near(person.feet.z, 500) && near((person.eye() - person.feet).dot(horizontal_right(person.yaw)), person.sway_meters) &&
          near(person.eye().y - person.feet.y, 1.7 + person.bob_meters - person.footfall_dip_meters), "the sway and bob are in the eye, not in the feet");
  const auto up = person.up();
  require(near(up.length(), 1.0) && near(up.y, std::cos(person.roll_radians)), "the up vector carries the roll");

  // Stopping: the step is finished, the walker halts on a footfall, and the head is exactly at rest.
  const auto release_x = person.feet.x;
  const auto release_footfalls = person.footfalls;
  int ticks = 0;
  while (person.gait_speed_mps > 0 && ticks < 300) { walk(person, {}, kTick, env); ++ticks; }
  require(person.gait_speed_mps == 0 && person.gait_phase == 0 && person.footfalls >= release_footfalls + 1,
          "releasing the keys finishes the step and stops on a footfall");
  require(person.feet.x - release_x > 0.3 && person.feet.x - release_x <= 2 * gait_step_length(gait, 8.0) + 1e-9,
          "the stop takes the rest of the step, at most two");
  require(person.bob_meters == 0 && person.sway_meters == 0 && person.roll_radians == 0, "standing, the head is exactly at rest");
  for (int i = 0; i < 120; ++i) walk(person, {}, kTick, env);
  require(person.footfall_dip_meters == 0 && near(person.eye(), person.feet + Vec3{0, 1.7, 0}) && near(person.up(), {0, 1, 0}),
          "the footfall jolt recovers and the eye returns to eye height exactly");
  const auto resting = person.feet;
  require(near(walk(person, {}, 0.5, env), 0.0) && near(person.feet, resting), "standing still is exactly still");

  // A footfall jolts the head; the jolt is larger when running.
  person.footfall_dip_meters = 0;
  double walk_jolt = 0;
  for (int i = 0; i < 120; ++i) { walk(person, {1, 0, false}, kTick, env); walk_jolt = std::max(walk_jolt, person.footfall_dip_meters); }
  double run_jolt = 0;
  for (int i = 0; i < 90; ++i) { walk(person, {1, 0, true}, kTick, env); run_jolt = std::max(run_jolt, person.footfall_dip_meters); }  // short of the ledge at x = 600
  require(walk_jolt > 0 && run_jolt > walk_jolt, "each landing foot jolts the head, harder at a run");
  require(person.gait_speed_mps > 8.0 && gait_step_length(gait, person.gait_speed_mps) >= person.gait_speed_mps / gait.max_cadence_hz - 1e-9,
          "running lengthens the steps within the cadence limit");

  // A jump from a walk carries the gait's mean speed, and landing starts the gait from rest.
  person.feet = {500, 10, 500}; person.grounded = true; person.gait_speed_mps = 0; person.gait_phase = 0;
  for (int i = 0; i < 60; ++i) walk(person, {1, 0, false}, kTick, env);
  walk(person, {1, 0, false, true}, kTick, env);
  require(!person.grounded && near(person.velocity_mps.x, 8.0, 0.05) && person.bob_meters == 0 && person.sway_meters == 0,
          "the jump carries the walking speed and the head stops bobbing in the air");
  while (!person.grounded) walk(person, {1, 0, false}, kTick, env);
  require(person.gait_speed_mps < 8.0, "after landing the walk starts again from rest");

  // Without the gait the walker glides at constant speed from the first tick.
  auto glide = env; glide.gait.enabled = false;
  person.feet = {500, 10, 500}; person.grounded = true; person.gait_speed_mps = 0;
  require(near(walk(person, {1, 0, false}, kTick, glide), 8.0 * kTick) && person.bob_meters == 0, "disabled, the walker glides");
}
void test_storms() {
  constexpr double kTick = 1.0 / 60.0;
  rejects([] { WindPush w; w.push_factor = 2; validate_wind_push(w); }, "a push factor above one is rejected");
  WindPush push;
  require(wind_drift(push, {5, 0}).length() == 0 && wind_drift(push, {12, 0}).length() == 0, "below the threshold you brace and do not move");
  const auto gale = wind_drift(push, {30, 0});
  require(near(gale.x, std::min(4.0, 0.02 * 18 * 18)) && gale.y == 0 && wind_drift(push, {0, 20}).y > 0, "above it the drift grows with the square, downwind, capped");
  require(wind_drift(push, {60, 0}).x == 4.0, "the drift is capped");

  // Standing in a gale you are dragged downwind; walking into it is slower than with it.
  auto env = flat_env(nullptr, true);
  env.wind_velocity_mps = {30, 0};
  FirstPersonCamera person;
  person.feet = {500, 10, 500}; person.yaw = 0; person.walk_speed_mps = 2.5;
  for (int i = 0; i < 60; ++i) walk(person, {}, kTick, env);
  require(person.feet.x > 500 + 3.0 && near(person.feet.z, 500) && person.gait_speed_mps == 0 && person.footfalls == 0,
          "a gale drags the standing walker downwind without steps");
  auto calm = env; calm.wind_velocity_mps = {};
  person.feet = {500, 10, 500};
  for (int i = 0; i < 60; ++i) walk(person, {}, kTick, calm);
  require(near(person.feet.x, 500), "calm air leaves the walker where it stands");
  person.feet = {500, 10, 500}; person.yaw = -kPi / 2;  // east, with the wind
  for (int i = 0; i < 120; ++i) walk(person, {1, 0, false}, kTick, env);
  const double with_wind = person.feet.x - 500;
  person.feet = {500, 10, 500}; person.yaw = kPi / 2; person.gait_speed_mps = 0; // west, into it
  for (int i = 0; i < 120; ++i) walk(person, {1, 0, false}, kTick, env);
  const double into_wind = 500 - person.feet.x;
  require(with_wind > into_wind + 5.0 && into_wind < 2.5 * 2, "the gale hurries you with it and holds you back against it");
  // In the air the wind takes you further.
  auto still = flat_env(); auto windy = flat_env(); windy.wind_velocity_mps = {25, 0};
  FirstPersonCamera a, b;
  a.feet = b.feet = {500, 10, 500}; a.yaw = b.yaw = -kPi / 2;
  walk(a, {0, 0, false, true}, kTick, still); walk(b, {0, 0, false, true}, kTick, windy);
  while (!a.grounded) walk(a, {}, kTick, still);
  while (!b.grounded) walk(b, {}, kTick, windy);
  require(b.feet.x > a.feet.x + 0.2, "a jump in a gale lands downwind");

  // Lightning: what a strike at a distance does.
  require(lightning_effect(500).hazard == LightningHazard::None && lightning_effect(120).hazard == LightningHazard::Close &&
          lightning_effect(40).hazard == LightningHazard::Dangerous && lightning_effect(5).hazard == LightningHazard::Struck,
          "hazard levels by distance");
  require(lightning_effect(5).knock_down_seconds > lightning_effect(40).knock_down_seconds && lightning_effect(120).knock_down_seconds == 0 &&
          lightning_effect(5).shake_meters > lightning_effect(40).shake_meters && lightning_effect(40).shake_meters > lightning_effect(120).shake_meters &&
          lightning_effect(120).shake_meters > lightning_effect(199).shake_meters && lightning_effect(199).shake_meters > 0,
          "closer strikes knock you down longer and shake harder");
  require(std::string(lightning_hazard_name(LightningHazard::Struck)) == "struck", "hazard names");
  rejects([] { (void)lightning_effect(-1); }, "a negative distance is rejected");
  // Knocked down: no movement or jumping until up again, the eye down, then recovering; the view shakes and settles.
  person.feet = {500, 10, 500}; person.yaw = -kPi / 2; person.gait_speed_mps = 0; person.crouch_meters = 0;
  const auto hit = lightning_effect(5);
  knock_down(person, hit.knock_down_seconds, hit.crouch_meters);
  shake(person, hit.shake_meters);
  require(person.stunned_seconds == 4.0 && person.shake_meters == 0.35, "knocked down and shaking");
  const auto down_at = person.feet;
  bool shook = false; double lowest_eye = 1e9;
  for (int i = 0; i < 60; ++i) {
    walk(person, {1, 0, false, true}, kTick, calm);
    if (person.shake_offset.length() > 0.01) shook = true;
    lowest_eye = std::min(lowest_eye, person.eye().y - person.feet.y);
  }
  require(near(person.feet.x, down_at.x) && person.grounded && person.stunned_seconds > 2.9, "while down, walking and jumping do nothing");
  require(shook && lowest_eye < 1.7 - 1.0, "the eye is thrown down and the view trembles");
  for (int i = 0; i < 60 * 5; ++i) walk(person, {}, kTick, calm);
  require(person.stunned_seconds == 0 && person.shake_meters == 0 && person.shake_offset.length() == 0 && near(person.eye().y, 11.7),
          "up again with a steady view");
  for (int i = 0; i < 30; ++i) walk(person, {1, 0, false}, kTick, calm);
  require(person.feet.x > down_at.x + 0.5, "and able to walk");
  rejects([&] { knock_down(person, -1, 0.5); }, "negative knock-down rejected");
  rejects([&] { shake(person, 20); }, "absurd shake rejected");
}
// A ramp of the given slope east of x = 500 on ground at 10 m.
WalkEnvironment ramp_env(double degrees) {
  WalkEnvironment env;
  env.standing_height = [degrees](double x, double z) -> std::optional<double> {
    if (x < 0 || x > 1000 || z < 0 || z > 1000) return std::nullopt;
    return 10.0 + std::max(0.0, x - 500.0) * std::tan(degrees * kPi / 180.0);
  };
  env.lower = kLower; env.upper = kUpper;
  env.gait.enabled = false;
  return env;
}
ClimbSurface surface(bool climbable, double max_slope, const char* name) { ClimbSurface c; c.climbable = climbable; c.max_slope_degrees = max_slope; c.name = name; return c; }

void test_climbing() {
  constexpr double kTick = 1.0 / 60.0;
  rejects([] { ClimbRules r; r.climb_speed_mps = 0; validate_climb_rules(r); }, "a zero climb speed is rejected");
  // Without a climb function a 60-degree face is a wall; on dry rock it is climbed, slowly.
  auto wall = ramp_env(60);
  FirstPersonCamera person;
  person.feet = {499, 10, 500}; person.yaw = -kPi / 2; person.walk_speed_mps = 2.5;
  for (int i = 0; i < 120; ++i) walk(person, {1, 0, false}, kTick, wall);
  require(person.feet.x < 500.3 && !person.climbing && person.stamina == 1.0, "a 60-degree face is a wall to a walker");
  auto rock = ramp_env(60);
  rock.climb = [](double, double) { return surface(true, 75.0, "rock"); };
  person.feet = {499, 10, 500};
  bool climbed = false; double max_rate = 0;
  double x_at_start_of_climb = 0; bool started = false;
  for (int i = 0; i < 60 * 5; ++i) {
    const double x0 = person.feet.x;
    walk(person, {1, 0, false}, kTick, rock);
    if (person.climbing) { climbed = true; if (!started) { started = true; x_at_start_of_climb = x0; } max_rate = std::max(max_rate, (person.feet.x - x0) / kTick); }
  }
  require(climbed && person.feet.x > 500.8 && person.feet.y > 11.0, "on rock the face is climbed");
  require(std::string(person.climb_surface) == "rock" && near(person.climb_slope_degrees, 60.0, 0.5), "the climb knows its surface and slope");
  const double expected_horizontal = rock.climb_rules.climb_speed_mps * std::cos(60.0 * kPi / 180.0);
  require(max_rate < expected_horizontal * 1.2 && max_rate > expected_horizontal * 0.5, "climbing progresses at the climb speed along the face");
  require(person.stamina < 1.0 && person.stamina > 0.8, "climbing spends stamina");
  (void)x_at_start_of_climb;
  // Too steep for the surface: refused with the reason; an unclimbable surface (a waterfall) likewise.
  auto overhang = ramp_env(80);
  overhang.climb = [](double, double) { return surface(true, 75.0, "rock"); };
  person.feet = {499, 10, 500}; person.stamina = 1.0;
  for (int i = 0; i < 120; ++i) walk(person, {1, 0, false}, kTick, overhang);
  require(person.feet.x < 500.3 && !person.climbing && std::string(person.climb_refusal) == "too steep", "an 80-degree face is too steep even for rock");
  auto falls = ramp_env(55);
  falls.climb = [](double, double) { return surface(false, 0.0, "waterfall"); };
  person.feet = {499, 10, 500};
  for (int i = 0; i < 120; ++i) walk(person, {1, 0, false}, kTick, falls);
  require(person.feet.x < 500.3 && std::string(person.climb_refusal) == "waterfall", "a waterfall cannot be climbed, and says so");
  // Stamina: forty seconds of climbing exhaust the walker, who then cannot ascend; resting restores it; descending is always possible.
  person.feet = {499, 10, 500}; person.stamina = 1.0; person.exhausted = false;
  int ticks = 0;
  while (!person.exhausted && ticks < 60 * 60) { walk(person, {1, 0, false}, kTick, rock); ++ticks; }
  require(person.exhausted && ticks > 60 * 30 && ticks < 60 * 45, "about forty seconds of climbing exhaust the walker");
  const double stuck_x = person.feet.x;
  require(stuck_x > 505.0 && stuck_x < 520.0, "it got that far up the face");
  for (int i = 0; i < 60 * 3; ++i) walk(person, {1, 0, false}, kTick, rock);
  require(near(person.feet.x, stuck_x) && std::string(person.climb_refusal) == "exhausted" && !person.climbing && person.exhausted,
          "exhausted, pressing on does nothing: the walker hangs where it is");
  person.yaw = kPi / 2;  // west: down the face
  for (int i = 0; i < 60; ++i) walk(person, {1, 0, false}, kTick, rock);
  require(person.feet.x < stuck_x - 0.5, "exhausted, it can still climb down");
  person.yaw = -kPi / 2;
  for (int i = 0; i < 60 * 25; ++i) walk(person, {}, kTick, rock);
  require(person.stamina > 0.9 && !person.exhausted, "resting restores the strength");
  const double rested_x = person.feet.x;
  for (int i = 0; i < 60; ++i) walk(person, {1, 0, false}, kTick, rock);
  require(person.feet.x > rested_x + 0.1 && person.climbing, "and the climb goes on");
  // Soil allows less than rock; snow less still; ice nothing.
  for (const auto& [max_slope, name, ok] : {std::tuple{55.0, "soil", false}, std::tuple{62.0, "soil", true}, std::tuple{40.0, "snow", false}}) {
    auto face = ramp_env(60);
    const double m = max_slope; const char* n = name;
    face.climb = [m, n](double, double) { return surface(true, m, n); };
    person.feet = {499, 10, 500}; person.stamina = 1.0;
    for (int i = 0; i < 120; ++i) walk(person, {1, 0, false}, kTick, face);
    require((person.feet.x > 500.3) == ok, "each surface allows its own steepness");
  }
}
void test_crouch_ice_and_falls() {
  constexpr double kTick = 1.0 / 60.0;
  rejects([] { CrouchRules c; c.speed_factor = 0; validate_crouch_rules(c); }, "a zero crouch speed is rejected");
  rejects([] { GripRules g; g.slip_speed_mps = 0; validate_grip_rules(g); }, "a zero slip speed is rejected");
  // Crouching: the eye comes down, walking is half as fast, no jumping, and a low branch can be passed under.
  auto env = flat_env();
  FirstPersonCamera person;
  person.feet = {500, 10, 500}; person.yaw = -kPi / 2; person.walk_speed_mps = 2.0;
  for (int i = 0; i < 60; ++i) walk(person, {0, 0, false, false, true}, kTick, env);
  require(person.crouched && near(person.crouch_blend, 1.0) && near(person.eye().y, 11.7 - person.crouch_drop_meters), "crouched: the eye is down");
  const double x0 = person.feet.x;
  for (int i = 0; i < 60; ++i) walk(person, {1, 0, true, false, true}, kTick, env);
  require(near(person.feet.x - x0, 2.0 * env.crouch.speed_factor, 0.02), "crouched, you walk at half speed and cannot run");
  walk(person, {0, 0, false, true, true}, kTick, env);
  require(person.grounded && person.vertical_velocity_mps == 0, "no jumping while crouched");
  const ObstacleField branch({[] { Obstacle o; o.x = 520; o.z = 500; o.radius = 3; o.bottom = 11.2; o.top = 13; return o; }()});
  auto low = flat_env(&branch);
  person.feet = {515, 10, 500};
  for (int i = 0; i < 120; ++i) walk(person, {1, 0, false, false, false}, kTick, low);
  require(person.feet.x < 517.2, "standing, a branch at head height blocks");
  person.feet = {515, 10, 500};
  for (int i = 0; i < 60; ++i) walk(person, {0, 0, false, false, true}, kTick, low);
  for (int i = 0; i < 660; ++i) walk(person, {1, 0, false, false, true}, kTick, low);
  require(person.feet.x > 523.5, "crouched, you pass under it");
  for (int i = 0; i < 60; ++i) walk(person, {}, kTick, env);
  require(!person.crouched && near(person.crouch_blend, 0.0) && near(person.eye().y, 11.7), "released, you stand up again");

  // Ice: slow to get going, long to stop, sliding down a slope, a slip on a steep one.
  auto rink = flat_env(nullptr, true);
  rink.grip = [](double, double) { return 0.1; };
  person.feet = {500, 10, 500}; person.yaw = -kPi / 2; person.walk_speed_mps = 2.5; person.gait_speed_mps = 0;
  walk(person, {1, 0, false}, 0.5, rink);
  const double icy_start = person.gait_speed_mps;
  auto grass = flat_env(nullptr, true);
  person.feet = {500, 10, 500}; person.gait_speed_mps = 0;
  walk(person, {1, 0, false}, 0.5, grass);
  require(icy_start < person.gait_speed_mps * 0.5 && person.grip == 1.0, "on ice you get going slowly");
  person.feet = {500, 10, 500};
  for (int i = 0; i < 120; ++i) walk(person, {1, 0, false}, kTick, rink);
  const double release_x = person.feet.x;
  int ticks = 0;
  while (person.gait_speed_mps > 0 && ticks < 1200) { walk(person, {}, kTick, rink); ++ticks; }
  require(person.feet.x - release_x > 2.0 && near(person.grip, 0.1), "on ice the stop takes several steps");
  auto icy_slope = ramp_env(20);
  icy_slope.grip = [](double, double) { return 0.1; };
  person.feet = {520, icy_slope.standing_height(520, 500).value(), 500}; person.yaw = 0; person.gait_speed_mps = 0; person.slide_mps = {};
  for (int i = 0; i < 180; ++i) walk(person, {}, kTick, icy_slope);
  require(person.feet.x < 519.0 && person.slide_mps.x < 0, "standing still on an icy slope you slide downhill");
  require(person.slipped || person.stunned_seconds > 0 || person.slide_mps.length() < icy_slope.grip_rules.slip_speed_mps,
          "a fast slide is a slip");
  bool slipped = false;
  person.feet = {540, icy_slope.standing_height(540, 500).value(), 500}; person.slide_mps = {}; person.stunned_seconds = 0;
  for (int i = 0; i < 600 && !slipped; ++i) { walk(person, {}, kTick, icy_slope); slipped = person.slipped; }
  require(slipped && person.stunned_seconds > 0, "sliding fast enough throws you down");
  auto dry_slope = ramp_env(20);
  person.feet = {520, dry_slope.standing_height(520, 500).value(), 500}; person.slide_mps = {}; person.stunned_seconds = 0;
  for (int i = 0; i < 180; ++i) walk(person, {}, kTick, dry_slope);
  require(near(person.feet.x, 520) && person.slide_mps.length() == 0, "on dry ground you stand on the slope");

  // Falls report their impact once; a crosswind turns the heading.
  person.feet = {599, 10, 500}; person.yaw = -kPi / 2; person.grounded = true; person.landed = false;
  auto drop = flat_env(); drop.standing_height = [](double x, double) -> std::optional<double> { return x < 600 ? 14.0 : 4.0; };
  person.feet = {599, 14, 500};
  bool landed = false; double impact = 0;
  for (int i = 0; i < 240; ++i) { walk(person, {1, 0, false}, kTick, drop); if (person.landed) { landed = true; impact = person.last_impact_mps; } }
  require(landed && near(impact, std::sqrt(2 * 9.81 * 10), 0.3) && !person.landed, "a ten-metre fall lands at sqrt(2 g h), reported for one step");
  auto windy = flat_env(); windy.wind_velocity_mps = {30, 0};
  person.feet = {500, 10, 500}; person.yaw = 0; person.grounded = true;  // north, the wind from the left... on the right side (+x)
  for (int i = 0; i < 60; ++i) walk(person, {}, kTick, windy);
  require(person.yaw > 0.2, "a crosswind turns the walker downwind");
  auto calm = flat_env();
  person.yaw = 0;
  for (int i = 0; i < 60; ++i) walk(person, {}, kTick, calm);
  require(person.yaw == 0, "calm air leaves the heading alone");
  // Swimming tires; spent, the swimmer is in trouble (the viewer's vitals decide what that costs).
  auto pool = flat_env(); pool.water = [](double, double, double ground) { FloodSample w; w.depth_meters = 3; w.surface_meters = ground + 3; return w; };
  person.feet = {500, 10, 500}; person.stamina = 1.0; person.grounded = true;
  for (int i = 0; i < 60 * 20; ++i) walk(person, {}, kTick, pool);
  require(person.swimming && person.stamina < 0.4 && person.stamina > 0.3, "twenty seconds of swimming spend two thirds of the strength");
}
}  // namespace

int main() {
  try {
    test_directions();
    test_look_and_speed();
    test_walking();
    test_transitions();
    test_gravity_and_jumping();
    test_gait();
    test_storms();
    test_climbing();
    test_crouch_ice_and_falls();
  } catch (const std::exception& error) {
    std::cerr << "camera tests failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "camera tests passed\n";
  return 0;
}
