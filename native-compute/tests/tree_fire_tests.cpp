#include <terraflux/graphics/tree_fire.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

using namespace terraflux;
using namespace terraflux::graphics;
using terraflux::math::Vec2;
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

VegetationInstance tree(double x, double z, double height, int kind = 0, double y = 0.0) {
  VegetationInstance t{};
  t.position[0] = static_cast<float>(x); t.position[1] = static_cast<float>(y); t.position[2] = static_cast<float>(z);
  t.scale = static_cast<float>(height); t.kind = static_cast<float>(kind); t.tint[0] = t.tint[1] = t.tint[2] = 1.0f;
  return t;
}
VegetationPlacement forest(const std::vector<VegetationInstance>& trees) {
  VegetationPlacement p;
  p.trees = trees;
  return p;
}
std::vector<VegetationMesh> meshes() {
  TreeMeshOptions conifer; conifer.shape = TreeShape::Conifer;
  TreeMeshOptions broadleaf; broadleaf.shape = TreeShape::Broadleaf; broadleaf.crown_width = 0.8;
  return {make_tree_mesh(conifer), make_tree_mesh(broadleaf)};
}
std::optional<double> flat(double, double) { return 0.0; }
// A 2x2-cell field with cell (0,0) active.
replay::FireField field_with_active(std::uint32_t active_x, std::uint32_t active_y, std::uint32_t burned_x = 99, std::uint32_t burned_y = 99) {
  replay::FireField f;
  f.min_x = 0; f.min_y = 0; f.width = 2; f.height = 2;
  f.states.assign(4, 0u);
  f.states[active_y * 2 + active_x] = 2u;
  if (burned_x < 2 && burned_y < 2) f.states[burned_y * 2 + burned_x] = 1u;
  return f;
}
void run(TreeFireModel& model, double seconds, double wind = 0.0, double direction = 90.0) {
  for (double t = 0; t < seconds; t += 1.0 / 60.0) model.advance(1.0 / 60.0, wind, direction);
}
std::size_t count_events(const std::vector<TreeFireEvent>& events, TreeFireEvent::Kind kind) {
  return static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const TreeFireEvent& e) { return e.kind == kind; }));
}

void test_bodies() {
  TreeFireStyle style;
  validate_tree_fire_style(style);
  rejects([] { TreeFireStyle s; s.consume_seconds = 0; validate_tree_fire_style(s); }, "zero consume time rejected");
  rejects([] { TreeFireStyle s; s.variation = 1.0; validate_tree_fire_style(s); }, "full variation rejected");
  rejects([] { TreeFireStyle s; s.conifer_density = 5000; validate_tree_fire_style(s); }, "absurd density rejected");
  const auto m = meshes();
  const double conifer_foot = mesh_footprint_radius(m[0]), broadleaf_foot = mesh_footprint_radius(m[1]);
  const auto big = tree_body(tree(0, 0, 20, 0), conifer_foot, 0.1, style, 1);
  const auto small = tree_body(tree(0, 0, 8, 0), conifer_foot, 0.1, style, 2);
  const auto hard = tree_body(tree(0, 0, 20, 1), broadleaf_foot, 0.1, style, 3);
  require(big.mass_kg > 4 * small.mass_kg && big.breaking_moment_nm > 4 * small.breaking_moment_nm, "a taller tree is far heavier and stronger");
  require(hard.mass_kg > big.mass_kg * 0.8 && hard.kind == 1 && big.kind == 0, "broadleaf wood is denser");
  require(big.consume_seconds > small.consume_seconds, "a bigger tree burns longer");
  const auto wet = tree_body(tree(0, 0, 20, 0), conifer_foot, 0.9, style, 1);
  require(wet.consume_seconds > big.consume_seconds * 1.5 && near(wet.moisture, 0.9), "a wet tree burns longer still");
  require(big.lean_radians >= 0 && big.lean_radians <= style.lean_degrees_max * kPi / 180 + 1e-12 && near(big.lean_direction.length(), 1.0),
          "a standing lean within the limit");
  // Every tree is its own: masses and strengths vary with the index, deterministically.
  std::set<long long> masses, strengths;
  for (std::uint32_t i = 0; i < 50; ++i) {
    const auto b = tree_body(tree(0, 0, 16, 0), conifer_foot, 0.2, style, i);
    masses.insert(std::llround(b.mass_kg)); strengths.insert(std::llround(b.breaking_moment_nm / 1000.0));
    const auto again = tree_body(tree(0, 0, 16, 0), conifer_foot, 0.2, style, i);
    require(again.mass_kg == b.mass_kg && again.breaking_moment_nm == b.breaking_moment_nm, "bodies are deterministic");
  }
  require(masses.size() > 30 && strengths.size() > 30, "no two trees weigh or hold the same");
  TreeFireStyle uniform = style; uniform.variation = 0;
  require(tree_body(tree(0, 0, 16, 0), conifer_foot, 0.2, uniform, 1).mass_kg == tree_body(tree(0, 0, 16, 0), conifer_foot, 0.2, uniform, 2).mass_kg,
          "without variation equal trees are equal");
  // Strength falls with charring, load grows with it and with the wind.
  require(charred_strength_nm(big, 0) == big.breaking_moment_nm && charred_strength_nm(big, 0.5) < big.breaking_moment_nm * 0.13 &&
          charred_strength_nm(big, 1.0) == 0, "charred wood loses its strength as the cube of what is left");
  const auto calm = standing_load(big, 0.0, 0.0, big.lean_direction, style), windy = standing_load(big, 0.0, 15.0, big.lean_direction, style);
  require(windy.moment_nm > calm.moment_nm && wind_moment_nm(big, 15, style) > wind_moment_nm(big, 5, style) * 5, "the wind loads the crown with its square");
  require(standing_load(big, 0.8, 0, big.lean_direction, style).moment_nm > calm.moment_nm, "an asymmetric burn shifts the weight outward");
  require(calm.moment_nm < charred_strength_nm(big, 0) && standing_load(big, 0.95, 0, big.lean_direction, style).moment_nm > charred_strength_nm(big, 0.95),
          "a sound tree stands, a burned-through one cannot");
  require(near(compass_vector(90).x, 1) && near(compass_vector(0).y, -1), "compass vectors");
  rejects([&] { (void)tree_body(tree(0, 0, 0, 0), conifer_foot, 0.1, style, 1); }, "a zero-height tree is rejected");
}

