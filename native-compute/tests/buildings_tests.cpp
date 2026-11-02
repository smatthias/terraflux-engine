#include <terraflux/graphics/buildings.hpp>
#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/collision.hpp>
#include <terraflux/graphics/render_pass.hpp>
#include <terraflux/graphics/roads.hpp>

#include <algorithm>
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
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template <class E = std::invalid_argument, class F> void rejects(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const E&) { caught = true; }
  require(caught, message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) < tolerance; }

// A plain at 50 m with a valley between x = 110 and x = 190 (as in the roads suite).
double ground_profile(double x) {
  if (x >= 110 && x <= 190) {
    if (x < 130) return 50.0 - (x - 110) * 1.5;
    if (x > 170) return 50.0 - (190 - x) * 1.5;
    return 20.0;
  }
  return 50.0;
}
const StandingHeightFn plain_ground = [](double x, double z) -> std::optional<double> {
  if (x < -100 || x > 1100 || z < -500 || z > 500) return std::nullopt;
  return ground_profile(x);
};
// The same plain, but a lake basin south of z = 8 (bed at 40 m under water at 45 m).
const StandingHeightFn lakeside_ground = [](double x, double z) -> std::optional<double> {
  if (x < -100 || x > 1100 || z < -500 || z > 500) return std::nullopt;
  return z > 8 ? 40.0 : ground_profile(x);
};
// Steep 1:1 ground north of z = -5, flat south of it.
const StandingHeightFn hillside_ground = [](double x, double z) -> std::optional<double> {
  if (x < -100 || x > 1100 || z < -500 || z > 500) return std::nullopt;
  return z < -5 ? 50.0 + (-5 - z) : 50.0;
};
// A gentle 8 % slope rising to the north across the road.
const StandingHeightFn sloping_ground = [](double x, double z) -> std::optional<double> {
  if (x < -100 || x > 1100 || z < -500 || z > 500) return std::nullopt;
  return 50.0 - 0.08 * z;
};

// A long road along x from 0 to 1000 with a bridge over the valley.
RoadNetworkDef road_def() {
  RoadNetworkDef def;
  def.nodes = {{{0, 0}}, {{100, 0}}, {{200, 0}}, {{1000, 0}}};
  def.segments = {{0, 1, RoadKind::Road}, {1, 2, RoadKind::Bridge}, {2, 3, RoadKind::Road}};
  return def;
}
std::vector<BuildingRequest> all_kinds() {
  return {{BuildingKind::Skyscraper, std::nullopt}, {BuildingKind::Hospital, std::nullopt}, {BuildingKind::House, std::nullopt}, {BuildingKind::Bungalow, std::nullopt}};
}

void test_specs() {
  for (const auto kind : {BuildingKind::Bungalow, BuildingKind::House, BuildingKind::Hospital, BuildingKind::Skyscraper}) {
    const auto spec = building_spec(kind);
    validate_building_spec(spec);
    require(spec.kind == kind && *building_kind_from_name(building_kind_name(kind)) == kind, "kinds round-trip by name");
  }
  require(building_spec(BuildingKind::Bungalow).storeys == 1 && building_spec(BuildingKind::House).storeys == 2, "a bungalow has one storey, a house two");
  require(building_spec(BuildingKind::Hospital).storeys >= 5, "a hospital has at least five storeys");
  const auto tower = building_spec(BuildingKind::Skyscraper);
  require(tower.storeys >= 20 && tower.storeys * tower.storey_height_meters >= 70, "a skyscraper has at least twenty storeys, seventy metres and more");
  require(!building_kind_from_name("castle"), "an unknown kind has no value");
  rejects([] { auto s = building_spec(BuildingKind::Hospital); s.storeys = 3; validate_building_spec(s); }, "a three-storey hospital is rejected");
  rejects([] { auto s = building_spec(BuildingKind::Skyscraper); s.storeys = 12; validate_building_spec(s); }, "a twelve-storey skyscraper is rejected");
  rejects([] { auto s = building_spec(BuildingKind::Bungalow); s.storeys = 2; validate_building_spec(s); }, "a two-storey bungalow is rejected");
  rejects([] { auto s = building_spec(BuildingKind::House); s.width_meters = 1; validate_building_spec(s); }, "a one-metre house is rejected");
  rejects([] { auto s = building_spec(BuildingKind::House); s.storey_height_meters = 9; validate_building_spec(s); }, "nine-metre storeys are rejected");
  validate_building_placement_style(BuildingPlacementStyle{});
  rejects([] { BuildingPlacementStyle s; s.candidate_spacing_meters = 0.1; validate_building_placement_style(s); }, "a tiny candidate spacing is rejected");
  rejects([] { BuildingPlacementStyle s; s.path_speed_factor = 0.5; validate_building_placement_style(s); }, "a slow footpath is rejected");
}

void test_placement() {
  RoadStyle road_style;
  RoadNetwork roads(road_def(), road_style, plain_ground);
  BuildingPlacementStyle style;
  const auto requests = all_kinds();
  const auto placed = place_buildings(roads, requests, plain_ground, std::nullopt, {50, 0}, style);
  require(placed.size() == 4, "all four buildings find a site along the roads");
  const double half_road = 0.5 * road_width(road_style);
  for (std::size_t i = 0; i < placed.size(); ++i) {
    const auto& b = placed[i];
    require(b.kind == requests[i].kind, "buildings come in request order");
    require(roads.def().segments[b.site.segment].kind == RoadKind::Road, "every building stands beside a road, never a bridge");
    // The door faces the road: the footpath runs from the door to the carriageway edge, straight toward the centre line.
    const auto seg = roads.def().segments[b.site.segment];
    const auto a = roads.def().nodes[seg.a].position;
    const auto on_road = a + roads.direction(b.site.segment) * b.site.along_meters;
    require(near((b.path_end - on_road).length(), half_road, 1e-6) && roads.sample(b.path_end.x + b.facing.x * 0.1, b.path_end.y + b.facing.y * 0.1).on,
            "the footpath ends at the edge of the carriageway");
    require(near((b.door - on_road).length(), half_road + b.spec.setback_meters, 1e-6) && !roads.sample(b.door.x, b.door.y).on, "the door stands the setback from the road");
    require((b.door - b.centre).dot(b.facing) > 0 && near(b.facing.dot(b.along), 0) && near(b.along.dot(roads.direction(b.site.segment)), 1.0),
            "the front wall is parallel to the road and the building faces it");
    require(near(b.ground_min_meters, 50) && near(b.ground_max_meters, 50) && near(b.floor_meters, 50.15), "on the plain the floor is a slab's height over the ground");
    require(b.storeys == building_spec(b.kind).storeys && b.height_meters > b.storeys * b.spec.storey_height_meters, "the roof rises over the storeys");
    for (const double sa : {-0.5, 0.5})
      for (const double sb : {-0.5, 0.5}) {
        const auto corner = b.centre + b.along * (sa * b.spec.width_meters) + b.facing * (sb * b.spec.depth_meters);
        require(!roads.sample(corner.x, corner.y).on, "no corner lies on a carriageway");
      }
    for (std::size_t j = 0; j < i; ++j)
      require((placed[j].centre - b.centre).length() > 0.5 * std::hypot(placed[j].spec.width_meters, placed[j].spec.depth_meters) + 0.5 * std::hypot(b.spec.width_meters, b.spec.depth_meters),
              "buildings do not overlap");
  }
  require(placed[0].kind == BuildingKind::Skyscraper && placed[0].storeys >= 20 && placed[0].height_meters >= 70, "the tower stands twenty storeys and more");
  // Nearest to the preferred point first: the tower takes the best spot, the others move along.
  const auto again = place_buildings(roads, requests, plain_ground, std::nullopt, {50, 0}, style);
  for (std::size_t i = 0; i < placed.size(); ++i) require(near(again[i].centre.x, placed[i].centre.x) && near(again[i].centre.y, placed[i].centre.y), "placement is deterministic");
  const auto far = place_buildings(roads, requests, plain_ground, std::nullopt, {900, 0}, style);
  require(far[0].centre.x > 600, "a preferred point further along puts the buildings there");
  // Water keeps the buildings on the dry side; steep ground keeps them on the flat side.
  RoadNetwork lakeside(road_def(), road_style, lakeside_ground);
  const auto dry = place_buildings(lakeside, requests, lakeside_ground, 45.0, {50, 0}, style);
  require(dry.size() == 4, "the dry side has room for all four");
  for (const auto& b : dry) require(b.site.side == -1 && b.centre.y < 0, "every building stands on the dry (northern) side of the road");
  RoadNetwork hillside(road_def(), road_style, hillside_ground);
  const auto flat = place_buildings(hillside, requests, hillside_ground, std::nullopt, {50, 0}, style);
  require(flat.size() == 4, "the flat side has room for all four");
  for (const auto& b : flat) require(b.site.side == 1 && b.ground_max_meters - b.ground_min_meters <= b.spec.max_site_step_meters, "every building stands on the flat (southern) side");
  // On a slope the entrance stays at grade: the floor sits on the ground at the door, the
  // buildings take the downhill side of the road (with the door uphill of the house an
  // uphill back wall would be buried) and the foundation takes up the fall behind.
  RoadNetwork sloping(road_def(), road_style, sloping_ground);
  const auto terraced = place_buildings(sloping, requests, sloping_ground, std::nullopt, {50, 0}, style);
  require(terraced.size() == 4, "a gentle slope has room for all four");
  for (const auto& b : terraced) {
    if (b.kind == BuildingKind::Skyscraper) require(b.site.side == 1, "on the slope the deepest building stands downhill of the road, its door at the top");
    require(near(b.floor_meters, *sloping_ground(b.door.x, b.door.y) + 0.15, 1e-9) && b.ground_max_meters <= b.floor_meters + b.spec.max_buried_meters && b.ground_min_meters < b.floor_meters,
            "the floor is at the door's grade, the ground behind rises little over it and the fall in front is foundation");
  }
  // A plain that is all under water has no site at all.
  const auto none = place_buildings(roads, requests, plain_ground, 49.9, {50, 0}, style);
  require(none.empty(), "with every site on the shore nothing is built");
  // Given sites are taken as they are — when they are suitable.
  std::vector<BuildingRequest> explicit_site{{BuildingKind::House, BuildingSite{2, 300.0, -1}}};
  const auto given = place_buildings(roads, explicit_site, plain_ground, std::nullopt, {0, 0}, style);
  require(given.size() == 1 && given[0].site.segment == 2 && near(given[0].site.along_meters, 300) && given[0].centre.y < 0, "a given site is used");
  rejects([&] { std::vector<BuildingRequest> r{{BuildingKind::House, BuildingSite{1, 50.0, 1}}}; place_buildings(roads, r, plain_ground, std::nullopt, {0, 0}, style); }, "a site on a bridge is rejected");
  rejects([&] { std::vector<BuildingRequest> r{{BuildingKind::House, BuildingSite{7, 50.0, 1}}}; place_buildings(roads, r, plain_ground, std::nullopt, {0, 0}, style); }, "a site on a missing segment is rejected");
  rejects([&] { std::vector<BuildingRequest> r{{BuildingKind::House, BuildingSite{2, 5000.0, 1}}}; place_buildings(roads, r, plain_ground, std::nullopt, {0, 0}, style); }, "a site beyond the segment is rejected");
  rejects([&] { std::vector<BuildingRequest> r{{BuildingKind::House, BuildingSite{0, 50.0, 2}}}; place_buildings(roads, r, plain_ground, std::nullopt, {0, 0}, style); }, "a side other than left or right is rejected");
  rejects([&] { std::vector<BuildingRequest> r{{BuildingKind::Hospital, BuildingSite{0, 50.0, 1}}, {BuildingKind::House, BuildingSite{0, 52.0, 1}}}; place_buildings(roads, r, plain_ground, std::nullopt, {0, 0}, style); },
          "a site inside another building is rejected");
  rejects([&] { std::vector<BuildingRequest> r{{BuildingKind::House, BuildingSite{0, 50.0, -1}}}; place_buildings(hillside, r, hillside_ground, std::nullopt, {0, 0}, style); }, "a site on the steep hillside is rejected");
  rejects([&] { std::vector<BuildingRequest> r{{BuildingKind::House, BuildingSite{0, 50.0, 1}}}; place_buildings(lakeside, r, lakeside_ground, 45.0, {0, 0}, style); }, "a site in the lake is rejected");
  rejects([&] { place_buildings(roads, requests, {}, std::nullopt, {0, 0}, style); }, "placing needs a ground function");
}

