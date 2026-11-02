#include <terraflux/graphics/buildings.hpp>
#include <terraflux/graphics/city.hpp>
#include <terraflux/graphics/roads.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace terraflux;
using namespace terraflux::graphics;
using terraflux::math::Vec2;

namespace {
constexpr double kPi = 3.14159265358979323846;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template <class E = std::invalid_argument, class F> void rejects(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const E&) { caught = true; }
  require(caught, message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) < tolerance; }

// A gently rolling plain over 4 x 4 km with a lake in its north-east quarter.
const StandingHeightFn rolling = [](double x, double z) -> std::optional<double> {
  if (x < -2000 || x > 2000 || z < -2000 || z > 2000) return std::nullopt;
  const double lake = std::hypot(x - 900, z + 900);
  if (lake < 350) return 30.0 - 12.0 * (1.0 - lake / 350.0);
  return 50.0 + 6.0 * std::sin(x / 400.0) * std::cos(z / 500.0);
};
constexpr double kWater = 32.0;

CityPlan test_plan() {
  CityPlan plan;
  plan.lower = {-2000, -2000}; plan.upper = {2000, 2000};
  plan.zones = {
      {ZoneKind::Cbd, "Central", {0, 0}, 240.0, 0.1},
      {ZoneKind::Suburb, "North", {0, -1000}, 170.0, 0.2},
      {ZoneKind::Suburb, "West", {-1100, 300}, 160.0, -0.1},
      {ZoneKind::Suburb, "South", {0, 1050}, 160.0, 0.3},
      {ZoneKind::Suburb, "East", {1150, 300}, 150.0, 0.0},
      {ZoneKind::Suburb, "Hill", {-1000, -900}, 130.0, 0.5},
      {ZoneKind::Airport, "Airport", {-900, 1300}, 400.0, 0.15},
  };
  // A ring highway around the CBD and a spur to the west edge that joins it at a T.
  std::vector<Vec2> ring;
  for (int k = 0; k <= 8; ++k) { const double a = k * 2.0 * kPi / 8.0; ring.push_back({700 * std::cos(a), 700 * std::sin(a)}); }
  plan.highways = {ring, {{-700, 0}, {-1700, 0}}};
  return plan;
}

void test_plan_validation() {
  validate_city_plan(test_plan());
  rejects([] { auto p = test_plan(); p.zones[0].radius_meters = 10; validate_city_plan(p); }, "a ten-metre zone is rejected");
  rejects([] { auto p = test_plan(); p.zones[0].name.clear(); validate_city_plan(p); }, "a nameless zone is rejected");
  rejects([] { auto p = test_plan(); p.zones.push_back({ZoneKind::Airport, "Second", {500, 500}, 300.0, 0}); validate_city_plan(p); }, "two airports are rejected");
  rejects([] { auto p = test_plan(); p.warp_amplitude_meters = 40; p.suburb_block_meters = 60; validate_city_plan(p); }, "a warp that lets lines meet is rejected");
  rejects([] { auto p = test_plan(); p.highways.push_back({{0, 0}}); validate_city_plan(p); }, "a one-corner highway is rejected");
  rejects([] { auto p = test_plan(); p.interchange_lift_meters = 1; validate_city_plan(p); }, "a one-metre viaduct is rejected");
  require(std::string(zone_kind_name(ZoneKind::Cbd)) == "cbd" && std::string(zone_kind_name(ZoneKind::Airport)) == "airport", "zone names");
  rejects([] { auto p = test_plan(); p.highways[0][0] = {5000, 0}; plan_city(p, rolling, kWater, RoadStyle{}, RoadPlanStyle{}); }, "a highway corner outside the bounds is rejected");
  rejects([] { plan_city(test_plan(), {}, kWater, RoadStyle{}, RoadPlanStyle{}); }, "planning needs a ground function");
}