void test_ignition_and_burning() {
  TreeFireStyle style;
  style.spread = false;
  // Two trees in cell (0,0) (x 0..15, z 0..-15), one in cell (1,1).
  TreeFireModel model(forest({tree(2, -2, 16), tree(8, -8, 12), tree(32, -32, 16)}), meshes(), style, flat);
  require(model.size() == 3 && model.stats().unburned == 3, "three trees, none burning");
  const auto n = model.ignite_from_field(field_with_active(0, 0), 0, 0, 30.0, false);
  require(n == 2 && model.states()[0].stage == TreeFireStage::Ignited && model.states()[2].stage == TreeFireStage::Unburned,
          "the active cell's trees ignite, the other cell's do not");
  require(model.ignite_from_field(field_with_active(0, 0), 0, 0, 30.0, false) == 0, "re-syncing the same field ignites nothing new");
  require(model.events().size() == 2 && model.events()[0].source == IgnitionSource::Cell, "ignition events with their source");
  run(model, 0.3);
  require(model.states()[0].stage == TreeFireStage::Ignited && model.states()[0].intensity == 0, "no flames before the ignition delay");
  run(model, 1.0);
  require(model.states()[0].stage == TreeFireStage::Burning && model.states()[0].intensity > 0.5 && model.states()[0].consumed > 0,
          "flames after the delay, fuel being consumed");
  const double c1 = model.states()[0].consumed;
  run(model, 2.0);
  require(model.states()[0].consumed > c1, "consumption progresses");
  require(model.states()[1].consumed > model.states()[0].consumed, "the smaller tree burns faster");
  run(model, 60.0);
  require(model.states()[0].stage == TreeFireStage::BurnedOut && model.states()[0].consumed == 1.0 && model.states()[0].intensity == 0,
          "burned out when no fuel is left");
  require(model.states()[0].fall_angle > 1.0, "a burned-through tree has toppled by then");
  require(model.states()[2].stage == TreeFireStage::Unburned, "without spread the other cell's tree never catches");
  const auto st = model.stats();
  require(st.burned_out == 2 && st.unburned == 1 && st.ignited_by_cell == 2 && st.grounded >= 1 && st.branches_dropped > 0, "stats add up");
  // Wind burns faster.
  TreeFireModel calm(forest({tree(2, -2, 16)}), meshes(), style, flat), gale(forest({tree(2, -2, 16)}), meshes(), style, flat);
  calm.ignite(0, IgnitionSource::Cell); gale.ignite(0, IgnitionSource::Cell);
  run(calm, 3.0, 0.0); run(gale, 3.0, 15.0);
  require(gale.states()[0].consumed > calm.states()[0].consumed * 1.5, "a gale burns a tree faster");
  // Reset and settle: burned cells after a seek are charred, about half down.
  TreeFireModel seek(forest({tree(2, -2, 16), tree(8, -8, 12), tree(32, -32, 16)}), meshes(), style, flat);
  seek.ignite_from_field(field_with_active(1, 0, 0, 0), 0, 0, 30.0, true);
  require(seek.states()[0].stage == TreeFireStage::BurnedOut && seek.states()[1].stage == TreeFireStage::BurnedOut &&
          seek.states()[0].consumed == 1.0, "a seek settles burned cells as burned out");
  seek.reset();
  require(seek.stats().unburned == 3 && seek.branches().empty(), "reset clears everything");
  rejects([&] { seek.advance(-1, 0, 0); }, "negative time rejected");
}

