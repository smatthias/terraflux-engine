#include <terraflux/graphics/fire_effects.hpp>
#include <terraflux/graphics/render_pass.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

using namespace terraflux;
using namespace terraflux::graphics;

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template <class Error = std::invalid_argument, class F> void rejects(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const Error&) { caught = true; }
  require(caught, message);
}
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) < tolerance; }

compute::GridCell cell(int x, int y, double z, double fuel, double moisture = 0.2) {
  compute::GridCell c{};
  c.x = x; c.y = y; c.elevation_meters = z; c.fuel_load = fuel; c.fuel_moisture = moisture;
  return c;
}
// 10x6 cells on a gentle plane: x 0..3 dense forest (fuel 0.9), x 4 a river
// (fuel 0), x 5..6 thin grass (fuel 0.2), x 7..9 forest again.
compute::Grid landscape_grid() {
  std::vector<compute::GridCell> cells;
  for (int y = 0; y < 6; ++y)
    for (int x = 0; x < 10; ++x) {
      const double fuel = x == 4 ? 0.0 : (x == 5 || x == 6) ? 0.2 : 0.9;
      cells.push_back(cell(x, y, 40.0 + 0.5 * x, fuel));
    }
  return compute::Grid(cells);
}
struct Scene {
  compute::Grid grid = landscape_grid();
  terrain::TerrainMesh mesh = terrain::build_terrain_mesh(grid, 30);
  TerrainStyle terrain = terrain_style_for(mesh, 0);
  TerrainSampler sampler{mesh};
  replay::FireField field = replay::make_fire_field(grid);
};
void burn(replay::FireField& field, int x, int y, std::uint32_t state) {
  field.states[static_cast<std::size_t>(y - field.min_y) * field.width + static_cast<std::size_t>(x - field.min_x)] = state;
}

void test_style_layout_and_quad() {
  FireStyle style;
  const auto u = pack_fire_style(style, {1, 2, 3}, 2.5);
  require(near(u[0], 1) && near(u[1], 0) && u[2] == 3.f && u[3] == 2.5f && u[4] == 1.f && u[6] == 3.f && u[7] == 1.f &&
          u[8] == 1.f && u[9] == 60.f && u[10] == 6000.f && near(u[13], 0.766, 1e-3), "std140 packing: wind, eye, params, light");
  const auto reject = [&](auto mutate, const char* what) { auto bad = style; mutate(bad); rejects([&] { (void)pack_fire_style(bad, {0, 0, 0}, 0); }, what); };
  reject([](FireStyle& s) { s.flame_scale = 4; }, "flame scale above three");
  reject([](FireStyle& s) { s.smoke_scale = -1; }, "negative smoke scale");
  reject([](FireStyle& s) { s.flames_per_cell = 0.5; }, "under one flame per cell");
  reject([](FireStyle& s) { s.flame_height_meters = 40; }, "flames above 30 m");
  reject([](FireStyle& s) { s.smoke_height_meters = 1; }, "smoke below 5 m");
  reject([](FireStyle& s) { s.fuel_threshold = 0.9; }, "threshold above a half");
  reject([](FireStyle& s) { s.draw_distance_meters = 10; }, "draw distance below 100 m");
  reject([](FireStyle& s) { s.wind_speed_mps = 50; }, "wind above 30 m/s");
  rejects([&] { (void)pack_fire_style(style, {0, 0, 0}, -1); }, "negative time");
  const auto quad = make_fire_quad();
  require(quad.vertices.size() == 4 && quad.indices.size() == 6 && quad.vertices[0].position[1] == 0.f && quad.vertices[2].position[1] == 1.f,
          "billboard quad spans y 0..1");
  const auto layout = fire_shader_layout();
  validate_shader_layout(layout);
  require(layout.vertex_stride_bytes == 16 && layout.attributes.size() == 2 && layout.bindings.size() == 3 &&
          layout.bindings[1].slot == kFireStyleSlot && layout.bindings[2].name == "InstanceData" &&
          shader_program_layout(ShaderProgramId::Fire).vertex_stride_bytes == 16, "fire layout contract");
}

