#include <terraflux/assets/obj.hpp>
#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/collision.hpp>
#include <terraflux/grid.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
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

// Flat ground at 10 m everywhere inside a 1 km square.
const Vec3 kLower{0, 0, 0}, kUpper{1000, 100, 1000};
std::optional<double> flat(double x, double z) {
  if (x < 0 || x > 1000 || z < 0 || z > 1000) return std::nullopt;
  return 10.0;
}
// A 3 m wall: ground rises 3 m between x = 500 and x = 500.5 (an 80-degree face), flat elsewhere.
std::optional<double> cliff(double x, double z) {
  if (x < 0 || x > 1000 || z < 0 || z > 1000) return std::nullopt;
  return 10.0 + 3.0 * std::clamp((x - 500.0) / 0.5, 0.0, 1.0);
}
// A long ramp of the given slope in degrees rising east of x = 500.
auto ramp(double degrees) {
  return [degrees](double x, double z) -> std::optional<double> {
    if (x < 0 || x > 1000 || z < 0 || z > 1000) return std::nullopt;
    return 10.0 + std::max(0.0, x - 500.0) * std::tan(degrees * kPi / 180.0);
  };
}
Obstacle cylinder(double x, double z, double radius, double bottom, double top) {
  Obstacle o; o.x = x; o.z = z; o.radius = radius; o.bottom = bottom; o.top = top; return o;
}
WalkEnvironment environment(StandingHeightFn ground, const ObstacleField* field = nullptr) {
  WalkEnvironment env;
  env.standing_height = std::move(ground);
  env.lower = kLower; env.upper = kUpper;
  env.gait.enabled = false;  // kinematic checks: constant speed
  if (field) env.obstacles.push_back(field);
  return env;
}
FirstPersonCamera walker(double x, double z, double yaw, double speed = 8.0) {
  FirstPersonCamera person;
  person.feet = {x, 10.0, z}; person.yaw = yaw; person.walk_speed_mps = speed;
  return person;
}

void test_field_and_body() {
  WalkerBody body;
  validate_walker_body(body);
  rejects([] { WalkerBody b; b.radius = 0; validate_walker_body(b); }, "zero radius rejected");
  rejects([] { WalkerBody b; b.step_height = 3; validate_walker_body(b); }, "step above the body rejected");
  rejects([] { WalkerBody b; b.max_slope_degrees = 90; validate_walker_body(b); }, "a vertical slope limit is rejected");
  rejects([] { (void)ObstacleField({cylinder(0, 0, -1, 0, 1)}); }, "negative radius rejected");
  rejects([] { (void)ObstacleField({cylinder(0, 0, 1, 2, 1)}); }, "top below bottom rejected");
  rejects([] { (void)ObstacleField({}, 0); }, "zero cell size rejected");

  const Obstacle trunk = cylinder(100, 100, 0.5, 10, 26);   // a 16 m tree
  const Obstacle pebble = cylinder(100, 100, 0.3, 10, 10.3); // a stone at the feet
  const Obstacle branch = cylinder(100, 100, 3.0, 13, 20);   // a crown high above
  require(obstacle_blocks(trunk, 10, body) && !obstacle_blocks(pebble, 10, body) && !obstacle_blocks(branch, 10, body),
          "trunks block, pebbles are stepped over, crowns are passed under");
  require(obstacle_blocks(branch, 12, body), "standing higher, the crown is in the way");
  require(!obstacle_blocks(trunk, 26, body), "standing on top of it, the trunk is below the feet");

  std::vector<Obstacle> many;
  for (int i = 0; i < 100; ++i) many.push_back(cylinder(5.0 * i, 7.0 * i, 0.4, 0, 20));
  const ObstacleField field(many, 16.0);
  require(field.size() == 100 && near(field.max_radius(), 0.4), "field keeps its obstacles and the largest radius");
  std::size_t visited = 0;
  field.for_each_near(250, 350, 2.0, [&](const Obstacle& o) { ++visited; require(std::hypot(o.x - 250, o.z - 350) < 40, "only nearby cells are visited"); });
  require(visited >= 1 && visited < 100, "neighbourhood query is a small superset");
  require(near(*field.clearance(251, 350, 5), 0.6) && !field.clearance(600, 600, 5), "clearance to the nearest trunk");

  const Obstacle asset = asset_obstacle({{10, 20, 30}, 4});
  require(near(asset.x, 10) && near(asset.z, 30) && near(asset.radius, 4) && near(asset.bottom, 16) && near(asset.top, 24),
          "asset cylinder wraps the bounding sphere");
}