void test_branches_and_gravity() {
  TreeFireStyle style;
  style.spread = false;
  style.branch_drop_rate = 8.0;
  TreeFireModel model(forest({tree(0, 0, 16, 0, 100.0)}), meshes(), style, [](double, double) -> std::optional<double> { return 100.0; });
  model.ignite(0, IgnitionSource::Cell);
  run(model, 2.5);
  require(!model.branches().empty() && model.stats().branches_dropped > 0, "a burning crown drops branches");
  const auto& first = model.branches().front();
  require(first.position.y > 100.0 && first.position.y <= 100.0 + 16.0, "branches start in the crown");
  // Freeze the picture: follow one branch by stepping once and comparing.
  std::vector<BurningBranch> before(model.branches());
  model.advance(1.0 / 60.0, 0, 0);
  bool fell = false;
  for (std::size_t i = 0; i < std::min(before.size(), model.branches().size()); ++i)
    if (!before[i].landed && !model.branches()[i].landed && model.branches()[i].velocity.y < before[i].velocity.y - 9.81 / 60.0 + 1e-6) fell = true;
  require(fell, "airborne branches accelerate downward at g");
  run(model, 6.0);
  bool landed = false;
  for (const auto& b : model.branches()) if (b.landed && near(b.position.y, 100.0)) landed = true;
  require(landed || model.branches().empty(), "branches land on the ground and burn there");
  run(model, 30.0);
  require(model.branches().empty(), "landed branches burn out");
  // Emitters: flames on the burning trunk, embers for the branches.
  TreeFireModel fresh(forest({tree(0, 0, 16, 0, 100.0)}), meshes(), style, [](double, double) -> std::optional<double> { return 100.0; });
  fresh.ignite(0, IgnitionSource::Cell);
  run(fresh, 2.5);
  std::vector<FireInstance> flames, smoke;
  fresh.emit(flames, smoke, FireStyle{});
  std::size_t crown = 0, embers = 0;
  for (const auto& fl : flames) { if (fl.kind > 2.5f) ++embers; else ++crown; }
  require(crown >= 1 && !smoke.empty() && embers + crown == flames.size(), "the burning tree flames and smokes, its branches glow");
}

