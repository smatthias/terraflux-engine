#pragma once

#include <terraflux/graphics/body_view.hpp>
#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/items.hpp>
#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace terraflux::graphics {

// Tools the walker always carries (a belt; keys 1/2/3 take one in hand,
// the same key again puts it away) against the hazards of the ground: a
// rope to get down a face you cannot climb down, or up into a tree out of a
// flood; a fire axe and a chainsaw to fell a tree (away from you — a burning
// one before it falls on you, a sound one to break a fire's path) and to cut
// branches and logs into pieces. The held thing (stone, branch, torch) moves
// to the left hand while a tool is out.
std::optional<ToolKind> tool_kind_from_name(std::string_view name);  // "rope", "axe", "chainsaw" (or "none")

struct AxeRules {
  double chop_depth_meters = 0.06;   // 0.005 .. 1 of trunk cut per chop
  double swing_seconds = 0.7;        // 0.1 .. 5: one chop; presses during a swing are ignored
  double reach_meters = 1.6;         // 0.3 .. 5 from the feet to a trunk, a log or a branch
};
struct ChainsawRules {
  double cut_rate_mps = 0.10;        // 0.001 .. 5 metres of trunk per second of sawing
  double fuel_seconds = 300.0;       // 1 .. 36000 of running
  double branch_seconds = 0.5;       // 0.05 .. 60 to cut a branch through
  double reach_meters = 1.6;         // 0.3 .. 5
};
void validate_axe_rules(const AxeRules& rules);
void validate_chainsaw_rules(const ChainsawRules& rules);
// The trunk is felled when this fraction of its diameter is cut (the rest is the hinge).
inline constexpr double kFellCutFraction = 0.7;
// How many chops, or how many seconds of sawing, bring a trunk of this radius down.
int chops_to_fell(double trunk_radius_meters, const AxeRules& rules);
double seconds_to_saw(double trunk_radius_meters, const ChainsawRules& rules);
// Branches are cut in halves down to this length.
inline constexpr double kMinBranchPiece = 0.25;
bool branch_can_be_cut(double length_meters);

struct ToolBelt {
  ToolKind selected = ToolKind::None;
  double chainsaw_fuel_seconds = 300.0;  // what is left in the tank
  double swing = 0.0;                    // an axe chop in progress (0 none .. 1 done)
  bool sawing = false;                   // the chainsaw ran this step
  double saw_phase = 0.0;                // seconds run (the tremble)
  // Takes a tool in hand, or puts it away when it is the one already out.
  // Returns what is in hand afterwards.
  ToolKind select(ToolKind kind);
};

