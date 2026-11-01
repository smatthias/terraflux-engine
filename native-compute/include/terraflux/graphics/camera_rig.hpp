#pragma once

#include <terraflux/graphics/collision.hpp>
#include <terraflux/graphics/flood.hpp>
#include <terraflux/math.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace terraflux::graphics {

// Backend-neutral viewer camera: the overview ("world view") orbits a target
// on the ground, the first-person view stands on the ground and looks around.
// Both share one yaw convention with the rest of the viewer: yaw 0 looks
// north (-Z), positive yaw turns clockwise seen from above, so the horizontal
// forward direction is (-sin yaw, 0, -cos yaw) and right is (cos yaw, 0, -sin yaw).
// Pitch is radians above the horizon. Units: meters, seconds, radians.
enum class CameraMode : std::uint8_t { World, FirstPerson };
const char* camera_mode_name(CameraMode mode);

// Where the viewer may stand at a plan position: the height of the ground (or
// the water surface over flooded ground) plus any allowance for visualization
// relief, or nullopt over holes and outside the terrain. Pure and deterministic
// so transitions and tests can rely on exact restoration.
using StandingHeightFn = std::function<std::optional<double>(double x, double z)>;

math::Vec3 horizontal_forward(double yaw);
math::Vec3 horizontal_right(double yaw);
// Unit look direction for a yaw and a pitch above the horizon.
math::Vec3 look_direction(double yaw, double pitch);

struct OrbitCamera {
  math::Vec3 target;          // the point the camera looks at (W/A/S/D move it over the ground)
  double yaw = 0.45;
  double pitch = 0.75;        // 0.1 .. 1.48 radians above the ground
  double distance = 1000.0;
  math::Vec3 eye() const;     // target + distance along (sin yaw cos pitch, sin pitch, cos yaw cos pitch)
};