void test_city() {
  RoadStyle road_style;
  RoadPlanStyle plan_style;
  plan_style.max_span_meters = 1000;
  const auto plan = test_plan();
  const auto city = plan_city(plan, rolling, kWater, road_style, plan_style);
  const auto& st = city.stats;
  require(st.zones == 7 && st.cbds == 1 && st.suburbs == 5 && st.airports == 1, "a CBD, five suburbs and an airport");
  require(st.interchanges >= 3, "at least three zones get a diamond interchange on the ring");
  require(st.interchanges + st.at_grade_junctions == 7, "every zone is joined to the highways or to a connected zone");
  require(st.lots > 150, "hundreds of lots are parcelled along the block edges");
  for (const auto& request : city.buildings) require(request.site.has_value() || request.preferred.has_value(), "every request is a lot or a civic building near a zone centre");
  // The network is valid (connected, no stand-alone bridge) and carries every class.
  RoadNetwork roads(city.roads, road_style, rolling);
  const auto rs = roads.stats();
  require(rs.highways > 20 && rs.ramps >= 6 && rs.avenues >= 6 && rs.streets > 60, "highways, ramps, avenues and streets");
  require(rs.runways >= 1 && rs.taxiways >= 3 && rs.aprons >= 1 && near(rs.runway_meters, std::min(plan.runway_length_meters, 2.0 * 400.0 * 0.95), 1e-6), "the airport's runway, taxiways and apron");
  require(rs.curve_joints > 40, "the streets and the highway arcs curve through mitred joints");
  require(rs.bridges >= 3 * 4 && rs.highest_deck_meters >= plan.interchange_lift_meters - 0.5, "the interchanges' viaducts are chains of bridges lifted over the avenues");
  std::size_t lifted = 0;
  for (const auto& n : city.roads.nodes) if (near(n.lift_meters, plan.interchange_lift_meters)) ++lifted;
  require(lifted == st.interchanges, "one crest node per diamond");
  // Every ramp starts on the highway and ends on an avenue; the avenue passes under the crest.
  for (const auto& ic : city.interchanges) {
    if (!ic.diamond) continue;
    const auto ground_under = *rolling(ic.at.x, ic.at.y);
    const auto on_deck = roads.sample(ic.at.x, ic.at.y, ground_under + plan.interchange_lift_meters);
    require(on_deck.on && on_deck.cls == RoadClass::Highway && on_deck.kind == RoadKind::Bridge, "standing at the crest's height, the crest is highway deck");
    const auto below = roads.sample(ic.at.x, ic.at.y, ground_under);
    require(below.on && below.cls == RoadClass::Avenue && below.kind == RoadKind::Road, "standing on the ground, it is the avenue passing under");
    require(*roads.platform_height(ic.at.x, ic.at.y, ground_under + plan.interchange_lift_meters) > ground_under + plan.interchange_lift_meters - 0.5 &&
            *roads.platform_height(ic.at.x, ic.at.y, ground_under) < ground_under + 1.0, "the platform follows the feet: deck above, avenue below");
    bool avenue_under = false;
    roads.for_each_segment_near(ic.at.x, ic.at.y, 5.0, [&](std::uint32_t i) {
      if (city.roads.segments[i].cls != RoadClass::Avenue) return;
      const auto a = city.roads.nodes[city.roads.segments[i].a].position, b = city.roads.nodes[city.roads.segments[i].b].position;
      const auto d = b - a; const double len = d.length();
      const double along = std::clamp((ic.at - a).dot(d) / len, 0.0, len);
      if ((a + d * (along / len) - ic.at).length() < 2.0) avenue_under = true;
    });
    require(avenue_under, "the zone's avenue runs under the viaduct");
  }
  // The T-junction: the spur's first corner lies on a filleted ring corner, so it is snapped to the arc and joins there.
  std::vector<std::size_t> degree(city.roads.nodes.size(), 0);
  for (const auto& seg : city.roads.segments) { ++degree[seg.a]; ++degree[seg.b]; }
  bool spur_joins = false;
  for (std::size_t n = 0; n < city.roads.nodes.size(); ++n)
    if ((city.roads.nodes[n].position - Vec2{-700, 0}).length() < 80 && degree[n] >= 3) spur_joins = true;
  require(spur_joins, "the spur joins the ring near its corner");
  // Buildings: the lots are placed (the unsuitable ones skipped), the CBD is mostly skyscrapers, police stations and the airport's buildings stand.
  BuildingPlacementStyle style;
  PlacementReport report;
  const auto placed = place_buildings(roads, city.buildings, rolling, kWater, {0, 0}, style, true, &report);
  require(report.placed == placed.size() && placed.size() > 100, "most lots are built");
  BuildingSet set(placed, rolling, style);
  const auto bs = set.stats();
  std::size_t cbd_towers = 0, cbd_total = 0;
  for (const auto& b : placed) if ((b.centre - plan.zones[0].centre).length() < plan.zones[0].radius_meters + 60) { ++cbd_total; if (b.kind == BuildingKind::Skyscraper) ++cbd_towers; }
  require(cbd_towers >= 10 && cbd_towers * 10 >= cbd_total * 8, "the CBD is mostly skyscrapers");
  require(bs.hospitals == 1 && bs.police_stations >= 3, "a hospital and police stations (the CBD's and every other suburb's)");
  require(bs.terminals == 1 && bs.towers == 1 && bs.hangars >= 1, "the airport has its terminal, control tower and hangars");
  require(bs.houses + bs.bungalows > 60, "the suburbs are houses and bungalows");
  // Every building stands beside a road segment with its path meeting the carriageway.
  for (const auto& b : placed) {
    require(b.site.segment < roads.size() && city.roads.segments[b.site.segment].kind == RoadKind::Road, "a building's site is a road segment");
    require(roads.sample(b.path_end.x + b.facing.x * 0.1, b.path_end.y + b.facing.y * 0.1).on, "its footpath meets the carriageway");
    require(!roads.sample(b.centre.x, b.centre.y).on, "and it does not stand on one");
  }
  // Suburbs have buildings of their own.
  for (std::size_t z = 1; z <= 5; ++z) {
    std::size_t n = 0;
    for (const auto& b : placed) if ((b.centre - plan.zones[z].centre).length() < plan.zones[z].radius_meters + 40) ++n;
    require(n >= 6, "every suburb has houses");
  }
  // The airfield is a clearing.
  require(city.clearings.size() == 1 && city.clearings[0].half_length > 300, "the airfield is kept clear of plants");
  // Deterministic.
  const auto again = plan_city(plan, rolling, kWater, road_style, plan_style);
  require(again.roads.nodes.size() == city.roads.nodes.size() && again.buildings.size() == city.buildings.size(), "planning is deterministic");
}

