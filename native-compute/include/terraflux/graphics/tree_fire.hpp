#pragma once

#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/collision.hpp>
#include <terraflux/graphics/fire_effects.hpp>
#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/math.hpp>
#include <terraflux/replay.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace terraflux::graphics {

// Tree-level fire: what a wildfire looks like from the ground. The grid
// simulation still decides where the front is (a cell is active for one
// replay frame, then burned); this model takes that as ignition and follows
// every tree from there, in replay seconds, deterministically:
//
// - A tree has a body: height, trunk radius, mass from its wood density
//   (conifer-like lighter than broadleaf-like, each varied per tree), a
//   breaking moment from its modulus of rupture (varied too), a standing lean,
//   and a fuel load that decides how long it burns (bigger and wetter trees
//   burn longer, wind makes every fire burn faster).
// - Ignited, it flames up after a short delay, is consumed over time, and
//   while it burns drops burning branches from its crown that fall under
//   gravity, burn on the ground for a while and can ignite the trees they land
//   beside.
// - As the trunk chars its strength falls ((1 - consumed)^3 of the sound
//   wood) while the load on it grows (the lean, the asymmetric burn shifting
//   the weight, the wind on the crown); when the load exceeds the strength
//   the tree topples as a rigid rod under gravity and the wind, toward its
//   lean and the wind. A neighbour in its way catches it: if the neighbour is
//   strong enough the tree leans on it and sets it alight by contact,
//   otherwise the neighbour breaks and falls too (alight at once if the
//   first was burning). On the ground a trunk keeps burning until no fuel is
//   left, lights trees it lies against, and is an obstacle the walker has to
//   step or jump over.
// - Radiant heat ignites neighbours within a reach that grows with the
//   wind downwind, so a fire among trees runs faster with the wind than the
//   grid front alone would show. This spread is visualisation: the simulation
//   contract, its fixtures and the ground fire overlay are untouched, and
//   `spread` can be disabled to keep trees strictly to their cells.
enum class TreeFireStage : std::uint8_t { Unburned, Ignited, Burning, Falling, Fallen, BurnedOut };
const char* tree_fire_stage_name(TreeFireStage stage);
enum class IgnitionSource : std::uint8_t { None, Cell, Contact, Radiation, Ember, Collapse, Lightning };
const char* ignition_source_name(IgnitionSource source);

struct TreeFireStyle {
  bool spread = true;                      // tree-to-tree ignition (contact, radiation, embers)
  double consume_seconds = 8.0;            // 1 .. 600: a reference tree (dry, calm) from ignition to burned out
  double reference_height_meters = 16.0;   // 1 .. 100
  double size_exponent = 0.5;              // 0 .. 2: consume time scales with (height / reference)^exponent
  double moisture_slowdown = 0.8;          // 0 .. 5: consume time x (1 + slowdown x moisture)
  double wind_burn_factor = 0.06;          // 0 .. 1 per m/s: burns faster in wind
  double ignition_delay_seconds = 0.6;     // 0 .. 60 from ignition to flames
  double branch_drop_rate = 0.5;           // 0 .. 20 burning branches per second from a fully burning tree
  double branch_burn_seconds = 4.0;        // 0.1 .. 600 on the ground
  double ember_ignition_radius_meters = 2.0; // 0 .. 50: a landed branch ignites trunks this close
  double radiant_reach_meters = 5.0;       // 0 .. 200: ignition reach of a fully burning reference tree, calm
  double radiant_wind_factor = 0.6;        // 0 .. 10 per m/s, downwind
  double radiant_rate = 0.35;              // 0 .. 100 ignitions per second at the foot of a burning tree
  double contact_ignition_seconds = 1.0;   // 0 .. 600 of touching a burning tree
  double lean_degrees_max = 5.0;           // 0 .. 30 standing lean, per tree
  double burn_shift = 0.35;                // 0 .. 2 radians of effective lean added by a fully asymmetric burn
  double gravity_mps2 = 9.81;              // 0.1 .. 100 (the walker's)
  double air_density = 1.2, drag_coefficient = 0.8; // wind load on the crown
  double conifer_density = 450.0, broadleaf_density = 650.0;   // kg/m^3, 100 .. 1500
  double conifer_rupture_pa = 60e6, broadleaf_rupture_pa = 90e6; // modulus of rupture, 1e6 .. 1e9
  double variation = 0.3;                  // 0 .. 0.9 per-tree spread of density and strength
  // Storms. Root anchorage holds a sound tree against the wind up to
  // anchor_factor x m g h (varied per tree by `variation`); a gust past that
  // uproots it (windthrow), burning or not. A lightning strike splits and
  // lights the nearest tree within strike_radius and sets alight the dry trees
  // within strike_flash_radius with strike_flash_chance.
  double anchor_factor = 0.25;             // 0.005 .. 1: uproots about 2 % of a wood at 30 m/s, 15 % at 35, 40 % at 40, 85 % at 50
  double strike_radius_meters = 12.0;      // 0 .. 200
  double strike_flash_radius_meters = 25.0; // 0 .. 500
  double strike_flash_chance = 0.5;        // 0 .. 1
  std::uint32_t seed = 0;
};
void validate_tree_fire_style(const TreeFireStyle& style);  // throws std::invalid_argument