void test_json() {
  const auto requests = parse_building_requests(R"({"version": 1, "routes": [], "buildings": ["bungalow", "house", {"kind": "hospital", "segment": 2, "along": 120, "side": "left"},
    {"kind": "skyscraper", "segment": 0, "along": 40, "side": -1}, {"kind": "house"}]})");
  require(requests.size() == 5 && requests[0].kind == BuildingKind::Bungalow && !requests[0].site && requests[1].kind == BuildingKind::House, "names are automatic requests");
  require(requests[2].kind == BuildingKind::Hospital && requests[2].site && requests[2].site->segment == 2 && near(requests[2].site->along_meters, 120) && requests[2].site->side == -1, "an object gives a site");
  require(requests[3].site && requests[3].site->side == -1 && requests[4].kind == BuildingKind::House && !requests[4].site, "numeric sides and siteless objects");
  require(parse_building_requests(R"({"version": 1})").empty(), "no buildings key, no requests");
  rejects([] { parse_building_requests(R"({"version": 1, "buildings": ["castle"]})"); }, "an unknown kind is rejected");
  rejects([] { parse_building_requests(R"({"version": 1, "buildings": [{"kind": "house", "segment": 1}]})"); }, "a site without along is rejected");
  rejects([] { parse_building_requests(R"({"version": 1, "buildings": [{"kind": "house", "segment": 1, "along": 5, "side": "north"}]})"); }, "an unknown side is rejected");
  rejects([] { parse_building_requests(R"({"version": 1, "buildings": [{"kind": "house", "segment": 1.5, "along": 5}]})"); }, "a fractional segment is rejected");
  rejects([] { parse_building_requests(R"({"version": 1, "buildings": 7})"); }, "a non-array is rejected");
  rejects([] { parse_building_requests(R"([])"); }, "a non-object is rejected");
}

void test_set_and_walker() {
  RoadStyle road_style;
  RoadNetwork roads(road_def(), road_style, plain_ground);
  BuildingPlacementStyle style;
  BuildingSet set(place_buildings(roads, all_kinds(), plain_ground, std::nullopt, {50, 0}, style), plain_ground, style);
  require(set.size() == 4 && !set.empty(), "four buildings in the set");
  const auto stats = set.stats();
  require(stats.buildings == 4 && stats.bungalows == 1 && stats.houses == 1 && stats.hospitals == 1 && stats.skyscrapers == 1 && stats.most_storeys >= 20 && stats.tallest_meters >= 70,
          "the stats count the kinds, the storeys and the height");
  const auto& house = set.buildings()[2];
  // The footpath is a surface to walk on at the road's pace; the plot itself is not.
  const auto mid_path = house.door + (house.path_end - house.door) * 0.5;
  require(set.platform_height(mid_path.x, mid_path.y) && near(*set.platform_height(mid_path.x, mid_path.y), 50 + style.surface_lift_meters) &&
          near(set.speed_factor(mid_path.x, mid_path.y), style.path_speed_factor), "the footpath carries the feet and hurries the walker");
  const auto& hospital = set.buildings()[1];
  require(!set.platform_height(hospital.centre.x, hospital.centre.y) && near(set.speed_factor(hospital.centre.x, hospital.centre.y), 1.0) && !set.platform_height(500, 400),
          "off the paths there is no surface (a solid building's plot is not one)");
  require(set.platform_height(house.centre.x, house.centre.y) && near(*set.platform_height(house.centre.x, house.centre.y), house.floor_meters), "the house is entered: its floor is a surface");
  require(set.building_at(house.centre.x, house.centre.y) == 2 && !set.building_at(mid_path.x, mid_path.y) && set.building_at(house.door.x, house.door.y, 1.0) == 2, "building_at finds the plot");
  require(set.entrance_near(house.door.x + 1, house.door.y, 3.0) == 2 && !set.entrance_near(house.door.x + 10, house.door.y + 10, 3.0), "entrance_near finds the door");
  rejects([&] { (void)set.platform_height(std::nan(""), 0); }, "non-finite queries are rejected");
  rejects([&] { BuildingSet bad(set.buildings(), {}, style); }, "a set needs a ground function");
  // Walls are solid: the walker coming up the footpath is stopped at the front wall.
  ObstacleField walls(set.obstacles());
  require(walls.size() > 100, "the walls are chains of posts");
  for (const auto& o : walls.obstacles())
    if (near(o.radius, 0.35)) require(o.top - o.bottom > 3.0, "wall and partition posts reach from the foundation to the roof (or the floor above)");
    else if (near(o.radius, 0.08)) require(o.top - o.bottom > 4.0, "the house's handrail runs up both storeys");
    else if (near(o.radius, 0.15)) require(o.top - o.bottom > 70.0, "the skyscraper's spine wall between its flights runs the full height");
    else require(o.top - o.bottom >= 0.2 && o.top - o.bottom <= 2.2, "the other posts are the furniture");
  ObstacleField doors(set.door_obstacles());
  WalkEnvironment env;
  env.standing_height = plain_ground;
  env.lower = {-100, 0, -500}; env.upper = {1100, 0, 500};
  env.obstacles = {&walls, &doors};
  env.platforms = [&](double x, double z, double) { return set.platform_height(x, z); };
  env.surface_speed = [&](double x, double z) { return set.speed_factor(x, z); };
  env.gait.enabled = false;
  FirstPersonCamera walker;
  walker.feet = {house.path_end.x - house.facing.x * 2.0, 50, house.path_end.y - house.facing.y * 2.0}; walker.grounded = true;  // on the road, two metres from the path
  walker.yaw = std::atan2(house.facing.x, house.facing.y);  // forward (-sin, -cos) = -facing: toward the building
  require(near(horizontal_forward(walker.yaw).x, -house.facing.x, 1e-9) && near(horizontal_forward(walker.yaw).z, -house.facing.y, 1e-9), "looking at the door");
  double on_path = 0;
  for (int i = 0; i < 60 * 10; ++i) {
    walk(walker, WalkInput{1, 0, false}, 1.0 / 60.0, env);
    if (walker.on_platform) on_path = std::max(on_path, walker.surface_speed_factor);
  }
  const Vec2 feet{walker.feet.x, walker.feet.z};
  const auto [a, b] = std::pair{(feet - house.centre).dot(house.along), (feet - house.centre).dot(house.facing)};
  require(near(on_path, style.path_speed_factor), "the footpath hurried the walker");
  require(b > 0.5 * house.spec.depth_meters && b < 0.5 * house.spec.depth_meters + 1.5 && std::abs(a) < 1.0, "the closed door stopped the walker at the threshold");
  require(!set.inside_of(feet.x, feet.y), "the walker is not inside the house");
}

void test_meshes() {
  RoadStyle road_style;
  RoadNetwork roads(road_def(), road_style, plain_ground);
  BuildingPlacementStyle style;
  BuildingSet set(place_buildings(roads, all_kinds(), plain_ground, std::nullopt, {50, 0}, style), plain_ground, style);
  const auto parts = set.build_meshes();
  std::map<std::string, std::size_t> count;
  for (const auto& p : parts) {
    require(p.indices.size() % 3 == 0 && !p.vertices.empty(), "whole triangles");
    for (const auto i : p.indices) require(i < p.vertices.size(), "indices in range");
    for (const auto& v : p.vertices) {
      require(std::isfinite(v.position[0]) && std::isfinite(v.position[1]) && std::isfinite(v.position[2]), "finite positions");
      require(near(std::hypot(v.normal[0], v.normal[1], v.normal[2]), 1.0, 1e-4), "unit normals");
    }
    ++count[p.what];
  }
  require(count["wall"] == 3 && count["glass"] == 1 && count["roof"] == 4 && count["door"] == 1 && count["doorframe"] == 3 && count["window"] == 2 && count["opening"] == 0 &&
          count["cross"] == 1 && count["chimney"] == 2 && count["masonry"] == 1 && count["trim"] == 2 && count["path"] == 1 && count["foundation"] == 1,
          "stone and concrete walls, a glass facade, four roofs, the hospital's painted door and three door frames, glass in the house's holes and on the hospital, real holes in the bungalow, the hospital's cross, two chimneys, the bungalow's masonry plinth, footpaths and foundations");
  require(count["floor"] == 5 && count["ceiling"] == 3 && count["table"] == 1 && count["chair"] == 2 && count["bed"] == 1 && count["mattress"] == 1 && count["wardrobe"] == 1 &&
          count["cupboard"] == 1 && count["worktop"] == 1 && count["sink"] == 1 && count["stove"] == 1,
          "the floors (the house's two, the tower's ground floor and its upper floors), ceilings and the furniture shared by the bungalow, the house and the tower (a part per kind, material and colour: the timber chairs and the receptionist's black one)");
  require(count["partition"] == 2 && count["core"] == 1 && count["stair"] == 2 && count["desk"] == 2 && count["sofa"] == 1 && count["television"] == 1 && count["stand"] == 1 && count["toilet"] == 1 && count["washbasin"] == 1 && count["cabinet"] == 2,
          "the partitions, the tower's concrete core and stair, its reception desk with its stone top, sofas, televisions, toilets, washbasins and cabinets");
  // Near a focus only the buildings within the radius get their rooms: the tower's rooms are most of the triangles.
  const auto exterior_only = set.build_meshes(Vec2{-5000, -5000}, 10.0);
  require(part_triangles(exterior_only) < part_triangles(parts) / 4, "without a focus within reach the interiors are left out");
  const auto near_tower = set.interiors_near(set.buildings()[0].centre, 50.0);
  require(std::find(near_tower.begin(), near_tower.end(), 0) != near_tower.end() && near_tower.size() < 4, "interiors_near lists the enterable buildings within the radius");
  // Materials: thatch and planks on the bungalow over a stone plinth and a brick chimney, stone and tiles on the house, concrete and glass on the rest.
  std::map<std::string, SurfaceMaterial> materials;
  for (const auto& p : parts) {
    double sx = 0, sz = 0;
    for (const auto& v : p.vertices) { sx += v.position[0]; sz += v.position[2]; }
    const auto who = set.building_at(sx / p.vertices.size(), sz / p.vertices.size(), 6.0);
    if (who) materials[std::string(p.what) + "@" + building_kind_name(set.buildings()[*who].kind)] = p.material;
  }
  require(materials["roof@bungalow"] == SurfaceMaterial::Thatch && materials["wall@bungalow"] == SurfaceMaterial::Planks && materials["masonry@bungalow"] == SurfaceMaterial::Stone &&
          materials["chimney@bungalow"] == SurfaceMaterial::Brick, "the bungalow is thatch over planks on a stone plinth with a brick chimney");
  require(materials["wall@house"] == SurfaceMaterial::Stone && materials["roof@house"] == SurfaceMaterial::Tiles && materials["chimney@house"] == SurfaceMaterial::Brick &&
          materials["doorframe@house"] == SurfaceMaterial::Timber && materials["stair@house"] == SurfaceMaterial::Timber && materials["window@house"] == SurfaceMaterial::Window,
          "the house is stone under tiles with a timber door frame and stair and glazed windows");
  require(materials["wall@hospital"] == SurfaceMaterial::Concrete && materials["glass@skyscraper"] == SurfaceMaterial::Glass && materials["window@house"] == SurfaceMaterial::Window,
          "concrete and glass where they belong");
  require(std::string(surface_material_name(SurfaceMaterial::Thatch)) == "thatch", "material names");
  require(part_triangles(parts) > 400, "enough triangles for the windows");
  // Colours: the hospital white with a dark grey roof and a red cross, the tower glass with a white roof, the house's roof red, the bungalow's thatch.
  const auto& tower = set.buildings()[0];
  const auto& hospital = set.buildings()[1];
  const auto& house = set.buildings()[2];
  const auto& bungalow = set.buildings()[3];
  const auto owner = [&](const MeshPart& p) -> std::optional<std::size_t> {
    double sx = 0, sz = 0;
    for (const auto& v : p.vertices) { sx += v.position[0]; sz += v.position[2]; }
    return set.building_at(sx / p.vertices.size(), sz / p.vertices.size(), 6.0);
  };
  double tower_top = 0, hospital_top = 0, bungalow_top = 0, house_top = 0;
  for (const auto& p : parts) {
    const std::string what = p.what;
    const auto who = owner(p);
    if (!who) continue;
    double top = 0;
    for (const auto& v : p.vertices) top = std::max(top, static_cast<double>(v.position[1]));
    if (*who == 0) tower_top = std::max(tower_top, top);
    if (*who == 1) hospital_top = std::max(hospital_top, top);
    if (*who == 2) house_top = std::max(house_top, top);
    if (*who == 3) bungalow_top = std::max(bungalow_top, top);
    if (what == "wall" && *who == 1) require(p.color.x > 0.9 && p.color.y > 0.9 && p.color.z > 0.88, "the hospital's walls are white concrete");
    if (what == "roof" && *who == 1) require(p.color.x < 0.3 && p.color.y < 0.3 && p.color.z < 0.3 && near(p.color.x, p.color.y, 0.03), "the hospital's roof is dark grey");
    if (what == "cross") require(p.color.x > 0.8 && p.color.y < 0.15 && p.color.z < 0.15 && *who == 1, "the red cross is red and on the hospital");
    if (what == "glass") require(*who == 0 && p.color.z > p.color.x, "the tower's facade is glass");
    if (what == "roof" && *who == 0) require(p.color.x > 0.9 && p.color.y > 0.9 && p.color.z > 0.9, "the tower's roof is white");
    if (what == "roof" && *who == 2) require(p.color.x > 0.5 && p.color.y < 0.3, "the house's roof is red tile");
    if (what == "wall" && *who == 2) require(near(p.color.x, p.color.y, 0.05) && p.color.x > 0.45 && p.color.x < 0.65, "the house's walls are grey stone");
    if (what == "roof" && *who == 3) require(p.color.x > 0.6 && p.color.y > 0.5 && p.color.z < 0.45, "the bungalow's roof is straw");
    if (what == "wall" && *who == 3) require(p.color.x > p.color.y && p.color.y > p.color.z, "the bungalow's walls are wood");
    if (what == "floor" || what == "ceiling") require(*who == 3 || *who == 2 || *who == 0, "the rooms are the bungalow's, the house's and the tower's");
    if (what == "core" || what == "desk") require(*who == 0, "the concrete core and the reception desk are the tower's");
    if (what == "window") require(p.color.z > 0.8 && p.color.z > p.color.x, "glass windows are blue-grey glass");
  }
  require(tower_top - tower.floor_meters >= 70 && tower_top - tower.floor_meters <= 90, "the tower rises at least seventy metres over its floor");
  require(hospital_top - hospital.floor_meters >= 5 * 3.6 && hospital_top - hospital.floor_meters < 5 * 3.6 + 2, "the hospital stands five storeys under a flat roof");
  require(house_top - house.floor_meters > 2 * 3.0 + 2.0 && bungalow_top - bungalow.floor_meters > 2.8 + 1.5, "the gable roofs rise over the walls");
  // Every door stands on the front wall facing the road, every path reaches the carriageway.
  for (const auto& p : parts) {
    if (std::string(p.what) != "door" && std::string(p.what) != "doorframe") continue;
    const auto who = owner(p);
    require(who.has_value(), "a door belongs to a building");
    const auto& b = set.buildings()[*who];
    double sx = 0, sz = 0;
    for (const auto& v : p.vertices) { sx += v.position[0]; sz += v.position[2]; }
    const Vec2 door_centre{sx / p.vertices.size(), sz / p.vertices.size()};
    require((door_centre - b.door).length() < 0.2, "the door is drawn at the entrance, on the road side");
  }
  // The renderer takes them.
  ValidatingRenderDevice device;
  RenderResourceOwner shared(device);
  const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const auto camera = shared.create_buffer({BufferUsage::Uniform, sizeof(identity)}, std::as_bytes(std::span(identity)));
  MeshPartRenderer renderer(device);
  renderer.set_parts(parts);
  require(renderer.parts() == parts.size() && renderer.triangles() == part_triangles(parts), "the parts are uploaded");
  RenderPassScope pass(device, {{64, 64}});
  require(renderer.draw(camera) == parts.size(), "one draw per part");
}

