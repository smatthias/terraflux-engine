#include <terraflux/graphics/items.hpp>
#include <terraflux/graphics/tree_fire.hpp>
#include <terraflux/grid.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

using namespace terraflux;
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

// 30 x 30 cells: the west half a steep rocky ramp (bare), the east half flat grassland.
terrain::TerrainMesh landscape() {
  std::vector<compute::GridCell> cells;
  for (int y = 0; y < 30; ++y)
    for (int x = 0; x < 30; ++x) {
      compute::GridCell c{};
      c.x = x; c.y = y;
      c.elevation_meters = x < 15 ? 100.0 + (15 - x) * 25.0 : 100.0;  // ~40 degrees west
      c.fuel_load = x < 15 ? 0.0 : 0.6; c.fuel_moisture = 0.3;
      cells.push_back(c);
    }
  return terrain::build_terrain_mesh(compute::Grid(cells), 30);
}

void test_stones() {
  const auto mesh = landscape();
  const TerrainSampler sampler(mesh);
  const auto terrain_style = terrain_style_for(mesh, 0);
  StoneStyle style;
  validate_stone_style(style);
  rejects([] { StoneStyle s; s.max_radius = 0.01; validate_stone_style(s); }, "a max radius below the min is rejected");
  const auto stones = place_stones(mesh, terrain_style, sampler, style);
  require(!stones.empty(), "stones are placed");
  std::size_t on_rock = 0, on_grass = 0;
  for (const auto& s : stones) {
    require(s.radius >= style.min_radius && s.radius <= style.max_radius && s.size.x > 0 && s.present, "stones are sized and present");
    const auto g = sampler.sample(s.position.x, s.position.z);
    require(g && s.position.y > g->position.y && s.position.y < g->position.y + s.size.y, "stones rest on the ground");
    (s.position.x < 450 ? on_rock : on_grass)++;
  }
  require(on_rock > on_grass * 4, "stones lie on the rocky slope, hardly on the grass");
  const auto again = place_stones(mesh, terrain_style, sampler, style);
  require(again.size() == stones.size() && again[3].position.x == stones[3].position.x && again[3].radius == stones[3].radius, "placement is deterministic");
  StoneStyle other = style; other.seed = 7;
  const auto different = place_stones(mesh, terrain_style, sampler, other);
  require(different.size() != stones.size() || different[3].position.x != stones[3].position.x, "the seed changes the layout");
  require(place_stones(mesh, terrain_style, sampler, style, 10000.0).empty(), "nothing under water");
  // Obstacles: only present stones, low enough to step over when small.
  auto copy = stones;
  copy[0].present = false;
  const auto obstacles = stone_obstacles(copy);
  require(obstacles.size() == stones.size() - 1 && obstacles[0].top - obstacles[0].bottom < 1.0, "present stones are low obstacles");
}