struct FirstPersonCamera {
  math::Vec3 feet;                    // standing point; feet.y is the support height when grounded
  double yaw = 0.0;
  double pitch = 0.0;                 // clamped to +-kMaxPitch
  double eye_height_meters = 1.7;     // eye above the feet
  double walk_speed_mps = 2.5;        // a brisk walk; kMinWalkSpeed .. kMaxWalkSpeed
  double run_multiplier = 3.0;        // Shift: a run at 7.5 m/s
  // Vertical dynamics (see walk): on the ground the feet follow the support;
  // in the air the horizontal momentum is kept and gravity pulls the feet down.
  bool grounded = true;
  double vertical_velocity_mps = 0.0;
  math::Vec2 velocity_mps;            // horizontal momentum while airborne (x, z)
  double crouch_meters = 0.0;         // landing dip of the eye, recovering over a few tenths of a second
  double air_time_seconds = 0.0;      // time since the feet left the ground (0 when grounded)
  // Gait (see GaitStyle): the walker moves in steps, not at a constant speed.
  double gait_speed_mps = 0.0;        // current ground speed (accelerates from rest, brakes to a footfall)
  math::Vec2 gait_direction;          // unit direction of the current step (kept while stopping)
  double gait_phase = 0.0;            // 0 at a footfall .. 1 at the next; the swing foot lands at 1
  double braking_mps2 = 0.0;          // uniform deceleration of a stop in progress (0: not stopping)
  double braking_step_length_meters = 0.0; // the step being finished by the stop
  int stance_foot = 0;                // 0 left, 1 right: the foot carrying the body in this step
  std::uint64_t footfalls = 0;        // steps completed since the camera was created
  double bob_meters = 0.0;            // head rise over the stance leg (0 at footfall, highest mid-step)
  double sway_meters = 0.0;           // head shift over the stance foot (+ right, - left)
  double roll_radians = 0.0;          // head roll toward the stance foot
  double footfall_dip_meters = 0.0;   // the jolt of the landing foot, recovering quickly
  // Storm effects (see knock_down / shake): on the ground and unable to move
  // for a while after a close strike, the view trembling from the shock.
  double stunned_seconds = 0.0;
  double stun_crouch_meters = 0.0;    // how far the eye is down while stunned (recovers with the landing dip)
  double shake_meters = 0.0;          // amplitude of the trembling, decaying
  double shake_phase = 0.0;
  math::Vec3 shake_offset;            // this frame's trembling offset of the eye
  // Water (see WaterRules): how deep the feet stand, whether the walker floats,
  // the current it is in, and whether the last step swept it off its feet.
  double water_depth_meters = 0.0;
  double current_mps = 0.0;
  bool swimming = false;
  bool swept = false;
  // Swimming (see WaterRules): strokes drive the speed and the body; Shift
  // crawls, Ctrl dives with a breath that runs out.
  double swim_phase = 0.0;         // 0 .. 1 within a stroke
  std::uint64_t strokes = 0;
  bool crawling = false;           // the fast stroke
  bool diving = false;             // under the surface
  double breath = 1.0;             // 1 full .. 0 out of air (surfaces by itself)
  double dive_blend = 0.0;         // 0 at the surface .. 1 fully down (eases)
  // Climbing (see ClimbRules): whether the last step was a climb, on what,
  // how steep, and the strength left for it (0 exhausted .. 1 fresh).
  bool climbing = false;
  double climb_slope_degrees = 0.0;
  const char* climb_surface = "";
  const char* climb_refusal = "";  // why the last steep step was refused ("" when not refused)
  double stamina = 1.0;
  bool exhausted = false;          // spent: no ascending until the stamina has recovered to resume_stamina
  // Crouching (see CrouchRules): lower, slower, under things, a smaller target.
  bool crouched = false;
  double crouch_blend = 0.0;       // 0 standing .. 1 fully crouched (eases over a few tenths of a second)
  // Ice: the walker slides; a slip throws it down.
  double grip = 1.0;               // the ground's grip under the feet this step
  math::Vec2 slide_mps;            // sliding velocity on low-grip ground
  bool slipped = false;            // this step: a slip on ice threw the walker down
  // On a rope (see tools.hpp): hanging from a rope thrown down a face or up
  // over a branch, so far below its anchor, hand over hand. walk() is not
  // called while on it; climb_rope() moves the walker along the rope instead.
  bool on_rope = false;
  double rope_t_meters = 0.0;      // distance below the anchor along the rope
  double rope_phase = 0.0;         // 0 .. 1 per pull (drives the arms)
  double rope_ground_meters = 0.0; // how much rope was let down with walkable ground underfoot (a ledge: stepped onto after a metre)
  // Falls: the impact of the last landing (m/s downward), reported once.
  double last_impact_mps = 0.0;
  bool landed = false;
  double crouch_drop_meters = 0.7; // how far the eye comes down fully crouched
  double dive_drop_meters = 1.2;   // how far the eye goes down diving (below the surface)
  math::Vec3 eye() const {
    const auto right = horizontal_right(yaw);
    return math::Vec3{feet.x + right.x * sway_meters,
                      feet.y + eye_height_meters - crouch_meters - footfall_dip_meters + bob_meters - crouch_blend * crouch_drop_meters -
                          dive_blend * dive_drop_meters,
                      feet.z + right.z * sway_meters} + shake_offset;
  }
  // The eye is under water when diving (the surface lies swim depth above the floating feet).
  bool underwater() const { return swimming && dive_blend > 0.3; }
  math::Vec3 target() const { return eye() + look_direction(yaw, pitch); }
  // The camera's up vector with the gait roll applied (unit length).
  math::Vec3 up() const;
};

inline constexpr double kFirstPersonMaxPitch = 1.4835298641951802;  // 85 degrees
inline constexpr double kMinWalkSpeed = 1.0, kMaxWalkSpeed = 60.0;
inline constexpr double kOrbitTargetHeight = 1.5;  // the orbit target hovers this far above the ground

// One frame of first-person input, already reduced to axes by the caller.
struct WalkInput {
  double forward = 0.0;   // +1 W, -1 S
  double strafe = 0.0;    // +1 D, -1 A
  bool run = false;
  bool jump = false;      // edge-triggered by the caller: the press, not the hold
  bool crouch = false;    // held
};

// Earth-like vertical physics. Gravity is the constant later environment
// interaction (thrown, falling, floating things) should share.
struct WalkerPhysics {
  double gravity_mps2 = 9.81;          // 0.1 .. 100
  double jump_height_meters = 1.0;     // apex of a standing jump; 0 disables jumping; <= 10
  double air_control = 0.25;           // 0 .. 1: per-second blend of the airborne momentum toward the input
  double max_fall_speed_mps = 60.0;    // terminal velocity; 1 .. 200
  double landing_dip_per_mps = 0.06;   // eye dip per m/s of impact speed; 0 .. 0.5
  double landing_dip_max_meters = 0.5; // 0 .. eye height
  double landing_recovery_per_second = 8.0; // exponential recovery rate of the dip; 0.1 .. 100
};
void validate_walker_physics(const WalkerPhysics& physics);  // throws std::invalid_argument