void test_vegetation() {
  RoadStyle road_style;
  RoadNetwork roads(road_def(), road_style, plain_ground);
  BuildingPlacementStyle style;
  std::vector<BuildingRequest> one{{BuildingKind::House, BuildingSite{0, 50.0, 1}}};
  BuildingSet set(place_buildings(roads, one, plain_ground, std::nullopt, {50, 0}, style), plain_ground, style);
  const auto& house = set.buildings()[0];
  VegetationPlacement placement;
  const auto plant = [&](Vec2 p, float kind) { VegetationInstance t{}; t.position[0] = static_cast<float>(p.x); t.position[2] = static_cast<float>(p.y); t.kind = kind; t.scale = 10; return t; };
  const auto mid_path = house.door + (house.path_end - house.door) * 0.5;
  placement.trees = {plant(house.centre, 0), plant(house.centre + house.along * (0.5 * house.spec.width_meters + 2.0), 1), plant(house.centre + house.along * 40.0, 1)};
  placement.grass = {plant(mid_path, 0), plant({500, -300}, 0)};
  placement.chunks = {{{0, 0, -500}, {1100, 10, 500}, 0, 3, 0, 2}};
  placement.trees_per_kind = {1, 2};
  const auto removed = clear_vegetation_for_buildings(placement, set);
  require(removed == 3 && placement.trees.size() == 1 && placement.grass.size() == 1 && placement.chunks.size() == 1, "the plants on the plot and the footpath are cleared");
  require(placement.chunks[0].tree_count == 1 && placement.chunks[0].grass_count == 1 && placement.trees_per_kind[1] == 1 && placement.trees_per_kind[0] == 0, "the chunk is rebuilt");
  rejects([&] { clear_vegetation_for_buildings(placement, set, -1); }, "a negative margin is rejected");
}

void test_civic_and_airport_kinds() {
  for (const auto kind : {BuildingKind::PoliceStation, BuildingKind::Terminal, BuildingKind::ControlTower, BuildingKind::Hangar}) {
    validate_building_spec(building_spec(kind));
    require(*building_kind_from_name(building_kind_name(kind)) == kind, "the civic and airport kinds round-trip by name");
  }
  require(*building_kind_from_name("police station") == BuildingKind::PoliceStation && *building_kind_from_name("control tower") == BuildingKind::ControlTower, "long names too");
  rejects([] { auto s = building_spec(BuildingKind::Hangar); s.storeys = 2; validate_building_spec(s); }, "a two-storey hangar is rejected");
  rejects([] { auto s = building_spec(BuildingKind::ControlTower); s.storeys = 3; validate_building_spec(s); }, "a squat control tower is rejected");
  RoadStyle road_style;
  RoadNetwork roads(road_def(), road_style, plain_ground);
  BuildingPlacementStyle style;
  // Explicit lots along the long road, including ones that cannot stand (over the valley, on each other): skipped, not thrown.
  std::vector<BuildingRequest> lots{{BuildingKind::PoliceStation, BuildingSite{2, 60.0, 1}}, {BuildingKind::Terminal, BuildingSite{2, 200.0, 1}},
                                    {BuildingKind::ControlTower, BuildingSite{2, 300.0, 1}}, {BuildingKind::Hangar, BuildingSite{2, 400.0, 1}},
                                    {BuildingKind::Hangar, BuildingSite{2, 410.0, 1}}, {BuildingKind::House, BuildingSite{0, 40.0, 1}}, {BuildingKind::House, BuildingSite{0, 58.0, 1}},
                                    {BuildingKind::House, BuildingSite{0, 76.0, 1}}};
  rejects([&] { place_buildings(roads, lots, plain_ground, std::nullopt, {0, 0}, style); }, "a lot inside another is rejected when asked to be strict");
  const auto placed = place_buildings(roads, lots, plain_ground, std::nullopt, {0, 0}, style, true);
  require(placed.size() == 7, "the second hangar on the first is skipped; the rest stand");
  BuildingSet set(placed, plain_ground, style);
  const auto st = set.stats();
  require(st.police_stations == 1 && st.terminals == 1 && st.towers == 1 && st.hangars == 1 && st.houses == 3, "the stats count the new kinds");
  // Three houses eighteen metres apart (seven metres between their walls): rectangles, not circles, decide the overlap, so a row fits.
  require(near(placed[4].centre.x - placed[5].centre.x, -18.0) && near(placed[5].centre.x - placed[6].centre.x, -18.0), "houses stand in a row seven metres apart");
  const auto parts = set.build_meshes();
  std::map<std::string, std::size_t> count;
  for (const auto& p : parts) ++count[p.what];
  require(count["band"] == 1 && count["beacon"] == 1 && count["cab"] == 1 && count["metal"] == 1, "the police station's blue band and beacon, the tower's cab, the hangar's metal walls");
  const auto owner = [&](const MeshPart& p) -> std::optional<std::size_t> {
    double sx = 0, sz = 0;
    for (const auto& v : p.vertices) { sx += v.position[0]; sz += v.position[2]; }
    return set.building_at(sx / p.vertices.size(), sz / p.vertices.size(), 8.0);
  };
  for (const auto& p : parts) {
    const std::string what = p.what;
    if (what == "band") require(p.color.z > 0.6 && p.color.x < 0.2 && owner(p) == 0, "the band is blue and on the police station");
    if (what == "door") {
      const auto who = owner(p);
      if (who && *who == 3) {
        double lo = 1e9, hi = -1e9;
        for (const auto& v : p.vertices) { lo = std::min(lo, static_cast<double>(v.position[1])); hi = std::max(hi, static_cast<double>(v.position[1])); }
        require(hi - lo > 8.5, "the hangar's door is nine metres tall");
      }
    }
    if (what == "cab") require(owner(p) == 2, "the cab is on the tower");
  }
  double tower_top = 0, terminal_windows = 0;
  for (const auto& p : parts) {
    const auto who = owner(p);
    if (who && *who == 2) for (const auto& v : p.vertices) tower_top = std::max(tower_top, static_cast<double>(v.position[1]));
    if (who && *who == 1 && std::string(p.what) == "window") terminal_windows = p.indices.size() / 12.0;
  }
  require(tower_top - placed[2].floor_meters > 7 * 3.5 + 3.0, "the cab rises over the shaft");
  require(terminal_windows >= 8, "the terminal's glass bands run along every face and storey");
}

// Walks `steps` frames with the given input, rebuilding the door field every frame (the door may be swinging).
void walk_frames(FirstPersonCamera& walker, WalkEnvironment& env, BuildingSet& set, ObstacleField& door_field, WalkInput input, int steps) {
  for (int i = 0; i < steps; ++i) {
    set.advance(1.0 / 60.0);
    door_field = ObstacleField(set.door_obstacles());
    walk(walker, input, 1.0 / 60.0, env);
  }
}