void test_fuel_gate() {
  Scene scene;
  const auto at = [&](double x, double z) {
    const auto s = scene.sampler.sample(x, z);
    require(s.has_value(), "sample inside the landscape");
    return terrain_material({s->position.y, s->normal, s->fuel_load, s->fuel_moisture, s->curvature}, scene.terrain);
  };
  require(fire_fuel_factor(0.9, at(30, -60), 0.05) > 0.99, "dense fuel on soil burns fully");
  require(fire_fuel_factor(0.0, at(120, -60), 0.05) == 0.0, "the river cell has no fuel and cannot burn");
  require(fire_fuel_factor(0.2, at(165, -60), 0.05) > 0.99 && fire_fuel_factor(0.08, at(165, -60), 0.05) < 0.5 &&
          fire_fuel_factor(0.08, at(165, -60), 0.05) > 0.0, "thin fuel burns weakly, below the threshold not at all");
  auto flooded = scene.terrain; flooded.submerged_level_meters = 60;
  const auto s = scene.sampler.sample(240, -60);
  const auto under_water = terrain_material({s->position.y, s->normal, s->fuel_load, s->fuel_moisture, s->curvature}, flooded);
  require(fire_fuel_factor(0.9, under_water, 0.05) == 0.0, "fuel under water cannot burn");
  TerrainMaterial rock; rock.rock = 1.0;
  require(fire_fuel_factor(0.9, rock, 0.05) == 0.0, "rock cannot burn");
  TerrainMaterial snow; snow.snow = 1.0;
  require(fire_fuel_factor(0.9, snow, 0.05) == 0.0, "snow cannot burn");
  rejects([&] { (void)fire_fuel_factor(std::nan(""), rock, 0.05); }, "NaN fuel rejected");
}

void test_emitters() {
  Scene scene;
  FireStyle style; style.seed = 2;
  const auto empty = build_fire_emitters(scene.field, scene.mesh.origin_grid_x, scene.mesh.origin_grid_y, 30, scene.sampler, scene.terrain, nullptr, style);
  require(empty.flames.empty() && empty.smoke.empty() && empty.active_cells == 0, "no fire, no effects");
  // Dense forest burning, thin grass burning, the river "burning" (the simulation never does this; the visual must refuse anyway).
  burn(scene.field, 1, 2, 2); burn(scene.field, 2, 2, 2); burn(scene.field, 4, 2, 2);
  burn(scene.field, 5, 1, 2); burn(scene.field, 5, 2, 2); burn(scene.field, 5, 3, 2); burn(scene.field, 6, 2, 2);
  burn(scene.field, 1, 1, 1); burn(scene.field, 2, 1, 1); burn(scene.field, 1, 3, 1);
  const auto e = build_fire_emitters(scene.field, scene.mesh.origin_grid_x, scene.mesh.origin_grid_y, 30, scene.sampler, scene.terrain, nullptr, style);
  require(e.active_cells == 7 && e.burned_cells == 3 && !e.flames.empty() && !e.smoke.empty(), "burning and burned cells counted");
  std::size_t forest = 0, grass = 0, river = 0;
  double forest_size = 0, grass_size = 0;
  for (const auto& f : e.flames) {
    require(f.kind == 0.f && f.size > 0 && f.intensity > 0 && f.intensity <= 1, "flames have size and intensity");
    const auto ground = scene.sampler.sample(f.position[0], f.position[2]);
    require(ground && near(ground->position.y, f.position[1], 1e-4), "ground flames stand on the surface");
    require(ground->fuel_load >= style.fuel_threshold, "every flame stands on ground that carries fuel");
    if (f.position[0] >= 116 && f.position[0] <= 124) ++river;  // the river's own lattice line: fuel 0
    else if (f.position[0] > 135) { ++grass; grass_size += f.size; }
    else { ++forest; forest_size += f.size; }
  }
  require(river == 0 && e.fuelless_samples > 0, "no flame is ever placed on the fuel-free river");
  require(forest > 0 && grass > 0, "both forest and grass cells burn");
  require(forest / 2.0 > grass / 4.0 && forest_size / forest > grass_size / grass, "less fuel means fewer and smaller flames per cell");
  std::size_t plumes = 0, smoulder = 0;
  for (const auto& p : e.smoke) { if (p.kind == 1.f) ++plumes; else if (p.kind == 2.f) ++smoulder; else throw std::runtime_error("unknown smoke kind"); }
  require(plumes >= 3 && smoulder >= 1 && smoulder <= 3, "burning cells send up plumes, some burned cells smoulder");
  const auto again = build_fire_emitters(scene.field, scene.mesh.origin_grid_x, scene.mesh.origin_grid_y, 30, scene.sampler, scene.terrain, nullptr, style);
  require(again.flames.size() == e.flames.size() && again.smoke.size() == e.smoke.size() &&
          near(again.flames.front().position[0], e.flames.front().position[0]), "emitters are deterministic");
  // Crown fires follow the trees standing in burning cells.
  VegetationStyle vegetation; vegetation.tree_spacing_meters = 6; vegetation.grass = false;
  const auto placement = place_vegetation(scene.mesh, scene.terrain, vegetation);
  const auto crowned = build_fire_emitters(scene.field, scene.mesh.origin_grid_x, scene.mesh.origin_grid_y, 30, scene.sampler, scene.terrain, &placement, style);
  require(crowned.crown_flames > 0 && crowned.flames.size() > e.flames.size(), "trees in burning cells carry crown flames");
  bool elevated = false;
  for (const auto& f : crowned.flames) {
    const auto ground = scene.sampler.sample(f.position[0], f.position[2]);
    if (ground && f.position[1] > ground->position.y + 1.0) elevated = true;
  }
  require(elevated, "crown flames sit in the canopy, above the ground");
  // Water extinguishes the visual: flooding the forest removes its flames.
  auto flooded = scene.terrain; flooded.submerged_level_meters = 60;
  const auto wet = build_fire_emitters(scene.field, scene.mesh.origin_grid_x, scene.mesh.origin_grid_y, 30, scene.sampler, flooded, nullptr, style);
  require(wet.flames.empty() && wet.smoke.empty() && wet.fuelless_samples > e.fuelless_samples, "nothing burns under water");
  // Budget: crown flames and plumes survive, ground flames are trimmed.
  const auto capped = build_fire_emitters(scene.field, scene.mesh.origin_grid_x, scene.mesh.origin_grid_y, 30, scene.sampler, scene.terrain, &placement, style, 8);
  require(capped.flames.size() + capped.smoke.size() <= 8, "instance budget respected");
  rejects([&] { (void)build_fire_emitters(scene.field, 0, 0, 0, scene.sampler, scene.terrain, nullptr, style); }, "zero cell size rejected");
  auto broken = scene.field; broken.states.pop_back();
  rejects([&] { (void)build_fire_emitters(broken, 0, 0, 30, scene.sampler, scene.terrain, nullptr, style); }, "inconsistent field rejected");
}