// How walking is broken into steps. A step is step_length_base + per_mps x
// speed long, never shorter than the cadence limit allows. Within a step the
// ground speed pulses: slowest as the swing foot lands (double support),
// fastest as the stance foot pushes off mid-step. Starting accelerates from
// rest; releasing the keys brakes so that the walker stops on a footfall, at
// most stop_deceleration, so the current step is finished, not cut. The head
// rises over the stance leg (bob), shifts and rolls toward the stance foot
// (sway, roll) and takes a small jolt at each footfall (dip), all scaled by
// speed and all exactly zero when standing. Disabled, the walker glides at
// constant speed (the earlier behaviour).
struct GaitStyle {
  bool enabled = true;
  double step_length_base_meters = 0.45;   // 0.1 .. 2
  double step_length_per_mps = 0.22;       // 0 .. 1
  double max_cadence_hz = 4.5;             // steps per second at most; 0.5 .. 10
  double speed_pulse = 0.2;                // 0 .. 0.5: fractional speed variation within a step
  double acceleration_mps2 = 20.0;         // 1 .. 200: reaching the walking speed from rest
  double stop_deceleration_mps2 = 40.0;    // 1 .. 400: the strongest braking onto a footfall
  double bob_base_meters = 0.02, bob_per_mps = 0.004, bob_max_meters = 0.07;   // each 0 .. 0.5
  double sway_base_meters = 0.015, sway_per_mps = 0.002, sway_max_meters = 0.04; // each 0 .. 0.5
  double roll_degrees = 0.6;               // 0 .. 10 toward the stance foot
  double footfall_dip_meters = 0.012;      // 0 .. 0.2 per footfall at walking pace, more when faster
  double footfall_recovery_per_second = 18.0; // 0.1 .. 100
};
void validate_gait_style(const GaitStyle& gait);  // throws std::invalid_argument
// Step length for a ground speed: base + per_mps x speed, but at least speed / max cadence.
double gait_step_length(const GaitStyle& gait, double speed_mps);
// The take-off speed that reaches the jump height: sqrt(2 g h).
double jump_takeoff_speed(const WalkerPhysics& physics);
// Analytic standing-jump air time: 2 sqrt(2 h / g).
double jump_air_time(const WalkerPhysics& physics);

// Turns the look direction; the pitch is clamped to +-85 degrees and the yaw wrapped to (-pi, pi].
void look(FirstPersonCamera& camera, double yaw_delta, double pitch_delta);
// Scales the walking speed by 2^(octaves) within the permitted range.
void adjust_walk_speed(FirstPersonCamera& camera, double octaves);

// What a walker moves through: the standing height, the terrain bounds, the
// obstacle fields (trees, assets; null entries are skipped) and the body that
// decides what is stepped over, slid around or too steep to climb.
// What the wind does to a walker. Below the threshold you brace and stand;
// above it the gust drifts you downwind at push_factor x (speed - threshold)^2
// (capped), slows you walking into it and hurries you with it; in the air
// drag on the body accelerates you toward the wind at
// air_drag x |relative| x relative.
struct WindPush {
  double threshold_mps = 12.0;     // 0 .. 60
  double push_factor = 0.02;       // 0 .. 1 (m/s per (m/s)^2)
  double max_drift_mps = 4.0;      // 0 .. 30
  double air_drag = 0.0056;        // 0 .. 0.1 per metre: 0.5 rho Cd A / m for a person
};
void validate_wind_push(const WindPush& push);
// The drift velocity a wind imposes on a standing walker (m/s, plan).
math::Vec2 wind_drift(const WindPush& push, math::Vec2 wind_velocity_mps);