void test_interior() {
  require(building_enterable(BuildingKind::Bungalow) && building_enterable(BuildingKind::House) && building_enterable(BuildingKind::Skyscraper) && !building_enterable(BuildingKind::Hospital) &&
          !building_enterable(BuildingKind::Hangar), "the bungalow, the house and the skyscraper are entered, the rest are solid");
  // The furniture fits between the walls, clear of the doorway, and stands on the floor.
  const auto spec = building_spec(BuildingKind::Bungalow);
  const auto pieces = furnish(spec);
  require(pieces.size() > 20, "a kitchen, a table with chairs, a wardrobe and a bed");
  std::map<std::string, int> kinds;
  const double iw = 0.5 * spec.width_meters - kInteriorWallThickness, id = 0.5 * spec.depth_meters - kInteriorWallThickness;
  for (const auto& f : pieces) {
    ++kinds[f.what];
    require(std::abs(f.centre.x) + f.half_along <= iw && std::abs(f.centre.y) + f.half_across <= id, "every piece stands within the walls");
    require(f.bottom >= 0 && f.height > 0 && f.bottom + f.height < spec.storey_height_meters, "every piece stands on the floor under the ceiling");
    if (f.solid) require(f.bottom + f.height > 0.5, "a solid piece stands higher than the step height (it is never climbed)");
    require(!(f.centre.y + f.half_across > id - 1.2 && std::abs(f.centre.x) - f.half_along < 1.0), "the entrance is kept clear");
  }
  require(kinds["cupboard"] == 2 && kinds["worktop"] == 1 && kinds["sink"] == 1 && kinds["stove"] == 1 && kinds["table"] >= 5 && kinds["chair"] >= 4 * 6 && kinds["wardrobe"] == 1 &&
          kinds["bed"] == 1 && kinds["mattress"] == 1 && kinds["blanket"] == 1 && kinds["pillow"] == 1, "the pieces of furniture");
  require(furnish(building_spec(BuildingKind::Hospital)).empty() && interior_plan(building_spec(BuildingKind::Hospital)).rooms.empty(), "a solid building has no furniture and no rooms");
  require(interior_plan(spec).rooms.size() == 1 && std::string(interior_plan(spec).rooms[0].name) == "room" && interior_plan(spec).walls.empty() && interior_plan(spec).flights.empty() && interior_plan(spec).floors == 1,
          "the bungalow is one room on one floor");
  rejects([] { auto s = building_spec(BuildingKind::Bungalow); s.width_meters = 1.0; furnish(s); }, "furnishing validates the spec");

  RoadStyle road_style;
  RoadNetwork roads(road_def(), road_style, plain_ground);
  BuildingPlacementStyle style;
  std::vector<BuildingRequest> two{{BuildingKind::Bungalow, BuildingSite{0, 50.0, 1}}, {BuildingKind::Hospital, BuildingSite{0, 50.0, -1}}};
  BuildingSet set(place_buildings(roads, two, plain_ground, std::nullopt, {50, 0}, style), plain_ground, style);
  require(set.size() == 2 && set.stats().interiors == 1, "one interior among two buildings");
  const auto& bungalow = set.buildings()[0];
  require(near(bungalow.floor_meters, 50 + 0.15), "the floor stands over the highest ground under the bungalow");
  // Inside, the floor is the surface; the doorway too; the path ramps up to it.
  require(set.inside(0, bungalow.centre.x, bungalow.centre.y) && set.inside_of(bungalow.centre.x, bungalow.centre.y) == 0 && !set.inside(0, bungalow.door.x + bungalow.facing.x * 2, bungalow.door.y + bungalow.facing.y * 2),
          "inside is within the walls");
  require(!set.inside(1, set.buildings()[1].centre.x, set.buildings()[1].centre.y) && !set.inside_of(set.buildings()[1].centre.x, set.buildings()[1].centre.y), "the hospital is never entered");
  rejects<std::out_of_range>([&] { (void)set.inside(5, 0, 0); }, "inside checks the index");
  require(near(*set.platform_height(bungalow.centre.x, bungalow.centre.y), bungalow.floor_meters) && near(set.speed_factor(bungalow.centre.x, bungalow.centre.y), style.path_speed_factor),
          "the floor carries the feet inside");
  const Vec2 in_doorway = bungalow.door - bungalow.facing * 0.1;
  require(near(*set.platform_height(in_doorway.x, in_doorway.y), bungalow.floor_meters) && near(*set.platform_height(bungalow.door.x, bungalow.door.y), bungalow.floor_meters), "and in the doorway");
  const Vec2 on_ramp = bungalow.door + bungalow.facing * 0.1, down_path = bungalow.door + bungalow.facing * 3.0;
  require(*set.platform_height(on_ramp.x, on_ramp.y) < bungalow.floor_meters && *set.platform_height(on_ramp.x, on_ramp.y) > 50 + style.surface_lift_meters &&
          near(*set.platform_height(down_path.x, down_path.y), 50 + style.surface_lift_meters), "the path ramps down from the door to the lifted ground");
  {
    // On a slope rising behind the bungalow the floor stands over the highest
    // corner and the footpath ramps up to the door at 3:10; a walker climbs it.
    RoadNetwork sloped_roads(road_def(), road_style, sloping_ground);
    std::vector<BuildingRequest> uphill{{BuildingKind::Bungalow, BuildingSite{0, 50.0, -1}}};
    BuildingSet sloped(place_buildings(sloped_roads, uphill, sloping_ground, std::nullopt, {50, 0}, style), sloping_ground, style);
    require(sloped.size() == 1, "the bungalow stands on the slope");
    const auto& b = sloped.buildings()[0];
    require(near(b.floor_meters, b.ground_max_meters + 0.15) && b.floor_meters - *sloping_ground(b.door.x, b.door.y) > 0.5, "the floor is raised over the uphill corners, well above the door's ground");
    const Vec2 near_door = b.door + b.facing * 1.0, far_down = b.door + b.facing * 5.0;
    require(near(*sloped.platform_height(near_door.x, near_door.y), b.floor_meters - 0.3) && near(*sloped.platform_height(far_down.x, far_down.y), *sloping_ground(far_down.x, far_down.y) + style.surface_lift_meters),
            "the ramp drops 0.3 m per metre until it meets the lifted ground");
    ObstacleField sloped_walls(sloped.obstacles()), sloped_door(sloped.door_obstacles());
    WalkEnvironment env;
    env.standing_height = sloping_ground;
    env.lower = {-100, 0, -500}; env.upper = {1100, 0, 500};
    env.obstacles = {&sloped_walls, &sloped_door};
    env.platforms = [&](double x, double z, double) { return sloped.platform_height(x, z); };
    env.gait.enabled = false;
    FirstPersonCamera walker;
    walker.feet = {b.path_end.x + b.facing.x * 1.0, *sloping_ground(b.path_end.x + b.facing.x, b.path_end.y + b.facing.y), b.path_end.y + b.facing.y * 1.0}; walker.grounded = true;
    walker.yaw = std::atan2(b.facing.x, b.facing.y);
    for (int i = 0; i < 60 * 6; ++i) walk(walker, WalkInput{1, 0, false}, 1.0 / 60.0, env);
    require(near(walker.feet.y, b.floor_meters, 0.15) && walker.feet.y > *sloping_ground(b.door.x, b.door.y) + 0.5 && (Vec2{walker.feet.x, walker.feet.z} - b.door).length() < 1.0, "the walker climbs the ramp to the closed door, nearly at floor level");
  }
  // The walls leave the doorway open; the door leaf shuts it until it is opened.
  ObstacleField walls(set.obstacles());
  const auto gap_at = [&](const ObstacleField& field, Vec2 p) {
    double nearest = 1e9;
    for (const auto& o : field.obstacles()) nearest = std::min(nearest, std::hypot(o.x - p.x, o.z - p.y) - o.radius);
    return nearest;
  };
  const Vec2 threshold = bungalow.door - bungalow.facing * 0.125;
  require(gap_at(walls, threshold) > 0.5, "the walls leave the doorway open");
  require(gap_at(walls, bungalow.door + bungalow.along * 2.0 - bungalow.facing * 0.125) < 0.0, "and stand on either side of it");
  require(set.doors().size() == 2 && near(set.doors()[0].open, 0.0) && !set.doors()[0].opening, "the doors start closed and at rest");
  ObstacleField door_field(set.door_obstacles());
  require(door_field.size() > 3 && gap_at(door_field, threshold) < 0.0, "the closed leaf stands across the doorway");
  require(!set.toggle_door(1) && set.toggle_door(0) && set.doors()[0].opening, "the bungalow's door is opened, the hospital's is painted on");
  rejects<std::out_of_range>([&] { set.toggle_door(7); }, "toggle checks the index");
  rejects([&] { set.advance(-1.0); }, "advance needs a non-negative step");
  const auto version = set.door_version();
  set.advance(0.3);
  require(near(set.doors()[0].open, 0.5) && set.door_version() != version, "half open after 0.3 s, the version moved");
  set.advance(1.0);
  require(near(set.doors()[0].open, 1.0) && set.doors()[0].opening, "fully open after the swing, resting");
  const auto resting = set.door_version();
  set.advance(1.0);
  require(set.door_version() == resting, "a resting door leaves the version alone");
  door_field = ObstacleField(set.door_obstacles());
  require(gap_at(door_field, threshold) > 0.3, "the open leaf stands clear of the doorway's middle");
  {
    // The open leaf lies along the doorway's side, inside the room.
    double along_sum = 0, across_sum = 0;
    for (const auto& o : door_field.obstacles()) { along_sum += (Vec2{o.x, o.z} - bungalow.door).dot(bungalow.along); across_sum += (Vec2{o.x, o.z} - bungalow.door).dot(bungalow.facing); }
    require(along_sum / door_field.size() > 0.4 && across_sum / door_field.size() < -0.4, "swung inward on the right-hand jamb");
  }
  set.toggle_door(0);
  require(!set.doors()[0].opening, "toggled again, it closes");
  set.advance(0.2);
  require(near(set.doors()[0].open, 1.0 - 0.2 / 0.6), "closing at the same pace");
  set.toggle_door(0);
  require(set.doors()[0].opening, "toggled mid-swing, it opens again");
  set.advance(1.0);
  // The door meshes follow the leaf.
  const auto open_meshes = set.build_door_meshes();
  require(open_meshes.size() == 2 && std::string(open_meshes[0].what) == "door" && std::string(open_meshes[1].what) == "handle" && open_meshes[0].material == SurfaceMaterial::Timber,
          "a timber leaf with a metal handle");
  double sx = 0, sz = 0;
  for (const auto& v : open_meshes[0].vertices) { sx += v.position[0]; sz += v.position[2]; }
  const Vec2 leaf_centre{sx / open_meshes[0].vertices.size(), sz / open_meshes[0].vertices.size()};
  require((leaf_centre - bungalow.door).dot(bungalow.facing) < -0.3 && (leaf_centre - bungalow.door).dot(bungalow.along) > 0.3, "the open leaf's mesh stands inside along the jamb");
  set.toggle_door(0); set.advance(1.0);
  const auto closed_meshes = set.build_door_meshes();
  sx = sz = 0;
  for (const auto& v : closed_meshes[0].vertices) { sx += v.position[0]; sz += v.position[2]; }
  require((Vec2{sx / closed_meshes[0].vertices.size(), sz / closed_meshes[0].vertices.size()} - bungalow.door).length() < 0.2, "the closed leaf's mesh fills the doorway");
  // The walker: stopped by the closed door, in once it is open, onto the floor, stopped by the table, never through a wall.
  WalkEnvironment env;
  env.standing_height = plain_ground;
  env.lower = {-100, 0, -500}; env.upper = {1100, 0, 500};
  env.obstacles = {&walls, &door_field};
  env.platforms = [&](double x, double z, double) { return set.platform_height(x, z); };
  env.surface_speed = [&](double x, double z) { return set.speed_factor(x, z); };
  env.gait.enabled = false;
  FirstPersonCamera walker;
  walker.feet = {bungalow.door.x + bungalow.facing.x * 3.0, 50, bungalow.door.y + bungalow.facing.y * 3.0}; walker.grounded = true;
  walker.yaw = std::atan2(bungalow.facing.x, bungalow.facing.y);  // toward the door
  const auto local = [&](const FirstPersonCamera& w) { const Vec2 f{w.feet.x, w.feet.z}; return std::pair{(f - bungalow.centre).dot(bungalow.along), (f - bungalow.centre).dot(bungalow.facing)}; };
  walk_frames(walker, env, set, door_field, WalkInput{1, 0, false}, 60 * 4);
  require(local(walker).second > 0.5 * spec.depth_meters && local(walker).second < 0.5 * spec.depth_meters + 1.0 && !set.inside_of(walker.feet.x, walker.feet.z), "the closed door stops the walker at the threshold");
  require(near(walker.feet.y, bungalow.floor_meters, 0.05), "up the ramp to the door's level");
  require(set.entrance_near(walker.feet.x, walker.feet.z, 2.0) == 0, "within reach of the door");
  set.toggle_door(0);
  walk_frames(walker, env, set, door_field, WalkInput{1, 0, false}, 60 * 3);
  require(set.inside_of(walker.feet.x, walker.feet.z) == 0 && near(walker.feet.y, bungalow.floor_meters, 1e-6) && walker.on_platform, "through the open door onto the floor");
  require(std::abs(local(walker).first) < 0.8 && local(walker).second > -(0.5 * spec.depth_meters - 0.3) && local(walker).second < 0.0, "stopped inside, short of the back wall (the stove stands there)");
  // Stand in front of the table (to the right of the door, looking in) and walk at it: the chairs on its near side stop the walker.
  const Vec2 table_centre = bungalow.centre + bungalow.along * 1.6 + bungalow.facing * 0.2;
  const Vec2 before_table = table_centre + bungalow.facing * 2.2;
  walker.feet = {before_table.x, bungalow.floor_meters, before_table.y};
  walker.yaw = std::atan2(bungalow.facing.x, bungalow.facing.y);  // toward the back wall, through the table
  walk_frames(walker, env, set, door_field, WalkInput{1, 0, false}, 60 * 3);
  const double table_gap = (table_centre - Vec2{walker.feet.x, walker.feet.z}).dot(bungalow.facing * -1.0);
  require(table_gap > 1.0 && table_gap < 1.4 && near(local(walker).first, 1.6, 0.1) && set.inside_of(walker.feet.x, walker.feet.z) == 0, "the chairs at the table stop the walker");
  // Walk at the side wall: still inside.
  walker.yaw = std::atan2(-bungalow.along.x, -bungalow.along.y);
  walk_frames(walker, env, set, door_field, WalkInput{1, 0, false}, 60 * 4);
  require(set.inside_of(walker.feet.x, walker.feet.z) == 0 && local(walker).first < 0.5 * spec.width_meters - kInteriorWallThickness - 0.3, "the wall holds the walker in");
  // Close the door from inside: the leaf swings shut and the way out is blocked.
  set.toggle_door(0);
  walk_frames(walker, env, set, door_field, WalkInput{0, 0, false}, 60);
  require(near(set.doors()[0].open, 0.0), "closed again");
  require(gap_at(door_field, threshold) < 0.0, "the leaf is back across the doorway");
}

