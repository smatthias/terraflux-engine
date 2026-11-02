#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/collision.hpp>
#include <terraflux/graphics/render_pass.hpp>
#include <terraflux/graphics/roads.hpp>
#include <terraflux/graphics/vitals.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace terraflux;
using namespace terraflux::graphics;
using terraflux::math::Vec2;
using terraflux::math::Vec3;

namespace {
constexpr double kPi = 3.14159265358979323846;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template <class E = std::invalid_argument, class F> void rejects(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const E&) { caught = true; }
  require(caught, message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) < tolerance; }

// A plain at 50 m with a valley between x = 110 and x = 190 (30 m deep, 20 m
// ramps on either side, so the sides are 1.5:1 — too steep for a road), a
// lake basin between x = 380 and x = 480 (the bed at 40 m under a water level
// of 45 m, banks of 1:2, also too steep) and a pond between x = 640 and
// x = 820 (the bed at 44 m, banks of 1:10 a road could take — only the water
// calls for a bridge), all running the whole width in z.
double ground_profile(double x) {
  if (x >= 110 && x <= 190) {
    if (x < 130) return 50.0 - (x - 110) * 1.5;
    if (x > 170) return 50.0 - (190 - x) * 1.5;
    return 20.0;
  }
  if (x >= 380 && x <= 480) {
    if (x < 400) return 50.0 - (x - 380) * 0.5;
    if (x > 460) return 50.0 - (480 - x) * 0.5;
    return 40.0;
  }
  if (x >= 640 && x <= 820) {
    if (x < 700) return 50.0 - (x - 640) * 0.1;
    if (x > 760) return 50.0 - (820 - x) * 0.1;
    return 44.0;
  }
  return 50.0;
}
const StandingHeightFn plain_ground = [](double x, double z) -> std::optional<double> {
  if (x < -50 || x > 1050 || z < -500 || z > 500) return std::nullopt;
  return ground_profile(x);
};
constexpr double kLake = 45.0;

// A hand-built network: a road from A to B, a bridge B-C over the valley, a road C-D.
RoadNetworkDef valley_def() {
  RoadNetworkDef def;
  def.nodes = {{{0, 0}}, {{100, 0}}, {{200, 0}}, {{300, 0}}};
  def.segments = {{0, 1, RoadKind::Road}, {1, 2, RoadKind::Bridge}, {2, 3, RoadKind::Road}};
  return def;
}

WalkEnvironment environment_for(const RoadNetwork& network, ObstacleField& field, bool with_speed = true) {
  WalkEnvironment env;
  env.standing_height = plain_ground;
  env.lower = {-50, 0, -500}; env.upper = {1050, 0, 500};
  env.platforms = [&network](double x, double z, double feet_y) { return network.platform_height(x, z, feet_y); };
  if (with_speed) env.surface_speed = [&network](double x, double z) { return network.speed_factor(x, z); };
  field = ObstacleField(network.obstacles());
  env.obstacles = {&field};
  env.gait.enabled = false;
  return env;
}

void test_style_and_validation() {
  RoadStyle style;
  validate_road_style(style);
  require(near(road_width(style), 7.0), "two lanes and two shoulders are seven metres");
  rejects([] { RoadStyle s; s.lane_width_meters = 1.0; validate_road_style(s); }, "a one-metre lane is rejected");
  rejects([] { RoadStyle s; s.road_speed_factor = 0.9; validate_road_style(s); }, "a road slower than the ground is rejected");
  rejects([] { RoadStyle s; s.rubble_speed_factor = 1.5; validate_road_style(s); }, "rubble faster than the ground is rejected");
  rejects([] { RoadStyle s; s.washout_seconds = 0; validate_road_style(s); }, "an instant washout is rejected");
  rejects([] { RoadStyle s; s.collapse_damage = 2; validate_road_style(s); }, "collapse damage above one is rejected");
  validate_road_plan_style(RoadPlanStyle{});
  rejects([] { RoadPlanStyle p; p.max_span_meters = 1; validate_road_plan_style(p); }, "a one-metre span limit is rejected");
  require(std::string(road_kind_name(RoadKind::Bridge)) == "bridge" && std::string(road_event_name(RoadEvent::Kind::WashedOut)) == "washed out", "names");

  validate_road_network(RoadNetworkDef{});
  validate_road_network(valley_def());
  rejects([] { auto d = valley_def(); d.segments[1].b = 9; validate_road_network(d); }, "a segment to a missing node is rejected");
  rejects([] { auto d = valley_def(); d.segments[1].b = 1; validate_road_network(d); }, "a segment from a node to itself is rejected");
  rejects([] { auto d = valley_def(); d.segments.push_back({0, 1, RoadKind::Road}); validate_road_network(d); }, "a duplicated segment is rejected");
  rejects([] { auto d = valley_def(); d.nodes[1].position = d.nodes[0].position; validate_road_network(d); }, "a zero-length segment is rejected");
  rejects([] { auto d = valley_def(); d.nodes.push_back({{500, 500}}); validate_road_network(d); }, "a node joined to nothing is rejected");
  rejects([] { RoadNetworkDef d; d.nodes = {{{0, 0}}, {{100, 0}}}; d.segments = {{0, 1, RoadKind::Bridge}}; validate_road_network(d); },
          "a stand-alone bridge is rejected");
  rejects([] { auto d = valley_def(); d.segments[2].kind = RoadKind::Bridge; validate_road_network(d); }, "two bridges end to end with no road beyond are rejected");
  rejects([] { auto d = valley_def(); d.segments.erase(d.segments.begin() + 1); validate_road_network(d); }, "a network in two pieces is rejected");
  rejects([] { auto d = valley_def(); d.nodes.push_back({{500, 0}}); d.nodes.push_back({{600, 0}}); d.segments.push_back({4, 5, RoadKind::Road}); validate_road_network(d); },
          "a road apart from the rest is rejected");
  {
    // A bridge between two roads that meet elsewhere is fine; so is a junction.
    RoadNetworkDef d;
    d.nodes = {{{0, 0}}, {{100, 0}}, {{200, 0}}, {{300, 0}}, {{100, 100}}};
    d.segments = {{0, 1, RoadKind::Road}, {1, 2, RoadKind::Bridge}, {2, 3, RoadKind::Road}, {1, 4, RoadKind::Road}};
    validate_road_network(d);
  }
  rejects([] { RoadNetwork n(valley_def(), RoadStyle{}, {}); }, "a network needs a ground function");
  rejects([] { auto d = valley_def(); d.nodes[3].position = {2000, 0}; RoadNetwork n(d, RoadStyle{}, plain_ground); }, "a node off the terrain is rejected");
}

