#include <terraflux/graphics/tools.hpp>

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace terraflux::graphics;
using terraflux::math::Vec3;

namespace {
void require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
template <class F> void rejects(F&& f, const std::string& what) {
  try { f(); } catch (const std::invalid_argument&) { return; } catch (const std::logic_error&) { return; }
  throw std::runtime_error(what);
}
bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

// A cliff: a plateau at 30 m for x < 10, the floor at 18 m beyond.
std::optional<double> cliff(double x, double z) {
  if (x < -100 || x > 100 || z < -100 || z > 100) return std::nullopt;
  return x < 10.0 ? 30.0 : 18.0;
}
WalkEnvironment cliff_environment() {
  WalkEnvironment env;
  env.standing_height = cliff;
  env.lower = {-100, 0, -100};
  env.upper = {100, 100, 100};
  return env;
}

void test_belt_and_cutting() {
  require(tool_kind_from_name("axe") == ToolKind::Axe && tool_kind_from_name("chainsaw") == ToolKind::Chainsaw &&
          tool_kind_from_name("rope") == ToolKind::Rope && tool_kind_from_name("none") == ToolKind::None && !tool_kind_from_name("hammer"),
          "tool names");
  require(std::string(tool_kind_name(ToolKind::Chainsaw)) == "chainsaw", "tool kind names");
  ToolBelt belt;
  require(belt.select(ToolKind::Axe) == ToolKind::Axe && belt.selected == ToolKind::Axe, "a tool is taken in hand");
  require(belt.select(ToolKind::Rope) == ToolKind::Rope, "another replaces it");
  require(belt.select(ToolKind::Rope) == ToolKind::None, "the same key again puts it away");
  AxeRules axe;
  ChainsawRules saw;
  validate_axe_rules(axe);
  validate_chainsaw_rules(saw);
  rejects([] { AxeRules a; a.chop_depth_meters = 0; validate_axe_rules(a); }, "a chop of nothing is rejected");
  rejects([] { ChainsawRules c; c.fuel_seconds = 0; validate_chainsaw_rules(c); }, "an empty tank is rejected");
  require(chops_to_fell(0.25, axe) == 6, "a half-metre trunk takes six chops (70 % of the diameter at 6 cm)");
  require(chops_to_fell(0.05, axe) == 2 && chops_to_fell(0.5, axe) == 12, "chops scale with the trunk");
  require(near(seconds_to_saw(0.25, saw), 3.5), "the saw takes 3.5 s through it");
  require(branch_can_be_cut(0.8) && branch_can_be_cut(0.5) && !branch_can_be_cut(0.4), "branches are cut in halves down to a quarter metre");
}

void test_rope_down_a_cliff() {
  RopeRules rules;
  validate_rope_rules(rules);
  rejects([] { RopeRules r; r.length_meters = 1; validate_rope_rules(r); }, "a rope of a metre is rejected");
  const auto env = cliff_environment();
  Rope rope;
  require(rope.state == RopeState::Coiled && std::string(rope_state_name(rope.state)) == "coiled", "a rope starts coiled");
  const Vec3 feet{9.0, 30.0, 0.0}, eye{9.0, 31.7, 0.0};
  throw_rope(rope, feet, eye, Vec3{1, -0.3, 0}.normalized(), rules);
  require(rope.state == RopeState::Flying, "thrown, it flies");
  rejects([&] { throw_rope(rope, feet, eye, {1, 0, 0}, rules); }, "a rope in the air cannot be thrown again");
  for (int i = 0; i < 600 && rope.state == RopeState::Flying; ++i) advance_rope(rope, 1.0 / 60.0, 9.81, cliff, env.lower, env.upper, {}, rules);
  require(rope.state == RopeState::Hung && rope.branch_tree < 0, "landing twelve metres below the feet, it hangs down the face");
  require(rope.anchor.x > 9.7 && rope.anchor.x <= 10.0 && near(rope.anchor.y, 30.0) && near(rope.end.y, 18.0) && rope.end.x > 10.0 && rope.end.x < 10.3,
          "pegged at the lip, the end at the foot of the face");
  require(rope.landing.x > 20.0 && near(rope.thrower_feet.x, 9.0), "the rest lies from the feet to the lip and on from the foot to where it fell");
  require(rope.span_meters() > 12.0 && rope.span_meters() < 12.1 && near(rope.anchor_height_meters, 12.0), "the hanging run is the face's height");
  require(rope_props(rope, feet, 12).size() >= 3, "drawn in its three runs");
  const auto mid = rope.point_at(rope.span_meters() * 0.5);
  require(near(mid.y, 24.0, 1e-6), "halfway down the rope is halfway down the face");
  // Take hold from the edge and let yourself down.
  FirstPersonCamera person;
  person.feet = feet;
  const auto at = rope_grab_point(rope, person.feet, 1.4, rules);
  require(at && near(*at, 0.0), "standing at the peg, the rope is taken at its top");
  require(!rope_grab_point(rope, {0.0, 30.0, 0.0}, 1.4, rules), "nine metres back it is out of reach");
  grab_rope(person, rope, *at);
  require(person.on_rope && !person.grounded && person.climbing && std::string(person.climb_surface) == "rope", "on the rope");
  const double stamina_before = person.stamina;
  WalkInput down; down.forward = -1;
  RopeClimbResult last;
  double seconds = 0;
  for (int i = 0; i < 60 * 60 && person.on_rope; ++i) { last = climb_rope(person, rope, down, 1.0 / 60.0, env, rules); seconds += 1.0 / 60.0; }
  require(last.stepped_off && person.grounded && !person.on_rope && near(person.feet.y, 18.0), "let down to the floor, standing again");
  require(person.feet.x > 10.0 && person.feet.x < 10.3, "on the floor at the foot of the face");
  require(near(person.stamina, stamina_before), "going down costs no strength");
  require(seconds > rope.span_meters() / rules.climb_speed_mps - 1.0 && seconds < rope.span_meters() / rules.climb_speed_mps + 1.0, "at the climbing speed");
  // Back up from the bottom.
  const auto bottom = rope_grab_point(rope, person.feet, 1.4, rules);
  require(bottom && *bottom > rope.span_meters() - 2.0, "at the bottom the rope is taken near its end");
  grab_rope(person, rope, *bottom);
  WalkInput up; up.forward = 1;
  for (int i = 0; i < 60 * 60 && !last.at_top; ++i) last = climb_rope(person, rope, up, 1.0 / 60.0, env, rules);
  require(last.at_top && person.on_rope && near(person.rope_t_meters, 0.0) && near(person.feet.y, 30.0), "climbed to the top, at the peg");
  require(person.stamina < stamina_before - 0.2, "climbing up spent strength");
  require(person.rope_phase >= 0 && person.rope_phase < 1, "the arms pulled hand over hand");
  require(step_off_rope_top(person, rope, env, rules) && person.grounded && !person.on_rope && person.feet.x < rope.anchor.x - 0.5 && near(person.feet.y, 30.0),
          "stepped off onto the edge, inward");
  // Exhausted on the rope: a slide to the bottom.
  grab_rope(person, rope, 2.0);
  person.stamina = 0.05;
  WalkInput hang;
  last = climb_rope(person, rope, hang, 1.0 / 60.0, env, rules);
  require(last.sliding && person.exhausted && last.moved_meters > 0, "spent, the climber slides");
  for (int i = 0; i < 60 * 60 && person.on_rope; ++i) last = climb_rope(person, rope, hang, 1.0 / 60.0, env, rules);
  require(last.stepped_off && person.landed && near(person.last_impact_mps, rules.slide_speed_mps), "to the bottom, landing at the slide speed");
  // Letting go falls.
  person.exhausted = false; person.stamina = 1.0;
  grab_rope(person, rope, 4.0);
  WalkInput jump; jump.jump = true;
  last = climb_rope(person, rope, jump, 1.0 / 60.0, env, rules);
  require(last.let_go && !person.on_rope && !person.grounded, "let go: falling");
  // Pulled in again.
  coil_rope(rope);
  require(rope.state == RopeState::Coiled, "coiled again");
  // A rope thrown over flat ground just lies there.
  throw_rope(rope, {50, 18, 0}, {50, 19.7, 0}, {1, 0, 0}, rules);
  for (int i = 0; i < 600 && rope.state == RopeState::Flying; ++i) advance_rope(rope, 1.0 / 60.0, 9.81, cliff, env.lower, env.upper, {}, rules);
  require(rope.state == RopeState::Laid && near(rope.anchor.x, 50.0) && rope.end.x > 55.0, "on the flat it lies from the feet to where it fell");
  require(!rope_grab_point(rope, {50, 18, 0}, 1.4, rules), "a rope lying on the ground is nothing to climb");
  require(rope_props(rope, {50, 19, 0}, 8).size() >= 1, "and is drawn");
  // A rope too short for the drop: the coil is checked at the rope's length and comes down short.
  RopeRules short_rope = rules; short_rope.length_meters = 6.0;
  Rope dangling;
  throw_rope(dangling, feet, eye, Vec3{1, -0.2, 0}.normalized(), short_rope);
  for (int i = 0; i < 600 && dangling.state == RopeState::Flying; ++i) advance_rope(dangling, 1.0 / 60.0, 9.81, cliff, env.lower, env.upper, {}, short_rope);
  require(dangling.state != RopeState::Flying && dangling.paid_out_meters <= short_rope.length_meters + 1e-6, "a short rope pays out its length and no more");
  // A gentle slope is no face: the rope lies on it however far it drops.
  const StandingHeightFn slope = [](double x, double z) -> std::optional<double> { if (std::abs(x) > 100 || std::abs(z) > 100) return std::nullopt; return 30.0 - 0.3 * std::max(0.0, x); };
  Rope on_slope;
  throw_rope(on_slope, {0, 30, 0}, {0, 31.7, 0}, {1, 0, 0}, rules);
  for (int i = 0; i < 600 && on_slope.state == RopeState::Flying; ++i) advance_rope(on_slope, 1.0 / 60.0, 9.81, slope, env.lower, env.upper, {}, rules);
  require(on_slope.state == RopeState::Laid && on_slope.end.y < 27.0, "on a walkable slope the rope only lies");
}

void test_rope_up_a_tree() {
  RopeRules rules;
  const StandingHeightFn flat = [](double x, double z) -> std::optional<double> { if (std::abs(x) > 100 || std::abs(z) > 100) return std::nullopt; return 20.0; };
  WalkEnvironment env;
  env.standing_height = flat;
  env.lower = {-100, 0, -100};
  env.upper = {100, 100, 100};
  // A crown between x 5 and 7, above 23 m: the coil passing through wraps round a branch there.
  int calls = 0;
  const BranchCatchFn crown = [&](Vec3 from, Vec3 to) -> std::optional<RopeCatch> {
    ++calls;
    const Vec3 p = (from + to) * 0.5;
    if (p.x >= 5.0 && p.x <= 7.0 && p.y >= 23.0) return RopeCatch{{5.5, p.y, p.z}, 3};
    return std::nullopt;
  };
  Rope rope;
  throw_rope(rope, {0, 20, 0}, {0, 21.7, 0}, Vec3{1, 1, 0}.normalized(), rules);
  for (int i = 0; i < 600 && rope.state == RopeState::Flying; ++i) advance_rope(rope, 1.0 / 60.0, 9.81, flat, env.lower, env.upper, crown, rules);
  require(calls > 0 && rope.state == RopeState::Hung && rope.branch_tree == 3, "thrown up through the crown, it wraps round a branch");
  require(near(rope.anchor.x, 5.5) && rope.anchor.y >= 23.0 && near(rope.end.x, 5.5) && near(rope.end.y, 20.0), "and hangs straight down to the ground");
  require(near(rope.anchor_height_meters, rope.anchor.y - 20.0), "the branch's height is known");
  FirstPersonCamera person;
  person.feet = {5.8, 20.0, 0.3};
  const auto at = rope_grab_point(rope, person.feet, 1.4, rules);
  require(at && *at > rope.span_meters() - 1.5, "standing under it, the rope is taken near its end");
  grab_rope(person, rope, *at);
  WalkInput up; up.forward = 1;
  RopeClimbResult last;
  for (int i = 0; i < 60 * 60 && !last.at_top; ++i) last = climb_rope(person, rope, up, 1.0 / 60.0, env, rules);
  require(last.at_top && person.on_rope && near(person.feet.y, rope.anchor.y), "climbed up under the branch");
  require(!step_off_rope_top(person, rope, env, rules) && person.on_rope, "there is nothing to step off onto up a tree: a perch");
  WalkInput down; down.forward = -1;
  for (int i = 0; i < 60 * 60 && person.on_rope; ++i) last = climb_rope(person, rope, down, 1.0 / 60.0, env, rules);
  require(last.stepped_off && person.grounded && near(person.feet.y, 20.0), "and back down to the ground");
  // The rope gone from under the hands (coiled by someone) drops the climber.
  grab_rope(person, rope, 2.0);
  coil_rope(rope);
  last = climb_rope(person, rope, WalkInput{}, 1.0 / 60.0, env, rules);
  require(last.let_go && !person.on_rope, "a rope pulled in drops whoever hung on it");
  // Props: a flight shows the line and the coil.
  Rope flying;
  throw_rope(flying, {0, 20, 0}, {0, 21.7, 0}, {1, 0, 0}, rules);
  advance_rope(flying, 0.2, 9.81, flat, env.lower, env.upper, {}, rules);
  const auto props = rope_props(flying, {0, 21.5, 0}, 8);
  require(flying.state == RopeState::Flying && props.size() >= 2 && props.back().size.x > 0.2, "a flying rope is drawn as a line and a coil");
  require(rope_props(Rope{}, {0, 0, 0}, 8).empty(), "a coiled rope is not drawn in the world");
}
}  // namespace

int main() {
  try { test_belt_and_cutting(); test_rope_down_a_cliff(); test_rope_up_a_tree(); }
  catch (const std::exception& error) { std::cerr << "tools tests failed: " << error.what() << '\n'; return 1; }
  std::cout << "tools tests passed\n";
  return 0;
}