void test_front_field() {
  Scene scene;
  burn(scene.field, 2, 2, 2); burn(scene.field, 3, 2, 2); burn(scene.field, 1, 2, 1);
  const auto at = [&](double x, double z) { return fire_front_at(scene.field, scene.mesh.origin_grid_x, scene.mesh.origin_grid_y, 30, x, z); };
  require(near(at(60, -60).active, 1.0) && at(60, -60).burned < 0.6 && near(at(30, -60).burned, 1.0),
          "cell centres read their own state at full strength");
  require(at(75, -60).active > 0.9 && at(60, -30).active > 0.5, "the field stays high between and beside burning cells");
  const double across = at(60, -95).active, further = at(60, -110).active;
  require(across > 0 && across < 1 && further < across && near(at(60, -200).active, 0.0), "the field falls off smoothly and vanishes within two cells");
  require(at(-1000, 0).active == 0 && at(-1000, 0).burned == 0, "outside the field nothing burns");
  // Edge noise is bounded and deterministic; thresholds need an existing field.
  double lo = 1, hi = -1;
  for (int i = 0; i < 400; ++i) { const double n = fire_edge_noise(i * 3.7, -i * 2.3); lo = std::min(lo, n); hi = std::max(hi, n); }
  require(lo >= -0.75 && hi <= 0.75 && hi - lo > 0.5 && near(fire_edge_noise(12.5, -40.0), fire_edge_noise(12.5, -40.0)),
          "edge noise is bounded, varied and deterministic");
  require(!fire_front_burning({0.0, 0.0}, 0.7) && !fire_front_burned({0.0, 0.0}, 0.7), "noise never ignites ground with no field");
  require(fire_front_burning({1.0, 0.0}, -0.3) && !fire_front_burning({1.0, 0.0}, -0.5) && fire_front_burning({0.3, 0.0}, 0.4),
          "the threshold moves the outline both ways");
  require(fire_front_burned({0.0, 1.0}, 0.0) && !fire_front_burned({1.0, 1.0}, 0.0), "burning wins over burned");
  rejects([&] { (void)fire_front_at(scene.field, 0, 0, 0, 1, 1); }, "zero cell size rejected");
  rejects([&] { (void)fire_edge_noise(std::nan(""), 0); }, "NaN position rejected");
  // Flames follow the ragged outline: some land in neighbouring unburned cells, none far away.
  FireStyle style; style.seed = 4;
  const auto e = build_fire_emitters(scene.field, scene.mesh.origin_grid_x, scene.mesh.origin_grid_y, 30, scene.sampler, scene.terrain, nullptr, style);
  std::size_t inside = 0, fringe = 0;
  for (const auto& f : e.flames) {
    const double dx = std::abs(f.position[0] - 75.0), dz = std::abs(f.position[2] + 60.0);
    if (dx <= 30 && dz <= 15) ++inside; else ++fringe;
    require(dx <= 30 + 45 && dz <= 15 + 45, "flames stay within 1.5 cells of the burning cells");
    const auto front = at(f.position[0], f.position[2]);
    require(front.active > 0.02, "every flame stands on ground the front field reaches");
  }
  require(inside > 0 && fringe > 0, "the ragged front spills irregularly past the cell squares");
}