// The rope: coiled on the belt, flying after a throw, lying on the ground, or
// hung — from the thrower's feet (pegged at the edge it was thrown over) or
// from a branch it wrapped round — as a straight line from anchor to end.
enum class RopeState : std::uint8_t { Coiled, Flying, Laid, Hung };
const char* rope_state_name(RopeState state);
struct RopeRules {
  double length_meters = 20.0;       // 2 .. 200
  double throw_speed_mps = 10.0;     // 1 .. 60
  double lift_degrees = 12.0;        // -60 .. 60 added to the look
  double hang_drop_meters = 2.0;     // 0 .. 50: a face at least this tall hangs the rope (less is jumped down)
  double face_slope_tangent = 1.0;   // 0.1 .. 50: ground falling faster than this (rise over run; 1 = 45 degrees) is a face the rope hangs down
  double climb_speed_mps = 0.8;      // 0.05 .. 5 hand over hand
  double stamina_seconds = 60.0;     // 1 .. 3600 of climbing up from fresh to exhausted (hanging costs a third, going down nothing)
  double slide_speed_mps = 4.0;      // 0.1 .. 20: an exhausted climber slides to the bottom
  double grab_reach_meters = 1.5;    // 0.1 .. 5 from the hands to the rope
  double step_off_meters = 0.5;      // 0 .. 3: ground this close below the feet is stepped onto
  double pull_seconds_per_meter = 0.0;  // 0 .. 10 (0: the rope is pulled in at once)
};
void validate_rope_rules(const RopeRules& rules);
struct Rope {
  RopeState state = RopeState::Coiled;
  math::Vec3 anchor;                 // the top (Hung) or where it was thrown from (Laid)
  math::Vec3 end;                    // the lower end, on the ground (or where the flight is)
  int branch_tree = -1;              // the tree it is wrapped round, or -1 (pegged at the feet)
  double anchor_height_meters = 0.0; // the anchor's height above the ground below it
  Projectile flight;                 // while flying (the coil)
  math::Vec3 thrower_feet;           // where it was thrown from (a pegged rope lies from here to the lip)
  math::Vec3 landing;                // where the coil came down (a pegged rope lies on from the foot to here)
  double paid_out_meters = 0.0;      // how much rope the flight has taken
  // The rope's length in the world (Hung / Laid): anchor to end.
  double span_meters() const;
  // The point so far below the anchor along the rope (Hung).
  math::Vec3 point_at(double t_meters) const;
};
// A crown the flying coil can catch on: called with each step of the flight;
// returns the branch point (and tree) when the segment passes through one.
struct RopeCatch { math::Vec3 point; int tree = -1; };
using BranchCatchFn = std::function<std::optional<RopeCatch>(math::Vec3 from, math::Vec3 to)>;
// Throws the coil from the hand along the look (lifted), paying the rope out behind it.
void throw_rope(Rope& rope, math::Vec3 feet, math::Vec3 eye, math::Vec3 forward, const RopeRules& rules);
// Advances a flying rope: the coil flies under gravity (like a thrown stone),
// never further than the rope's length from the thrower (taut, it drops);
// passing through a crown it wraps round a branch (Hung from the branch, the
// end hanging straight down to the ground). Landed, the ground between the
// feet and the landing point is walked in quarter-metre steps: where it falls
// away steeper than a face slope for at least the hang drop, the rope lies to
// that lip, hangs down the steep run to its foot (Hung, pegged at the lip,
// the anchor) and lies on from there; with no such face it lies on the
// ground (Laid, from the feet to where it fell). Lost down a hole it is
// pulled back in (Coiled).
void advance_rope(Rope& rope, double dt, double gravity_mps2, const StandingHeightFn& ground, math::Vec3 lower, math::Vec3 upper,
                  const BranchCatchFn& branches, const RopeRules& rules);
// Pulls the rope in (Coiled) — not while someone is on it.
void coil_rope(Rope& rope);
// Where a walker standing here can take hold of a hung rope: the distance
// below the anchor of the nearest point of the rope within grab reach of the
// hands (at shoulder height), or nullopt when out of reach.
std::optional<double> rope_grab_point(const Rope& rope, math::Vec3 feet, double shoulder_height, const RopeRules& rules);
// Puts the walker on the rope at a point (on_rope, hanging there).
void grab_rope(FirstPersonCamera& camera, const Rope& rope, double t_meters);
struct RopeClimbResult {
  bool at_top = false;          // at the anchor: on the edge (pegged) or under the branch
  bool stepped_off = false;     // the feet found ground within the step-off distance: standing again
  bool let_go = false;          // released (falls from here)
  bool sliding = false;         // exhausted, sliding down
  double moved_meters = 0.0;    // along the rope, + down
};
// One step on a hung rope: W climbs up (spending stamina), S lets down,
// neither hangs (a third of the cost); exhausted, the climber slides down at
// the slide speed until the ground or the end. Reaching ground within the
// step-off distance below the feet (the bottom, or a ledge the rope crosses)
// steps off onto it; `jump` lets go. The feet never go below the ground the
// rope runs over. The arms pull hand over hand (rope_phase).
RopeClimbResult climb_rope(FirstPersonCamera& camera, const Rope& rope, const WalkInput& input, double dt, const WalkEnvironment& environment,
                           const RopeRules& rules);
// Steps off the top of a pegged rope onto the edge: the feet move inward
// (from the end toward the anchor, past it) to ground within the step-off
// distance of the anchor's height; false when there is no such ground.
bool step_off_rope_top(FirstPersonCamera& camera, const Rope& rope, const WalkEnvironment& environment, const RopeRules& rules);

// Drawing: the rope as a line of thin boxes (a hung or laid rope, or a
// flight's trail from the thrower's hand to the coil); at most `max_props`.
std::vector<Prop> rope_props(const Rope& rope, math::Vec3 hand, std::size_t max_props);

}  // namespace terraflux::graphics
