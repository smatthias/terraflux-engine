#include <terraflux/graphics/camera_rig.hpp>
#include <terraflux/graphics/flood.hpp>
#include <terraflux/grid.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <cmath>
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

// A 20 x 20 texel field (30 m) over z -570..0: a valley along x at z = -270 whose catchment
// grows eastward (flow 0.2 .. 1.0), 5 degrees of slope, draining east; the
// slopes either side have no catchment.
RunoffField valley() {
  RunoffField f;
  f.width = f.height = 20; f.x0 = 0; f.z0 = -570; f.spacing = 30;  // the terrain's north is -z
  f.texels.assign(400, RunoffTexel{});
  for (std::uint32_t z = 0; z < 20; ++z)
    for (std::uint32_t x = 0; x < 20; ++x) {
      auto& t = f.texels[z * 20 + x];
      const double across = std::abs(static_cast<double>(z) - 10.0);
      t.flow = static_cast<float>(across <= 1 ? 0.2 + 0.8 * x / 19.0 : 0.05);
      t.slope_degrees = 5.0f;
      t.direction_x = 1.0f; t.direction_z = 0.0f;
      t.steepness = 0.1f;
      if (x == 15 && across <= 1) { t.waterfall = 0.8f; t.slope_degrees = 50.0f; }
    }
  return f;
}
// Terrain for the mesh test: a 12 x 12 cell grid sloping down to the east, 30 m cells, over the same plan.
terrain::TerrainMesh slope_terrain() {
  std::vector<compute::GridCell> cells;
  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 20; ++x) {
      compute::GridCell c{};
      c.x = x; c.y = y; c.elevation_meters = 100.0 - 2.0 * x; c.fuel_load = 0.3; c.fuel_moisture = 0.2;
      cells.push_back(c);
    }
  return terrain::build_terrain_mesh(compute::Grid(cells), 30);
}