void test_planner() {
  RoadStyle style;
  RoadPlanStyle plan;
  const std::vector<std::vector<Vec2>> routes{{{0, 0}, {900, 0}}};
  const auto def = plan_road_network(routes, plain_ground, kLake, style, plan);
  require(def.segments.size() == 7, "the route is road, bridge, road, bridge, road, bridge, road");
  std::size_t bridges = 0;
  for (const auto& s : def.segments) if (s.kind == RoadKind::Bridge) ++bridges;
  require(bridges == 3, "a bridge over the valley, one over the lake and one over the pond");
  // Segments follow the route in order: the bridge ends lie on the rims and the banks, at the plain's height.
  const auto& valley = def.segments[1];
  const auto& lake = def.segments[3];
  require(valley.kind == RoadKind::Bridge && lake.kind == RoadKind::Bridge, "the second and fourth segments are the bridges");
  const double v0 = def.nodes[valley.a].position.x, v1 = def.nodes[valley.b].position.x;
  require(v0 <= 110 + 1e-9 && v1 >= 190 - 1e-9 && v1 - v0 <= 120, "the valley bridge spans the steep sides and little more");
  require(near(ground_profile(v0), 50) && near(ground_profile(v1), 50), "the valley bridge lands on the plain");
  const double l0 = def.nodes[lake.a].position.x, l1 = def.nodes[lake.b].position.x;
  require(ground_profile(l0) > kLake && ground_profile(l1) > kLake && l0 < 400 && l1 > 460, "the lake bridge starts and ends on dry ground over the whole water");
  for (const auto& s : def.segments) require(s.a < def.nodes.size() && s.b < def.nodes.size(), "indices");
  const auto& pond = def.segments[5];
  require(pond.kind == RoadKind::Bridge && def.nodes[pond.a].position.x < 690 && def.nodes[pond.b].position.x > 770, "the pond bridge spans the water");
  RoadNetwork network(def, style, plain_ground);
  require(network.clearance(1) > 29 && network.clearance(1) < 31, "the valley deck stands thirty metres over the valley floor");
  require(network.clearance(3) > 9 && network.clearance(3) < 11, "the lake deck stands ten metres over the bed");
  const auto stats = network.stats();
  require(stats.bridges == 3 && stats.roads == 4 && near(stats.road_meters + stats.bridge_meters, 900, 1e-6), "the stats add up to the route");

  // A gentle dip is a road, not a bridge; no water: only the valley needs one.
  const StandingHeightFn rolling = [](double x, double) -> std::optional<double> { return 50.0 - 5.0 * std::sin(x / 400.0 * 3.14159265358979323846); };
  const auto gentle = plan_road_network(routes, rolling, std::nullopt, style, plan);
  require(gentle.segments.size() == 1 && gentle.segments[0].kind == RoadKind::Road, "a gentle dip gets no bridge");
  const auto dry = plan_road_network(routes, plain_ground, std::nullopt, style, plan);
  require(dry.segments.size() == 5 && dry.segments[1].kind == RoadKind::Bridge && dry.segments[3].kind == RoadKind::Bridge,
          "without water the pond's gentle banks are a road; the valley and the steep-banked basin are still bridged");
  // Too wide to span: the road goes down into the valley and up again.
  RoadPlanStyle narrow = plan; narrow.max_span_meters = 60;
  const auto through = plan_road_network(routes, plain_ground, std::nullopt, style, narrow);
  require(through.segments.size() == 1 && through.segments[0].kind == RoadKind::Road, "a valley wider than the span limit is crossed on the ground");
  // Routes that share a waypoint join there; routes that do not are two networks and rejected.
  const std::vector<std::vector<Vec2>> crossing{{{0, 0}, {300, 0}, {900, 0}}, {{300, -200}, {300, 0}, {300, 200}}};
  const auto joined = plan_road_network(crossing, plain_ground, kLake, style, plan);
  std::size_t at_junction = 0;
  for (const auto& s : joined.segments)
    for (const auto i : {s.a, s.b}) if (near(joined.nodes[i].position.x, 300) && near(joined.nodes[i].position.y, 0)) ++at_junction;
  require(at_junction == 4, "four segments meet at the shared waypoint");
  rejects([&] { const std::vector<std::vector<Vec2>> apart{{{0, 0}, {300, 0}}, {{0, 200}, {300, 200}}}; plan_road_network(apart, plain_ground, kLake, style, plan); },
          "two routes that never meet are rejected");
  rejects([&] { const std::vector<std::vector<Vec2>> off{{{0, 0}, {5000, 0}}}; plan_road_network(off, plain_ground, kLake, style, plan); }, "a waypoint off the terrain is rejected");
  rejects([&] { const std::vector<std::vector<Vec2>> one{{{0, 0}}}; plan_road_network(one, plain_ground, kLake, style, plan); }, "a route of one waypoint is rejected");
  // Deterministic.
  const auto again = plan_road_network(routes, plain_ground, kLake, style, plan);
  require(again.nodes.size() == def.nodes.size() && again.segments.size() == def.segments.size(), "planning is deterministic");
  for (std::size_t i = 0; i < def.nodes.size(); ++i) require(near(again.nodes[i].position.x, def.nodes[i].position.x), "same nodes");
}