struct TreeBody {
  double height_meters = 0.0, trunk_radius_meters = 0.0;
  double mass_kg = 0.0;                 // trunk and crown
  double breaking_moment_nm = 0.0;      // of the sound trunk at its base
  double anchor_moment_nm = 0.0;        // what the roots hold against: past it the tree is uprooted
  double consume_seconds = 0.0;         // fuel: time from flames to burned out, calm
  double moisture = 0.0;                // 0 dry .. 1 wet (from the instance tint's species class is unknown: taken from the cell fuel moisture when given)
  double lean_radians = 0.0;            // standing lean
  math::Vec2 lean_direction{1, 0};      // unit, plan
  int kind = 0;                         // 0 conifer-like, 1 broadleaf-like
};
// The body of a placed tree: deterministic from the instance, its mesh footprint
// and the seed (moisture from the ground it stands on, 0 .. 1).
TreeBody tree_body(const VegetationInstance& tree, double footprint_radius_fraction, double moisture, const TreeFireStyle& style,
                   std::uint32_t index);
// The moment the wind puts on a crown (N m, about the base) at a wind speed.
double wind_moment_nm(const TreeBody& body, double wind_speed_mps, const TreeFireStyle& style);
// The strength left in a trunk that has burned this far (N m).
double charred_strength_nm(const TreeBody& body, double consumed);
// The moment gravity and the wind put on the trunk standing with its
// effective lean (the standing lean plus the burn shift x consumed), as a
// magnitude (N m) and the plan direction it pushes the tree toward; past the
// charred strength the tree topples that way.
struct TreeLoad { double moment_nm = 0.0; math::Vec2 direction{1, 0}; };
TreeLoad standing_load(const TreeBody& body, double consumed, double wind_speed_mps, math::Vec2 wind_direction,
                       const TreeFireStyle& style);
// The unit plan vector a compass direction (degrees clockwise from north) points toward.
math::Vec2 compass_vector(double degrees);

struct TreeFireState {
  TreeFireStage stage = TreeFireStage::Unburned;
  IgnitionSource source = IgnitionSource::None;
  double burn_seconds = 0.0;        // since ignition
  double consumed = 0.0;            // 0 .. 1 of the fuel
  double intensity = 0.0;           // 0 .. 1 flames
  double fall_angle = 0.0;          // radians from vertical (0 standing, ~pi/2 on the ground)
  double fall_rate = 0.0;           // radians per second while falling
  math::Vec2 fall_direction{1, 0};  // unit, plan
  int leaning_on = -1;              // tree holding this fallen trunk up, or -1
  double contact_seconds = 0.0;     // touched by burning wood (before ignition)
  std::uint32_t branches_dropped = 0;
  // Cut by an axe or a saw: the fraction of the diameter cut through (0 sound),
  // and the way the tree will fall (away from the cutter). A cut trunk is
  // weaker by (1 - cut)^2 against the wind and its own burning lean.
  double cut = 0.0;
  math::Vec2 cut_direction{1, 0};
  bool felled = false;              // brought down by a cut (not fire, wind or lightning)
};

// A burning branch: falls from the crown under gravity, burns on the ground.
struct BurningBranch {
  math::Vec3 position, velocity;
  double size_meters = 0.6;
  double seconds_left = 0.0;        // on the ground
  std::uint32_t tree = 0;
  bool landed = false;
  double ground_y = 0.0;
};

struct TreeFireEvent {
  enum class Kind : std::uint8_t { Ignited, Flames, BranchDropped, Toppled, Leaning, Collapsed, Grounded, BurnedOut, Windthrown, Struck, Felled } kind;
  std::uint32_t tree = 0;
  int other = -1;                   // the neighbour involved, if any
  IgnitionSource source = IgnitionSource::None;
};

struct TreeFireStats {
  std::size_t trees = 0, unburned = 0, ignited = 0, burning = 0, falling = 0, fallen = 0, burned_out = 0;
  std::size_t ignited_by_cell = 0, ignited_by_contact = 0, ignited_by_radiation = 0, ignited_by_ember = 0, ignited_by_collapse = 0,
              ignited_by_lightning = 0;
  std::size_t branches_dropped = 0, branches_burning = 0, leaning = 0, collapses = 0, grounded = 0, windthrown = 0, struck = 0, felled = 0, cut = 0;
  double heaviest_kg = 0.0, lightest_kg = 0.0, strongest_nm = 0.0, weakest_nm = 0.0;
  double seconds = 0.0;             // model time advanced
};