void test_flood_wave() {
  FloodStyle style;
  validate_flood_style(style);
  rejects([] { FloodStyle s; s.manning_n = 0; validate_flood_style(s); }, "zero roughness rejected");
  rejects([] { FloodStyle s; s.channel_depth_meters = -1; validate_flood_style(s); }, "negative depth rejected");
  rejects([] { (void)FloodModel(RunoffField{}, FloodStyle{}); }, "an empty field is rejected");
  FloodModel flood(valley(), style);
  require(flood.flash() == 0 && flood.channel_depth(500, -270) == 0, "dry to begin with");
  // A thunderstorm fills the channels within about a quarter of an hour; a drizzle never does.
  flood.advance(900, 25.0);
  require(near(flood.flash(), 1 - std::exp(-1.0), 1e-9), "the wave rises with the rise time constant toward the storm rain");
  flood.advance(3600, 25.0);
  require(flood.flash() > 0.98, "a long storm fills it");
  flood.advance(3600, 0.0);
  require(flood.flash() < 0.4 && flood.flash() > 0.3, "after an hour without rain it has drained to about 1/e");
  FloodModel drizzle(valley(), style);
  drizzle.advance(36000, 0.5);
  require(drizzle.flash() < 0.03, "a drizzle barely wets the channels");
  rejects([&] { flood.advance(-1, 0); }, "negative time rejected");
  rejects([&] { flood.set_flash(2); }, "flash above one rejected");

  // Depth grows with the catchment and the wave; nothing on the slopes.
  flood.set_flash(1.0);
  const double upstream = flood.channel_depth(60, -270), downstream = flood.channel_depth(540, -270);
  require(upstream < downstream && downstream > 0.5 && downstream <= style.channel_depth_meters + 1e-9, "deeper downstream, up to the full channel");
  require(flood.channel_depth(300, -540) == 0 && flood.channel_depth(300, 0) == 0, "the valley sides are dry");
  flood.set_flash(0.5);
  require(near(flood.channel_depth(540, -270), downstream * 0.5, 1e-9), "depth follows the wave");
  flood.set_flash(1.0);

  // A sample: depth, surface, a Manning current along the drainage, a waterfall where the channel drops.
  const auto w = flood.sample(540, -270, 20.0, std::nullopt);
  require(w.channel && near(w.depth_meters, downstream) && near(w.surface_meters, 20.0 + downstream), "the surface sits depth above the ground");
  require(w.current.x > 0.99 && w.current_mps > 1.0 && w.current_mps <= style.max_current_mps, "the current runs east at a Manning speed");
  require(flood.sample(150, -270, 0, std::nullopt).current_mps < w.current_mps, "shallower water runs slower");
  require(flood.sample(450, -270, 0, std::nullopt).waterfall && !w.waterfall, "the drop is a waterfall");
  const auto dry = flood.sample(300, -540, 50.0, std::nullopt);
  require(dry.depth_meters == 0 && dry.surface_meters == 50.0 && !dry.channel && dry.current_mps == 0, "dry ground: no water, the ground as surface");
  // The lake wins where it is deeper.
  const auto lake = flood.sample(300, -540, 50.0, 52.0);
  require(!lake.channel && near(lake.depth_meters, 2.0) && near(lake.surface_meters, 52.0) && lake.current_mps == 0, "standing water from the lake, still");
  const auto both = flood.sample(540, -270, 20.0, 20.2);
  require(both.channel && near(both.depth_meters, downstream), "the channel counts where it is deeper than the lake");
  rejects([&] { (void)flood.sample(std::nan(""), 0, 0, std::nullopt); }, "non-finite sample rejected");

  // The mesh lies on the channel; dry corners are ground; nothing under the lake.
  const auto terrain = slope_terrain();
  const auto mesh = flood.build_mesh(terrain, std::nullopt);
  require(!mesh.indices.empty() && mesh.indices.size() % 3 == 0 && mesh.submerged_vertices > 0 && mesh.max_depth_meters > 0.5, "a flood surface over the valley");
  for (const auto& v : mesh.vertices) {
    const auto& src = terrain.vertices[v.source_vertex];
    require(v.bed_height_meters == src.position.y && v.position.y >= src.position.y - 1e-9, "the surface lies on or above the bed");
    require(near(v.position.y - v.bed_height_meters, flood.channel_depth(v.position.x, v.position.z), 1e-9), "the surface is the channel depth above the bed");
  }
  flood.set_flash(0.0);
  require(flood.build_mesh(terrain, std::nullopt).indices.empty(), "no wave, no surface");
  flood.set_flash(1.0);
  require(flood.build_mesh(terrain, 1000.0).indices.empty(), "everything under the lake is the lake's");
  const auto st = flood.stats(terrain, std::nullopt);
  require(st.wet_vertices > 0 && st.max_depth_meters > 0.5 && st.max_current_mps > 1.0, "stats");
}

// Flat ground at 10 m; water as a function of x: a ford 0.5 m deep with a
// current for x in 520..540, a pool 2 m deep for x in 600..640, a torrent
// 0.8 m deep running 3 m/s east for x in 700..720.
FloodSample water_at(double x, double, double ground) {
  FloodSample w; w.surface_meters = ground;
  if (x >= 520 && x <= 540) { w.depth_meters = 0.5; w.surface_meters = ground + 0.5; w.channel = true; w.current = {1, 0}; w.current_mps = 0.8; }
  else if (x >= 600 && x <= 640) { w.depth_meters = 2.0; w.surface_meters = ground + 2.0; }
  else if (x >= 700 && x <= 720) { w.depth_meters = 0.8; w.surface_meters = ground + 0.8; w.channel = true; w.current = {1, 0}; w.current_mps = 3.0; }
  return w;
}
WalkEnvironment wet_env() {
  WalkEnvironment env;
  env.standing_height = [](double x, double z) -> std::optional<double> { if (x < 0 || x > 1000 || z < 0 || z > 1000) return std::nullopt; return 10.0; };
  env.lower = {0, 0, 0}; env.upper = {1000, 100, 1000};
  env.gait.enabled = false;
  env.water = water_at;
  return env;
}