void test_classes_and_viaducts() {
  RoadStyle style;
  require(road_class_lanes(RoadClass::Street) == 2 && road_class_lanes(RoadClass::Avenue) == 4 && road_class_lanes(RoadClass::Highway) == 6 && road_class_lanes(RoadClass::Runway) == 0,
          "two, four and six lanes; pavements have none");
  require(near(road_class_width(style, RoadClass::Street), 7.0) && near(road_class_width(style, RoadClass::Avenue), 13.0) && near(road_class_width(style, RoadClass::Highway), 21.0) &&
          near(road_class_width(style, RoadClass::Runway), 45.0) && near(road_class_width(style, RoadClass::Apron), 60.0), "widths by class");
  require(*road_class_from_name("highway") == RoadClass::Highway && !road_class_from_name("canal") && std::string(road_class_name(RoadClass::Taxiway)) == "taxiway", "class names");
  rejects([] { RoadStyle s; s.runway_width_meters = 5; validate_road_style(s); }, "a five-metre runway is rejected");
  // A viaduct: a highway lifted over a street by a chain of bridges between two lifted nodes.
  RoadNetworkDef def;
  // Along z at x = 50 (flat plain), over a street running along x at z = 200.
  def.nodes = {{{50, 0}}, {{50, 100}}, {{50, 160}, 4.0}, {{50, 200}, 7.0}, {{50, 240}, 4.0}, {{50, 300}}, {{50, 400}}, {{-40, 200}}, {{100, 200}}};
  def.segments = {{0, 1, RoadKind::Road, RoadClass::Highway}, {1, 2, RoadKind::Bridge, RoadClass::Highway}, {2, 3, RoadKind::Bridge, RoadClass::Highway},
                  {3, 4, RoadKind::Bridge, RoadClass::Highway}, {4, 5, RoadKind::Bridge, RoadClass::Highway}, {5, 6, RoadKind::Road, RoadClass::Highway},
                  {7, 8, RoadKind::Road, RoadClass::Street}};
  rejects([&] { validate_road_network(def); }, "the street under the viaduct is not joined: the network is in two pieces");
  def.segments.push_back({7, 0, RoadKind::Road, RoadClass::Street});  // joined to the highway's foot
  validate_road_network(def);
  rejects([&] { auto d = def; d.segments[5].kind = RoadKind::Bridge; validate_road_network(d); }, "a chain of bridges reaching roads at one node only is rejected");
  { auto d = def; d.segments[5].kind = RoadKind::Bridge; d.segments.push_back({6, 8, RoadKind::Bridge, RoadClass::Street}); validate_road_network(d); }  // ... but landing on the street beyond is fine
  rejects([&] { auto d = def; d.segments[1].kind = RoadKind::Road; validate_road_network(d); }, "a road reaching a lifted node is rejected");
  rejects([&] { auto d = def; d.nodes[3].lift_meters = 80; validate_road_network(d); }, "an eighty-metre lift is rejected");
  RoadNetwork network(def, style, plain_ground);
  require(near(network.width(0), 21.0) && near(network.width(6), 7.0), "segment widths follow the class");
  require(near(network.surface_at_node(3), 50 + style.surface_lift_meters + 7.0) && near(network.clearance(2), 7.0 + style.surface_lift_meters, 1e-6), "the lifted node's surface is its lift over the ground");
  const auto mid = network.sample(50, 200);
  require(mid.on && mid.kind == RoadKind::Bridge && mid.cls == RoadClass::Highway && near(mid.surface_meters, 50 + style.surface_lift_meters + 7.0), "the deck passes seven metres over the street");
  require(network.sample(90, 200).on && network.sample(90, 200).cls == RoadClass::Street && near(network.sample(90, 200).surface_meters, 50 + style.surface_lift_meters),
          "the street runs under it at ground level");
  require(*network.platform_height(50, 200) > 56 && near(*network.platform_height(90, 200), 50.12), "the viaduct is a platform over the street, the street one at grade");
  // No pier stands on the street below; the railings of the highway stand at its wider edges.
  bool pier_on_street = false;
  for (const auto& o : network.obstacles()) {
    if (o.radius > 0.3) { if (std::abs(o.z - 200) <= 3.5 + 0.6) pier_on_street = true; }  // a pier
    else require(near(std::abs(o.x - 50), 10.5 - 0.15, 1e-6), "highway railings stand at the edge of a 21 m deck");
  }
  require(!pier_on_street, "no pier in the middle of the street");
  const auto stats = network.stats();
  require(stats.highways == 6 && stats.streets == 2 && stats.bridges == 4 && near(stats.highway_meters, 400), "stats count the classes");
  // Meshes: a median and yellow lines for the highway, one merged asphalt part and one pavement part for an apron.
  std::map<std::string, std::size_t> count;
  for (const auto& p : network.build_meshes()) ++count[p.what];
  require(count["median"] == 1 && count["marking"] == 2 && count["surface"] == 2 && count["deck"] == 4, "a highway draws its median, white and yellow markings, the surfaces merged per class colour (highway, street) and its decks");
  for (const auto& p : network.build_meshes()) {
    const std::string what = p.what;
    if (what == "surface") require(p.material == SurfaceMaterial::Asphalt, "carriageways are asphalt");
    if (what == "marking") require(p.material == SurfaceMaterial::Paint, "markings are paint");
    if (what == "railing") require(p.material == SurfaceMaterial::Metal, "railings are metal");
    if (what == "deck" || what == "pier" || what == "median") require(p.material == SurfaceMaterial::Concrete, "decks, piers and medians are concrete");
  }
  {
    RoadNetworkDef field;
    field.nodes = {{{0, 0}}, {{300, 0}}, {{300, 200}}};
    field.segments = {{0, 1, RoadKind::Road, RoadClass::Apron}, {1, 2, RoadKind::Road, RoadClass::Runway}};
    RoadNetwork airfield(field, style, plain_ground);
    std::map<int, std::size_t> surfaces;
    for (const auto& p : airfield.build_meshes()) if (std::string(p.what) == "surface") ++surfaces[static_cast<int>(p.material)];
    require(surfaces[static_cast<int>(SurfaceMaterial::Pavement)] == 1 && surfaces[static_cast<int>(SurfaceMaterial::Asphalt)] == 1, "an apron is concrete pavement, a runway asphalt");
  }
  // Routes with classes and lifts plan a viaduct; curves of short legs are mitred joints counted as such.
  std::vector<RoadRoute> routes;
  routes.push_back({{{50, 0}, {50, 100}, {50, 160}, {50, 200}, {50, 240}, {50, 300}, {50, 400}}, RoadClass::Highway, {0, 0, 4, 7, 4, 0, 0}});
  std::vector<Vec2> arc;  // a quarter circle of 100 m radius from (0, -100) round to (100, -200), on the flat plain
  for (int i = 0; i <= 12; ++i) { const double a = kPi + 0.5 * kPi * i / 12.0; arc.push_back({100 + 100 * std::cos(a), -100 + 100 * std::sin(a)}); }
  routes.push_back({arc, RoadClass::Avenue, {}});
  routes.push_back({{{0, -100}, {50, 0}}, RoadClass::Street, {}});  // a street from the arc's start to the highway where it is at grade
  const auto planned = plan_road_network(routes, plain_ground, std::nullopt, style, RoadPlanStyle{});
  RoadNetwork curved(planned, style, plain_ground);
  const auto ps = curved.stats();
  require(ps.bridges == 4 && ps.highways == 6 && ps.avenues == 12 && ps.curve_joints >= 10, "the viaduct is planned from the lifts and the arc is a curve of mitred joints");
  const auto parts = curved.build_meshes();
  for (const auto& p : parts) for (const auto& v : p.vertices) require(std::isfinite(v.position[0]) && std::isfinite(v.position[1]), "finite curve geometry");
  rejects([&] { std::vector<RoadRoute> bad{{{{0, 0}, {100, 0}}, RoadClass::Street, {0}}}; plan_road_network(bad, plain_ground, std::nullopt, style, RoadPlanStyle{}); }, "lifts must match the waypoints");
  // JSON with classes and lifts.
  const auto parsed = parse_road_network(R"({"version": 1, "nodes": [[0, 0], [100, 0], {"x": 200, "z": 0, "lift": 6}, [300, 0], [400, 0]],
    "segments": [{"from": 0, "to": 1, "class": "highway"}, {"from": 1, "to": 2, "kind": "bridge", "class": "highway"}, {"from": 2, "to": 3, "kind": "bridge", "class": "highway"}, {"from": 3, "to": 4, "class": "highway"}]})",
    plain_ground, std::nullopt, style, RoadPlanStyle{});
  require(parsed.segments[0].cls == RoadClass::Highway && near(parsed.nodes[2].lift_meters, 6), "classes and lifts are read");
  const auto routed = parse_road_network(R"({"version": 1, "routes": [{"points": [[0, 0], [100, 0], [200, 0], [300, 0], [400, 0]], "class": "avenue", "lifts": [0, 0, 5, 0, 0]}]})",
    plain_ground, std::nullopt, style, RoadPlanStyle{});
  require(routed.segments.size() == 4 && routed.segments[1].kind == RoadKind::Bridge && routed.segments[0].cls == RoadClass::Avenue, "route objects plan viaducts of a class");
  rejects([&] { parse_road_network(R"({"version": 1, "nodes": [[0, 0], [100, 0]], "segments": [{"from": 0, "to": 1, "class": "canal"}]})", plain_ground, std::nullopt, style, RoadPlanStyle{}); },
          "an unknown class is rejected");
}