// The hazards: fire by material, heat on what does not burn, impact, flood, wind and lightning.
void test_hazards() {
  using E = BuildingElement;
  using K = BuildingEvent::Kind;
  // Materials: fuel or not, and what damages the rest.
  require(material_hazard(SurfaceMaterial::Thatch).flammable && material_hazard(SurfaceMaterial::Planks).flammable && material_hazard(SurfaceMaterial::Timber).flammable,
          "thatch, planks and timber burn");
  for (const auto m : {SurfaceMaterial::Concrete, SurfaceMaterial::Stone, SurfaceMaterial::Glass, SurfaceMaterial::Metal, SurfaceMaterial::Brick, SurfaceMaterial::Tiles, SurfaceMaterial::Asphalt, SurfaceMaterial::Pavement})
    require(!material_hazard(m).flammable && material_hazard(m).heat_threshold < 1.0 && material_hazard(m).heat_damage_seconds > 0, "the rest do not burn but extreme heat damages them");
  require(material_hazard(SurfaceMaterial::Glass).heat_threshold < material_hazard(SurfaceMaterial::Metal).heat_threshold &&
          material_hazard(SurfaceMaterial::Metal).heat_threshold < material_hazard(SurfaceMaterial::Concrete).heat_threshold, "glass bursts before metal buckles before concrete spalls");
  require(material_hazard(SurfaceMaterial::Tiles).impact_tonnes < material_hazard(SurfaceMaterial::Concrete).impact_tonnes && material_hazard(SurfaceMaterial::Thatch).wind_mps < material_hazard(SurfaceMaterial::Tiles).wind_mps,
          "tiles smash under less than concrete, thatch blows off before tiles");
  require(*building_element_material(BuildingKind::Bungalow, E::Roof) == SurfaceMaterial::Thatch && *building_element_material(BuildingKind::House, E::Walls) == SurfaceMaterial::Stone &&
          !building_element_material(BuildingKind::Bungalow, E::Windows) && *building_element_material(BuildingKind::House, E::Interior) == SurfaceMaterial::Timber && !building_element_material(BuildingKind::Hospital, E::Interior) &&
          *building_element_material(BuildingKind::Hangar, E::Roof) == SurfaceMaterial::Metal && *building_element_material(BuildingKind::Skyscraper, E::Walls) == SurfaceMaterial::Concrete &&
          *building_element_material(BuildingKind::Skyscraper, E::Windows) == SurfaceMaterial::Glass,
          "the elements are what the kinds are built of");
  require(std::string(building_element_name(E::Interior)) == "interior" && std::string(building_event_name(K::WashedOut)) == "washed out", "names");
  rejects([] { BuildingPlacementStyle s; s.flood_scour_seconds = 0; validate_building_placement_style(s); }, "an instant scour is rejected");
  rejects([] { BuildingPlacementStyle s; s.heat_reach_meters = 0.5; validate_building_placement_style(s); }, "a tiny heat reach is rejected");

  RoadStyle road_style;
  RoadNetwork roads(road_def(), road_style, plain_ground);
  BuildingPlacementStyle style;
  std::vector<BuildingRequest> three{{BuildingKind::Bungalow, BuildingSite{0, 50.0, 1}}, {BuildingKind::House, BuildingSite{0, 50.0, -1}}, {BuildingKind::Hospital, BuildingSite{2, 300.0, 1}}};
  const auto placed = place_buildings(roads, three, plain_ground, std::nullopt, {50, 0}, style);
  require(placed.size() == 3, "a bungalow, a house and a hospital");
  const auto count = [](const BuildingSet& set, K kind, std::size_t building, std::optional<E> element = std::nullopt) {
    std::size_t n = 0;
    for (const auto& e : set.events()) if (e.kind == kind && e.building == building && (!element || e.element == *element)) ++n;
    return n;
  };
  {
    // Fire: heat beside the bungalow and the house (a burning tree between them).
    BuildingSet set(placed, plain_ground, style);
    const auto& bungalow = set.buildings()[0];
    const auto& house = set.buildings()[1];
    for (const auto& st : set.states()) require(!st.alight() && !st.destroyed && st.integrity() == 1.0 && st.element(E::Walls).present && st.element(E::Roof).present, "everything starts sound");
    require(!set.states()[0].element(E::Windows).present && set.states()[0].element(E::Interior).present && set.states()[1].element(E::Interior).present && !set.states()[2].element(E::Interior).present,
            "the bungalow has no glass; the bungalow and the house have rooms, the hospital has none modelled");
    BuildingHazards fire;
    fire.heat = [&](double x, double z) { return (Vec2{x, z} - Vec2{50, 0}).length() < 40.0 ? 1.0 : 0.0; };  // the road between them is ablaze
    const auto version = set.version();
    set.advance(1.0, fire);
    require(set.states()[0].element(E::Roof).alight && count(set, K::Alight, 0, E::Roof) == 1 && set.states()[0].element(E::Roof).cause == "fire", "a second of full heat lights the thatch");
    require(!set.states()[0].element(E::Walls).alight && !set.states()[1].element(E::Walls).alight, "planks take longer, stone never");
    set.advance(3.0, fire);
    require(set.states()[0].element(E::Walls).alight && set.states()[0].element(E::Door).alight && !set.states()[0].element(E::Interior).alight, "three seconds on, the plank walls and the timber door are alight; the room not yet (only the building's own flames reach it)");
    require(set.states()[1].element(E::Door).alight && !set.states()[1].element(E::Walls).alight && !set.states()[1].element(E::Roof).alight, "the house's timber door burns, its stone walls and tiled roof do not");
    require(set.states()[0].alight() && set.states()[0].intensity() > 0.9 && set.stats().alight == 2, "both buildings count as alight");
    // Heat on what does not burn: the house's glass bursts, the walls are only slightly damaged.
    set.advance(8.0, fire);
    require(set.states()[0].element(E::Interior).alight && set.states()[0].element(E::Interior).cause == "fire", "the room caught from the burning roof and walls");
    require(set.states()[1].element(E::Windows).damage >= 1.0 && set.states()[1].element(E::Windows).cause == "heat" && count(set, K::Damaged, 1, E::Windows) + count(set, K::Damaged, 1, E::Windows) >= 0,
            "the house's windows burst in the heat");
    require(set.states()[1].element(E::Walls).damage > 0 && set.states()[1].element(E::Walls).damage < 0.05 && set.states()[1].element(E::Walls).cause == "heat", "the stone walls only spall a little");
    require(set.states()[2].element(E::Walls).damage == 0 && !set.states()[2].alight(), "the hospital far away feels nothing");
    // Exposure: full heat at the bungalow, none far away; flames and plumes are emitted.
    require(near(set.exposure_at(bungalow.centre.x, bungalow.centre.y).heat, 1.0) && set.exposure_at(bungalow.centre.x, bungalow.centre.y).smoke > 0 &&
            set.exposure_at(bungalow.centre.x + 500, bungalow.centre.y).heat == 0, "the burning bungalow's heat at its walls, nothing far off");
    std::vector<FireInstance> flames, smoke;
    set.emit(flames, smoke, FireStyle{});
    require(flames.size() > 10 && smoke.size() == 2, "flames on the burning elements and a plume over each burning building");
    require(set.version() > version, "the char changes what is drawn");
    // The thatch burns through (25 s): the roof falls in on the room; the walls take a minute and the bungalow collapses.
    set.advance(25.0, fire);
    require(set.states()[0].roof_fallen && set.states()[0].element(E::Roof).damage >= 1.0 && count(set, K::RoofFell, 0) == 1, "the thatch burned through and the roof fell in");
    require(!set.sheltered(bungalow.centre.x, bungalow.centre.y), "no shelter under a fallen roof");
    for (int i = 0; i < 80 && !set.states()[0].destroyed; ++i) set.advance(1.0, fire);
    require(set.states()[0].destroyed && set.states()[0].destroyed_by == "fire" && count(set, K::Collapsed, 0) == 1, "the plank walls burned through: the bungalow collapsed");
    require(!set.inside(0, bungalow.centre.x, bungalow.centre.y) && near(*set.platform_height(bungalow.centre.x, bungalow.centre.y), bungalow.floor_meters + 0.45) &&
            near(set.speed_factor(bungalow.centre.x, bungalow.centre.y), style.rubble_speed_factor), "rubble: a slow heap to walk over, no room");
    require(!set.toggle_door(0) && set.door_obstacles().empty() && set.build_door_meshes().empty(), "no door left");
    std::size_t posts_near = 0;
    for (const auto& o : set.obstacles()) if (std::hypot(o.x - bungalow.centre.x, o.z - bungalow.centre.y) < 8.0) ++posts_near;
    require(posts_near == 1, "only the chimney stands in the rubble");
    require(set.stats().destroyed == 1 && set.states()[0].integrity() == 0.0 && !set.states()[0].alight(), "the stats count it, its fire is out");
    flames.clear(); smoke.clear();
    set.emit(flames, smoke, FireStyle{});
    bool smoulder = false;
    for (const auto& pl : smoke) if (pl.kind == 2.0f) smoulder = true;
    require(smoulder, "the burned-down bungalow smoulders");
    std::map<std::string, std::size_t> parts;
    bool bungalow_room = false;
    for (const auto& p : set.build_meshes()) {
      ++parts[p.what];
      const std::string what = p.what;
      if (what != "floor" && what != "ceiling" && what != "table" && what != "bed" && what != "roof") continue;  // (a leaning wall slab lies in the rubble)
      for (const auto& v : p.vertices) if (set.building_at(v.position[0], v.position[2]) == 0) bungalow_room = true;
    }
    require(parts["rubble"] >= 1 && parts["roof"] == 2 && parts["window"] == 1 && parts["burst"] == 1 && !bungalow_room,
            "rubble drawn, two roofs left (the house's and the hospital's), the house's windows burst (dark holes), nothing of the bungalow's rooms");
    // The house stands: its door burned away, its tiles scorched, the walls hardly touched.
    require(!set.states()[1].destroyed && set.states()[1].element(E::Door).damage >= 1.0 && set.states()[1].element(E::Roof).damage > 0 && set.states()[1].element(E::Roof).damage < 0.2 &&
            set.states()[1].integrity() > 0.8, "the stone house survives the fire with its door gone");
    require(set.stats().damaged == 1, "one damaged building left standing");
    // Rain halves the burning, wind speeds it: two fresh sets, the same fire.
    BuildingSet wet(placed, plain_ground, style), windy(placed, plain_ground, style);
    BuildingHazards rainy = fire; rainy.rain_mm_h = style.rain_slowdown_mm_h;
    BuildingHazards gale = fire; gale.wind_speed_mps = 20.0;
    wet.advance(11.0, rainy); windy.advance(11.0, gale);
    require(wet.states()[0].element(E::Roof).damage < windy.states()[0].element(E::Roof).damage * 0.5, "rain slows the fire, wind drives it");
    rejects([&] { set.advance(-1.0, fire); }, "a negative step is rejected");
    rejects<std::out_of_range>([&] { set.ignite(9, E::Roof); }, "ignite checks the index");
    set.reset();
    require(!set.states()[0].destroyed && set.states()[0].integrity() == 1.0 && set.stats().alight == 0 && set.inside(0, bungalow.centre.x, bungalow.centre.y), "reset restores everything");
    (void)house;
  }
  {
    // Ignition by hand, and the spread between buildings: a torch at the bungalow's door.
    BuildingSet set(placed, plain_ground, style);
    require(!set.ignite(1, E::Walls, "torch") && !set.ignite(0, E::Windows, "torch"), "stone will not take a torch, nor an element that is not there");
    require(set.ignite(0, E::Door, "torch") && !set.ignite(0, E::Door, "torch") && set.states()[0].element(E::Door).cause == "torch", "the timber door takes it once");
    set.advance(8.0, {});
    const auto& bungalow = set.buildings()[0];
    const Vec2 beside = bungalow.door + bungalow.facing * 6.0, across = set.buildings()[1].door;
    require(set.exposure_at(beside.x, beside.y).heat > 0.5 && set.exposure_at(beside.x, beside.y).heat < 1.0 && set.exposure_at(across.x, across.y).heat == 0.0,
            "the blaze's heat fades over its reach: strong six metres before the door, gone at the house across the road");
    for (int i = 0; i < 120; ++i) set.advance(1.0, {});
    require(set.states()[0].destroyed, "left alone, the burning door takes the whole bungalow");
    require(set.states()[1].element(E::Door).damage == 0 && !set.states()[1].alight(), "the house across the road is beyond its reach");
    // Two bungalows side by side would not be: a second set with the house replaced by a bungalow next door.
    std::vector<BuildingRequest> pair{{BuildingKind::Bungalow, BuildingSite{0, 50.0, 1}}, {BuildingKind::Bungalow, BuildingSite{0, 66.0, 1}}};
    BuildingSet row(place_buildings(roads, pair, plain_ground, std::nullopt, {50, 0}, style), plain_ground, style);
    require(row.size() == 2 && row.ignite(0, E::Roof, "torch"), "two bungalows in a row, the first lit");
    for (int i = 0; i < 60 && !row.states()[1].alight(); ++i) row.advance(1.0, {});
    require(row.states()[1].alight() && row.states()[1].element(E::Roof).cause == "fire", "the fire jumps to the bungalow next door");
  }
  {
    // Impact: a trunk across the house smashes the tiles (the roof falls in) and cracks the walls; a concrete roof takes it.
    BuildingSet set(placed, plain_ground, style);
    const auto& house = set.buildings()[1];
    const auto& hospital = set.buildings()[2];
    const Vec2 start = house.centre - house.along * 20.0;
    require(set.log_landed(5, start, house.along, 30.0, false, 2000.0) == 1 && count(set, K::Hit, 1) == 1 && set.events()[0].tree == 5, "a two-tonne trunk across the house hits it");
    require(set.states()[1].roof_fallen && set.states()[1].element(E::Roof).cause == "impact" && count(set, K::RoofFell, 1) == 1, "the tiled roof is smashed and falls in");
    require(near(set.states()[1].element(E::Walls).damage, 0.5 * 2.0 / material_hazard(SurfaceMaterial::Stone).impact_tonnes, 1e-9) && !set.states()[1].destroyed, "the stone walls are cracked by half the mass");
    require(set.logs().size() == 1 && set.logs()[0].building == 1 && set.log_landed(5, start, house.along, 30.0, false, 2000.0) == 0, "the trunk lies on it once");
    require(set.log_landed(6, start + house.facing * 40.0, house.along, 30.0, false, 2000.0) == 0, "a trunk forty metres off misses");
    const Vec2 hs = hospital.centre - hospital.along * 30.0;
    require(set.log_landed(7, hs, hospital.along, 60.0, true, 2000.0) == 1 && near(set.states()[2].element(E::Roof).damage, 2.0 / material_hazard(SurfaceMaterial::Concrete).impact_tonnes, 1e-9) && !set.states()[2].roof_fallen,
            "a concrete roof is dented, not smashed");
    // Burning on the hospital's roof, the trunk heats it: concrete spalls slowly; on the house's it would have lit nothing (tiles).
    BuildingHazards log;
    log.log_intensity = [](std::uint32_t tree) { return tree == 7 ? 1.0 : 0.0; };
    const double before = set.states()[2].element(E::Roof).damage;
    set.advance(10.0, log);
    require(set.states()[2].element(E::Roof).damage > before && set.states()[2].element(E::Roof).cause == "heat" && set.logs()[1].burning, "the burning trunk spalls the concrete roof");
    log.log_intensity = [](std::uint32_t) { return 0.0; };
    set.advance(10.0, log);
    require(!set.logs()[1].burning, "burned out, the log heats nothing");
    std::map<std::string, std::size_t> parts;
    for (const auto& p : set.build_meshes()) ++parts[p.what];
    require(parts["roof"] == 2, "the house's roof is not drawn any more");
    rejects([&] { set.log_landed(1, start, house.along, 30.0, false, -5.0); }, "a negative mass is rejected");
    // An enormous trunk brings a house down outright.
    require(set.log_landed(8, start, house.along, 30.0, false, 20000.0) == 1 && set.states()[1].destroyed && set.states()[1].destroyed_by == "impact", "twenty tonnes flatten the house");
  }
  {
    // Flood: water over the floor soaks the room, the door and the planks; a torrent at the walls scours the foundation; water puts fire out.
    BuildingSet set(placed, plain_ground, style);
    const auto& bungalow = set.buildings()[0];
    BuildingHazards flood;
    flood.water = [](double, double, double ground_y) { FloodSample w; w.depth_meters = 0.6; w.surface_meters = ground_y + 0.6; return w; };
    {
      BuildingSet lit(placed, plain_ground, style);
      require(lit.ignite(0, E::Walls, "torch"), "the walls are lit before the water comes");
      lit.advance(0.5, {});
      require(lit.states()[0].element(E::Walls).alight && !lit.states()[0].element(E::Roof).alight, "burning");
      lit.advance(0.5, flood);
      require(!lit.states()[0].element(E::Walls).alight && count(lit, K::Extinguished, 0, E::Walls) == 1, "the water put the walls out");
      require(!lit.ignite(0, E::Interior, "torch") && !lit.ignite(0, E::Door, "torch") && lit.ignite(0, E::Roof, "torch"), "nothing under water can be lit; the roof, above it, can");
    }
    set.advance(1.0, flood);
    require(near(set.states()[0].flood_depth_meters, 0.45) && count(set, K::Flooded, 0) == 1 && set.stats().flooded == 3, "water 0.45 m over the bungalow's floor (the other two are flooded too)");
    set.advance(450.0, flood);
    require(near(set.states()[0].element(E::Interior).damage, 451.0 / material_hazard(SurfaceMaterial::Timber).soak_seconds, 1e-6) && set.states()[0].element(E::Interior).cause == "flood",
            "the furniture rots in its soak time");
    require(set.states()[0].element(E::Door).damage > 0.4 && set.states()[0].element(E::Walls).damage > 0.4 && set.states()[0].element(E::Walls).cause == "flood", "so do the door and the planks");
    require(set.states()[1].element(E::Walls).damage == 0 && set.states()[1].element(E::Door).damage > 0.4, "the house's stone stands in water, its timber door rots");
    require(set.states()[2].element(E::Walls).damage == 0, "concrete does not mind");
    for (int i = 0; i < 1000 && !set.states()[0].destroyed; ++i) set.advance(1.0, flood);
    require(set.states()[0].destroyed && set.states()[0].destroyed_by == "flood" && count(set, K::WashedOut, 0) == 1, "rotted through, the plank walls give: the bungalow goes");
    // A torrent at the walls takes any building in the scour time.
    BuildingSet scour(placed, plain_ground, style);
    BuildingHazards torrent;
    torrent.water = [&](double x, double z, double ground_y) {
      FloodSample w;
      if ((Vec2{x, z} - set.buildings()[2].centre).length() < 40.0) { w.depth_meters = 1.5; w.surface_meters = ground_y + 1.5; w.current_mps = 4.0; w.current = {1, 0}; w.channel = true; }
      else w.surface_meters = ground_y;
      return w;
    };
    scour.advance(style.flood_scour_seconds - 1.0, torrent);
    require(!scour.states()[2].destroyed && near(scour.states()[2].scour_seconds, style.flood_scour_seconds - 1.0), "the torrent scours the hospital's foundation");
    scour.advance(2.0, torrent);
    require(scour.states()[2].destroyed && scour.states()[2].destroyed_by == "flood" && !scour.states()[0].destroyed, "and the concrete hospital goes with it; the bungalow, dry, stands");
    std::vector<FireInstance> flames, smoke;
    scour.emit(flames, smoke, FireStyle{});
    require(flames.empty() && smoke.empty(), "a washed-out building does not smoulder");
    require(!scour.platform_height(bungalow.centre.x + 1000, bungalow.centre.y), "(sanity) nothing far off");
  }
  {
    // Wind: a gale strips the thatch, not the tiles; the windows hold below their wind.
    BuildingSet set(placed, plain_ground, style);
    BuildingHazards gale; gale.wind_speed_mps = 35.0;
    set.advance(30.0, gale);
    require(set.states()[0].element(E::Roof).damage > 0.5 && set.states()[0].element(E::Roof).cause == "wind" && set.states()[1].element(E::Roof).damage == 0 && set.states()[1].element(E::Windows).damage == 0,
            "thatch tears loose at 35 m/s, tiles and glass hold");
    set.advance(30.0, gale);
    require(set.states()[0].roof_fallen && !set.states()[0].destroyed, "the thatch is stripped: open to the sky, the walls standing");
    gale.wind_speed_mps = 42.0;
    set.advance(60.0, gale);
    require(set.states()[1].element(E::Roof).damage > 0.5 && set.states()[1].element(E::Windows).damage >= 1.0, "at 42 m/s the tiles go and the glass is smashed by debris");
  }
  {
    // Lightning: a strike lights the thatch, scorches the tiles, misses far off; the tallest draws the bolt.
    BuildingSet set(placed, plain_ground, style);
    const auto& bungalow = set.buildings()[0];
    const auto& house = set.buildings()[1];
    require(set.strike(bungalow.centre.x + 0.5 * bungalow.spec.width_meters + 5.0, bungalow.centre.y) == 0 && set.states()[0].strikes == 1 && set.states()[0].element(E::Roof).alight &&
            set.states()[0].element(E::Roof).cause == "lightning" && count(set, K::Struck, 0) == 1, "a strike five metres from the bungalow's wall lights its thatch");
    require(set.strike(house.centre.x, house.centre.y) == 1 && near(set.states()[1].element(E::Roof).damage, style.strike_scorch, 1e-9) && set.states()[1].element(E::Roof).cause == "lightning" && !set.states()[1].alight(),
            "a strike on the house scorches its tiles");
    require(!set.strike(house.centre.x, house.centre.y + 500.0), "a strike far off hits nothing");
    require(set.sheltered(bungalow.centre.x, bungalow.centre.y) && set.sheltered(house.centre.x, house.centre.y) && !set.sheltered(set.buildings()[2].centre.x, set.buildings()[2].centre.y),
            "inside the bungalow and the house is under a roof (the hospital cannot be entered)");
    rejects([&] { set.strike(std::nan(""), 0); }, "a strike needs a finite position");
    // The hospital (five storeys) draws a bolt that lands between it and a spot nearer the house.
    const Vec2 mid = house.centre + (set.buildings()[2].centre - house.centre) * 0.5;
    (void)mid;
    set.destroy(2, "test");
    require(set.states()[2].destroyed && set.states()[2].destroyed_by == "test" && count(set, K::Collapsed, 2) == 1, "destroy brings a building down outright");
  }
}

