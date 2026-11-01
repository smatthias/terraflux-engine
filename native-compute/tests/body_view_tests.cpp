#include <terraflux/graphics/body_view.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace terraflux::graphics;
using terraflux::math::Vec3;

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template <class F> void rejects(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const std::invalid_argument&) { caught = true; }
  require(caught, message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) < tolerance; }
BodyState state(BodyActivity a, double phase = 0.0, int stance = 0, double speed = 1.0, double pitch = 0.0) {
  BodyState s; s.activity = a; s.phase = phase; s.stance_foot = stance; s.speed_fraction = speed; s.pitch = pitch; return s;
}
Vec3 origin(const terraflux::math::Mat4& m) { return {m.at(0, 3), m.at(1, 3), m.at(2, 3)}; }

void test_poses() {
  BodyStyle style;
  validate_body_style(style);
  rejects([] { BodyStyle s; s.arm_radius = 0; validate_body_style(s); }, "a zero arm radius is rejected");
  // Segment lengths are the style's, the arms are mirrored, the hands are where the forearms end.
  const auto idle = body_pose(state(BodyActivity::Idle), style);
  for (int side = 0; side < 2; ++side) {
    const auto base = static_cast<std::size_t>(side) * kArmSegments;
    const auto& upper = idle[base], &fore = idle[base + 1], &palm = idle[base + 2];
    require(near(upper.size.z, style.upper_arm_length, 1e-6) && near(fore.size.z, style.forearm_length, 1e-6) && near(palm.size.z, style.hand_length * 0.5, 1e-6),
            "segment lengths follow the style");
    require(palm.color.x == style.skin_color.x && upper.color.x == style.sleeve_color.x, "sleeves and skin");
    // Five digits: a thumb and four fingers, each of two segments, all skin.
    for (std::size_t k = base + 3; k < base + kArmSegments; ++k) require(idle[k].size.z > 0.01 && idle[k].color.x == style.skin_color.x, "fingers are skin");
    const auto tips = fingertips(idle, side);
    const auto palm_centre = Vec3{palm.transform.at(0, 3), palm.transform.at(1, 3), palm.transform.at(2, 3)};
    for (const auto& tip : tips) require((tip - palm_centre).length() > 0.05 && (tip - palm_centre).length() < style.hand_length + 0.05, "fingertips lie a finger's length from the palm");
    // The thumb stands apart from the fingers; the fingers are spread across the palm.
    require((tips[0] - tips[1]).length() > 0.03 && (tips[1] - tips[4]).length() > 0.03, "the thumb is apart from the fingers, which are spread");
  }
  require(idle[kHeldSegment].size.z == 0, "empty hands hold nothing");
  // A fist curls the fingertips back toward the palm; an open hand reaches forward.
  auto fist = state(BodyActivity::Climb); auto open = state(BodyActivity::Swim);
  const auto fist_pose = body_pose(fist, style), open_pose = body_pose(open, style);
  const auto fist_tips = fingertips(fist_pose, 1), open_tips = fingertips(open_pose, 1);
  const auto fist_palm = hand_positions(fist_pose)[1], open_palm = hand_positions(open_pose)[1];
  require((open_tips[2] - open_palm).length() > (fist_tips[2] - fist_palm).length() + 0.03, "a gripping hand curls its fingers in, an open hand stretches them");
  // A held stone sits in the right hand, which grips it and lifts it into view.
  auto holding = state(BodyActivity::Idle); holding.held = HeldKind::Stone; holding.held_size = 0.12;
  const auto held_pose = body_pose(holding, style);
  require(held_pose[kHeldSegment].size.x > 0.2 && hand_positions(held_pose)[1].z > 0.3 && hand_positions(held_pose)[1].y > -0.6, "a stone is held up in the right hand");
  const auto stone_centre = held_pose[kHeldSegment].transform.transformPoint({0, 0, 0});
  require((stone_centre - hand_positions(held_pose)[1]).length() < 0.2, "the stone lies in the palm");
  auto torch = holding; torch.held = HeldKind::Torch; torch.held_size = 0.8;
  const auto torch_pose = body_pose(torch, style);
  require(near(torch_pose[kHeldSegment].size.z, 0.8, 1e-9), "a branch is held at its length");
  {
    // Thumb-up grip: the stick stands up out of the right fist, its lower end a hand below the palm.
    const auto& m = torch_pose[kHeldSegment].transform;
    const Vec3 axis{m.at(0, 2), m.at(1, 2), m.at(2, 2)};
    const Vec3 centre{m.at(0, 3), m.at(1, 3), m.at(2, 3)};
    const Vec3 palm = hand_positions(torch_pose)[1];
    const Vec3 to_palm = palm - centre;
    const double along = to_palm.dot(axis);
    const double off_axis = (to_palm - axis * along).length();
    require(axis.y > 0.8 && off_axis < 0.08 && along < -0.2 && along > -0.4, "the stick rises up through the fist");
    const auto tips = fingertips(torch_pose, 1);
    require(tips[0].y > palm.y + 0.02, "the thumb is on top");
  }
  require(std::string(held_kind_name(HeldKind::Torch)) == "torch", "held kind names");
  const auto hands_idle = hand_positions(idle);
  require(near(hands_idle[0].x, -hands_idle[1].x, 1e-9) && near(hands_idle[0].y, hands_idle[1].y) && hands_idle[0].x < 0, "the arms are mirrored");
  require(hands_idle[0].y < -0.6 && hands_idle[0].z < 0.3, "idle, the hands hang below the view");
  // Looking down brings the hanging hands in front of the eye.
  const auto down = hand_positions(body_pose(state(BodyActivity::Idle, 0, 0, 0, -1.1), style));
  require(down[0].z > 0.5 && down[0].y > hands_idle[0].y, "looking down, the hands come into view");
  // Running pumps the arms: the hands rise and swing, alternating with the stance foot.
  const auto run_a = hand_positions(body_pose(state(BodyActivity::Run, 0.25, 0), style));
  const auto run_b = hand_positions(body_pose(state(BodyActivity::Run, 0.75, 0), style));
  // With the left foot planted the right arm is forward (and up), the left back.
  require(run_a[1].y > hands_idle[1].y + 0.2 && run_a[1].z > 0.3 && run_a[0].z < run_a[1].z, "running, the arm opposite the stance foot is up in front");
  require((run_a[0].z - run_b[0].z) * (run_a[1].z - run_b[1].z) < 0, "the arms swing against each other");
  const auto run_other = hand_positions(body_pose(state(BodyActivity::Run, 0.25, 1), style));
  require(run_other[0].z > run_other[1].z, "with the other foot planted the swing is mirrored");
  // Climbing: both hands above the eye, reaching alternately.
  const auto climb_a = hand_positions(body_pose(state(BodyActivity::Climb, 0.25), style));
  const auto climb_b = hand_positions(body_pose(state(BodyActivity::Climb, 0.75), style));
  require(climb_a[0].y > 0.0 && climb_a[1].y > 0.0 && climb_a[0].z > 0.3, "climbing, the hands reach up in front");
  require((climb_a[0].y - climb_b[0].y) * (climb_a[1].y - climb_b[1].y) < 0, "one hand reaches while the other holds");
  // Swimming: hands forward and low, sweeping out; the crawl reaches alternately; diving together.
  const auto swim_in = hand_positions(body_pose(state(BodyActivity::Swim, 0.0), style));
  const auto swim_out = hand_positions(body_pose(state(BodyActivity::Swim, 0.5), style));
  require(swim_in[0].z > 0.4 && swim_in[0].y < -0.3 && std::abs(swim_out[0].x) > std::abs(swim_in[0].x) + 0.2, "the breaststroke sweeps the hands out");
  const auto crawl = hand_positions(body_pose(state(BodyActivity::Crawl, 0.25), style));
  require((crawl[0].z - crawl[1].z) != 0 && std::abs(crawl[0].z - crawl[1].z) > 0.2, "the crawl reaches one hand ahead of the other");
  const auto dive = hand_positions(body_pose(state(BodyActivity::Dive), style));
  require(near(dive[0].x, -dive[1].x) && std::abs(dive[0].x) < 0.2 && dive[0].z > 0.4, "diving, the hands go forward together");
  const auto down_pose = hand_positions(body_pose(state(BodyActivity::Down), style));
  require(down_pose[0].y < -0.7 && down_pose[0].z > 0.3, "down, the arms are out on the ground");
  // Reach is limited: no pose stretches an arm beyond its length, and the poses are smooth in the phase.
  for (const auto activity : {BodyActivity::Walk, BodyActivity::Run, BodyActivity::Climb, BodyActivity::Swim, BodyActivity::Crawl}) {
    Vec3 last{};
    for (int i = 0; i <= 40; ++i) {
      const auto pose = body_pose(state(activity, i / 40.0), style);
      const auto upper = origin(pose[0].transform), hand = origin(pose[2].transform);  // shoulder end and palm
      require((hand - upper).length() < style.upper_arm_length + style.forearm_length + style.hand_length, "the arm never overreaches");
      if (i > 0) require((hand - last).length() < 0.08, "the pose changes smoothly with the phase");
      last = hand;
    }
  }
  rejects([&] { BodyState s; s.phase = std::nan(""); (void)body_pose(s, style); }, "a non-finite state is rejected");
  require(std::string(body_activity_name(BodyActivity::Crawl)) == "crawl", "activity names");
}

