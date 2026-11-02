#include <terraflux/graphics/body_view.hpp>

#include <terraflux/graphics/mesh_upload.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>

namespace terraflux::graphics {
namespace {

constexpr double kPi = 3.14159265358979323846;
bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// A basis whose z axis points along `direction`, placed at `centre`.
math::Mat4 frame_along(math::Vec3 centre, math::Vec3 direction, math::Vec3 up_hint) {
  const double len = direction.length();
  const math::Vec3 z = len > 1e-9 ? direction / len : math::Vec3{0, 0, 1};
  math::Vec3 x = up_hint.cross(z);
  if (x.length() < 1e-6) x = math::Vec3{1, 0, 0}.cross(z);
  x = x.normalized();
  const math::Vec3 y = z.cross(x).normalized();
  math::Mat4 m = math::Mat4::identity();
  m.at(0, 0) = x.x; m.at(1, 0) = x.y; m.at(2, 0) = x.z;
  m.at(0, 1) = y.x; m.at(1, 1) = y.y; m.at(2, 1) = y.z;
  m.at(0, 2) = z.x; m.at(1, 2) = z.y; m.at(2, 2) = z.z;
  m.at(0, 3) = centre.x; m.at(1, 3) = centre.y; m.at(2, 3) = centre.z;
  return m;
}

// Body space (x right, y up, z forward, level) to camera space (pitched by the look).
math::Vec3 body_to_camera(math::Vec3 b, double pitch) {
  const double c = std::cos(pitch), s = std::sin(pitch);
  return {b.x, b.y * c - b.z * s, b.y * s + b.z * c};
}

// A segment of `length` along the local z axis of `frame`, starting at local `start`.
BodySegment segment_in(const math::Mat4& frame, math::Vec3 start, math::Vec3 direction, double length, double radius, math::Vec3 color) {
  const math::Vec3 d = direction.normalized();
  const math::Vec3 centre_local = start + d * (length * 0.5);
  // The segment frame: z along the direction, expressed in the parent frame.
  const math::Mat4 local = frame_along(centre_local, d, {0, 1, 0});
  BodySegment seg;
  seg.transform = frame * local;
  seg.size = {radius * 2.0, radius * 2.0, length};
  seg.color = color;
  return seg;
}

// Palm, thumb and four fingers in the wrist frame (z along the hand, y the
// back of the hand, x across it). The thumb lies on the medial side: with the
// back of the hand up, the right thumb is at -x and the left at +x.
void pose_hand(BodySegment* out, const math::Mat4& wrist, double side_sign, const HandShape& shape, const BodyStyle& style) {
  const double palm_len = style.hand_length * 0.5, finger_len = style.hand_length * 0.5;
  const double w = style.palm_width, fr = style.finger_radius;
  const double thumb_side = -side_sign;
  // Palm: a flat box from the wrist forward.
  {
    const math::Mat4 local = frame_along({0, 0, palm_len * 0.5}, {0, 0, 1}, {0, 1, 0});
    out[0].transform = wrist * local;
    out[0].size = {w, fr * 2.6, palm_len};
    out[0].color = style.skin_color;
  }
  // Fingers: four across the palm's end, curling toward the palm (-y).
  const double curl = std::clamp(shape.curl, 0.0, 1.5), spread = std::clamp(shape.spread, 0.0, 1.0);
  for (int f = 0; f < 4; ++f) {
    const double x = (-1.5 + f) * (w / 4.2) * (0.6 + 0.4 * spread) * thumb_side;  // index nearest the thumb
    const double length_scale = f == 1 || f == 2 ? 1.0 : (f == 3 ? 0.8 : 0.9);
    const double proximal = finger_len * 0.55 * length_scale, distal = finger_len * 0.45 * length_scale;
    const double a1 = curl * 0.9, a2 = curl * 1.2;
    const math::Vec3 d1{0, -std::sin(a1), std::cos(a1)};
    const math::Vec3 base{x, 0, palm_len};
    out[1 + f * 2] = segment_in(wrist, base, d1, proximal, fr, style.skin_color);
    const math::Vec3 knuckle = base + d1 * proximal;
    const math::Vec3 d2{0, -std::sin(a1 + a2), std::cos(a1 + a2)};
    out[2 + f * 2] = segment_in(wrist, knuckle, d2, distal, fr * 0.9, style.skin_color);
  }
  // Thumb: from the side of the palm, angled out and forward, opposing with its curl.
  {
    const double t = std::clamp(shape.thumb_curl, 0.0, 1.5);
    const math::Vec3 base{thumb_side * w * 0.5, -fr * 0.5, palm_len * 0.35};
    const math::Vec3 d1 = math::Vec3{thumb_side * (0.75 - 0.45 * t), -0.25 * t, 0.65 + 0.1 * t}.normalized();
    out[9] = segment_in(wrist, base, d1, finger_len * 0.5, fr * 1.15, style.skin_color);
    const math::Vec3 joint = base + d1 * (finger_len * 0.5);
    const math::Vec3 d2 = math::Vec3{thumb_side * (0.45 - 0.55 * t), -0.35 * t, 0.85}.normalized();
    out[10] = segment_in(wrist, joint, d2, finger_len * 0.4, fr * 1.05, style.skin_color);
  }
}

// Two-segment arm from the shoulder to a wrist target, the elbow toward the
// pole, then the hand in the wrist frame. Returns the wrist frame.
math::Mat4 pose_arm(BodySegment* out, math::Vec3 shoulder, math::Vec3 hand, math::Vec3 pole, const BodyStyle& style, double pitch,
                    double side_sign, const HandShape& shape, std::optional<math::Vec3> grip_axis = std::nullopt) {
  const double l1 = style.upper_arm_length, l2 = style.forearm_length;
  math::Vec3 to = hand - shoulder;
  double d = to.length();
  const double reach = l1 + l2 - 0.01;
  if (d > reach) { to = to * (reach / d); d = reach; hand = shoulder + to; }
  if (d < 1e-6) { to = {0, -1e-3, 0}; d = 1e-3; hand = shoulder + to; }
  const math::Vec3 dir = to / d;
  const double a = std::clamp((l1 * l1 - l2 * l2 + d * d) / (2.0 * d), -l1, l1);
  const double h = std::sqrt(std::max(0.0, l1 * l1 - a * a));
  math::Vec3 perp = pole - dir * pole.dot(dir);
  if (perp.length() < 1e-6) perp = math::Vec3{0, -1, 0} - dir * dir.y;
  perp = perp.normalized();
  const math::Vec3 elbow = shoulder + dir * a + perp * h;
  const math::Vec3 wrist = hand;
  const math::Vec3 fore_dir = (wrist - elbow).normalized();
  const math::Vec3 up_hint = body_to_camera({0, 1, 0}, pitch);
  const double r = style.arm_radius * 2.0;
  out[0].transform = frame_along((shoulder + elbow) * 0.5, elbow - shoulder, up_hint);
  out[0].size = {r, r, (elbow - shoulder).length()};
  out[0].color = style.sleeve_color;
  out[1].transform = frame_along((elbow + wrist) * 0.5, wrist - elbow, up_hint);
  out[1].size = {r * 0.85, r * 0.85, (wrist - elbow).length()};
  out[1].color = style.sleeve_color;
  // The hand continues along the forearm; its back faces the up hint unless
  // the forearm is turned to grip a stick: then the wrist's -x axis (the thumb
  // side, see pose_hand) lies along `grip_axis` as far as the forearm allows.
  math::Vec3 hand_up = up_hint;
  if (grip_axis) {
    const math::Vec3 perp = *grip_axis - fore_dir * grip_axis->dot(fore_dir);
    if (perp.length() > 1e-6) hand_up = perp.normalized().cross(fore_dir) * side_sign;
  }
  const math::Mat4 wrist_frame = frame_along(wrist, fore_dir, hand_up);
  pose_hand(out + 2, wrist_frame, side_sign, shape, style);
  return wrist_frame;
}

}  // namespace

const char* held_kind_name(HeldKind kind) {
  switch (kind) {
    case HeldKind::Nothing: return "nothing";
    case HeldKind::Stone: return "stone";
    case HeldKind::Branch: return "branch";
    case HeldKind::Torch: return "torch";
  }
  throw std::invalid_argument("unknown held kind");
}

const char* tool_kind_name(ToolKind kind) {
  switch (kind) {
    case ToolKind::None: return "none";
    case ToolKind::Rope: return "rope";
    case ToolKind::Axe: return "axe";
    case ToolKind::Chainsaw: return "chainsaw";
  }
  throw std::invalid_argument("unknown tool kind");
}

namespace {
// Whether the arms are free for carrying (not climbing, swimming or lying down).
bool hands_free(BodyActivity activity) {
  return activity != BodyActivity::Climb && activity != BodyActivity::Down && activity != BodyActivity::Swim &&
         activity != BodyActivity::Crawl && activity != BodyActivity::Dive;
}
double smoothstep01(double t) { t = std::clamp(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }
math::Vec3 lerp3(math::Vec3 a, math::Vec3 b, double t) { return a + (b - a) * t; }
}  // namespace

int item_hand(const BodyState& state) { return state.tool != ToolKind::None && hands_free(state.activity) ? 0 : 1; }

std::array<HandShape, 2> hand_shapes(const BodyState& state) {
  HandShape shape;
  switch (state.activity) {
    case BodyActivity::Idle: shape = {0.45, 0.25, 0.5}; break;
    case BodyActivity::Walk: shape = {0.55, 0.2, 0.55}; break;
    case BodyActivity::Run: shape = {0.9, 0.1, 0.8}; break;
    case BodyActivity::Crouch: shape = {0.5, 0.3, 0.5}; break;
    case BodyActivity::Climb: shape = {1.15, 0.15, 0.9}; break;
    case BodyActivity::Swim: case BodyActivity::Crawl: case BodyActivity::Dive: shape = {0.05, 0.05, 0.15}; break;
    case BodyActivity::Down: shape = {0.3, 0.5, 0.3}; break;
  }
  std::array<HandShape, 2> shapes{shape, shape};
  const auto ih = static_cast<std::size_t>(item_hand(state));
  if (state.held == HeldKind::Stone) {
    // The fingers close over a stone as far as it lets them: a pebble in a fist, a big stone in an open grasp.
    const double curl = std::clamp(1.3 - std::max(0.0, state.held_size) * 6.0, 0.4, 1.2);
    shapes[ih] = {curl, 0.15, std::min(1.1, curl)};
  } else if (state.held != HeldKind::Nothing) shapes[ih] = {1.2, 0.05, 1.1};  // the hand grips the stick
  if (state.tool != ToolKind::None && hands_free(state.activity)) {
    shapes[1] = {1.2, 0.05, 1.1};  // the right hand grips the tool
    // Both hands on an axe or a chainsaw when the left is free.
    if (state.tool != ToolKind::Rope && state.held == HeldKind::Nothing) shapes[0] = {1.2, 0.05, 1.1};
  }
  return shapes;
}
const char* body_activity_name(BodyActivity activity) {
  switch (activity) {
    case BodyActivity::Idle: return "idle";
    case BodyActivity::Walk: return "walk";
    case BodyActivity::Run: return "run";
    case BodyActivity::Crouch: return "crouch";
    case BodyActivity::Climb: return "climb";
    case BodyActivity::Swim: return "swim";
    case BodyActivity::Crawl: return "crawl";
    case BodyActivity::Dive: return "dive";
    case BodyActivity::Down: return "down";
  }
  throw std::invalid_argument("unknown body activity");
}

void validate_body_style(const BodyStyle& s) {
  if (!within(s.shoulder_width, 0.1, 1) || !within(s.shoulder_drop, 0, 1) || !within(s.upper_arm_length, 0.1, 0.6) ||
      !within(s.forearm_length, 0.1, 0.6) || !within(s.hand_length, 0.05, 0.4) || !within(s.arm_radius, 0.01, 0.15) ||
      !within(s.min_hand_distance, 0, 2) || !within(s.palm_width, 0.03, 0.2) || !within(s.finger_radius, 0.002, 0.03) ||
      !finite(s.sleeve_color) || !finite(s.skin_color))
    throw std::invalid_argument("body style out of range");
}

BodyState body_state_from(const FirstPersonCamera& c, double walk_speed_mps, double run_multiplier) {
  BodyState b;
  b.pitch = c.pitch;
  b.crouch_blend = c.crouch_blend;
  b.stance_foot = c.stance_foot;
  if (c.stunned_seconds > 0) { b.activity = BodyActivity::Down; return b; }
  if (c.swimming) {
    b.activity = c.diving ? BodyActivity::Dive : (c.crawling ? BodyActivity::Crawl : BodyActivity::Swim);
    b.phase = c.swim_phase;
    b.speed_fraction = 1.0;
    return b;
  }
  if (c.climbing || c.on_rope) { b.activity = BodyActivity::Climb; b.phase = c.on_rope ? c.rope_phase : c.gait_phase; b.speed_fraction = 1.0; return b; }
  const double run_speed = std::max(walk_speed_mps * std::max(run_multiplier, 1.0), 1e-6);
  b.speed_fraction = std::clamp(c.gait_speed_mps / run_speed, 0.0, 1.0);
  b.phase = c.gait_phase;
  if (c.crouch_blend > 0.5) { b.activity = BodyActivity::Crouch; return b; }
  if (c.gait_speed_mps <= 0.05) { b.activity = BodyActivity::Idle; return b; }
  b.activity = c.gait_speed_mps > walk_speed_mps * 1.3 ? BodyActivity::Run : BodyActivity::Walk;
  return b;
}

namespace {
// The tool in the right hand: where the hands go (body space) and its two
// boxes (camera space). The axe is swung in three movements (raised over the
// right shoulder, brought down and forward, recovered); the chainsaw is held
// at the waist with the left hand on its top handle and pushed forward,
// trembling, when it saws; the rope hangs coiled from the right hand.
struct ToolRig {
  math::Vec3 right_hand, left_hand;
  bool two_handed = false;
  std::optional<math::Vec3> right_grip, left_grip;  // grip axes (camera space)
  std::array<BodySegment, kToolSegments> segments;
};
ToolRig tool_rig(const BodyState& state, const BodyStyle& style, double swing_x) {
  ToolRig rig;
  const double drop = style.shoulder_drop;
  for (auto& seg : rig.segments) seg.size = {0, 0, 0};
  const auto cam = [&](math::Vec3 b) { return body_to_camera(b, state.pitch); };
  switch (state.tool) {
    case ToolKind::None: break;
    case ToolKind::Rope: {
      rig.right_hand = {0.27, -drop - 0.36 + 0.02 * swing_x, 0.28 + 0.03 * swing_x};
      // The coil: a flat loop hanging below the hand, its plane across the body.
      const math::Vec3 centre = cam(rig.right_hand + math::Vec3{0.0, -0.16, 0.02});
      rig.segments[0].transform = frame_along(centre, cam({1, 0, 0}), cam({0, 1, 0}));
      rig.segments[0].size = {0.22, 0.26, 0.06};
      rig.segments[0].color = {0.72, 0.60, 0.40};
      break;
    }
    case ToolKind::Axe: {
      // Three keyframes of the right hand and the haft's direction (body space).
      const math::Vec3 rest_hand{0.18, -drop - 0.32, 0.36}, rest_dir = math::Vec3{-0.35, 0.75, 0.55}.normalized();
      const math::Vec3 up_hand{0.30, -drop + 0.12, 0.12}, up_dir = math::Vec3{-0.15, 0.55, -0.82}.normalized();
      const math::Vec3 hit_hand{0.10, -drop - 0.22, 0.56}, hit_dir = math::Vec3{0.12, -0.35, 0.93}.normalized();
      math::Vec3 hand = rest_hand, dir = rest_dir;
      const double p = std::clamp(state.swing, 0.0, 1.0);
      if (p > 0 && p < 1) {
        if (p < 0.35) { const double t = smoothstep01(p / 0.35); hand = lerp3(rest_hand, up_hand, t); dir = lerp3(rest_dir, up_dir, t); }
        else if (p < 0.55) { const double t = smoothstep01((p - 0.35) / 0.2); hand = lerp3(up_hand, hit_hand, t); dir = lerp3(up_dir, hit_dir, t); }
        else { const double t = smoothstep01((p - 0.55) / 0.45); hand = lerp3(hit_hand, rest_hand, t); dir = lerp3(hit_dir, rest_dir, t); }
        dir = dir.normalized();
      }
      rig.right_hand = hand;
      rig.left_hand = hand + dir * 0.30;
      rig.two_handed = true;
      rig.right_grip = cam(dir);
      rig.left_grip = cam(dir);
      const double haft = 0.80;
      const math::Vec3 start = cam(hand - dir * 0.06), d = cam(dir);
      rig.segments[0].transform = frame_along(start + d * (haft * 0.5), d, cam({0, 1, 0}));
      rig.segments[0].size = {0.04, 0.04, haft};
      rig.segments[0].color = {0.45, 0.33, 0.20};
      // The head: its edge forward of the haft, in the plane of the swing.
      math::Vec3 blade = cam({0, 0, 1}) - d * cam({0, 0, 1}).dot(d);
      if (blade.length() < 1e-6) blade = cam({0, -1, 0});
      blade = blade.normalized();
      rig.segments[1].transform = frame_along(start + d * (haft - 0.04) + blade * 0.06, blade, d);
      rig.segments[1].size = {0.05, 0.11, 0.20};
      rig.segments[1].color = {0.75, 0.12, 0.10};
      break;
    }
    case ToolKind::Chainsaw: {
      const double push = state.sawing ? 0.14 : 0.0;
      const double tremble = state.sawing ? 0.008 : 0.0;
      const math::Vec3 shake{tremble * std::sin(2.0 * kPi * 31.0 * state.saw_phase), tremble * std::cos(2.0 * kPi * 23.0 * state.saw_phase), 0.0};
      // The bar runs forward on the right of the body (clear of the left arm on
      // the front handle); sawing, it is pushed down and forward into the wood.
      const math::Vec3 dir = (state.sawing ? math::Vec3{0.12, -0.35, 0.93} : math::Vec3{0.18, 0.06, 0.98}).normalized();
      rig.right_hand = math::Vec3{0.22, -drop - 0.30, 0.34} + dir * push + shake;  // the rear handle
      const math::Vec3 body_centre = rig.right_hand + math::Vec3{-0.06, 0.07, 0.0} + dir * 0.16;
      rig.left_hand = body_centre + math::Vec3{-0.10, 0.12, -0.02};                // the top handle
      rig.two_handed = true;
      rig.right_grip = cam(dir);
      rig.left_grip = cam({1, 0, 0});
      rig.segments[0].transform = frame_along(cam(body_centre), cam(dir), cam({0, 1, 0}));
      rig.segments[0].size = {0.22, 0.15, 0.26};
      rig.segments[0].color = {0.90, 0.45, 0.10};
      const double bar = 0.60;
      rig.segments[1].transform = frame_along(cam(body_centre + dir * (0.13 + bar * 0.5) + math::Vec3{0.08, 0.02, 0.0}), cam(dir), cam({0, 1, 0}));
      rig.segments[1].size = {0.03, 0.08, bar};
      rig.segments[1].color = {0.30, 0.30, 0.32};
      break;
    }
  }
  return rig;
}
}  // namespace

BodyPose body_pose(const BodyState& state, const BodyStyle& style) {
  validate_body_style(style);
  if (!std::isfinite(state.phase) || !std::isfinite(state.speed_fraction) || !std::isfinite(state.crouch_blend) || !std::isfinite(state.pitch) ||
      !std::isfinite(state.swing) || !std::isfinite(state.saw_phase))
    throw std::invalid_argument("body state must be finite");
  BodyPose pose;
  const double w = style.shoulder_width * 0.5, drop = style.shoulder_drop;
  const double phi = state.phase - std::floor(state.phase);
  const double s = std::sin(2.0 * kPi * phi), r = 0.5 * (1.0 - std::cos(2.0 * kPi * phi));
  const bool free = hands_free(state.activity);
  const bool tool_out = state.tool != ToolKind::None && free;
  const int ih = item_hand(state);
  const bool item_out = state.held != HeldKind::Nothing && free;
  const bool stick = item_out && (state.held == HeldKind::Branch || state.held == HeldKind::Torch);
  // The tool first: the left hand may go to it.
  const double swing_x = (state.stance_foot == 1 ? s : -s);
  const ToolRig rig = tool_rig(state, style, tool_out ? swing_x : 0.0);
  if (tool_out) pose[kToolSegment] = rig.segments[0], pose[kToolSegment + 1] = rig.segments[1];
  else for (std::size_t k = 0; k < kToolSegments; ++k) pose[kToolSegment + k].size = {0, 0, 0};
  const auto shapes = hand_shapes(state);
  const math::Vec3 stick_axis_c = body_to_camera(math::Vec3{0.16 * (ih == 1 ? 1.0 : -1.0), 0.90, 0.40}.normalized(), state.pitch);
  for (int side = 0; side < 2; ++side) {
    const double sx = side == 0 ? -1.0 : 1.0;
    // The arm opposite to the stance foot swings forward.
    const double swing = (side == 0) == (state.stance_foot == 1) ? s : -s;
    math::Vec3 shoulder{sx * w, -drop, 0.04};
    math::Vec3 hand, pole{sx * 0.6, -1.0, -0.3};
    switch (state.activity) {
      case BodyActivity::Idle:
        hand = {sx * (w + 0.02), -drop - 0.50, 0.20};
        break;
      case BodyActivity::Walk: {
        const double amp = 0.12 + 0.22 * state.speed_fraction;
        hand = {sx * (w + 0.01), -drop - 0.46 + 0.06 * std::abs(swing), 0.14 + amp * swing};
        break;
      }
      case BodyActivity::Run:
        hand = {sx * (w - 0.03), -drop - 0.18 + 0.10 * swing, 0.30 + 0.22 * swing};
        pole = {sx * 0.9, -0.6, -0.6};
        break;
      case BodyActivity::Crouch:
        hand = {sx * (w + 0.02), -drop - 0.30, 0.32 + 0.10 * swing * state.speed_fraction};
        break;
      case BodyActivity::Climb: {
        const double reach = side == 0 ? s : -s;
        hand = {sx * (w + 0.06), 0.26 + 0.18 * reach, 0.46};
        pole = {sx * 0.8, -0.2, -0.5};
        break;
      }
      case BodyActivity::Swim:
        hand = {sx * (0.12 + 0.34 * r), -drop - 0.16, 0.58 - 0.18 * r};
        pole = {sx * 0.3, -0.9, -0.2};
        break;
      case BodyActivity::Crawl: {
        const double reach = side == 0 ? s : -s;
        hand = {sx * (w - 0.02), -drop - 0.10 + 0.22 * std::max(0.0, reach), 0.36 + 0.26 * reach};
        pole = {sx * 0.8, -0.5, -0.3};
        break;
      }
      case BodyActivity::Dive:
        hand = {sx * 0.12, -drop - 0.05, 0.62};
        pole = {sx * 0.3, -0.9, -0.2};
        break;
      case BodyActivity::Down:
        hand = {sx * 0.30, -drop - 0.62, 0.55};
        pole = {sx * 0.5, -1.0, 0.2};
        break;
    }
    std::optional<math::Vec3> grip_axis;
    // A held thing lifts its hand into view, gripped in front of the chest.
    if (item_out && side == ih) {
      if (stick) {
        // A stick is carried thumb-up in a fist at the waist, the elbow out so
        // the forearm comes across level and the stick stands up out of the fist.
        hand = {sx * 0.25, -drop - 0.30 + 0.03 * swing, 0.34 + 0.04 * swing};
        pole = {sx * 1.0, -0.35, -0.2};
        grip_axis = stick_axis_c;
      } else {
        hand = {sx * 0.20, -drop - 0.12 + 0.03 * swing, 0.40 + 0.04 * swing};
      }
    }
    // The tool in the right hand; the left joins it when it has nothing else to hold.
    if (tool_out && side == 1) { hand = rig.right_hand; grip_axis = rig.right_grip; pole = {0.9, -0.5, -0.3}; }
    if (tool_out && side == 0 && rig.two_handed && !(item_out && ih == 0)) { hand = rig.left_hand; grip_axis = rig.left_grip; pole = {-0.9, -0.5, -0.3}; }
    const math::Vec3 hand_c = body_to_camera(hand, state.pitch), shoulder_c = body_to_camera(shoulder, state.pitch);
    const math::Vec3 pole_c = body_to_camera(pole, state.pitch);
    const auto wrist = pose_arm(&pose[static_cast<std::size_t>(side) * kArmSegments], shoulder_c, hand_c, pole_c, style, state.pitch, sx,
                                shapes[static_cast<std::size_t>(side)], grip_axis);
    if (side == ih) {
      auto& item = pose[kHeldSegment];
      item.size = {0, 0, 0};
      if (!item_out) continue;
      // Where the curled fingers close (wrist frame): a little below the palm, at its far edge.
      const math::Vec3 grip_local{0, -0.03, style.hand_length * 0.55};
      if (state.held == HeldKind::Stone) {
        // The stone sits under the palm, a pebble at its centre and a big
        // stone further out under the fingers, which close over it as far as its size lets them.
        const double r = std::clamp(state.held_size, 0.03, 0.3);
        item.transform = wrist * frame_along({0, -(r * 0.8 + style.finger_radius * 2.0), style.hand_length * 0.25 + r * 0.6}, {1, 0, 0}, {0, 1, 0});
        item.size = {r * 2.0, r * 1.6, r * 1.8};
        item.color = {0.45, 0.44, 0.40};
      } else if (stick) {
        const double len = std::clamp(state.held_size, 0.2, 1.5);
        // Through the fist along the grip axis, the short end a hand below the grip, the long end rising.
        const math::Vec3 grip_c = wrist.transformPoint(grip_local);
        const math::Vec3 along_c{wrist.at(0, 2), wrist.at(1, 2), wrist.at(2, 2)};
        const double below = std::min(0.14, len * 0.3);
        item.transform = frame_along(grip_c + stick_axis_c * (len * 0.5 - below), stick_axis_c, along_c);
        item.size = {0.035, 0.035, len};
        item.color = state.held == HeldKind::Torch ? math::Vec3{0.18, 0.12, 0.08} : math::Vec3{0.36, 0.27, 0.16};
      }
    }
  }
  return pose;
}

std::optional<math::Vec3> tool_tip(const BodyPose& pose, ToolKind tool) {
  if (tool == ToolKind::None) return std::nullopt;
  const auto& seg = pose[tool == ToolKind::Rope ? kToolSegment : kToolSegment + 1];
  if (seg.size.x <= 0) return std::nullopt;
  // The axe's edge is the head's +z face; the bar's tip is its far end; the coil is its centre.
  const double along = tool == ToolKind::Rope ? 0.0 : seg.size.z * 0.5;
  return seg.transform.transformPoint({0, 0, along});
}

std::array<math::Vec3, 2> hand_positions(const BodyPose& pose) {
  std::array<math::Vec3, 2> out;
  for (int side = 0; side < 2; ++side) {
    const auto& m = pose[static_cast<std::size_t>(side) * kArmSegments + 2].transform;  // the palm
    out[static_cast<std::size_t>(side)] = {m.at(0, 3), m.at(1, 3), m.at(2, 3)};
  }
  return out;
}

std::array<math::Vec3, 5> fingertips(const BodyPose& pose, int side) {
  if (side < 0 || side > 1) throw std::invalid_argument("side must be 0 or 1");
  std::array<math::Vec3, 5> tips;
  const auto base = static_cast<std::size_t>(side) * kArmSegments;
  // The distal segments: thumb at +10, fingers at +4, +6, +8, +2 (index .. little in finger order 1..4 -> distal 2,4,6,8).
  const std::size_t distal[5] = {base + 2 + 10, base + 2 + 2, base + 2 + 4, base + 2 + 6, base + 2 + 8};
  for (int i = 0; i < 5; ++i) {
    const auto& seg = pose[distal[i]];
    tips[static_cast<std::size_t>(i)] = seg.transform.transformPoint({0, 0, seg.size.z * 0.5});
  }
  return tips;
}

BoxMesh make_unit_box() {
  BoxMesh box;
  const math::Vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int f = 0; f < 6; ++f) {
    const auto n = normals[f];
    const math::Vec3 u = std::abs(n.y) > 0.5 ? math::Vec3{1, 0, 0} : math::Vec3{0, 1, 0};
    const math::Vec3 v = n.cross(u);
    const auto base = static_cast<std::uint32_t>(box.vertices.size());
    for (int k = 0; k < 4; ++k) {
      const double su = (k == 1 || k == 2) ? 0.5 : -0.5, sv = (k >= 2) ? 0.5 : -0.5;
      const math::Vec3 p = n * 0.5 + u * su + v * sv;
      TerrainGpuVertex vert{};
      vert.position[0] = static_cast<float>(p.x); vert.position[1] = static_cast<float>(p.y); vert.position[2] = static_cast<float>(p.z);
      vert.normal[0] = static_cast<float>(n.x); vert.normal[1] = static_cast<float>(n.y); vert.normal[2] = static_cast<float>(n.z);
      box.vertices.push_back(vert);
    }
    // Wind the face so its front faces outward.
    const math::Vec3 a{box.vertices[base].position[0], box.vertices[base].position[1], box.vertices[base].position[2]};
    const math::Vec3 b{box.vertices[base + 1].position[0], box.vertices[base + 1].position[1], box.vertices[base + 1].position[2]};
    const math::Vec3 c{box.vertices[base + 2].position[0], box.vertices[base + 2].position[1], box.vertices[base + 2].position[2]};
    const bool ccw = (b - a).cross(c - a).dot(n) > 0;
    for (const auto i : (ccw ? std::array<std::uint32_t, 6>{0, 1, 2, 0, 2, 3} : std::array<std::uint32_t, 6>{0, 2, 1, 0, 3, 2})) box.indices.push_back(base + i);
  }
  return box;
}

math::Mat4 camera_to_world(math::Vec3 eye, math::Vec3 right, math::Vec3 up, math::Vec3 forward) {
  if (!finite(eye) || !finite(right) || !finite(up) || !finite(forward)) throw std::invalid_argument("camera basis must be finite");
  math::Mat4 m = math::Mat4::identity();
  m.at(0, 0) = right.x; m.at(1, 0) = right.y; m.at(2, 0) = right.z;
  m.at(0, 1) = up.x; m.at(1, 1) = up.y; m.at(2, 1) = up.z;
  m.at(0, 2) = forward.x; m.at(1, 2) = forward.y; m.at(2, 2) = forward.z;
  m.at(0, 3) = eye.x; m.at(1, 3) = eye.y; m.at(2, 3) = eye.z;
  return m;
}

BodyRenderer::BodyRenderer(RenderDevice& device) : device_(device), resources_(device) {
  const auto box = make_unit_box();
  const auto vertices = std::as_bytes(std::span(box.vertices));
  const auto indices = std::as_bytes(std::span(box.indices));
  vb_ = resources_.create_buffer({BufferUsage::Vertex, vertices.size()}, vertices);
  ib_ = resources_.create_buffer({BufferUsage::Index, indices.size()}, indices);
  indices_ = static_cast<std::uint32_t>(box.indices.size());
  const auto zero = make_object_uniform(math::Mat4::identity(), {0.5, 0.5, 0.5});
  for (auto& u : uniforms_) u = resources_.create_buffer({BufferUsage::Uniform, sizeof(zero)}, std::as_bytes(std::span(&zero, 1)));
  pipeline_ = resources_.create_pipeline({ShaderProgramId::Object, object_shader_layout(), true, false, true});
}

std::size_t BodyRenderer::draw(BufferHandle camera, const BodyPose& pose, const math::Mat4& to_world) {
  if (!device_.active_render_pass_id()) throw std::logic_error("body requires an active pass");
  std::size_t drawn = 0;
  for (std::size_t i = 0; i < kBodySegments; ++i) {
    const auto& seg = pose[i];
    if (seg.size.x <= 0 || seg.size.y <= 0 || seg.size.z <= 0) continue;
    ++drawn;
    const auto model = to_world * seg.transform * math::Mat4::scaling(seg.size);
    const auto uniform = make_object_uniform(model, {std::clamp(seg.color.x, 0.0, 1.0), std::clamp(seg.color.y, 0.0, 1.0), std::clamp(seg.color.z, 0.0, 1.0)});
    device_.update_buffer(uniforms_[i], 0, std::as_bytes(std::span(&uniform, 1)));
    device_.draw_indexed({pipeline_, vb_, ib_, 0, indices_, {{0, camera}, {2, uniforms_[i]}}, {}, {}});
  }
  return drawn;
}

}  // namespace terraflux::graphics