// The house: three rooms downstairs, a stair, the bedroom upstairs; the walker climbs.
void test_house() {
  const auto spec = building_spec(BuildingKind::House);
  const auto plan = interior_plan(spec);
  const double iw = 0.5 * spec.width_meters - kInteriorWallThickness, id = 0.5 * spec.depth_meters - kInteriorWallThickness;
  std::map<std::string, InteriorRoom> rooms;
  for (const auto& r : plan.rooms) rooms[r.name] = r;
  require(rooms.size() == 5 && rooms.count("living room") && rooms.count("kitchen") && rooms.count("bathroom") && rooms.count("stairs") && rooms.count("bedroom"), "five rooms");
  require(rooms["living room"].level == 0 && rooms["kitchen"].level == 0 && rooms["bathroom"].level == 0 && rooms["bedroom"].level == 1, "three downstairs, the bedroom up");
  require(rooms["living room"].b1 == id && rooms["kitchen"].b0 == -id && rooms["bathroom"].b0 == -id && rooms["kitchen"].a1 < rooms["bathroom"].a0, "the living room at the front, the kitchen and the bathroom behind it, left and right");
  require(near(rooms["bedroom"].a0, -iw) && near(rooms["bedroom"].a1, iw) && near(rooms["bedroom"].b0, -id) && near(rooms["bedroom"].b1, id), "the bedroom is the whole upper floor");
  require(plan.walls.size() == 2 && plan.walls[0].openings.size() == 2 && plan.walls[1].openings.empty() && near(plan.walls[0].top, spec.storey_height_meters - plan.slab),
          "a partition with two doorways behind the living room, a blind one between the kitchen and the bathroom, up to the floor above");
  require(plan.stair() && near(plan.stair()->rise, spec.storey_height_meters) && plan.stair()->width == 1.0 && plan.stair()->foot.y > plan.stair()->head.y && near(plan.stair()->foot.x, -iw + 0.5),
          "a straight flight up the left wall from the front toward the back, rising a storey");
  require(plan.rails.size() == 1 && plan.rails[0].top > spec.storey_height_meters && plan.rails[0].bottom < 0, "a handrail on the open side, running up both storeys");
  require(plan.floors == 2 && near(plan.floor_height, spec.storey_height_meters), "two floors a storey apart");
  // Furniture: every piece within its room, upstairs pieces on the upper floor.
  const auto pieces = furnish(spec);
  std::map<std::string, int> kinds;
  const auto in_room = [&](const Furnishing& f, const char* name) {
    const auto& r = rooms[name];
    return f.centre.x - f.half_along >= r.a0 - 1e-6 && f.centre.x + f.half_along <= r.a1 + 1e-6 && f.centre.y - f.half_across >= r.b0 - 1e-6 && f.centre.y + f.half_across <= r.b1 + 1e-6;
  };
  for (const auto& f : pieces) {
    ++kinds[f.what];
    const std::string what = f.what;
    const bool upstairs = f.bottom >= spec.storey_height_meters;
    if (what == "bed" || what == "mattress" || what == "wardrobe" || what == "blanket" || what == "pillow") require(upstairs && in_room(f, "bedroom"), "the bed and the wardrobe are in the bedroom upstairs");
    else if (what == "toilet" || what == "washbasin" || what == "tap") require(!upstairs && in_room(f, "bathroom"), "the toilet and the basin are in the bathroom");
    else if (what == "cupboard" || what == "worktop" || what == "sink" || what == "stove" || what == "table" && f.half_along > 0.6 || what == "chair") require(!upstairs && in_room(f, "kitchen") || what == "table" && in_room(f, "living room"), "the cooking area, the table and the chairs are in the kitchen");
    else if (what == "sofa" || what == "television" || what == "stand") require(!upstairs && in_room(f, "living room"), "the sofa and the television are in the living room");
    if (f.solid) require(f.bottom - (upstairs ? spec.storey_height_meters : 0.0) + f.height > 0.5, "solid pieces stand higher than the step height over their floor");
  }
  require(kinds["stove"] == 1 && kinds["sink"] == 1 && kinds["cupboard"] == 2 && kinds["chair"] >= 24 && kinds["cabinet"] == 2 && kinds["toilet"] == 3 && kinds["washbasin"] == 3 &&
          kinds["television"] == 2 && kinds["stand"] == 1 && kinds["sofa"] == 5 && kinds["bed"] == 1 && kinds["wardrobe"] == 1, "the pieces asked for");
  // The set: levels, rooms and a walker on the stairs.
  RoadStyle road_style;
  RoadNetwork roads(road_def(), road_style, plain_ground);
  BuildingPlacementStyle style;
  std::vector<BuildingRequest> one{{BuildingKind::House, BuildingSite{0, 50.0, 1}}};
  BuildingSet set(place_buildings(roads, one, plain_ground, std::nullopt, {50, 0}, style), plain_ground, style);
  require(set.size() == 1 && set.stats().interiors == 1, "the house is an interior");
  const auto& h = set.buildings()[0];
  const auto at = [&](double a, double b) { return h.centre + h.along * a + h.facing * b; };
  const double floor = h.floor_meters, upper = floor + spec.storey_height_meters;
  const Vec2 living = at(2.0, 2.0), kitchen = at(-2.0, -2.5), bath = at(3.0, -2.5), stair_mid = at(-iw + 0.5, 0.5 * (plan.stair()->foot.y + plan.stair()->head.y)), stair_head = at(-iw + 0.5, plan.stair()->head.y - 0.3);
  require(near(*set.platform_height(living.x, living.y, floor), floor) && near(*set.platform_height(living.x, living.y, upper), upper) && near(*set.platform_height(living.x, living.y, floor + 1.0), floor),
          "on the ground floor the floor, upstairs the upper floor, a jump's height up still the floor");
  require(near(*set.platform_height(stair_mid.x, stair_mid.y, floor + 1.4), floor + 0.5 * spec.storey_height_meters) && near(*set.platform_height(stair_mid.x, stair_mid.y, floor), floor),
          "the stair is a ramp half way up at its middle; from the floor below it, the floor");
  require(near(*set.platform_height(stair_head.x, stair_head.y, upper), upper), "past the head the upper floor");
  require(std::string(set.room_at(0, living.x, living.y, floor)) == "living room" && std::string(set.room_at(0, kitchen.x, kitchen.y, floor)) == "kitchen" &&
          std::string(set.room_at(0, bath.x, bath.y, floor)) == "bathroom" && std::string(set.room_at(0, stair_mid.x, stair_mid.y, floor + 1.4)) == "stairs" &&
          std::string(set.room_at(0, living.x, living.y, upper)) == "bedroom" && !set.room_at(0, h.door.x + h.facing.x * 3, h.door.y + h.facing.y * 3, floor), "the rooms by position and level");
  rejects<std::out_of_range>([&] { set.room_at(3, 0, 0, 0); }, "room_at checks the index");
  // Walker: through the doorway into the kitchen, stopped by the blind partition, up the stairs to the bedroom, down again.
  ObstacleField walls(set.obstacles()), doors(set.door_obstacles());
  WalkEnvironment env;
  env.standing_height = plain_ground;
  env.lower = {-100, 0, -500}; env.upper = {1100, 0, 500};
  env.obstacles = {&walls, &doors};
  env.platforms = [&](double x, double z, double feet_y) { return set.platform_height(x, z, feet_y); };
  env.gait.enabled = false;
  FirstPersonCamera walker;
  const auto stand = [&](Vec2 p, double y, Vec2 toward) { walker.feet = {p.x, y, p.y}; walker.grounded = true; walker.yaw = std::atan2(-toward.x, -toward.y); walker.velocity_mps = {}; walker.gait_speed_mps = 0; };
  const auto go = [&](double seconds) { for (int i = 0; i < static_cast<int>(seconds * 60); ++i) walk(walker, WalkInput{1, 0, false}, 1.0 / 60.0, env); };
  const auto local = [&]() { const Vec2 f{walker.feet.x, walker.feet.z}; return std::pair{(f - h.centre).dot(h.along), (f - h.centre).dot(h.facing)}; };
  // From the living room straight at the kitchen doorway.
  const double doorway_a = -iw + plan.walls[0].openings[0].along;
  stand(at(doorway_a, 2.0), floor, h.facing * -1.0);
  go(3.0);
  require(std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "kitchen" && near(walker.feet.y, floor, 1e-6), "through the doorway into the kitchen");
  // At the blind partition from the kitchen side.
  stand(at(plan.walls[1].from.x - 1.5, -2.5), floor, h.along);
  go(2.0);
  require(local().first < plan.walls[1].from.x - 0.5 && std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "kitchen", "the partition keeps the kitchen from the bathroom");
  // Up the stairs: from the foot, walking toward the back.
  stand(at(-iw + 0.5, plan.stair()->foot.y + 0.3), floor, h.facing * -1.0);
  double highest = floor;
  for (int i = 0; i < 60 * 6; ++i) { walk(walker, WalkInput{1, 0, false}, 1.0 / 60.0, env); highest = std::max(highest, walker.feet.y); }
  require(near(walker.feet.y, upper, 1e-6) && local().second < plan.stair()->head.y && std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "bedroom",
          "up the stairs onto the upper floor, in the bedroom");
  // The bedroom's wardrobe stops the walker; the banister keeps it out of the stairwell.
  stand(at(iw - 2.0, id - 0.95), upper, h.along);
  go(2.0);
  require(local().first < iw - 0.62 - 0.3 && near(walker.feet.y, upper, 1e-6), "the wardrobe stops the walker upstairs");
  stand(at(-iw + 2.0, 2.0), upper, h.along * -1.0);
  go(2.0);
  require(local().first > -iw + 1.0 && near(walker.feet.y, upper, 1e-6), "the banister keeps the walker out of the stairwell");
  // Down again: from the bedroom over the head of the stairs and down the flight.
  stand(at(-iw + 0.5, plan.stair()->head.y - 1.0), upper, h.facing);
  go(8.0);
  require(near(walker.feet.y, floor, 0.05) && local().second > plan.stair()->foot.y - 0.1 && walker.grounded, "down the stairs to the ground floor (stopped at the front wall)");
  // Downstairs under the upper floor the bed does not block, and the sofa does.
  stand(at(iw - 2.75, -id + 1.05 + 2.5), floor, h.facing * -1.0);
  go(2.0);
  require(local().second < -id + 1.05 + 0.5, "the bed upstairs is no obstacle downstairs");
  const double lb = 0.5 * (plan.walls[0].from.y + 0.5 * kInteriorWallThickness + id) + 0.7;
  stand(at(iw - 2.85, lb + 2.5), floor, h.facing * -1.0);
  go(2.0);
  require(local().second > lb + 1.1 + 0.3, "the sofa stops the walker in the living room");
  (void)highest;
}

