#pragma once

#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/resource_owner.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace terraflux::graphics {

// The walker's own body as seen from its eyes: two arms (upper arm, forearm,
// hand), posed for what the body is doing and drawn in front of the camera.
// Poses are computed in camera space — x right, y up, z forward, origin at
// the eye — from the rig's state (gait phase and stance foot, speed, climb,
// swim stroke, crouch, stun), so the arms pump when running, reach alternately
// when climbing, sweep when swimming and hang when idle (into view when the
// walker looks down). The viewer maps them into the world with the camera's
// basis and draws them with the Object program after the scene.
enum class BodyActivity : std::uint8_t { Idle, Walk, Run, Crouch, Climb, Swim, Crawl, Dive, Down };
const char* body_activity_name(BodyActivity activity);

// What the hand holds (drawn in it, gripped): in the right hand, or in the
// left when a tool is out.
enum class HeldKind : std::uint8_t { Nothing, Stone, Branch, Torch };
const char* held_kind_name(HeldKind kind);
// The tool in the right hand (see tools.hpp): a coiled rope, a fire axe or a chainsaw.
enum class ToolKind : std::uint8_t { None, Rope, Axe, Chainsaw };
const char* tool_kind_name(ToolKind kind);

struct BodyState {
  BodyActivity activity = BodyActivity::Idle;
  double phase = 0.0;          // gait phase 0..1 (walk/run/crouch), climb phase, or swim stroke phase
  int stance_foot = 0;         // the gait's stance foot: the opposite arm swings forward
  double speed_fraction = 0.0; // 0 .. 1 of the walking (or running) speed
  double crouch_blend = 0.0;   // 0 .. 1
  double pitch = 0.0;          // the look pitch (radians): hanging arms stay with the body, not the head
  HeldKind held = HeldKind::Nothing;
  double held_size = 0.0;      // a stone's radius or a branch's length (m)
  ToolKind tool = ToolKind::None;  // in the right hand; the held thing moves to the left
  double swing = 0.0;          // an axe chop in progress: 0 none, 0 .. 1 raised over the shoulder and brought down
  bool sawing = false;         // the chainsaw running into the wood (both hands on it, trembling)
  double saw_phase = 0.0;      // the saw's vibration phase (seconds)
};
// The rig's state reduced to a body state.
BodyState body_state_from(const FirstPersonCamera& camera, double walk_speed_mps, double run_multiplier);

// One arm segment: a box of `size` (x width, y height, z length) placed by
// `transform` (camera space; the box is centred on its origin), with a color.
struct BodySegment {
  math::Mat4 transform;
  math::Vec3 size{0.08, 0.08, 0.28};
  math::Vec3 color{0.24, 0.26, 0.22};
};
// Per arm: upper arm, forearm, palm, thumb (2 segments), four fingers (2 each) = 13;
// both arms 26; then one segment for a held item and two for a tool (an axe's
// haft and head, a chainsaw's body and bar, a rope's coil), size zero when absent.
inline constexpr std::size_t kArmSegments = 13;
inline constexpr std::size_t kToolSegments = 2;
inline constexpr std::size_t kBodySegments = 2 * kArmSegments + 1 + kToolSegments;
inline constexpr std::size_t kHeldSegment = 2 * kArmSegments;
inline constexpr std::size_t kToolSegment = 2 * kArmSegments + 1;
using BodyPose = std::array<BodySegment, kBodySegments>;
// How a hand is held: fingers curled (0 open .. ~1.4 a fist), spread apart, the thumb opposed.
struct HandShape { double curl = 0.4, spread = 0.25, thumb_curl = 0.5; };
// The shape of each hand for an activity (and what the hands hold).
std::array<HandShape, 2> hand_shapes(const BodyState& state);
// Which hand holds the item: the right (1), or the left (0) when a tool is out.
int item_hand(const BodyState& state);

struct BodyStyle {
  double shoulder_width = 0.42;       // 0.1 .. 1
  double shoulder_drop = 0.28;        // below the eye; 0 .. 1
  double upper_arm_length = 0.29;     // 0.1 .. 0.6
  double forearm_length = 0.27;       // 0.1 .. 0.6
  double hand_length = 0.18;          // 0.05 .. 0.4: palm plus fingers
  double arm_radius = 0.045;          // 0.01 .. 0.15
  double palm_width = 0.085;          // 0.03 .. 0.2
  double finger_radius = 0.009;       // 0.002 .. 0.03
  double min_hand_distance = 0.36;    // hands never closer to the eye than this (the near plane)
  math::Vec3 sleeve_color{0.24, 0.26, 0.22};
  math::Vec3 skin_color{0.80, 0.62, 0.50};
};
void validate_body_style(const BodyStyle& style);
// The pose for a state: deterministic, smooth in the phase.
BodyPose body_pose(const BodyState& state, const BodyStyle& style);
// Where the hands (palm centres) are in a pose (camera space), for tests and reach checks.
std::array<math::Vec3, 2> hand_positions(const BodyPose& pose);
// Where the fingertips of a hand are (camera space): thumb first, then index .. little.
std::array<math::Vec3, 5> fingertips(const BodyPose& pose, int side);
// The working end of the tool in a pose (camera space): the axe's edge, the
// chainsaw bar's tip, the rope coil; nullopt without a tool.
std::optional<math::Vec3> tool_tip(const BodyPose& pose, ToolKind tool);

// A unit box mesh (-0.5..0.5) in the object vertex format with flat normals.
struct BoxMesh { std::vector<TerrainGpuVertex> vertices; std::vector<std::uint32_t> indices; };
BoxMesh make_unit_box();
// Camera space to world: right, up and forward are the camera's basis at the eye.
math::Mat4 camera_to_world(math::Vec3 eye, math::Vec3 right, math::Vec3 up, math::Vec3 forward);

class BodyRenderer {
 public:
  explicit BodyRenderer(RenderDevice& device);
  BodyRenderer(const BodyRenderer&) = delete;
  BodyRenderer& operator=(const BodyRenderer&) = delete;
  // Inside an active pass after the scene: draws the six segments of the pose
  // transformed by camera_to_world. Returns the number of draws.
  std::size_t draw(BufferHandle camera, const BodyPose& pose, const math::Mat4& to_world);
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  BufferHandle vb_, ib_;
  std::array<BufferHandle, kBodySegments> uniforms_{};
  PipelineHandle pipeline_;
  std::uint32_t indices_ = 0;
};

}  // namespace terraflux::graphics