void test_toppling_and_neighbours() {
  TreeFireStyle style;
  style.spread = true;
  style.branch_drop_rate = 0;
  // A burning tree with a sound neighbour 8 m downwind: it topples with the wind
  // onto the neighbour, which is strong enough to hold it and catches fire by contact.
  TreeFireModel model(forest({tree(0, 0, 16, 0), tree(8, 0, 16, 1)}), meshes(), style, flat);
  model.ignite(0, IgnitionSource::Cell);
  bool toppled = false, leaned = false, contact = false;
  int other = -1;
  double leaning_angle = 0;
  for (int i = 0; i < 60 * 12 && !leaned; ++i) {
    model.advance(1.0 / 60.0, 12.0, 90.0);  // wind toward +x
    for (const auto& e : model.events()) {
      if (e.kind == TreeFireEvent::Kind::Toppled && e.tree == 0) toppled = true;
      if (e.kind == TreeFireEvent::Kind::Leaning && e.tree == 0) { leaned = true; other = e.other; leaning_angle = model.states()[0].fall_angle; }
      if (e.kind == TreeFireEvent::Kind::Ignited && e.tree == 1) contact = e.source == IgnitionSource::Contact || e.source == IgnitionSource::Radiation;
    }
  }
  require(toppled, "the charred trunk topples");
  require(leaned && other == 1 && leaning_angle < kPi / 2 - 0.2, "it falls against its downwind neighbour and leans on it");
  require(model.states()[0].fall_direction.x > 0.9, "it fell with the wind");
  require(contact && model.states()[1].stage != TreeFireStage::Unburned, "the neighbour catches fire");
  const auto st = model.stats();
  require((st.ignited_by_contact + st.ignited_by_radiation) == 1, "stats: one neighbour ignited by the tree");
  // The neighbour burning through drops the leaner to the ground; both end burned out and down.
  run(model, 120.0, 12.0, 90.0);
  require(model.states()[0].fall_angle > 1.5 && model.states()[1].fall_angle > 1.5 && model.stats().burned_out == 2 && model.stats().grounded == 2,
          "both burn out and finish on the ground");

  // A weak sapling in the way breaks and falls too, alight at once (radiation
  // off so that only the collapse can light it).
  TreeFireStyle weak = style;
  weak.radiant_rate = 0; weak.ember_ignition_radius_meters = 0;
  TreeFireModel crush(forest({tree(0, 0, 18, 1), tree(6, 0, 4, 0)}), meshes(), weak, flat);
  crush.ignite(0, IgnitionSource::Cell);
  bool collapsed = false;
  for (int i = 0; i < 60 * 90 && !collapsed; ++i) {
    crush.advance(1.0 / 60.0, 12.0, 90.0);
    for (const auto& e : crush.events()) if (e.kind == TreeFireEvent::Kind::Collapsed && e.other == 1) collapsed = true;
  }
  require(collapsed && crush.states()[1].stage != TreeFireStage::Unburned && crush.states()[1].source == IgnitionSource::Collapse,
          "a sapling is crushed and ignited by the falling tree");
  run(crush, 10.0, 12.0, 90.0);
  require(crush.states()[0].fall_angle > 1.5, "the big tree carries on to the ground");

  // Fallen trunks are obstacles along their length, standing ones as given.
  std::vector<Obstacle> standing;
  for (std::size_t i = 0; i < 2; ++i) { Obstacle o; o.x = crush.bodies()[i].height_meters; o.radius = 0.5; o.top = 10; standing.push_back(o); }
  const auto obstacles = crush.obstacles(standing);
  require(obstacles.size() > 2, "a trunk on the ground becomes a chain of low obstacles");
  double farthest = 0;
  for (const auto& o : obstacles) { farthest = std::max(farthest, o.x); require(o.top - o.bottom <= 2.0 * crush.bodies()[0].trunk_radius_meters + 1e-9 || o.top == 10, "logs lie low"); }
  require(farthest > 15.0, "the chain reaches along the fallen trunk");
  require(crush.obstacle_version() > 0, "falls bump the obstacle version");

  // Without wind and without a neighbour the tree still falls toward its own lean.
  TreeFireStyle lonely = style;
  TreeFireModel alone(forest({tree(0, 0, 16, 0)}), meshes(), lonely, flat);
  alone.ignite(0, IgnitionSource::Cell);
  run(alone, 60.0, 0.0, 0.0);
  const auto& s = alone.states()[0];
  require(s.fall_angle > 1.5 && near(s.fall_direction.dot(alone.bodies()[0].lean_direction), 1.0, 1e-6), "calm: it falls toward its lean");
}