void test_push_out() {
  WalkerBody body;
  const ObstacleField trees({cylinder(100, 100, 0.5, 10, 26), cylinder(101.6, 100, 0.5, 10, 26)});
  const ObstacleField* fields[] = {&trees};
  const auto p = push_out_of_obstacles(fields, {100.3, 100.2}, 10, body);
  require(std::hypot(p.x - 100, p.y - 100) >= 0.85 - 1e-9 && std::hypot(p.x - 101.6, p.y - 100) >= 0.85 - 1e-9,
          "pushed clear of both neighbouring trunks");
  const auto axis = push_out_of_obstacles(fields, {100, 100}, 10, body);
  require(near(std::hypot(axis.x - 100, axis.y - 100), 0.85) || near(std::hypot(axis.x - 101.6, axis.y - 100), 0.85),
          "standing on the axis still gets pushed out");
  const auto untouched = push_out_of_obstacles(fields, {120, 120}, 10, body);
  require(near(untouched.x, 120) && near(untouched.y, 120), "clear positions are untouched");
  const auto over = push_out_of_obstacles(fields, {100.3, 100.2}, 30, body);
  require(near(over.x, 100.3) && near(over.y, 100.2), "above the trunk tops nothing blocks");
  const ObstacleField* none[] = {nullptr};
  const auto skipped = push_out_of_obstacles(none, {100, 100}, 10, body);
  require(near(skipped.x, 100), "null fields are skipped");
  rejects([&] { (void)push_out_of_obstacles(fields, {std::nan(""), 0}, 10, body); }, "non-finite position rejected");
}

void test_walking_around_trees() {
  const ObstacleField trees({cylinder(520, 500, 0.5, 10, 26)});
  const auto env = environment(flat, &trees);
  // Straight at the trunk from the west: stop touching it, never inside.
  auto person = walker(500, 500, -kPi / 2);  // looking east
  const double walked = walk(person, {1, 0, true}, 2.0, env);  // 64 m asked
  require(walked < 20 && person.feet.x <= 520 - 0.85 + 1e-6 && near(person.feet.x, 520 - 0.85, 1e-6) && near(person.feet.z, 500, 1e-6),
          "the trunk stops the walker at body radius plus trunk radius");
  // Slightly off-axis: slides around the trunk and continues east.
  person = walker(500, 500.2, -kPi / 2);
  walk(person, {1, 0, true}, 2.0, env);
  require(person.feet.x > 530 && std::hypot(person.feet.x - 520, person.feet.z - 500) >= 0.85 - 1e-6,
          "an off-centre approach slides around the trunk and carries on");
  // Grass is not in the field; a low stone is stepped over.
  const ObstacleField stones({cylinder(520, 500, 0.3, 10, 10.4)});
  const auto stony = environment(flat, &stones);
  person = walker(500, 500, -kPi / 2);
  walk(person, {1, 0, true}, 2.0, stony);
  require(person.feet.x > 540, "a stone below the step height is walked over");
  // Tunnelling: a thin trunk and a long fast step still collide.
  const ObstacleField thin({cylinder(510, 500, 0.05, 10, 26)});
  WalkEnvironment quick = environment(flat, &thin);
  quick.body.radius = 0.1;
  person = walker(500, 500, -kPi / 2, 60.0);
  walk(person, {1, 0, true}, 0.1, quick);  // 24 m in one frame
  require(near(person.feet.x, 510 - 0.15, 1e-6), "sub-stepping prevents tunnelling through a thin trunk");
  // The surrounded case: ringed by trunks you cannot go anywhere.
  std::vector<Obstacle> ring;
  for (int i = 0; i < 12; ++i) ring.push_back(cylinder(500 + 1.2 * std::cos(i * kPi / 6), 500 + 1.2 * std::sin(i * kPi / 6), 0.3, 10, 26));
  const ObstacleField ringed(ring);
  const auto pen = environment(flat, &ringed);
  person = walker(500, 500, 0.7);
  const double escaped = walk(person, {1, 0, true}, 1.0, pen);
  require(std::hypot(person.feet.x - 500, person.feet.z - 500) < 1.0 && escaped < 1.0, "a ring of trunks pens the walker in");
}