void test_walker_in_water() {
  constexpr double kTick = 1.0 / 60.0;
  rejects([] { WaterRules r; r.swim_depth_meters = 0; validate_water_rules(r); }, "a zero swim depth is rejected");
  const auto env = wet_env();
  FirstPersonCamera person;
  person.feet = {500, 10, 500}; person.yaw = -kPi / 2; person.walk_speed_mps = 2.0;
  // Wading the ford: slower, pushed along by the current, feet on the bed.
  double dry_tick = walk(person, {1, 0, false}, kTick, env);
  person.feet = {525, 10, 500};
  double wet_tick = walk(person, {1, 0, false}, kTick, env);
  require(near(person.water_depth_meters, 0.5) && !person.swimming && near(person.feet.y, 10.0), "wading: the feet stay on the bed");
  require(wet_tick < dry_tick && wet_tick > dry_tick * 0.5, "wading is slower");
  person.feet = {525, 10, 500};
  walk(person, {}, 1.0, env);
  require(person.feet.x > 525.2 && person.feet.x < 526.5, "standing in the ford the current pushes you along");
  require(!person.swept && person.stunned_seconds == 0, "a gentle ford does not sweep you away");
  // Crossing the current sideways: still pushed downstream.
  person.feet = {530, 10, 500}; person.yaw = 0;  // north
  walk(person, {1, 0, false}, 1.0, env);
  require(person.feet.z < 500 && person.feet.x > 530.1, "crossing, you are carried downstream");

  // The pool: deep enough to float; the eye stays above the surface; no jumping; no bob.
  person.feet = {620, 10, 500}; person.yaw = -kPi / 2; person.grounded = true;
  walk(person, {1, 0, false, true}, kTick, env);
  require(person.swimming && near(person.water_depth_meters, 2.0), "in the pool you swim");
  require(near(person.feet.y, 12.0 - env.water_rules.swim_depth_meters) && person.eye().y > 12.0 && person.grounded,
          "floating with the feet under and the head above the surface");
  require(near(person.vertical_velocity_mps, 0.0) && person.feet.y < 12.0, "no jumping out of deep water");
  const double x_before = person.feet.x;
  const std::uint64_t strokes_before = person.strokes;
  double max_tick = 0, min_tick = 1e9;
  for (int i = 0; i < 84; ++i) { const double d = walk(person, {1, 0, false}, kTick, env); max_tick = std::max(max_tick, d); min_tick = std::min(min_tick, d); }
  require(near(person.feet.x - x_before, env.water_rules.swim_speed_mps * 1.4, 0.1) && person.strokes == strokes_before + 1 && max_tick > min_tick * 1.5 &&
          person.footfalls == 0, "a stroke of swimming covers the swim speed, surging with the pull, without footfalls");
  // Walking out of the pool: back onto the bed.
  person.feet = {639, person.feet.y, 500};
  for (int i = 0; i < 150; ++i) walk(person, {1, 0, false}, kTick, env);
  require(!person.swimming && near(person.feet.y, 10.0) && person.feet.x > 640, "out of the pool the feet find the ground again");
  // Stepping off a ledge into deep water lands floating, not on the bed.
  WalkEnvironment cliff = env;
  cliff.standing_height = [](double x, double) -> std::optional<double> { return x < 600 ? 14.0 : 10.0; };
  person.feet = {599, 14, 500}; person.grounded = true;
  for (int i = 0; i < 120; ++i) walk(person, {1, 0, false}, kTick, cliff);
  require(person.grounded && person.swimming && near(person.feet.y, 12.0 - env.water_rules.swim_depth_meters), "off the ledge into the pool: afloat");

  // The torrent: deeper than the sweep depth and faster than the sweep current: off your feet and carried.
  person.feet = {705, 10, 500}; person.grounded = true; person.stunned_seconds = 0; person.crouch_meters = 0; person.yaw = kPi / 2;  // trying to walk west, upstream
  walk(person, {1, 0, false}, kTick, env);
  require(person.swept && person.stunned_seconds > 0 && person.crouch_meters > 0.5, "the torrent sweeps you off your feet");
  const double x0 = person.feet.x;
  for (int i = 0; i < 60; ++i) walk(person, {1, 0, false}, kTick, env);
  require(person.feet.x > x0 + 2.0 && person.feet.x < 720.01, "carried downstream, unable to walk against it");
  require(!person.swept, "swept is an edge of the step that did it");
  // Sweeping is a rule of the water, not of the depth alone.
  WalkEnvironment braver = env; braver.water_rules.sweep_current_mps = 5.0;
  person.feet = {705, 10, 500}; person.stunned_seconds = 0; person.crouch_meters = 0;
  walk(person, {1, 0, false}, kTick, braver);
  require(!person.swept && person.stunned_seconds == 0, "with a higher sweep threshold you keep your feet");
}
void test_swimming() {
  constexpr double kTick = 1.0 / 60.0;
  rejects([] { WaterRules r; r.crawl_factor = 5; validate_water_rules(r); }, "an absurd crawl factor is rejected");
  const auto env = wet_env();
  FirstPersonCamera person;
  person.feet = {610, 10, 500}; person.yaw = -kPi / 2; person.walk_speed_mps = 2.5; person.grounded = true;
  walk(person, {}, kTick, env);
  require(person.swimming && person.breath == 1.0 && !person.diving, "afloat in the pool with a full breath");
  // The crawl is faster and tires faster.
  const double x0 = person.feet.x, stamina0 = person.stamina;
  for (int i = 0; i < 60; ++i) walk(person, {1, 0, true}, kTick, env);
  const double crawl = person.feet.x - x0, crawl_cost = stamina0 - person.stamina;
  require(person.crawling, "Shift swims the crawl");
  person.feet = {610, person.feet.y, 500}; person.stamina = stamina0;
  for (int i = 0; i < 60; ++i) walk(person, {1, 0, false}, kTick, env);
  const double breast = person.feet.x - 610, breast_cost = stamina0 - person.stamina;
  require(crawl > breast * 1.4 && crawl_cost > breast_cost * 1.4 && !person.crawling, "the crawl covers more water at a higher cost");
  // Diving: the eye goes under, the breath runs out and the swimmer surfaces by itself; at the surface the breath returns.
  person.feet = {610, person.feet.y, 500};
  const double surface = person.feet.y + env.water_rules.swim_depth_meters;
  for (int i = 0; i < 60; ++i) walk(person, {0, 0, false, false, true}, kTick, env);
  require(person.diving && near(person.dive_blend, 1.0) && person.underwater() && person.eye().y < surface, "Ctrl dives: the eye is under the surface");
  require(person.breath < 1.0 && person.breath > 0.9, "the breath runs out slowly");
  const double x1 = person.feet.x;
  for (int i = 0; i < 60; ++i) walk(person, {1, 0, true, false, true}, kTick, env);
  require(person.feet.x - x1 < crawl && person.feet.x - x1 > 0, "under water you swim, but not the crawl");
  int ticks = 0;
  while (person.diving && ticks < 60 * 40) { walk(person, {0, 0, false, false, true}, kTick, env); ++ticks; }
  require(!person.diving && person.breath <= 0.0 + 1e-9 && ticks < 60 * 30, "out of breath, the diver surfaces by itself");
  for (int i = 0; i < 60; ++i) walk(person, {0, 0, false, false, true}, kTick, env);
  require(!person.diving && !person.underwater(), "and cannot dive again without breath");
  for (int i = 0; i < 60 * 10; ++i) walk(person, {}, kTick, env);
  require(person.breath > 0.95, "at the surface the breath returns");
  // Leaving the water ends the dive and the strokes.
  person.feet = {650, 10, 500}; person.grounded = true;
  walk(person, {0, 0, false, false, true}, kTick, env);
  walk(person, {0, 0, false, false, true}, kTick, env);  // the crouch reads the water state of the step before
  require(!person.swimming && !person.diving && !person.crawling && person.crouched, "on land Ctrl is a crouch again");
}
}  // namespace

int main() {
  try {
    test_flood_wave();
    test_walker_in_water();
    test_swimming();
  } catch (const std::exception& error) {
    std::cerr << "flood tests failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "flood tests passed\n";
  return 0;
}