void test_json() {
  RoadStyle style;
  RoadPlanStyle plan;
  const std::string explicit_text = R"({"version": 1, "nodes": [{"x": 0, "z": 0}, {"x": 100, "z": 0}, [200, 0], {"x": 300, "z": 0}],
    "segments": [{"from": 0, "to": 1}, {"from": 1, "to": 2, "kind": "bridge"}, {"from": 2, "to": 3, "kind": "road"}]})";
  const auto def = parse_road_network(explicit_text, plain_ground, kLake, style, plan);
  require(def.nodes.size() == 4 && def.segments.size() == 3 && def.segments[1].kind == RoadKind::Bridge && def.segments[0].kind == RoadKind::Road, "explicit graph parsed");
  const auto shifted = parse_road_network(explicit_text, plain_ground, kLake, style, plan, {10, 5});
  require(near(shifted.nodes[2].position.x, 210) && near(shifted.nodes[2].position.y, 5), "the origin is added to every position");
  const auto planned = parse_road_network(R"({"version": 1, "routes": [[[0, 0], [900, 0]]]})", plain_ground, kLake, style, plan);
  require(planned.segments.size() == 7, "routes are planned");
  rejects([&] { parse_road_network(R"({"version": 2, "routes": [[[0, 0], [600, 0]]]})", plain_ground, kLake, style, plan); }, "another version is rejected");
  rejects([&] { parse_road_network(R"({"routes": [[[0, 0], [600, 0]]]})", plain_ground, kLake, style, plan); }, "a missing version is rejected");
  rejects([&] { parse_road_network(R"({"version": 1, "nodes": [[0, 0], [100, 0]], "segments": [{"from": 0, "to": 1, "kind": "bridge"}]})", plain_ground, kLake, style, plan); },
          "a stand-alone bridge in JSON is rejected");
  rejects([&] { parse_road_network(R"({"version": 1, "nodes": [[0, 0], [100, 0]], "segments": [{"from": 0, "to": 1, "kind": "tunnel"}]})", plain_ground, kLake, style, plan); },
          "an unknown kind is rejected");
  rejects([&] { parse_road_network(R"({"version": 1, "nodes": [[0, 0], [100, 0]], "segments": [{"from": 0, "to": 1.5}]})", plain_ground, kLake, style, plan); }, "a fractional index is rejected");
  rejects([&] { parse_road_network(R"({"version": 1, "nodes": [[0, 0], [9000, 0]], "segments": [{"from": 0, "to": 1}]})", plain_ground, kLake, style, plan); }, "a node off the terrain is rejected");
  rejects<std::exception>([&] { parse_road_network("{", plain_ground, kLake, style, plan); }, "broken JSON is rejected");
  rejects([&] { parse_road_network("[]", plain_ground, kLake, style, plan); }, "a non-object is rejected");
}