void test_state_and_geometry() {
  FirstPersonCamera c;
  require(body_state_from(c, 2.5, 3.0).activity == BodyActivity::Idle, "standing still is idle");
  c.gait_speed_mps = 2.5; c.gait_phase = 0.3; c.stance_foot = 1;
  auto s = body_state_from(c, 2.5, 3.0);
  require(s.activity == BodyActivity::Walk && near(s.phase, 0.3) && s.stance_foot == 1 && near(s.speed_fraction, 2.5 / 7.5), "walking at the walking speed");
  c.gait_speed_mps = 7.0;
  require(body_state_from(c, 2.5, 3.0).activity == BodyActivity::Run, "faster is running");
  c.crouch_blend = 1.0;
  require(body_state_from(c, 2.5, 3.0).activity == BodyActivity::Crouch, "crouched");
  c.crouch_blend = 0; c.climbing = true;
  require(body_state_from(c, 2.5, 3.0).activity == BodyActivity::Climb, "climbing");
  c.climbing = false; c.swimming = true; c.swim_phase = 0.6;
  require(body_state_from(c, 2.5, 3.0).activity == BodyActivity::Swim && near(body_state_from(c, 2.5, 3.0).phase, 0.6), "swimming with the stroke phase");
  c.crawling = true;
  require(body_state_from(c, 2.5, 3.0).activity == BodyActivity::Crawl, "crawling");
  c.diving = true;
  require(body_state_from(c, 2.5, 3.0).activity == BodyActivity::Dive, "diving");
  c.stunned_seconds = 2;
  require(body_state_from(c, 2.5, 3.0).activity == BodyActivity::Down, "knocked down");
  // The box and the camera-to-world mapping.
  const auto box = make_unit_box();
  require(box.vertices.size() == 24 && box.indices.size() == 36, "a box of six faces");
  for (const auto& v : box.vertices) require(std::abs(v.position[0]) <= 0.5 && std::abs(v.position[1]) <= 0.5 && std::abs(v.position[2]) <= 0.5, "a unit box");
  for (std::size_t t = 0; t < box.indices.size(); t += 3) {
    const auto& a = box.vertices[box.indices[t]]; const auto& b = box.vertices[box.indices[t + 1]]; const auto& c3 = box.vertices[box.indices[t + 2]];
    const Vec3 pa{a.position[0], a.position[1], a.position[2]}, pb{b.position[0], b.position[1], b.position[2]}, pc{c3.position[0], c3.position[1], c3.position[2]};
    const Vec3 n{a.normal[0], a.normal[1], a.normal[2]};
    require((pb - pa).cross(pc - pa).dot(n) > 0, "faces wind outward");
  }
  const auto m = camera_to_world({10, 20, 30}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1});
  const auto p = m.transformPoint({0.5, -0.3, 0.6});
  require(near(p.x, 10.5) && near(p.y, 19.7) && near(p.z, 29.4), "camera space maps to the world along the basis");
  rejects([] { (void)camera_to_world({std::nan(""), 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}); }, "non-finite basis rejected");
}
}  // namespace