void test_walls_and_slopes() {
  // The 80-degree face is a wall: stopped at its foot, sliding along it.
  auto env = environment(cliff);
  auto person = walker(499, 500, -kPi / 2);
  const double walked = walk(person, {1, 0, false}, 1.0, env);
  require(person.feet.x <= 500.5 && person.feet.y < 11.0 && walked < 1.6, "the cliff face stops the climb");
  person = walker(499, 500, -kPi / 4);  // north-east into the face
  walk(person, {1, 0, false}, 2.0, env);
  require(person.feet.z < 500 - 5.0 && person.feet.x <= 500.5 && person.feet.y < 11.0, "the motion slides north along the cliff foot");
  // Walking down the same face is allowed.
  person = walker(502, 500, kPi / 2); person.feet.y = 13.0;  // looking west from the top
  walk(person, {1, 0, false}, 1.0, env);
  require(person.feet.x < 499 && near(person.feet.y, 10.0), "descending a cliff is not blocked");
  // A 30-degree ramp is walkable, a 60-degree ramp is not, and the limit is the body's.
  auto gentle = environment(ramp(30));
  person = walker(499, 500, -kPi / 2);
  walk(person, {1, 0, false}, 1.0, gentle);
  require(person.feet.x > 506 && person.feet.y > 13, "a 30-degree slope is climbed");
  auto steep = environment(ramp(60));
  person = walker(499, 500, -kPi / 2);
  walk(person, {1, 0, false}, 1.0, steep);
  require(person.feet.x <= 500 + 0.2 && person.feet.y < 10.4, "a 60-degree slope is a wall (reached within a sub-step of its foot)");
  steep.body.max_slope_degrees = 70;
  person = walker(499, 500, -kPi / 2);
  walk(person, {1, 0, false}, 1.0, steep);
  require(person.feet.x > 506, "raising the limit makes it climbable");
  // The convenience overload has no slope limit (existing behaviour).
  person = walker(499, 500, -kPi / 2);
  walk(person, {1, 0, false}, 1.0, ramp(60), kLower, kUpper);
  require(person.feet.x > 506, "the plain overload climbs anything");
  // Holes still stop the walker.
  auto holed = environment([](double x, double z) -> std::optional<double> { return x > 505 ? std::nullopt : flat(x, z); });
  person = walker(504, 500, -kPi / 2);
  walk(person, {1, 0, true}, 1.0, holed);
  require(person.feet.x <= 505 + 1e-9 && person.feet.x > 504.5, "a hole stops the walker at its edge");
  rejects([&] { WalkEnvironment bad = environment(flat); bad.body.radius = 0; walk(person, {1, 0, false}, 1.0, bad); },
          "an invalid body is rejected");
}