void test_sample_and_surfaces() {
  RoadStyle style;
  RoadNetwork network(valley_def(), style, plain_ground);
  require(network.size() == 3 && !network.empty(), "three segments");
  const auto on_road = network.sample(50, 1);
  require(on_road.on && on_road.kind == RoadKind::Road && on_road.segment == 0 && near(on_road.along_meters, 50) && near(on_road.offset_meters, 1),
          "a point on the road is on it, fifty metres along and a metre to the right");
  require(near(on_road.surface_meters, 50 + style.surface_lift_meters) && near(on_road.speed_factor, style.road_speed_factor) && !on_road.blocked && !on_road.destroyed,
          "the surface lies the lift above the ground and hurries the walker");
  require(!network.sample(50, 4).on && !network.sample(-1, 0).on && !network.sample(301, 0).on, "beside the road and past its ends is off it");
  require(near(network.sample(50, -3.4).offset_meters, -3.4), "the left edge is still on the road");
  const auto on_bridge = network.sample(150, 0);
  require(on_bridge.on && on_bridge.kind == RoadKind::Bridge && on_bridge.segment == 1 && near(on_bridge.surface_meters, 50 + style.surface_lift_meters) &&
          near(on_bridge.speed_factor, style.bridge_speed_factor) && !on_bridge.deck_missing, "mid-span the deck is at the rims' height");
  require(near(network.clearance(1), 30 + style.surface_lift_meters), "the deck stands thirty metres over the valley floor");
  require(near(*network.platform_height(150, 0), 50 + style.surface_lift_meters) && !network.platform_height(150, 10), "the deck is a platform, the air beside it is not");
  require(near(network.speed_factor(500, 500), 1.0), "open ground is plain walking");
  require(near(network.length(1), 100) && near(network.direction(1).x, 1) && near(network.surface_at_node(2), 50 + style.surface_lift_meters), "segment geometry");
  rejects([&] { (void)network.length(7); }, "a segment index out of range is rejected");
  rejects([&] { (void)network.sample(std::nan(""), 0); }, "a non-finite sample position is rejected");
  // Railings and piers.
  const auto obstacles = network.obstacles();
  std::size_t railings = 0, piers = 0;
  for (const auto& o : obstacles) {
    if (o.top - o.bottom > 5) { ++piers; require(o.x > 110 && o.x < 190 && near(o.top, 50 + style.surface_lift_meters - style.deck_thickness_meters, 1e-6), "piers stand in the valley up to the slab"); }
    else { ++railings; require(near(o.bottom, 50 + style.surface_lift_meters, 1e-6) && near(o.top - o.bottom, style.railing_height_meters, 1e-6) && near(std::abs(o.z), 3.5 - 0.15, 1e-6), "railings stand on the deck at its edges"); }
  }
  require(piers == 4 && railings > 600, "four piers and a chain of railing posts along both sides");
  WalkerBody body;
  require(obstacle_blocks(obstacles.back(), 50.12, body) && !obstacle_blocks(obstacles.back(), 20.0, body), "a railing blocks a walker on the deck, not one in the valley");
}

void test_walker() {
  RoadStyle style;
  style.stub_meters = 12.0;  // the stubs of a destroyed bridge reach out over the valley's rim
  RoadNetwork network(valley_def(), style, plain_ground);
  ObstacleField field;
  auto env = environment_for(network, field);
  auto plain = environment_for(network, field, false);
  FirstPersonCamera walker;
  walker.feet = {10, 50.12, 0}; walker.yaw = -3.14159265358979323846 / 2;  // looking east (+x)
  require(near(horizontal_forward(walker.yaw).x, 1, 1e-9), "yaw -90 degrees looks east");
  // On the road the feet rest on the surface and the walk is faster by the road factor.
  walk(walker, WalkInput{}, 0.1, env);
  require(walker.on_platform && near(walker.feet.y, 50.12), "the feet rest on the road surface");
  const auto start = walker.feet;
  const double on_road = walk(walker, WalkInput{1, 0, false}, 2.0, env);
  require(near(walker.surface_speed_factor, style.road_speed_factor) && near(on_road, 2.5 * 2.0 * style.road_speed_factor, 1e-6), "two seconds on the road cover the road factor more");
  FirstPersonCamera other = walker; other.feet = start;
  const double off_road = walk(other, WalkInput{1, 0, false}, 2.0, plain);
  require(near(off_road, 5.0, 1e-6) && near(other.surface_speed_factor, 1.0), "without the surface hook the same walk is plain");
  // Across the bridge: the deck carries the walker over the valley, never dipping.
  walker.feet = {95, 50.12, 0};
  double lowest = 1e9;
  for (int i = 0; i < 60; ++i) { walk(walker, WalkInput{1, 0, false}, 1.0 / 10.0, env); lowest = std::min(lowest, walker.feet.y); }
  require(walker.feet.x > 110 && walker.grounded && near(lowest, 50.12, 1e-6) && walker.on_platform, "the walker crossed onto the deck and stayed at its height");
  require(near(walker.surface_speed_factor, style.bridge_speed_factor), "the deck hurries the walker by the bridge factor");
  // The railing holds: strafing right for three seconds keeps the walker on the deck.
  walker.feet = {150, 50.12, 0};
  for (int i = 0; i < 30; ++i) walk(walker, WalkInput{0, 1, false}, 0.1, env);
  require(walker.grounded && std::abs(walker.feet.z) < 3.5 && near(walker.feet.y, 50.12, 1e-6), "the railing keeps the walker on the deck");
  // In the valley below, the deck is out of reach: the walker walks under it on the ground.
  FirstPersonCamera below;
  below.feet = {150, 20, 0}; below.yaw = walker.yaw;
  walk(below, WalkInput{1, 0, false}, 1.0, env);
  require(!below.on_platform && near(below.feet.y, 20) && below.feet.x > 150.5, "under the bridge the walker is on the valley floor");
  // A pier is solid.
  const auto piers = network.obstacles();
  Vec2 pier{0, 0};
  for (const auto& o : piers) if (o.top - o.bottom > 5) { pier = {o.x, o.z}; break; }
  below.feet = {pier.x - 3, ground_profile(pier.x - 3), pier.y};
  for (int i = 0; i < 40; ++i) walk(below, WalkInput{1, 0, false}, 0.1, env);
  require(below.feet.x < pier.x - 0.5 || std::abs(below.feet.z - pier.y) > 0.5, "the pier is walked around, not through");
  // The bridge destroyed under the walker: nothing holds the feet mid-span; they fall to the valley floor.
  walker.feet = {150, 50.12, 0}; walker.grounded = true;
  network.destroy(1, "test");
  require(network.states()[1].destroyed && !network.platform_height(150, 0) && network.platform_height(102, 0), "mid-span the deck is gone, the stubs remain");
  require(network.events().size() == 2 && network.events()[1].kind == RoadEvent::Kind::Collapsed, "destroying a bridge raises Destroyed and Collapsed");
  field = ObstacleField(network.obstacles());
  for (int i = 0; i < 150 && !walker.landed; ++i) walk(walker, WalkInput{}, 1.0 / 30.0, env);
  require(walker.landed && near(walker.feet.y, 20) && walker.last_impact_mps > 20, "the walker fell thirty metres to the valley floor");
  // From the stub the broken end is a ledge.
  walker = FirstPersonCamera{}; walker.feet = {101, 50.12, 0}; walker.yaw = -3.14159265358979323846 / 2;
  for (int i = 0; i < 80 && walker.grounded; ++i) walk(walker, WalkInput{1, 0, false}, 0.1, env);
  require(!walker.grounded && walker.feet.x > 111.9 && walker.feet.x < 113, "walking off the stub's broken end is a fall");
  require(network.sample(150, 0).deck_missing && !network.sample(103, 0).deck_missing && near(network.sample(103, 0).speed_factor, 1.0), "the stubs stand but give no bonus");
  network.reset();
  require(!network.states()[1].destroyed && network.platform_height(150, 0), "reset rebuilds the deck");
  rejects([&] { auto bad = env; bad.surface_speed = [](double, double) { return -1.0; }; FirstPersonCamera w; w.feet = {10, 50.12, 0}; walk(w, WalkInput{}, 0.1, bad); },
          "a negative surface factor is rejected");
}