void test_tools() {
  BodyStyle style;
  const auto state = [](BodyActivity activity) { BodyState s; s.activity = activity; return s; };
  const auto origin = [](const terraflux::math::Mat4& m) { return Vec3{m.at(0, 3), m.at(1, 3), m.at(2, 3)}; };
  const auto bare = body_pose(state(BodyActivity::Idle), style);
  require(bare[kToolSegment].size.x == 0 && bare[kToolSegment + 1].size.x == 0 && !tool_tip(bare, ToolKind::None), "no tool, no tool segments");
  require(kBodySegments == 2 * kArmSegments + 1 + kToolSegments && kToolSegment == kHeldSegment + 1, "the tool segments follow the held item");
  require(std::string(tool_kind_name(ToolKind::Rope)) == "rope", "tool names");
  // The axe: haft and head, both hands on the haft, the head at its end.
  auto axe = state(BodyActivity::Idle); axe.tool = ToolKind::Axe;
  const auto axe_pose = body_pose(axe, style);
  const auto& haft = axe_pose[kToolSegment];
  const auto& head = axe_pose[kToolSegment + 1];
  require(haft.size.z > 0.7 && head.size.z > 0.1 && head.color.x > 0.6, "an axe has a long haft and a red head");
  const auto hands = hand_positions(axe_pose);
  const Vec3 haft_dir{haft.transform.at(0, 2), haft.transform.at(1, 2), haft.transform.at(2, 2)};
  const auto off_haft = [&](Vec3 p) { const Vec3 d = p - origin(haft.transform); return (d - haft_dir * d.dot(haft_dir)).length(); };
  require(off_haft(hands[1]) < 0.1 && off_haft(hands[0]) < 0.1 && (hands[0] - hands[1]).length() > 0.2, "both hands grip the haft, apart");
  require((origin(head.transform) - origin(haft.transform)).length() > 0.3, "the head is at the far end");
  const auto tip = tool_tip(axe_pose, ToolKind::Axe);
  require(tip && (*tip - origin(head.transform)).length() < 0.15, "the edge is at the head");
  require(item_hand(axe) == 0, "with a tool out the item goes to the left hand");
  // The swing: raised over the shoulder, then brought down and forward, then back to rest.
  auto raised = axe; raised.swing = 0.35;
  auto struck = axe; struck.swing = 0.55;
  auto recovered = axe; recovered.swing = 1.0;
  const auto rest_hand = hand_positions(axe_pose)[1], up_hand = hand_positions(body_pose(raised, style))[1], hit_hand = hand_positions(body_pose(struck, style))[1];
  require(up_hand.y > rest_hand.y + 0.25, "raised, the right hand is well above its rest");
  require(hit_hand.z > up_hand.z + 0.2 && hit_hand.y < up_hand.y - 0.2, "struck, it is down and forward");
  const auto hit_tip = tool_tip(body_pose(struck, style), ToolKind::Axe);
  require(hit_tip && hit_tip->z > hit_hand.z, "the edge leads the hand into the wood");
  require((hand_positions(body_pose(recovered, style))[1] - rest_hand).length() < 1e-9, "a finished swing is the rest");
  Vec3 last = rest_hand;
  for (int i = 1; i <= 50; ++i) {
    auto s = axe; s.swing = i / 50.0;
    const auto h = hand_positions(body_pose(s, style))[1];
    require((h - last).length() < 0.12, "the swing is smooth");
    last = h;
  }
  // The chainsaw: body and bar, the left hand on its top handle; sawing pushes it forward and trembles.
  auto saw = state(BodyActivity::Idle); saw.tool = ToolKind::Chainsaw;
  const auto saw_pose = body_pose(saw, style);
  require(saw_pose[kToolSegment].size.x > 0.2 && saw_pose[kToolSegment + 1].size.z > 0.4, "a chainsaw has a body and a long bar");
  const auto saw_hands = hand_positions(saw_pose);
  require(saw_hands[0].y > saw_hands[1].y && saw_hands[0].x < saw_hands[1].x, "the left hand is up on the top handle, the right on the rear");
  const auto bar_tip = tool_tip(saw_pose, ToolKind::Chainsaw);
  require(bar_tip && bar_tip->z > saw_hands[1].z + 0.5, "the bar's tip is well ahead of the hands");
  auto running = saw; running.sawing = true; running.saw_phase = 0.01;
  auto running2 = running; running2.saw_phase = 0.02;
  const auto run_tip = tool_tip(body_pose(running, style), ToolKind::Chainsaw), run_tip2 = tool_tip(body_pose(running2, style), ToolKind::Chainsaw);
  require(run_tip && run_tip->z > bar_tip->z + 0.08 && (*run_tip - *run_tip2).length() > 1e-4, "sawing, the bar is pushed forward and trembles");
  // A torch in the left hand while the axe is out; the rope coil hangs from the right hand.
  auto both = axe; both.held = HeldKind::Torch; both.held_size = 0.8;
  const auto both_pose = body_pose(both, style);
  const auto torch_centre = origin(both_pose[kHeldSegment].transform);
  require(both_pose[kHeldSegment].size.z == 0.8 && torch_centre.x < 0 && (torch_centre - hand_positions(both_pose)[0]).length() < 0.5, "the torch is in the left hand");
  require(both_pose[kToolSegment].size.z > 0.7, "and the axe still in the right");
  auto rope = state(BodyActivity::Walk); rope.tool = ToolKind::Rope;
  const auto rope_pose = body_pose(rope, style);
  require(rope_pose[kToolSegment].size.x > 0.15 && rope_pose[kToolSegment + 1].size.x == 0 && origin(rope_pose[kToolSegment].transform).y < hand_positions(rope_pose)[1].y,
          "the coil hangs below the right hand");
  // Climbing or swimming the hands are needed: no tool in them.
  auto climbing = state(BodyActivity::Climb); climbing.tool = ToolKind::Axe;
  require(body_pose(climbing, style)[kToolSegment].size.x == 0 && item_hand(climbing) == 1, "climbing, the tool is away");
  const auto shapes = hand_shapes(axe);
  require(shapes[0].curl > 1.0 && shapes[1].curl > 1.0, "both hands grip the axe");
  rejects([&] { BodyState s; s.swing = std::nan(""); (void)body_pose(s, style); }, "a non-finite swing is rejected");
}

int main() {
  try { test_poses(); test_state_and_geometry(); test_tools(); }
  catch (const std::exception& error) { std::cerr << "body view tests failed: " << error.what() << '\n'; return 1; }
  std::cout << "body view tests passed\n";
  return 0;
}