// Water at the feet. Shallow water is waded: the speed falls by wade_slowdown
// per metre of depth (never below min_wade_factor) and, past grip_depth, the
// current pushes the walker (fully by grip_depth + grip_fade). From swim_depth
// on the walker floats: the feet hang swim_depth under the surface (the eye
// above it), it moves at swim_speed, cannot jump, and the current carries it
// entirely. Wading in a current faster than sweep_current and deeper than
// sweep_depth sweeps the walker off its feet: knocked down for sweep_stun
// seconds and carried.
struct WaterRules {
  double swim_depth_meters = 1.3;      // 0.3 .. 5
  double wade_slowdown_per_meter = 0.6; // 0 .. 2
  double min_wade_factor = 0.25;       // 0.05 .. 1
  double swim_speed_mps = 1.2;         // 0.1 .. 5 (breaststroke)
  double crawl_factor = 1.7;           // 1 .. 4: Shift swims this much faster and tires this much faster
  double stroke_seconds = 1.4;         // 0.2 .. 10 per stroke at the swim speed (crawl: / crawl factor)
  double stroke_pulse = 0.5;           // 0 .. 0.9: the speed surges with the stroke and glides between
  double grip_depth_meters = 0.25;     // 0 .. 2
  double grip_fade_meters = 0.6;       // 0.05 .. 3
  double sweep_current_mps = 2.0;      // 0.1 .. 30
  double sweep_depth_meters = 0.5;     // 0 .. 5
  double sweep_stun_seconds = 1.5;     // 0 .. 60
  double breath_seconds = 25.0;        // 1 .. 600 under water
  double breath_recovery_seconds = 8.0; // 1 .. 600 at the surface
  double dive_speed_factor = 0.8;      // 0.1 .. 1
};
void validate_water_rules(const WaterRules& rules);
// Water at a plan position: depth over the ground there, the surface and the current.
using WaterFn = std::function<FloodSample(double x, double z, double ground_y)>;

// Climbing. Ground steeper than the body's walking limit is a wall unless
// the walker can climb it: the surface decides how steep a climb it allows
// (ClimbSurface from the caller's ClimbFn — dry rock most, soil less, snow
// little, ice and running water or a waterfall nothing), the walker climbs at
// climb_speed along the face (its horizontal progress is that times the
// cosine of the slope), and climbing drains stamina over stamina_seconds of
// continuous climbing while resting restores it over recovery_seconds; an
// exhausted walker (below min_stamina) cannot ascend a climb, only descend.
struct ClimbSurface {
  bool climbable = false;
  double max_slope_degrees = 0.0;  // the steepest climb this surface allows
  const char* name = "";           // rock, wet rock, soil, snow, ice, waterfall, running water ...
};
using ClimbFn = std::function<ClimbSurface(double x, double z)>;
struct ClimbRules {
  double climb_speed_mps = 0.6;      // 0.05 .. 5 along the face
  double stamina_seconds = 40.0;     // 1 .. 3600 of continuous climbing from fresh to exhausted
  double recovery_seconds = 20.0;    // 1 .. 3600 from exhausted to fresh, resting
  double min_stamina = 0.1;          // 0 .. 1: below it the walker is exhausted and cannot ascend
  double resume_stamina = 0.5;       // min .. 1: an exhausted walker climbs again from here
};
void validate_climb_rules(const ClimbRules& rules);

// Crouching: held, the walker sinks by crouch_drop (eased over ease_seconds),
// walks at speed_factor, is body height x height_factor tall for passing
// under things, and can neither jump nor climb.
struct CrouchRules {
  double speed_factor = 0.5;       // 0.1 .. 1
  double height_factor = 0.6;      // 0.2 .. 1
  double ease_seconds = 0.25;      // 0.01 .. 5
};
void validate_crouch_rules(const CrouchRules& rules);
// The ground's grip at a plan position (0 none .. 1 full): ice ~0.1, packed
// snow ~0.5, wet rock ~0.7, dry ground 1. Empty: full grip everywhere.
using GripFn = std::function<double(double x, double z)>;
// Low grip: the gait accelerates and brakes only by the grip, the walker
// slides downhill on slopes (gravity along the slope times (1 - grip), braked
// by the grip), and a slide faster than slip_speed throws it down.
struct GripRules {
  double slide_slope_degrees = 5.0;  // 0 .. 60: no sliding on gentler ground
  double slip_speed_mps = 2.5;       // 0.1 .. 30
  double slip_stun_seconds = 1.2;    // 0 .. 60
  double slide_drag = 0.5;           // 0 .. 10 per second at full grip
};
void validate_grip_rules(const GripRules& rules);
// A crosswind turns a walker: the heading drifts downwind at
// turn_per_mps x (crosswind - threshold) radians per second, so keeping a
// course across a gale takes steering.
struct HeadingPush {
  double threshold_mps = 15.0;     // 0 .. 60
  double turn_per_mps = 0.02;      // 0 .. 1 rad/s per m/s
};
void validate_heading_push(const HeadingPush& push);