// The skyscraper: the gallery, the concrete stairwell up every storey, the lift, four apartments a floor.
void test_tower() {
  const auto spec = building_spec(BuildingKind::Skyscraper);
  const auto plan = interior_plan(spec);
  const double iw = 0.5 * spec.width_meters - kInteriorWallThickness;
  require(plan.floors == static_cast<int>(spec.storeys) && near(plan.floor_height, spec.storey_height_meters), "a level per storey");
  std::map<std::string, int> names;
  int apartments_rooms = 0;
  for (const auto& r : plan.rooms) { ++names[r.name]; if (std::string(r.name) == "living room" || std::string(r.name) == "kitchen" || std::string(r.name) == "bedroom" || std::string(r.name) == "bathroom") ++apartments_rooms; }
  require(names["gallery"] == 1 && names["living room"] == 4 * (plan.floors - 1) && names["kitchen"] == 4 * (plan.floors - 1) && names["bedroom"] == 4 * (plan.floors - 1) && names["bathroom"] == 4 * (plan.floors - 1) &&
          names["stairwell"] == plan.floors && names["lift"] == plan.floors && names["hall"] == 4 * (plan.floors - 1), "the gallery, four apartments of four rooms on every floor above, halls, the stairwell and the lift on every floor");
  require(plan.flights.size() == 2 * static_cast<std::size_t>(plan.floors - 1) && plan.landings.size() == static_cast<std::size_t>(plan.floors - 1) && plan.holes.size() == 2 * static_cast<std::size_t>(plan.floors - 1),
          "two flights and a landing a storey, a stairwell hole and a shaft hole through every upper floor");
  for (std::size_t k = 0; k + 1 < plan.flights.size(); k += 2)
    require(near(plan.flights[k].rise, 0.5 * spec.storey_height_meters) && near(plan.flights[k + 1].base, plan.flights[k].base + plan.flights[k].rise) && near(plan.flights[k].base, plan.level(static_cast<int>(k / 2))),
            "a flight climbs half a storey to the landing, the next climbs on from it");
  require(plan.lift && plan.lift->speed_mps > 0 && plan.walls.size() > 7 + 4 * 4, "a lift, the core's walls and every apartment's");
  bool concrete = false, per_floor_doors = false;
  for (const auto& w : plan.walls) { if (w.material == SurfaceMaterial::Concrete) concrete = true; if (w.openings.size() >= 2 * static_cast<std::size_t>(plan.floors)) per_floor_doors = true; }
  require(concrete && per_floor_doors, "the core is concrete, its front face has a stairwell door and a lift door on every floor");
  // Furniture: a reception desk in the gallery, four apartments' worth per floor above.
  const auto pieces = furnish(spec);
  std::map<std::string, int> kinds;
  int upstairs = 0;
  for (const auto& f : pieces) { ++kinds[f.what]; if (f.bottom >= spec.storey_height_meters - 1e-6) ++upstairs; }
  require(kinds["desk"] == 2 && kinds["stove"] == 4 * (plan.floors - 1) && kinds["toilet"] == 3 * 4 * (plan.floors - 1) && kinds["bed"] == 4 * (plan.floors - 1) && kinds["wardrobe"] == 4 * (plan.floors - 1) &&
          kinds["television"] == 2 * (4 * (plan.floors - 1) + 2) && upstairs > 3000, "the desk, and a kitchen, a bathroom, a bedroom and a living room in every apartment");
  // The set: the levels, the lift, a walker up the stairs and into an apartment.
  RoadStyle road_style;
  RoadNetwork roads(road_def(), road_style, plain_ground);
  BuildingPlacementStyle style;
  std::vector<BuildingRequest> one{{BuildingKind::Skyscraper, BuildingSite{2, 300.0, 1}}};
  BuildingSet set(place_buildings(roads, one, plain_ground, std::nullopt, {300, 0}, style), plain_ground, style);
  require(set.size() == 1 && set.plan(0).floors == plan.floors, "the tower stands, its plan cached");
  const auto& t = set.buildings()[0];
  const auto at = [&](double a, double b) { return t.centre + t.along * a + t.facing * b; };
  const double floor = t.floor_meters, storey = spec.storey_height_meters;
  const auto& flight_a = plan.flights[0];
  const auto& flight_b = plan.flights[1];
  const auto& landing = plan.landings[0];
  const Vec2 gallery = at(8.0, 8.0), a_mid = at(flight_a.foot.x, 0.5 * (flight_a.foot.y + flight_a.head.y)), landing_mid = at(0.5 * (landing.a0 + landing.a1), 0.5 * (landing.b0 + landing.b1));
  require(near(*set.platform_height(gallery.x, gallery.y, floor), floor) && near(*set.platform_height(gallery.x, gallery.y, floor + storey), floor + storey) &&
          near(*set.platform_height(gallery.x, gallery.y, floor + 7 * storey + 0.3), floor + 7 * storey), "a floor on every level");
  require(near(*set.platform_height(a_mid.x, a_mid.y, floor + 0.9), floor + 0.25 * storey) && near(*set.platform_height(landing_mid.x, landing_mid.y, floor + 1.9), floor + 0.5 * storey) &&
          near(*set.platform_height(a_mid.x, a_mid.y, floor + storey + 0.9), floor + 1.25 * storey), "the flights and landings, storey over storey");
  require(std::string(set.room_at(0, gallery.x, gallery.y, floor)) == "gallery" && std::string(set.room_at(0, a_mid.x, a_mid.y, floor + 0.9)) == "stairs" && std::string(set.room_at(0, landing_mid.x, landing_mid.y, floor + 1.9)) == "stairs" &&
          set.level_at(0, floor + 3 * storey + 0.2) == 3 && set.level_at(0, floor + 0.2) == 0, "the rooms and the levels by the feet");
  // The lift: at the ground floor with its doors open; called to the fourth floor it closes, travels and opens there; ridden down a floor.
  const auto& lift = *plan.lift;
  const Vec2 car = at(0.5 * (lift.a0 + lift.a1), 0.5 * (lift.b0 + lift.b1)), shaft_door = at(lift.door_along, lift.b1 + 0.5);
  require(set.lift_state(0) && set.lift_state(0)->floor == 0 && set.lift_state(0)->open && !set.lift_state(0)->moving && set.in_lift(0, car.x, car.y) && !set.in_lift(0, gallery.x, gallery.y), "the car waits at the ground floor, open");
  require(set.lift_door_near(shaft_door.x, shaft_door.y, floor, 2.0) == 0 && !set.lift_door_near(gallery.x, gallery.y, floor, 2.0) && !set.lift_door_near(shaft_door.x, shaft_door.y, floor + 1.5, 2.0),
          "the shaft door is found from the hall at a floor's level, not from the stairs");
  require(std::string(set.room_at(0, car.x, car.y, floor)) == "lift" && near(*set.platform_height(car.x, car.y, floor), floor), "in the car, on its floor");
  {
    ObstacleField closed(set.door_obstacles());
    std::size_t doors_at_shaft = 0;
    for (const auto& o : closed.obstacles()) if (std::hypot(o.x - shaft_door.x, o.z - shaft_door.y) < 1.5) ++doors_at_shaft;
    require(doors_at_shaft > 0, "the shaft doors on the other floors stand closed");
  }
  require(!set.call_lift(0, plan.floors) && !set.call_lift(0, -1) && set.call_lift(0, 4) && set.lift_state(0)->moving && !set.lift_state(0)->open && set.lift_state(0)->target == 4, "called to the fourth floor");
  const auto version = set.door_version();
  set.advance(1.0);
  require(near(set.lift_state(0)->height, lift.speed_mps) && set.door_version() != version, "travelling at its speed");
  require(near(*set.platform_height(car.x, car.y, floor + lift.speed_mps), floor + lift.speed_mps), "the car is the surface in the shaft as it goes");
  for (int i = 0; i < 600 && set.lift_state(0)->moving; ++i) set.advance(0.05);
  require(!set.lift_state(0)->moving && set.lift_state(0)->open && set.lift_state(0)->floor == 4 && near(set.lift_state(0)->height, 4 * storey), "arrived and open on the fourth floor");
  require(set.ride_lift(0, -1) && set.lift_state(0)->target == 3, "ridden down a floor");
  for (int i = 0; i < 600 && set.lift_state(0)->moving; ++i) set.advance(0.05);
  require(set.lift_state(0)->floor == 3 && !set.ride_lift(0, -4), "on the third floor; it cannot go under the ground floor");
  require(set.call_lift(0, 3) && !set.lift_state(0)->moving, "called where it is, it stays");
  // A walker: carried up in the car, up the stairs to the first floor, into an apartment.
  ObstacleField walls(set.obstacles()), doors(set.door_obstacles());
  WalkEnvironment env;
  env.standing_height = plain_ground;
  env.lower = {-100, 0, -500}; env.upper = {1100, 0, 500};
  env.obstacles = {&walls, &doors};
  env.platforms = [&](double x, double z, double feet_y) { return set.platform_height(x, z, feet_y); };
  env.gait.enabled = false;
  FirstPersonCamera walker;
  const auto stand = [&](Vec2 p, double y, Vec2 toward) { walker.feet = {p.x, y, p.y}; walker.grounded = true; walker.yaw = std::atan2(-toward.x, -toward.y); walker.velocity_mps = {}; walker.gait_speed_mps = 0; };
  const auto go = [&](double seconds, double forward = 1.0) { for (int i = 0; i < static_cast<int>(seconds * 60); ++i) walk(walker, WalkInput{forward, 0, false}, 1.0 / 60.0, env); };
  set.call_lift(0, 0);
  for (int i = 0; i < 600 && set.lift_state(0)->moving; ++i) set.advance(0.05);
  doors = ObstacleField(set.door_obstacles());
  stand(car, floor, t.facing);
  set.call_lift(0, 2);
  for (int i = 0; i < 60 * 8 && set.lift_state(0)->moving; ++i) { set.advance(1.0 / 60.0); doors = ObstacleField(set.door_obstacles()); walk(walker, WalkInput{}, 1.0 / 60.0, env); }
  require(near(walker.feet.y, floor + 2 * storey, 1e-6) && walker.grounded && std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "lift", "the car carries the standing walker up two floors");
  // Out of the car through its open door into the hall, on the second floor.
  go(3.0);
  require(!set.in_lift(0, walker.feet.x, walker.feet.z) && set.level_at(0, walker.feet.y) == 2 && std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "hall", "out into the second floor's hall");
  // Up the stairs from the ground floor: the first flight, the landing, the second flight, the first floor.
  stand(at(flight_a.foot.x, flight_a.foot.y + 0.2), floor, t.facing * -1.0);
  go(5.0);
  require(std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "stairs" && walker.feet.y > floor + 0.4 * storey && walker.feet.y <= floor + 0.5 * storey + 1e-6, "up the first flight to the landing");
  stand({walker.feet.x, walker.feet.z}, walker.feet.y, t.along);
  go(1.5);
  stand({walker.feet.x, walker.feet.z}, walker.feet.y, t.facing);
  go(5.0);
  require(near(walker.feet.y, floor + storey, 1e-6) && set.level_at(0, walker.feet.y) == 1, "across the landing and up the second flight to the first floor");
  stand({walker.feet.x, walker.feet.z}, walker.feet.y, t.facing);
  go(2.0);
  require(std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "hall" && near(walker.feet.y, floor + storey, 1e-6), "through the stairwell door into the first floor's hall");
  // Into the front-left apartment through its door, and on into its kitchen through the doorway.
  const InteriorRoom* living = nullptr;
  for (const auto& r : plan.rooms) if (std::string(r.name) == "living room" && r.level == 1 && r.a1 < 0 && r.b0 > 0) living = &r;
  require(living != nullptr, "the front-left apartment's living room");
  const double door_a = living->a1 - 1.5;  // the door is 1.5 m from the inner corner along the hall-side wall
  stand(at(door_a, living->b0 - 1.5), floor + storey, t.facing);
  go(3.0);
  require(std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "living room", "through the apartment's door into its living room");
  stand(at(living->a0 + 1.0, 0.5 * (living->b0 + living->b1)), floor + storey, t.along * -1.0);
  go(4.0);
  require(std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "kitchen", "through the doorway into the kitchen");
  // The apartment's wall to the hall holds where there is no door.
  stand(at(living->a0 + 2.0, living->b0 - 1.5), floor + storey, t.facing);
  go(2.0);
  require(std::string(set.room_at(0, walker.feet.x, walker.feet.z, walker.feet.y)) == "hall", "the wall beside the door holds");
  rejects<std::out_of_range>([&] { set.call_lift(3, 1); }, "call_lift checks the index");
}

}  // namespace

int main() {
  try {
    test_specs();
    test_placement();
    test_json();
    test_set_and_walker();
    test_meshes();
    test_vegetation();
    test_civic_and_airport_kinds();
    test_interior();
    test_hazards();
    test_house();
    test_tower();
    std::cout << "TerraFlux buildings: 11 tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "TerraFlux buildings test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