void test_renderer() {
  Scene scene;
  burn(scene.field, 1, 2, 2); burn(scene.field, 2, 2, 2); burn(scene.field, 1, 1, 1);
  FireStyle style;
  ValidatingRenderDevice device;
  RenderResourceOwner shared(device);
  const std::array<float, 16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const auto camera = shared.create_buffer({BufferUsage::Uniform, sizeof(identity)}, std::as_bytes(std::span(identity)));
  const auto packed = pack_fire_style(style, {0, 50, 0}, 0);
  const auto style_buffer = shared.create_buffer({BufferUsage::Uniform, sizeof(packed)}, std::as_bytes(std::span(packed)));
  const auto pipeline = shared.create_pipeline({ShaderProgramId::Fire, fire_shader_layout(), true, true, false});
  rejects([&] { (void)shared.create_pipeline({ShaderProgramId::Fire, fire_shader_layout(), true, false, false}); }, "opaque fire rejected");
  rejects([&] { (void)shared.create_pipeline({ShaderProgramId::Fire, fire_shader_layout(), true, true, true}); }, "depth-writing fire rejected");
  const auto baseline = device.live_resource_bytes();
  {
    FireEffectsRenderer renderer(device, 64);
    renderer.set_emitters(build_fire_emitters(scene.field, scene.mesh.origin_grid_x, scene.mesh.origin_grid_y, 30, scene.sampler, scene.terrain, nullptr, style));
    require(!renderer.emitters().flames.empty() && renderer.emitters().flames.size() <= 64, "emitters installed within capacity");
    const auto everything = Frustum::from_view_projection(math::Mat4::identity());
    rejects<std::logic_error>([&] { renderer.draw(pipeline, camera, style_buffer, everything, {0, 50, 0}, 6000); }, "drawing needs a pass");
    RenderPassScope pass(device, {{64, 64}});
    const auto all = renderer.draw(pipeline, camera, style_buffer, everything, {0, 50, 0}, 6000, false);
    require(all.flames == renderer.emitters().flames.size() && all.smoke == renderer.emitters().smoke.size() && all.draws == 2, "unculled draw submits everything");
    const auto far = renderer.draw(pipeline, camera, style_buffer, everything, {1e5, 50, 0}, 6000, false);
    require(far.flames == 0 && far.smoke == 0 && far.draws == 0, "effects beyond the draw distance are skipped");
    const auto none = renderer.draw(pipeline, camera, style_buffer, Frustum::from_view_projection(math::Mat4::translation({100, 100, 100})), {0, 50, 0}, 6000);
    require(none.draws == 0, "frustum culling removes everything");
    FireEmitters many; many.flames.assign(200, FireInstance{});
    renderer.set_emitters(many);
    require(renderer.emitters().flames.size() == 64, "emitters are truncated to the capacity");
  }
  require(device.live_resource_bytes() == baseline, "renderer releases its buffers");
  rejects([&] { FireEffectsRenderer zero(device, 0); }, "zero capacity rejected");
}

}  // namespace

int main() {
  try {
    test_style_layout_and_quad();
    test_fuel_gate();
    test_emitters();
    test_front_field();
    test_renderer();
    std::cout << "TerraFlux fire effects: 5 tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "TerraFlux fire effects test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