struct WalkEnvironment {
  StandingHeightFn standing_height;
  math::Vec3 lower, upper;
  std::vector<const ObstacleField*> obstacles;
  WalkerBody body;
  WalkerPhysics physics;
  GaitStyle gait;
  math::Vec2 wind_velocity_mps;    // the wind of the moment (plan, m/s); zero for calm
  WindPush wind;
  HeadingPush heading;
  WaterFn water;                   // empty: dry ground everywhere
  WaterRules water_rules;
  ClimbFn climb;                   // empty: nothing can be climbed
  ClimbRules climb_rules;
  CrouchRules crouch;
  GripFn grip;                     // empty: full grip everywhere
  GripRules grip_rules;
  double swim_stamina_seconds = 30.0;  // 1 .. 3600: swimming spends stamina too; spent, the swimmer is drowning
};

// A close lightning strike: what it does to a walker at a distance.
enum class LightningHazard : std::uint8_t { None, Close, Dangerous, Struck };
struct LightningEffect {
  LightningHazard hazard = LightningHazard::None;
  double knock_down_seconds = 0.0;  // on the ground, unable to move
  double crouch_meters = 0.0;       // how far down
  double shake_meters = 0.0;        // trembling amplitude
};
const char* lightning_hazard_name(LightningHazard hazard);
// Struck within 15 m (thrown down for seconds, the view shaking hard),
// dangerous within 50 m (staggered, knocked down briefly), close within
// 200 m (the shock wave shakes the view), nothing beyond.
LightningEffect lightning_effect(double distance_meters);
// Throws the walker down for `seconds` with the eye `crouch` lower; input is
// ignored until it gets up. Shake sets the trembling amplitude (max with the current).
void knock_down(FirstPersonCamera& camera, double seconds, double crouch_meters);
void shake(FirstPersonCamera& camera, double amplitude_meters);
// What the feet rest on at a plan position: the standing height, or the top
// of an obstacle whose footprint (plus half the body radius) covers the point
// when that top is no higher than the feet plus the step height — a rock or
// ledge you can stand on after stepping or jumping onto it. Nullopt over holes.
std::optional<double> support_height(const WalkEnvironment& environment, double x, double z, double feet_y);
// Advances the walker by dt seconds. On the ground it moves along the
// horizontal look direction at the walking (or running) speed in sub-steps no
// longer than half the body radius so nothing is tunnelled through: each
// sub-step is pushed out of blocking obstacles (sliding around trunks),
// clamped to the terrain bounds and checked against the support: a hole
// stops the walker; an ascent steeper than the body's slope limit is a wall,
// along whose contour the remaining motion slides instead; a drop deeper than
// the step height is a ledge the walker steps off and falls from. A jump
// (while grounded, with a positive jump height) leaves the ground at the
// take-off speed with the current horizontal velocity as momentum. In the air
// gravity integrates the feet (semi-implicit Euler at no more than 1/120 s),
// the momentum persists with limited air control, walls stop it, and touching
// the support lands with an eye dip proportional to the impact speed that
// recovers exponentially. Returns the horizontal distance moved.
double walk(FirstPersonCamera& camera, const WalkInput& input, double dt, const WalkEnvironment& environment);
// Convenience: no obstacles, no slope limit (an 89-degree limit), Earth
// gravity and no gait (constant speed), for kinematic checks.
double walk(FirstPersonCamera& camera, const WalkInput& input, double dt,
            const StandingHeightFn& standing_height, math::Vec3 lower, math::Vec3 upper);
// Moves the feet out of any obstacle they were placed inside of (after
// entering first person among trees) and, when grounded, re-samples the
// support (a flood rising under you lifts you); airborne feet keep their
// height. Returns false, leaving the camera untouched, when the pushed-out
// point has no support.
bool settle(FirstPersonCamera& camera, const WalkEnvironment& environment);
// Puts the camera on the ground at (x, z) or, over a hole, at the nearest
// standing point found by rings of `step` meters up to `max_radius` out.
bool place_on_ground(FirstPersonCamera& camera, double x, double z, const StandingHeightFn& standing_height,
                     math::Vec3 lower, math::Vec3 upper, double step, double max_radius);

// Transitions keep the heading: entering first person stands you on the ground
// under the orbit target looking level along the orbit's viewing direction;
// returning to the world view orbits the point you stood on, seen from the
// previous orbit pitch and distance, behind the way you were looking.
bool enter_first_person(FirstPersonCamera& person, const OrbitCamera& orbit, const StandingHeightFn& standing_height,
                        math::Vec3 lower, math::Vec3 upper, double search_step, double search_radius);
void enter_world_view(OrbitCamera& orbit, const FirstPersonCamera& person);

}  // namespace terraflux::graphics