void test_small_cities() {
  RoadStyle road_style;
  RoadPlanStyle plan_style;
  // One zone and no highway: a lattice alone.
  CityPlan lone;
  lone.lower = {-2000, -2000}; lone.upper = {2000, 2000};
  lone.zones = {{ZoneKind::Suburb, "Only", {0, 0}, 150.0, 0.0}};
  const auto alone = plan_city(lone, rolling, kWater, road_style, plan_style);
  require(alone.stats.zones == 1 && alone.stats.interchanges == 0 && alone.stats.at_grade_junctions == 0 && alone.roads.segments.size() > 4, "a single zone needs no connection");
  // Two zones and no highway: a link road joins them.
  lone.zones.push_back({ZoneKind::Suburb, "Other", {600, 0}, 150.0, 0.0});
  const auto pair = plan_city(lone, rolling, kWater, road_style, plan_style);
  require(pair.stats.at_grade_junctions == 1, "the second zone is linked to the first");
  RoadNetwork linked(pair.roads, road_style, rolling);  // validated: connected
  require(linked.stats().avenues >= 1, "the link is an avenue");
  // The generic plan fits a terrain.
  const auto generic = generic_city_plan({0, 0}, {-2000, -2000}, {2000, 2000});
  validate_city_plan(generic);
  require(generic.zones.size() == 7 && generic.highways.size() == 1, "the generic plan is a CBD, five suburbs, an airport and a ring");
  const auto generic_city = plan_city(generic, rolling, kWater, road_style, RoadPlanStyle{});
  require(generic_city.stats.zones == 7 && generic_city.stats.interchanges + generic_city.stats.at_grade_junctions == 7, "the generic city joins every zone");
  const auto demo = demo_city_plan({0, 0}, {-1065, -1065}, {1065, 1065});
  validate_city_plan(demo);
  require(demo.zones.size() == 8 && demo.highways.size() == 3, "the demo plan is a CBD, six suburbs, an airport and three highways");
}

}  // namespace

int main() {
  try {
    test_plan_validation();
    test_city();
    test_small_cities();
    std::cout << "TerraFlux city: 3 tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "TerraFlux city test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