void test_spread_with_wind() {
  TreeFireStyle style;
  style.branch_drop_rate = 0;
  // A line of trees 6 m apart along +x; the first burns. Downwind the fire runs
  // farther in the same time than across the wind, and nothing spreads with spread off.
  std::vector<VegetationInstance> line;
  for (int i = 0; i < 12; ++i) line.push_back(tree(6.0 * i, 0, 14));
  TreeFireModel downwind(forest(line), meshes(), style, flat), crosswind(forest(line), meshes(), style, flat);
  TreeFireStyle off = style; off.spread = false;
  TreeFireModel isolated(forest(line), meshes(), off, flat);
  downwind.ignite(0, IgnitionSource::Cell); crosswind.ignite(0, IgnitionSource::Cell); isolated.ignite(0, IgnitionSource::Cell);
  run(downwind, 12.0, 10.0, 90.0);   // toward +x
  run(crosswind, 12.0, 10.0, 0.0);   // toward -z
  run(isolated, 12.0, 10.0, 90.0);
  const auto reach = [](const TreeFireModel& m) {
    std::size_t n = 0;
    for (const auto& s : m.states()) if (s.stage != TreeFireStage::Unburned) ++n;
    return n;
  };
  require(reach(downwind) > reach(crosswind) && reach(crosswind) >= 1, "the fire runs farther downwind");
  require(reach(isolated) == 1, "with spread off only the ignited tree burns");
  const auto st = downwind.stats();
  require(st.ignited_by_radiation + st.ignited_by_contact + st.ignited_by_collapse + st.ignited_by_ember == reach(downwind) - 1,
          "every other ignition came from a tree");
  // Determinism: the same seed and inputs give the same story.
  TreeFireModel again(forest(line), meshes(), style, flat);
  again.ignite(0, IgnitionSource::Cell);
  run(again, 12.0, 10.0, 90.0);
  for (std::size_t i = 0; i < line.size(); ++i)
    require(again.states()[i].stage == downwind.states()[i].stage && again.states()[i].consumed == downwind.states()[i].consumed &&
            again.states()[i].fall_angle == downwind.states()[i].fall_angle, "the model is deterministic");
  // Applying to a placement writes the instance records.
  auto placement = forest(line);
  downwind.apply_to_placement(placement);
  require(placement.trees[0].consumed > 0 && placement.trees[0].char_amount > 0, "burned trees are charred in their instances");
  bool tilted = false;
  for (const auto& t : placement.trees) if (t.fall_angle > 0) tilted = true;
  require(tilted, "fallen trees are tilted in their instances");
  auto wrong = forest({tree(0, 0, 1)});
  rejects([&] { downwind.apply_to_placement(wrong); }, "a mismatched placement is rejected");
}
void test_storms() {
  TreeFireStyle style;
  style.branch_drop_rate = 0;
  rejects([] { TreeFireStyle s; s.anchor_factor = 0; validate_tree_fire_style(s); }, "zero anchorage rejected");
  rejects([] { TreeFireStyle s; s.strike_flash_chance = 2; validate_tree_fire_style(s); }, "a flash chance above one is rejected");
  // Anchorage varies per tree and scales with weight and height.
  const auto m = meshes();
  const auto a = tree_body(tree(0, 0, 16), mesh_footprint_radius(m[0]), 0.2, style, 1), b = tree_body(tree(0, 0, 16), mesh_footprint_radius(m[0]), 0.2, style, 2);
  require(a.anchor_moment_nm > 0 && a.anchor_moment_nm != b.anchor_moment_nm && a.anchor_moment_nm < a.breaking_moment_nm, "roots hold less than the trunk and differ per tree");
  // Windthrow: a wood of 40 trees, spaced widely (no crushing), in a calm, a breeze, a gale and a hurricane.
  std::vector<VegetationInstance> wood;
  for (int i = 0; i < 40; ++i) wood.push_back(tree(40.0 * (i % 8), 40.0 * (i / 8), 12 + (i % 5) * 2, i % 2));
  const auto count_falls = [&](double wind) {
    TreeFireModel model(forest(wood), m, style, flat);
    run(model, 8.0, wind, 90.0);
    return model.stats().windthrown;
  };
  require(count_falls(0.0) == 0 && count_falls(10.0) == 0, "calm air and a breeze uproot nothing");
  const auto gale = count_falls(38.0), hurricane = count_falls(55.0);
  require(gale > 0 && gale < wood.size() && hurricane > gale, "a gale uproots the weakest-rooted trees, a hurricane more of them");
  {
    TreeFireModel model(forest(wood), m, style, flat);
    run(model, 8.0, 38.0, 90.0);
    bool any_down = false;
    for (const auto& s : model.states()) {
      if (s.source != IgnitionSource::None) throw std::runtime_error("windthrow set nothing alight");
      if (s.fall_angle > 1.5) { any_down = true; require(s.fall_direction.x > 0.7, "uprooted trees fall downwind"); }
    }
    const auto st = model.stats();
    require(any_down && st.burning == 0 && st.burned_out == 0 && st.ignited_by_cell + st.ignited_by_lightning + st.ignited_by_radiation == 0 &&
            st.windthrown == st.fallen + st.falling, "uprooted trees lie on the ground, unburned");
    require(model.obstacle_version() > 0, "windthrow changes the obstacles");
    bool windthrown_event = false;
    TreeFireModel again(forest(wood), m, style, flat);
    for (int i = 0; i < 60 * 8 && !windthrown_event; ++i) {
      again.advance(1.0 / 60.0, 38.0, 90.0);
      for (const auto& e : again.events()) if (e.kind == TreeFireEvent::Kind::Windthrown) windthrown_event = true;
    }
    require(windthrown_event, "windthrow is an event");
  }
  // Lightning: the nearest tree to the bolt is split and alight at once; near trees may catch; far ones do not.
  TreeFireStyle calm = style;
  calm.strike_flash_chance = 1.0;
  TreeFireModel model(forest({tree(0, 0, 16), tree(20, 0, 14), tree(200, 0, 14)}), m, calm, flat);
  const int struck = model.strike(3.0, 1.0, 0.0, 0.0);
  require(struck == 0, "the nearest tree within the strike radius is struck");
  require(model.states()[0].stage == TreeFireStage::Falling && model.states()[0].intensity > 0 && model.states()[0].source == IgnitionSource::Lightning,
          "the struck tree is split: alight and coming down at once");
  require(model.states()[1].stage != TreeFireStage::Unburned && model.states()[1].source == IgnitionSource::Lightning, "a tree in the flash radius catches");
  require(model.states()[2].stage == TreeFireStage::Unburned, "a distant tree is untouched");
  bool struck_event = false;
  for (const auto& e : model.events()) if (e.kind == TreeFireEvent::Kind::Struck && e.tree == 0) struck_event = true;
  require(struck_event && model.stats().struck == 1 && model.stats().ignited_by_lightning == 2, "strike events and stats");
  run(model, 6.0);
  require(model.states()[0].fall_angle > 1.5 && model.states()[0].consumed > 0, "the split tree lies burning on the ground");
  require(model.strike(500.0, 500.0, 0.0, 0.0) == -1, "a bolt into empty ground strikes no tree");
  rejects([&] { model.strike(std::nan(""), 0, 0, 0); }, "a non-finite strike is rejected");
  // Exposure: heat beside a burning trunk, smoke downwind of it, nothing far away.
  TreeFireModel hot(forest({tree(0, 0, 16)}), m, calm, flat);
  hot.ignite(0, IgnitionSource::Cell);
  run(hot, 2.0);
  const auto close = hot.exposure_at(2.0, 0, 90.0), downwind = hot.exposure_at(30.0, 0, 90.0), upwind = hot.exposure_at(-30.0, 0, 90.0), far = hot.exposure_at(300, 0, 90.0);
  require(close.heat > 0.9 && downwind.heat < 0.1 && far.heat == 0 && far.smoke == 0, "the flames heat you beside the trunk only");
  require(downwind.smoke > upwind.smoke * 2 && downwind.smoke > 0.1, "the smoke lies downwind");
  require(hot.exposure_at(2.0, 0, 90.0).heat >= hot.exposure_at(8.0, 0, 90.0).heat, "heat fades with distance");
}
}  // namespace