void test_throwing_and_reach() {
  const auto v = throw_velocity({0, 0, -1}, 12.0, 15.0);
  require(near(v.length(), 12.0) && v.y > 0 && v.z < 0 && near(std::atan2(v.y, -v.z), 15.0 * kPi / 180.0), "a throw lifts the look direction by the angle");
  rejects([] { (void)throw_velocity({0, 0, -1}, 500, 0); }, "an absurd throw speed is rejected");
  // A stone thrown from 1.6 m up over flat ground lands about where ballistics says.
  const StandingHeightFn flat = [](double x, double z) -> std::optional<double> { if (x < 0 || x > 1000 || z < 0 || z > 1000) return std::nullopt; return 10.0; };
  Projectile p;
  p.item.kind = HeldKind::Stone; p.item.size = 0.1;
  p.position = {500, 11.6, 500}; p.velocity = throw_velocity({1, 0, 0}, 12.0, 15.0);
  for (int i = 0; i < 600 && !p.landed; ++i) advance_projectile(p, 1.0 / 60.0, 9.81, flat, {0, 0, 0}, {1000, 100, 1000});
  require(p.landed && near(p.position.y, 10.0) && p.position.x > 510.5 && p.position.x < 512.0, "the stone lands about 11 m away, as ballistics says");
  require(p.velocity.length() == 0 && p.seconds > 0.9 && p.seconds < 1.05, "at rest where it landed, after the flight time");
  advance_projectile(p, 1.0, 9.81, flat, {0, 0, 0}, {1000, 100, 1000});
  require(p.landed && near(p.position.y, 10.0), "a landed thing stays");
  // Reach: the nearest stone in front, not one behind or to the side.
  std::vector<Stone> stones(3);
  stones[0].position = {501.0, 10.1, 500}; stones[1].position = {500.5, 10.1, 500}; stones[2].position = {499.0, 10.1, 500};
  const Vec3 eye{500, 11.7, 500}, forward{1, -0.6, 0};
  const auto pick = stone_in_reach(stones, eye, forward, 2.0, 60.0);
  require(pick && *pick == 1, "the nearest stone in front is in reach");
  stones[1].present = false;
  require(stone_in_reach(stones, eye, forward, 2.0, 60.0) == 0, "a taken stone is skipped");
  require(!stone_in_reach(stones, eye, {-1, -0.6, 0}, 2.0, 60.0).has_value() == false || *stone_in_reach(stones, eye, {-1, -0.6, 0}, 2.0, 60.0) == 2, "looking back reaches the stone behind");
  require(!stone_in_reach(stones, eye, forward, 0.5, 60.0), "too far is out of reach");
  std::vector<DroppedBranch> branches(1);
  branches[0].position = {501.0, 10.05, 500};
  require(branch_in_reach(branches, eye, forward, 2.0, 60.0) == 0, "a branch in reach");
  Obstacle trunk; trunk.x = 501.5; trunk.z = 500; trunk.radius = 0.4; trunk.bottom = 10; trunk.top = 26;
  Obstacle log = trunk; log.x = 499.0; log.top = 11;
  const ObstacleField trunks({trunk, log});
  require(trunk_in_reach(trunks, {500, 10, 500}, 1.5) == 0, "a trunk within arm's reach of the feet");
  require(!trunk_in_reach(trunks, {505, 10, 500}, 1.5), "not from further away");
  // Props: nearest first, bounded.
  std::vector<Stone> many(50);
  for (std::size_t i = 0; i < many.size(); ++i) { many[i].position = {500.0 + i, 10, 500}; many[i].size = {0.3, 0.2, 0.3}; }
  const auto props = props_near(many, branches, eye, 20.0, 8);
  require(props.size() == 8 && props[0].transform.at(0, 3) <= props[7].transform.at(0, 3), "props nearest first, bounded");
}

void test_branches_in_the_fire() {
  TreeFireStyle style;
  style.branch_drop_rate = 0;
  VegetationPlacement placement;
  VegetationInstance t{}; t.scale = 16; t.tint[0] = t.tint[1] = t.tint[2] = 1;
  placement.trees.push_back(t);
  t.position[0] = 6; placement.trees.push_back(t);
  TreeMeshOptions c; std::vector<VegetationMesh> meshes{make_tree_mesh(c)};
  TreeFireModel model(placement, meshes, style, [](double, double) -> std::optional<double> { return 0.0; });
  require(!model.take_branch(0, 0, 5.0), "nothing to take from a cold wood");
  model.add_burning_branch({6.5, 0, 0.5}, 40.0);
  require(model.branches().size() == 1 && model.branches()[0].landed, "a burning branch put down lies burning");
  model.advance(1.0 / 60.0, 0, 0);
  require(model.states()[1].stage != TreeFireStage::Unburned && model.states()[1].source == IgnitionSource::Ember, "it lights the trunk it lies beside");
  const auto taken = model.take_branch(6.5, 0.5, 1.0);
  require(taken && *taken < 40.0 && *taken > 39.0 && model.branches().empty(), "taken again with the burn time it has left");
  rejects([&] { model.add_burning_branch({0, 0, 0}, -1); }, "negative burn time rejected");
}
}  // namespace

int main() {
  try { test_stones(); test_throwing_and_reach(); test_branches_in_the_fire(); }
  catch (const std::exception& error) { std::cerr << "items tests failed: " << error.what() << '\n'; return 1; }
  std::cout << "items tests passed\n";
  return 0;
}