void test_blockage_and_fire() {
  RoadStyle style;
  RoadNetwork network(valley_def(), style, plain_ground);
  // A trunk across the road at x = 50: blocked there, not ten metres on.
  require(network.log_landed(7, {50, -12}, {0, 1}, 20, true) == 1, "a trunk lying across the road blocks it");
  require(network.blockages().size() == 1 && near(network.blockages()[0].along_meters, 50, 1e-6) && network.blockages()[0].burning, "the blockage is fifty metres along");
  require(network.events().size() == 1 && network.events()[0].kind == RoadEvent::Kind::Blocked && network.events()[0].tree == 7, "Blocked is raised for the tree");
  require(network.sample(51, 0).blocked && near(network.sample(51, 0).speed_factor, 1.0) && !network.sample(60, 0).blocked && near(network.sample(60, 0).speed_factor, style.road_speed_factor),
          "the bonus is gone within the blockage reach only");
  require(network.log_landed(7, {50, -12}, {0, 1}, 20, true) == 0, "the same trunk is not counted twice");
  require(network.log_landed(8, {50, 4}, {0, 1}, 20, true) == 0, "a trunk lying beside the road (outside the shoulder) does not block it");
  require(network.log_landed(9, {50, -12}, {1, 0}, 20, true) == 0, "a trunk lying parallel to the road twelve metres away does not block it");
  // A trunk under the bridge mid-span lies on the valley floor, not on the deck.
  require(network.log_landed(10, {150, -12}, {0, 1}, 20, true) == 0, "a trunk under the deck does not block the bridge");
  // Burning, it damages the road; Damaged is raised once; at full damage the road is rubble.
  const std::uint64_t version = network.version();
  network.advance(10.0, [](std::uint32_t) { return 1.0; }, {});
  require(near(network.states()[0].damage, 10 * style.road_fire_damage_per_second, 1e-9), "ten seconds of a burning trunk damage the road by the rate");
  require(network.events().size() == 1 && network.events()[0].kind == RoadEvent::Kind::Damaged, "Damaged raised when the damage begins");
  require(network.sample(60, 0).speed_factor < style.road_speed_factor && network.sample(60, 0).speed_factor > 1.0, "a damaged road gives less of a bonus");
  network.advance(10.0, [](std::uint32_t) { return 1.0; }, {});
  require(network.events().empty(), "Damaged is not repeated");
  network.advance(5.0, [](std::uint32_t) { return 0.0; }, {});
  require(near(network.states()[0].damage, 20 * style.road_fire_damage_per_second, 1e-9) && !network.blockages()[0].burning, "a trunk that has burned out damages nothing more");
  network.advance(1000.0, [](std::uint32_t) { return 1.0; }, {});
  require(network.states()[0].destroyed && network.states()[0].destroyed_by == "fire" && near(network.sample(60, 0).speed_factor, style.rubble_speed_factor),
          "burned through, the road is rubble and slow");
  require(network.version() > version, "damage that changes the drawing bumps the version");
  bool destroyed_event = false;
  for (const auto& e : network.events()) if (e.kind == RoadEvent::Kind::Destroyed) destroyed_event = true;
  require(destroyed_event, "Destroyed is raised for the road");
  require(network.stats().destroyed == 1 && network.stats().blocked == 1 && network.stats().blockages == 1, "stats count the destroyed and blocked segments");
  // A burning trunk at the abutment lies on the deck: the bridge is alight and collapses.
  require(network.log_landed(11, {103, -12}, {0, 1}, 20, true) == 1, "a trunk at the abutment lies across the deck");
  network.advance(1.0, [](std::uint32_t) { return 1.0; }, {});
  bool alight = false;
  for (const auto& e : network.events()) if (e.kind == RoadEvent::Kind::Alight && e.segment == 1) alight = true;
  require(alight && near(network.states()[1].damage, style.bridge_fire_damage_per_second, 1e-9), "the deck is alight");
  std::vector<FireInstance> flames, smoke;
  network.emit(flames, smoke, FireStyle{});
  require(flames.size() >= 5 && !smoke.empty(), "a burning deck carries flames and a plume");
  network.advance(1.0 / style.bridge_fire_damage_per_second, [](std::uint32_t) { return 1.0; }, {});
  bool collapsed = false;
  for (const auto& e : network.events()) if (e.kind == RoadEvent::Kind::Collapsed && e.segment == 1) collapsed = true;
  require(collapsed && network.states()[1].destroyed && !network.platform_height(150, 0), "burned through, the bridge collapses");
  flames.clear(); smoke.clear();
  network.emit(flames, smoke, FireStyle{});
  require(flames.empty() && smoke.size() == 3, "a burned-out bridge smoulders at its stubs, the burned-through road where the trunk lies");
  rejects([&] { network.advance(-1, {}, {}); }, "negative time rejected");
  rejects([&] { network.log_landed(1, {0, 0}, {1, 0}, -1, false); }, "negative trunk length rejected");
  rejects([&] { network.destroy(9, "x"); }, "destroying a segment out of range is rejected");
  // Unburning trunks only block.
  network.reset();
  require(network.blockages().empty() && network.stats().destroyed == 0, "reset clears the blockages and the damage");
  network.log_landed(12, {250, -12}, {0, 1}, 20, false);
  network.advance(100.0, [](std::uint32_t) { return 0.0; }, {});
  require(network.states()[2].blockages == 1 && network.states()[2].damage == 0, "a sound trunk blocks the road without damaging it");
}