void test_cutting_and_felling() {
  TreeFireStyle style;
  style.branch_drop_rate = 0;
  style.spread = false;
  const auto m = meshes();
  // A sound tree cut into from the west falls east, unlit; a burning one falls alight.
  TreeFireModel model(forest({tree(0, 0, 16), tree(30, 0, 16), tree(60, 0, 16)}), m, style, flat);
  const double radius = model.bodies()[0].trunk_radius_meters;
  require(model.standing(0) && model.states()[0].cut == 0.0, "a tree starts sound and standing");
  rejects([&] { model.cut(0, -1, {1, 0}); }, "a negative cut is rejected");
  const double first = model.cut(0, 0.06, {1, 0});
  require(near(first, 0.06 / (2.0 * radius)) && model.standing(0) && !model.states()[0].felled, "a chop cuts its depth of the diameter; the tree stands");
  require(model.states()[0].cut_direction.x == 1.0 && model.stats().cut == 1, "the cut faces the way it will fall, and is counted");
  int chops = 1;
  while (model.standing(0) && chops < 100) { model.cut(0, 0.06, {1, 0}); ++chops; }
  require(!model.standing(0) && model.states()[0].felled && model.states()[0].stage == TreeFireStage::Falling, "enough chops fell it");
  require(chops == static_cast<int>(std::ceil(2.0 * radius * TreeFireModel::kFellCut / 0.06 - 1e-9)), "at 70 % of the diameter");
  require(count_events(model.events(), TreeFireEvent::Kind::Felled) == 1 && model.states()[0].fall_direction.x > 0.99, "event Felled, falling east");
  require(near(model.cut(0, 0.06, {1, 0}), 1.0), "cutting a falling tree does nothing more");
  run(model, 6.0);
  require(model.states()[0].fall_angle > 1.4 && model.states()[0].stage == TreeFireStage::Fallen && model.states()[0].source == IgnitionSource::None,
          "down on the ground, never lit");
  require(model.stats().felled == 1 && model.stats().cut == 0, "felled is counted, not cut");
  // Felling a burning tree: it comes down alight, the way it is cut, not the way it leans.
  model.ignite(1, IgnitionSource::Cell);
  run(model, 1.0);
  while (model.standing(1)) model.cut(1, 0.06, {0, -1});
  require(model.states()[1].fall_direction.y < -0.99 && model.states()[1].intensity > 0, "a burning tree is felled north, still burning");
  // The chainsaw's continuous cut: small depths add up the same.
  double fraction = 0.0;
  int steps = 0;
  while (model.standing(2) && steps < 100000) { fraction = model.cut(2, 0.10 / 60.0, {-1, 0}); ++steps; }
  require(!model.standing(2) && fraction == 1.0 && std::abs(steps / 60.0 - 2.0 * model.bodies()[2].trunk_radius_meters * 0.7 / 0.10) < 0.05,
          "sawing at 10 cm/s fells it in the time the diameter says");
  // A trunk cut part way is weaker: burning, it breaks at the cut (the cut's way) while its uncut twin still stands.
  std::vector<VegetationInstance> pair;
  pair.push_back(tree(0, 0, 16));
  pair.push_back(tree(40, 0, 16));
  TreeFireModel burning(forest(pair), m, style, flat);
  const double diameter = 2.0 * burning.bodies()[0].trunk_radius_meters;
  burning.cut(0, diameter * 0.6, {0, 1});  // most of the way through (short of felling), set to fall south
  require(burning.standing(0) && near(burning.states()[0].cut, 0.6), "cut to 60 %, still standing");
  burning.ignite(0, IgnitionSource::Cell);
  burning.ignite(1, IgnitionSource::Cell);
  double seconds = 0;
  while (burning.standing(0) && seconds < 60) { burning.advance(1.0 / 60.0, 0, 90); seconds += 1.0 / 60.0; }
  require(!burning.standing(0) && burning.standing(1), "the cut tree comes down first");
  require(burning.states()[0].felled && burning.states()[0].fall_direction.y > 0.99 && burning.states()[0].intensity > 0, "breaking at the cut, the cut's way, alight");
  // Branches cut in pieces: a burning branch becomes two halves that burn on; short ones cannot be cut.
  TreeFireModel branches(forest({tree(0, 0, 16)}), m, style, flat);
  branches.add_burning_branch({3, 0, 3}, 30.0, 0.8);
  require(branches.split_branch(3, 3, 1.0) && branches.branches().size() == 2, "a branch is cut in two");
  require(near(branches.branches()[0].size_meters, 0.4) && near(branches.branches()[1].size_meters, 0.4) && branches.branches()[1].seconds_left == 30.0,
          "two halves, burning on");
  require(!branches.split_branch(3, 3, 1.0), "a quarter-metre piece cannot be cut again");
  require(!branches.split_branch(50, 50, 1.0), "nothing to cut far away");
  rejects([&] { branches.split_branch(3, 3, -1); }, "a negative radius is rejected");
  require(std::string(ignition_source_name(IgnitionSource::Ember)) == "ember", "ignition source names");
}

int main() {
  try {
    test_bodies();
    test_ignition_and_burning();
    test_branches_and_gravity();
    test_toppling_and_neighbours();
    test_spread_with_wind();
    test_storms();
    test_cutting_and_felling();
  } catch (const std::exception& error) {
    std::cerr << "tree fire tests failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "tree fire tests passed\n";
  return 0;
}