void test_settle() {
  const ObstacleField trees({cylinder(500, 500, 0.5, 10, 26)});
  const auto env = environment(flat, &trees);
  auto person = walker(500.1, 500, 0);
  require(settle(person, env) && near(std::hypot(person.feet.x - 500, person.feet.z - 500), 0.85, 1e-6) && near(person.feet.y, 10),
          "settling moves the feet out of the trunk and onto the ground");
  auto outside = walker(1100, 500, 0);
  const auto env_hole = environment([](double x, double z) -> std::optional<double> { return x > 1000 ? std::nullopt : flat(x, z); });
  require(settle(outside, env_hole) && near(outside.feet.x, 1000), "settling clamps to the bounds");
  auto doomed = walker(500, 500, 0);
  const auto nowhere = environment([](double, double) -> std::optional<double> { return std::nullopt; });
  require(!settle(doomed, nowhere) && near(doomed.feet.x, 500), "no ground: untouched and reported");
}

void test_tree_obstacles(const char* obj_path) {
  TreeMeshOptions conifer; conifer.shape = TreeShape::Conifer;
  TreeMeshOptions broadleaf; broadleaf.shape = TreeShape::Broadleaf; broadleaf.crown_width = 0.8;
  const auto conifer_mesh = make_tree_mesh(conifer), broadleaf_mesh = make_tree_mesh(broadleaf);
  const double conifer_foot = mesh_footprint_radius(conifer_mesh), broadleaf_foot = mesh_footprint_radius(broadleaf_mesh);
  require(near(conifer_foot, 0.035 + 0.02 * conifer.crown_width, 1e-6) && near(broadleaf_foot, 0.035 + 0.02 * 0.8, 1e-6),
          "a procedural tree's footprint is its trunk radius, not its crown");
  require(near(mesh_footprint_radius(make_grass_mesh(3)), 0.28, 1e-6), "a grass tuft has a footprint too, but tree_obstacles never uses it");
  std::ifstream file(obj_path);
  require(static_cast<bool>(file), "fixture OBJ opens");
  const auto model = assets::import_obj(file);
  const auto custom = vegetation_mesh_from_model(model, {}, {0.3, 0.5, 0.3}, "pyramid");
  require(mesh_footprint_radius(custom) > 0.3, "a custom model's base is its footprint");
  rejects([&] { (void)mesh_footprint_radius(custom, 0); }, "zero footprint height rejected");

  VegetationPlacement placement;
  VegetationInstance tree{};
  tree.position[0] = 10; tree.position[1] = 50; tree.position[2] = 20; tree.scale = 16; tree.kind = 0;
  placement.trees.push_back(tree);
  tree.position[0] = 30; tree.kind = 1; tree.scale = 8;
  placement.trees.push_back(tree);
  VegetationInstance tuft{}; tuft.scale = 0.6;
  placement.grass.push_back(tuft);
  const VegetationMesh meshes[] = {conifer_mesh, broadleaf_mesh};
  const auto obstacles = tree_obstacles(placement, meshes);
  require(obstacles.size() == 2, "one obstacle per tree, none per grass tuft");
  require(near(obstacles[0].x, 10) && near(obstacles[0].z, 20) && near(obstacles[0].radius, conifer_foot * 16, 1e-6) &&
          near(obstacles[0].bottom, 50) && near(obstacles[0].top, 66), "trunk scaled by the tree height, standing on its ground point");
  require(near(obstacles[1].radius, broadleaf_foot * 8, 1e-6), "the second kind uses the second mesh");
  const VegetationMesh single[] = {conifer_mesh};
  require(near(tree_obstacles(placement, single)[1].radius, conifer_foot * 8, 1e-6), "with one mesh every kind uses it");
  require(tree_obstacles(placement, {}).empty(), "no meshes, no obstacles");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::runtime_error("usage: terraflux-collision-tests <pyramid_materials.obj>");
    test_field_and_body();
    test_push_out();
    test_walking_around_trees();
    test_walls_and_slopes();
    test_settle();
    test_tree_obstacles(argv[1]);
  } catch (const std::exception& error) {
    std::cerr << "collision tests failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "collision tests passed\n";
  return 0;
}