void test_washout() {
  RoadStyle style;
  RoadNetwork network(valley_def(), style, plain_ground);
  const WaterFn torrent = [](double x, double, double ground_y) {
    FloodSample w;
    if (x > 110 && x < 190) { w.depth_meters = 1.5; w.surface_meters = ground_y + 1.5; w.current_mps = 4.0; w.current = {1, 0}; w.channel = true; }
    else w.surface_meters = ground_y;
    return w;
  };
  const WaterFn still = [](double, double, double ground_y) { FloodSample w; w.depth_meters = 3.0; w.surface_meters = ground_y + 3.0; return w; };
  network.advance(30.0, {}, torrent);
  require(near(network.states()[1].washout_seconds, 30) && !network.states()[1].destroyed, "running water at the piers counts toward the washout");
  network.advance(20.0, {}, still);
  require(near(network.states()[1].washout_seconds, 10) && !network.states()[1].destroyed, "still water lets the bridge recover");
  network.advance(40.0, {}, torrent);
  require(network.states()[1].destroyed && network.states()[1].destroyed_by == "flood", "the flood washes the bridge out");
  bool washed = false;
  for (const auto& e : network.events()) if (e.kind == RoadEvent::Kind::WashedOut && e.segment == 1) washed = true;
  require(washed && network.states()[0].washout_seconds == 0, "WashedOut is raised for the bridge alone");
  std::vector<FireInstance> flames, smoke;
  network.emit(flames, smoke, FireStyle{});
  require(flames.empty() && smoke.empty(), "a washed-out bridge does not smoulder");
  // A torrent over the road itself (not a lake standing on it) washes the road away, in twice the time.
  RoadNetwork again(valley_def(), style, plain_ground);
  const WaterFn over_road = [](double x, double, double ground_y) {
    FloodSample w;
    if (x > 20 && x < 60) { w.depth_meters = 1.2; w.surface_meters = ground_y + 1.2; w.current_mps = 3.5; w.current = {0, 1}; w.channel = true; }
    else w.surface_meters = ground_y;
    return w;
  };
  const WaterFn lake_on_road = [](double x, double, double ground_y) {
    FloodSample w;
    if (x > 20 && x < 60) { w.depth_meters = 2.0; w.surface_meters = ground_y + 2.0; w.current_mps = 3.5; w.current = {0, 1}; }
    else w.surface_meters = ground_y;
    return w;
  };
  again.advance(style.washout_seconds + 1.0, {}, over_road);
  require(!again.states()[0].destroyed && near(again.states()[0].washout_seconds, style.washout_seconds + 1.0), "a road stands a bridge's washout time under a torrent");
  again.advance(style.washout_seconds, {}, over_road);
  require(again.states()[0].destroyed && again.states()[0].destroyed_by == "flood" && !again.states()[1].destroyed && !again.states()[2].destroyed, "twice that and the road is washed away, the bridge and the far road untouched");
  bool road_washed = false;
  for (const auto& e : again.events()) if (e.kind == RoadEvent::Kind::WashedOut && e.segment == 0) road_washed = true;
  require(road_washed && near(again.sample(40, 0).speed_factor, style.rubble_speed_factor), "WashedOut is raised for the road, which is rubble");
  RoadNetwork lake(valley_def(), style, plain_ground);
  lake.advance(10.0 * style.washout_seconds, {}, lake_on_road);
  require(!lake.states()[0].destroyed && lake.states()[0].washout_seconds == 0, "a lake's water standing on a road does not wash it away (only a channel's torrent does)");
}

void test_impact() {
  RoadStyle style;
  RoadNetwork network(valley_def(), style, plain_ground);
  // A sound trunk of three tonnes across the road: blocked and cracked by the mass, not alight.
  require(network.log_landed(20, {50, -12}, {0, 1}, 20, false, 3000.0) == 1, "a heavy trunk lands across the road");
  require(near(network.states()[0].damage, 3.0 / style.impact_tonnes, 1e-9) && !network.states()[0].destroyed, "the road is cracked by mass / impact_tonnes");
  bool damaged = false, blocked = false;
  for (const auto& e : network.events()) { if (e.kind == RoadEvent::Kind::Damaged && e.tree == 20) damaged = true; if (e.kind == RoadEvent::Kind::Blocked) blocked = true; }
  require(damaged && blocked, "Blocked and Damaged are raised for the impact");
  network.advance(10.0, [](std::uint32_t) { return 0.0; }, {});
  require(near(network.states()[0].damage, 3.0 / style.impact_tonnes, 1e-9), "a trunk that does not burn does no more");
  // A trunk of the ruining mass smashes a deck outright (two thirds of the road's), and it collapses.
  require(network.log_landed(21, {103, -12}, {0, 1}, 20, false, style.impact_tonnes * 2.0 / 3.0 * 1000.0) == 1, "a huge trunk lands on the deck");
  bool collapsed = false;
  for (const auto& e : network.events()) if (e.kind == RoadEvent::Kind::Collapsed && e.segment == 1) collapsed = true;
  require(collapsed && network.states()[1].destroyed && network.states()[1].destroyed_by == "impact", "the deck is smashed and collapses");
  std::vector<FireInstance> flames, smoke;
  network.emit(flames, smoke, FireStyle{});
  require(flames.empty() && smoke.empty(), "a smashed bridge does not smoulder");
  rejects([&] { network.log_landed(22, {50, -12}, {0, 1}, 20, false, -1.0); }, "a negative mass is rejected");
  rejects([] { RoadStyle s; s.impact_tonnes = 0; validate_road_style(s); }, "a zero impact mass is rejected");
  // Without a mass (the old call) nothing but the blockage happens.
  RoadNetwork plain(valley_def(), style, plain_ground);
  plain.log_landed(23, {50, -12}, {0, 1}, 20, false);
  require(plain.states()[0].damage == 0, "a trunk without a mass only blocks");
}