class TreeFireModel {
 public:
  // Ground under the branches: the standing height at a plan position (the
  // tree's own base when absent or a hole).
  TreeFireModel(const VegetationPlacement& placement, std::span<const VegetationMesh> tree_meshes, const TreeFireStyle& style,
                StandingHeightFn ground = {}, std::function<double(double x, double z)> moisture = {});
  const TreeFireStyle& style() const { return style_; }
  std::size_t size() const { return bodies_.size(); }
  const std::vector<TreeBody>& bodies() const { return bodies_; }
  const std::vector<TreeFireState>& states() const { return states_; }
  const std::vector<BurningBranch>& branches() const { return branches_; }
  // Events of the last advance() / ignite calls; cleared by advance().
  const std::vector<TreeFireEvent>& events() const { return events_; }
  TreeFireStats stats() const;
  // Branches as things: a burning branch lying within `radius` of a plan
  // position can be taken (removed; its remaining burn time returned), and a
  // burning branch can be put down (or thrown) to burn on the ground and
  // light what lies beside it.
  std::optional<double> take_branch(double x, double z, double radius);
  void add_burning_branch(math::Vec3 position, double seconds_left, double size_meters = 0.6);
  // Cut in pieces: the burning branch nearest a plan position within `radius`
  // (at least 2 x min_piece long) becomes two halves burning on. Returns
  // whether one was split.
  bool split_branch(double x, double z, double radius, double min_piece_meters = 0.25);
  // Tools on a standing trunk: deepens the cut by `depth_meters` of the
  // diameter, set to fall `toward` (plan; the side away from the cutter). At
  // kFellCut of the diameter the tree is felled: it topples that way at once
  // (event Felled; source unchanged, so a sound tree falls unlit). Returns the
  // cut fraction afterwards (1 when it is down); nothing happens to a tree
  // already falling or down (returns its cut).
  double cut(std::uint32_t tree, double depth_meters, math::Vec2 toward);
  static constexpr double kFellCut = 0.7;
  // Whether the tree stands (not falling, not down).
  bool standing(std::uint32_t tree) const;
  // Trees standing in active cells of the field ignite (source Cell). With
  // `settle_burned`, trees in burned cells that have not burned here yet are
  // set burned out (charred, about half of them down), for a seek into the replay.
  std::size_t ignite_from_field(const replay::FireField& field, std::int32_t origin_grid_x, std::int32_t origin_grid_y,
                                double cell_size_meters, bool settle_burned);
  std::size_t ignite(std::uint32_t tree, IgnitionSource source);
  // A lightning strike at a plan position: the nearest standing tree within the
  // strike radius is split (alight at once and toppling, event Struck) and the
  // unburned trees within the flash radius catch with the flash chance
  // (source Lightning). Returns the struck tree or -1.
  int strike(double x, double z, double wind_speed_mps, double wind_direction_degrees);
  // Advances the model by dt seconds of replay time in fixed 1/60 s sub-steps
  // (the remainder is carried), with the wind blowing toward the compass direction.
  void advance(double dt, double wind_speed_mps, double wind_direction_degrees);
  void reset();  // everything unburned, branches gone, time zero
  // Rendering: per-tree fall direction/angle, char, glow and consumption into the
  // placement's instances (same order as construction).
  void apply_to_placement(VegetationPlacement& placement) const;
  // Flames along burning trunks and crowns, embers for the branches, smoke
  // from burned-out trunks, appended to the lists.
  void emit(std::vector<FireInstance>& flames, std::vector<FireInstance>& smoke, const FireStyle& style) const;
  // What a walker at a plan position feels from the burning trees: heat
  // (1 within 3 m of a fully burning trunk or lying log, fading to 0 by
  // heat_radius) and smoke (the burning trees' intensity summed downwind of
  // them, fading by smoke_radius), both 0 .. 1.
  struct Exposure { double heat = 0.0, smoke = 0.0; };
  Exposure exposure_at(double x, double z, double wind_direction_degrees, double heat_radius_meters = 12.0,
                       double smoke_radius_meters = 60.0) const;
  // Walking collision: standing trees as before, fallen trunks as chains of
  // low cylinders (the trunk's diameter high) along their length.
  std::vector<Obstacle> obstacles(std::span<const Obstacle> standing) const;
  std::uint64_t obstacle_version() const { return obstacle_version_; }  // changes whenever a tree falls or rises

 private:
  struct Neighbour { std::uint32_t index; double distance; };
  void step(double dt, double wind_speed, math::Vec2 wind_dir);
  void begin_fall(std::uint32_t i, double wind_speed, math::Vec2 wind_dir, double lean_weight);
  template <class Visit> void for_each_near(double x, double z, double radius, Visit&& visit) const;
  double rand01(std::uint32_t tree, std::uint32_t salt) const;
  double ground_at(double x, double z, double fallback) const;
  TreeFireStyle style_;
  std::vector<TreeBody> bodies_;
  std::vector<TreeFireState> states_;
  std::vector<math::Vec3> positions_;
  std::vector<BurningBranch> branches_;
  std::vector<TreeFireEvent> events_;
  ObstacleField index_;                // the standing trunks, for neighbour queries
  StandingHeightFn ground_;
  double carry_ = 0.0, seconds_ = 0.0;
  std::uint64_t tick_ = 0, obstacle_version_ = 0;
  std::size_t windthrown_ = 0, struck_ = 0;
  std::vector<std::uint8_t> windthrown_flag_;
};

}  // namespace terraflux::graphics