void test_meshes_and_renderer() {
  RoadStyle style;
  RoadNetwork network(valley_def(), style, plain_ground);
  const auto parts = network.build_meshes();
  std::size_t surfaces = 0, decks = 0, markings = 0, railings = 0, piers = 0, rubble = 0, triangles = 0;
  for (const auto& p : parts) {
    require(p.indices.size() % 3 == 0 && !p.vertices.empty(), "whole triangles");
    for (const auto i : p.indices) require(i < p.vertices.size(), "indices in range");
    for (const auto& v : p.vertices) {
      require(std::isfinite(v.position[0]) && std::isfinite(v.position[1]) && std::isfinite(v.position[2]), "finite positions");
      require(near(std::hypot(v.normal[0], v.normal[1], v.normal[2]), 1.0, 1e-4), "unit normals");
    }
    triangles += p.indices.size() / 3;
    const std::string what = p.what;
    if (what == "surface") { ++surfaces; for (const auto& v : p.vertices) require(v.position[1] >= 49.9 && v.position[1] <= 50.2, "the surface lies on the ground or the deck"); }
    else if (what == "deck") ++decks;
    else if (what == "marking") ++markings;
    else if (what == "railing") ++railings;
    else if (what == "pier") ++piers;
    else if (what == "rubble") ++rubble;
  }
  require(surfaces == 1 && decks == 1 && markings == 1 && railings == 1 && piers == 1 && rubble == 0, "the sound surfaces merged into one part, one deck, the markings, railings and piers");
  require(triangles > 500, "enough triangles for the ribbons, dashes, posts and piers");
  // Destroyed, the span is gone: stubs, rubble, fewer railings; reset restores the first result exactly.
  network.destroy(1, "fire");
  const auto broken = network.build_meshes();
  std::size_t broken_railings = 0, broken_rubble = 0, broken_deck_vertices = 0;
  for (const auto& p : broken) {
    const std::string what = p.what;
    if (what == "railing") broken_railings = p.indices.size();
    if (what == "rubble") ++broken_rubble;
    if (what == "deck") broken_deck_vertices = p.vertices.size();
  }
  std::size_t whole_railings = 0;
  for (const auto& p : parts) if (std::string(p.what) == "railing") whole_railings = p.indices.size();
  require(broken_rubble == 1 && broken_railings < whole_railings && broken_deck_vertices == 48, "a destroyed bridge is two stubs, less railing and rubble");
  std::size_t broken_surfaces = 0;
  for (const auto& p : broken) if (std::string(p.what) == "surface") ++broken_surfaces;
  require(broken_surfaces == 2, "a destroyed span's surface is its own part beside the merged sound ones");
  network.reset();
  const auto restored = network.build_meshes();
  require(restored.size() == parts.size(), "reset restores the parts");
  for (std::size_t i = 0; i < parts.size(); ++i)
    require(restored[i].vertices.size() == parts[i].vertices.size() && restored[i].indices == parts[i].indices, "reset restores the geometry exactly");

  ValidatingRenderDevice device;
  RenderResourceOwner shared(device);
  const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const auto camera = shared.create_buffer({BufferUsage::Uniform, sizeof(identity)}, std::as_bytes(std::span(identity)));
  const auto baseline = device.live_resource_bytes();
  {
    RoadRenderer renderer(device);
    require(renderer.parts() == 0 && renderer.triangles() == 0, "empty to begin with");
    renderer.set_parts(parts);
    require(renderer.parts() == parts.size() && renderer.triangles() == triangles, "the parts are uploaded");
    rejects<std::logic_error>([&] { renderer.draw(camera); }, "drawing needs a pass");
    const auto with_parts = device.live_resource_bytes();
    {
      RenderPassScope pass(device, {{64, 64}});
      require(renderer.draw(camera) == parts.size(), "one draw per part");
    }
    renderer.set_parts(broken);
    require(renderer.parts() == broken.size(), "replacing the parts");
    renderer.set_parts({});
    require(renderer.parts() == 0 && device.live_resource_bytes().total() < with_parts.total(), "the old parts are released");
    RoadMeshPart bad; bad.vertices.resize(3); bad.indices = {0, 1, 7};
    rejects([&] { renderer.set_parts(std::span<const RoadMeshPart>(&bad, 1)); }, "an index out of range is rejected");
  }
  require(device.live_resource_bytes() == baseline, "the renderer releases everything");
}

void test_vegetation_clearing() {
  RoadStyle style;
  RoadNetwork network(valley_def(), style, plain_ground);
  VegetationPlacement placement;
  const auto plant = [&](double x, double z, float kind) { VegetationInstance t{}; t.position[0] = static_cast<float>(x); t.position[2] = static_cast<float>(z); t.kind = kind; t.scale = 10; return t; };
  // Chunk 0: a tree on the road, one beside it within the margin, one clear; chunk 1: grass on and off the road.
  placement.trees = {plant(50, 0, 0), plant(50, 5.0, 1), plant(50, 8.0, 1), plant(250, 30, 0)};
  placement.grass = {plant(20, 0, 0), plant(20, 3.7, 0), plant(20, 4.0, 0)};
  placement.chunks = {{{0, 0, -10}, {100, 10, 10}, 0, 3, 0, 3}, {{200, 0, 0}, {300, 10, 40}, 3, 1, 3, 0}};
  placement.trees_per_kind = {2, 2};
  const auto removed = clear_vegetation_along(placement, network);
  require(removed == 4, "the tree on the road, the one within two metres of its edge, and the grass within 0.3 m of the edge are removed");
  require(placement.trees.size() == 2 && placement.grass.size() == 1 && placement.chunks.size() == 2, "two trees and one tuft remain in two chunks");
  require(placement.chunks[0].tree_first == 0 && placement.chunks[0].tree_count == 1 && placement.chunks[0].grass_first == 0 && placement.chunks[0].grass_count == 1 &&
          placement.chunks[1].tree_first == 1 && placement.chunks[1].tree_count == 1 && placement.chunks[1].grass_count == 0, "the chunk ranges are rebuilt");
  require(placement.trees_per_kind[0] == 1 && placement.trees_per_kind[1] == 1 && near(placement.grass[0].position[2], 4.0), "counts and survivors");
  rejects([&] { clear_vegetation_along(placement, network, -1); }, "a negative margin is rejected");
}

void test_vitals_collapse() {
  Vitals v;
  Exposure e;
  e.collapse = 0.35;
  const auto report = advance_vitals(v, e, 1.0 / 60.0, VitalsStyle{});
  require(near(report.health_lost, 0.35) && v.last_cause == DamageCause::Collapse && std::string(damage_cause_name(DamageCause::Collapse)) == "collapse",
          "a deck giving way costs the collapse damage at once");
  rejects([] { Vitals w; Exposure x; x.collapse = 2; advance_vitals(w, x, 1, VitalsStyle{}); }, "collapse above one rejected");
}

}  // namespace

int main() {
  try {
    test_style_and_validation();
    test_planner();
    test_classes_and_viaducts();
    test_json();
    test_sample_and_surfaces();
    test_walker();
    test_blockage_and_fire();
    test_washout();
    test_impact();
    test_meshes_and_renderer();
    test_vegetation_clearing();
    test_vitals_collapse();
    std::cout << "TerraFlux roads: 12 tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "TerraFlux roads test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
