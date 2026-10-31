#include <terraflux/terrain_water.hpp>
#include <terraflux/graphics/water_shading.hpp>
#include <terraflux/graphics/render_pass.hpp>
#include <terraflux/graphics/resource_owner.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

using namespace terraflux;
using namespace terraflux::graphics;
using terraflux::terrain::build_terrain_mesh;
using terraflux::terrain::build_water_mesh;

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template <class F> void rejects(F&& f, const char* message) {
  bool caught = false;
  try { f(); } catch (const std::invalid_argument&) { caught = true; }
  require(caught, message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) < tolerance; }
compute::GridCell cell(int x, int y, std::optional<double> z, double fuel = 1.0) {
  compute::GridCell c{};
  c.x = x; c.y = y; c.elevation_meters = z; c.fuel_load = fuel; c.fuel_moisture = 0.2;
  return c;
}
// A 5x5 bowl: rim at 40 m, center at 0 m.
terrain::TerrainMesh bowl() {
  std::vector<compute::GridCell> cells;
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 5; ++x) cells.push_back(cell(x, y, 10.0 * std::max(std::abs(x - 2), std::abs(y - 2)) * 2.0));
  return build_terrain_mesh(compute::Grid(cells), 30);
}

void test_water_mesh_clipping() {
  const auto land = bowl();
  require(build_water_mesh(land, -5).indices.empty() && build_water_mesh(land, 0).indices.empty(),
          "a level at or below the lowest vertex makes no water");
  const auto lake = build_water_mesh(land, 15);
  require(!lake.indices.empty() && lake.indices.size() % 3 == 0, "triangles with a submerged corner are kept");
  require(lake.level_meters == 15 && near(lake.max_depth_meters, 15) && lake.submerged_vertices == 1,
          "level, maximum depth and submerged vertex count");
  std::set<std::uint32_t> sources;
  for (const auto& v : lake.vertices) {
    require(v.position.y == 15, "surface vertices sit at the level");
    const auto& source = land.vertices[v.source_vertex];
    require(v.position.x == source.position.x && v.position.z == source.position.z &&
            v.bed_height_meters == source.position.y, "bed height and plan position copied from the terrain");
    require(sources.insert(v.source_vertex).second, "each terrain vertex is copied at most once");
  }
  for (std::size_t t = 0; t < lake.indices.size(); t += 3) {
    bool submerged = false;
    for (int k = 0; k < 3; ++k) submerged = submerged || lake.vertices[lake.indices[t + k]].bed_height_meters < 15;
    require(submerged, "no fully dry triangle is included");
  }
  // The full plane covers every terrain triangle, in the terrain's order and winding.
  const auto flood = build_water_mesh(land, 100);
  require(flood.indices.size() == land.indices.size() && flood.vertices.size() == land.vertices.size(),
          "a level above the rim floods the whole terrain");
  for (std::size_t i = 0; i < flood.indices.size(); ++i)
    require(flood.vertices[flood.indices[i]].source_vertex == land.indices[i], "winding and order follow the terrain");
  const auto count = [&](double level) { return build_water_mesh(land, level).indices.size() / 3; };
  require(count(5) < count(25) && count(25) < count(45), "rising water covers more triangles");
}

void test_water_mesh_gradient_and_holes() {
  // Plane h = 0.5 X - 0.25 (-Z): gradient (0.5, 0.25) toward rising ground.
  std::vector<compute::GridCell> cells;
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) cells.push_back(cell(x, y, 0.5 * x * 30 - 0.25 * y * 30));
  const auto plane = build_terrain_mesh(compute::Grid(cells), 30);
  const auto water = build_water_mesh(plane, 1000);
  for (const auto& v : water.vertices)
    require(near(v.bed_gradient.x, 0.5, 1e-9) && near(v.bed_gradient.y, 0.25, 1e-9),
            "bed gradient is the terrain slope in meters per meter");
  // A hole (void cell) stays a hole in the water surface too.
  auto holed = cells;
  holed[5].elevation_meters = std::nullopt;
  const auto holed_mesh = build_terrain_mesh(compute::Grid(holed), 30);
  const auto holed_water = build_water_mesh(holed_mesh, 1000);
  require(holed_water.indices.size() == holed_mesh.indices.size() && holed_mesh.indices.size() < plane.indices.size(),
          "water never bridges terrain holes");
  // Near-vertical beds cap the gradient; a vertical normal never divides by zero.
  terrain::TerrainMesh wall = plane;
  for (auto& v : wall.vertices) v.normal = {0.9999, 1e-12, 0};
  const auto capped = build_water_mesh(wall, 1000);
  for (const auto& v : capped.vertices) require(v.bed_gradient.x == 0 && v.bed_gradient.y == 0, "degenerate normals give no gradient");
  for (auto& v : wall.vertices) v.normal = {0.99, 0.01, 0};
  for (const auto& v : build_water_mesh(wall, 1000).vertices) require(v.bed_gradient.length() < 19.1, "gradient capped at 87 degrees");
  rejects([&] { (void)build_water_mesh(plane, std::numeric_limits<double>::quiet_NaN()); }, "NaN level rejected");
  auto broken = plane;
  broken.indices.push_back(0);
  rejects([&] { (void)build_water_mesh(broken, 10); }, "partial triangle rejected");
  broken = plane;
  broken.indices[0] = 999;
  rejects([&] { (void)build_water_mesh(broken, 10); }, "out-of-range index rejected");
}

void test_upload_layout_and_pipeline() {
  const auto water = build_water_mesh(bowl(), 15);
  const auto upload = prepare_water_upload(water);
  require(upload.vertices.size() == water.vertices.size() && upload.indices == water.indices, "upload preserves topology");
  require(sizeof(WaterGpuVertex) == 24 && upload.vertices[0].position[1] == 15.f &&
          upload.vertices[0].bed_height == static_cast<float>(water.vertices[0].bed_height_meters),
          "packed vertex carries level and bed height");
  const auto layout = water_shader_layout();
  validate_shader_layout(layout);
  require(layout.vertex_stride_bytes == 24 && layout.attributes.size() == 3 && layout.bindings.size() == 2 &&
          layout.bindings[1].name == "WaterStyle" && layout.bindings[1].slot == kWaterStyleSlot &&
          layout.bindings[1].stages == (kVertexStage | kFragmentStage), "water layout contract");
  require(shader_program_layout(ShaderProgramId::Water).vertex_stride_bytes == 24, "registry knows the water identity");

  ValidatingRenderDevice device;
  RenderResourceOwner owner(device);
  const auto pipeline = owner.create_pipeline({ShaderProgramId::Water, layout, true, true, false});
  rejects([&] { (void)owner.create_pipeline({ShaderProgramId::Water, layout, true, false, false}); }, "opaque water rejected");
  rejects([&] { (void)owner.create_pipeline({ShaderProgramId::Water, layout, true, true, true}); }, "depth-writing water rejected");
  rejects([&] { (void)owner.create_pipeline({ShaderProgramId::Water, layout, false, true, false}); }, "untested water rejected");
  const auto vb = owner.create_buffer({BufferUsage::Vertex, std::as_bytes(std::span(upload.vertices)).size()}, std::as_bytes(std::span(upload.vertices)));
  const auto ib = owner.create_buffer({BufferUsage::Index, std::as_bytes(std::span(upload.indices)).size()}, std::as_bytes(std::span(upload.indices)));
  const std::array<float, 16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const auto camera = owner.create_buffer({BufferUsage::Uniform, sizeof(identity)}, std::as_bytes(std::span(identity)));
  WaterStyle style; style.level_meters = 15;
  const auto packed = pack_water_style(style, {0, 100, 0}, 0);
  const auto styled = owner.create_buffer({BufferUsage::Uniform, sizeof(packed)}, std::as_bytes(std::span(packed)));
  const std::array<float, 16> half{};
  const auto truncated = owner.create_buffer({BufferUsage::Uniform, sizeof(half)}, std::as_bytes(std::span(half)));
  RenderPassScope pass(device, {{64, 64}});
  device.draw_indexed({pipeline, vb, ib, 0, static_cast<std::uint32_t>(upload.indices.size()), {{0, camera}, {kWaterStyleSlot, styled}}, {}});
  rejects([&] { device.draw_indexed({pipeline, vb, ib, 0, 3, {{0, camera}, {kWaterStyleSlot, truncated}}, {}}); },
          "short water style buffer rejected");
  require(device.draw_count() == 1, "water draw recorded");
  auto wall = bowl();
  wall.vertices[0].position.x = std::numeric_limits<double>::infinity();
  rejects([&] { (void)prepare_water_upload(build_water_mesh(wall, 100)); }, "non-finite upload rejected");
}

void test_style_packing_and_validation() {
  WaterStyle style; style.level_meters = 42; style.wave_direction_degrees = 90; style.light_direction = {0, 3, 0};
  const auto u = pack_water_style(style, {1, 2, 3}, 7.5);
  require(u.size() == 32 && u[0] == 42.f && u[1] == 0.6f && u[2] == 24.f && u[3] == 3.f &&
          near(u[4], 1, 1e-6) && near(u[5], 0, 1e-6) && u[6] == 7.5f && u[7] == 2.0f &&
          u[11] == 8.f && u[15] == 4.f && u[16] == 1.f && u[17] == 2.f && u[18] == 3.f && u[19] == 0.5f &&
          u[23] == 160.f && u[27] == 1.f && u[28] == 0.f && u[29] == 1.f && u[30] == 0.f && near(u[31], 0.9, 1e-6),
          "std140 packing order");
  const auto north = wave_travel_vector(0), east = wave_travel_vector(90), south = wave_travel_vector(180), west = wave_travel_vector(270);
  require(near(north.x, 0) && near(north.y, -1) && near(east.x, 1) && near(east.y, 0) &&
          near(south.x, 0) && near(south.y, 1) && near(west.x, -1) && near(west.y, 0), "compass to world xz");
  const auto reject = [&](auto mutate, const char* what) {
    auto bad = style; mutate(bad);
    rejects([&] { (void)pack_water_style(bad, {0, 0, 0}, 0); }, what);
  };
  reject([](WaterStyle& s) { s.wave_height_meters = -0.1; }, "negative wave height");
  reject([](WaterStyle& s) { s.wave_height_meters = 6; }, "wave height above five meters");
  reject([](WaterStyle& s) { s.wave_length_meters = 0.5; }, "wave length below one meter");
  reject([](WaterStyle& s) { s.wave_speed_mps = 51; }, "wave speed above fifty");
  reject([](WaterStyle& s) { s.wave_direction_degrees = 361; }, "direction above 360");
  reject([](WaterStyle& s) { s.choppiness = 2; }, "choppiness above one");
  reject([](WaterStyle& s) { s.foam_depth_meters = -1; }, "negative foam depth");
  reject([](WaterStyle& s) { s.deep_color.z = 1.5; }, "color above one");
  reject([](WaterStyle& s) { s.absorption_meters = 0; }, "zero absorption");
  reject([](WaterStyle& s) { s.clarity_meters = -1; }, "negative clarity");
  reject([](WaterStyle& s) { s.clarity_meters = 0.2; }, "clarity below half a meter");
  reject([](WaterStyle& s) { s.clarity_meters = 60; }, "clarity above fifty meters");
  reject([](WaterStyle& s) { s.reflectivity = 1.2; }, "reflectivity above one");
  reject([](WaterStyle& s) { s.light_direction = {0, 0, 0}; }, "zero light");
  reject([](WaterStyle& s) { s.specular_power = 0.5; }, "specular power below one");
  reject([](WaterStyle& s) { s.level_meters = std::numeric_limits<double>::infinity(); }, "infinite level");
  rejects([&] { (void)pack_water_style(style, {std::numeric_limits<double>::quiet_NaN(), 0, 0}, 0); }, "NaN eye");
  rejects([&] { (void)pack_water_style(style, {0, 0, 0}, -1); }, "negative time");
}

void test_wave_physics_reference() {
  require(near(wave_shallowness(1000, 24), 1.0) && wave_shallowness(1.0, 24) < 0.9 &&
          wave_shallowness(0.1, 24) < wave_shallowness(1.0, 24) && wave_shallowness(0, 24) >= 0.3,
          "waves feel the bottom in shallow water; the ratio is clamped");
  const math::Vec2 travel{1, 0}, shore{0, -1}; // shore rises to the north of an eastward wave
  const auto deep = wave_travel_direction(travel, shore * 0.3, 500, 24);
  require(near(deep.x, 1) && near(deep.y, 0), "deep water keeps the travel direction");
  const auto shallow = wave_travel_direction(travel, shore * 0.3, 0.4, 24);
  require(near(shallow.length(), 1) && shallow.dot(shore) > 0.3 && shallow.x > 0,
          "shallow water bends the wave toward rising ground");
  const auto flat = wave_travel_direction(travel, {0, 0}, 0.4, 24);
  require(near(flat.x, 1) && near(flat.y, 0), "a flat bed gives no refraction");
  require(wave_travel_direction(travel, shore * 0.3, 0.4, 24).dot(shore) >
          wave_travel_direction(travel, shore * 0.3, 3.0, 24).dot(shore), "refraction strengthens as the depth drops");
  rejects([&] { (void)wave_travel_direction({0, 0}, shore, 1, 24); }, "zero travel rejected");

  WaterStyle style; style.wave_height_meters = 0.6; style.wave_length_meters = 24;
  const auto open = wave_shoaling(200, style);
  require(near(open.amplitude_meters, 0.6) && open.breaking == 0 && near(open.wave_length_meters, 24),
          "open water carries the full wave unchanged");
  const auto shoaling = wave_shoaling(2.0, style);
  require(shoaling.amplitude_meters > 0.6 && shoaling.breaking == 0 && shoaling.wave_length_meters < 24,
          "waves grow and shorten as they shoal");
  const auto surf = wave_shoaling(0.5, style);
  require(near(surf.amplitude_meters, 0.3) && surf.breaking == 1, "depth-limited waves break");
  WaterStyle calm = style; calm.wave_height_meters = 0;
  const auto still = wave_shoaling(0.5, calm);
  require(still.amplitude_meters == 0 && still.breaking == 0, "flat water never breaks");
  rejects([&] { (void)wave_shoaling(std::numeric_limits<double>::quiet_NaN(), style); }, "NaN depth rejected");
}

void test_flat_water_sample_reference() {
  WaterStyle style; style.level_meters = 10; style.light_direction = {0, 1, 0};
  const math::Vec3 surface{0, 10, 0};
  const auto above = water_surface_sample(surface, 0.5, {0, 500, 0}, style);
  const auto deep = water_surface_sample(surface, 50, {0, 500, 0}, style);
  require(deep.alpha > above.alpha && deep.alpha > 0.99 && above.alpha < 0.3, "deeper water is more opaque");
  WaterStyle matte = style; matte.specular_intensity = 0; matte.reflectivity = 0;
  const auto matte_deep = water_surface_sample(surface, 50, {0, 500, 0}, matte);
  const auto matte_shallow = water_surface_sample(surface, 0.5, {0, 500, 0}, matte);
  require(near(matte_deep.color.z, style.deep_color.z, 1e-3) && matte_shallow.color.y > matte_deep.color.y &&
          std::abs(matte_shallow.color.x - style.shallow_color.x) < std::abs(matte_shallow.color.x - style.deep_color.x),
          "color absorbs toward the deep color with depth");
  // Straight down: Fresnel is 2 %, the reflected ray hits the zenith sun.
  const double spec = std::pow(1.0, style.specular_power) * style.specular_intensity;
  require(near(deep.color.y, (style.deep_color.y * (1 - std::exp(-50 / 8.0)) + style.shallow_color.y * std::exp(-50 / 8.0)) * 0.98 +
                             style.zenith_color.y * 0.02 + spec, 1e-6), "nadir view: body plus 2 % sky plus sun glitter");
  const auto grazing = water_surface_sample(surface, 50, {1000, 12, 0}, style);
  const auto steep = water_surface_sample(surface, 50, {10, 500, 0}, style);
  require(grazing.color.z > steep.color.z - 0.3 && grazing.alpha >= steep.alpha && grazing.alpha > 0.99,
          "grazing views reflect the sky and are opaque");
  WaterStyle mirror = style; mirror.reflectivity = 0; mirror.specular_intensity = 0;
  const auto dull = water_surface_sample(surface, 50, {1000, 12, 0}, mirror);
  require(dull.color.x < grazing.color.x && near(dull.alpha, 1 - std::exp(-50 / 4.0)), "reflectivity zero leaves only the absorbed body");
  const auto shallow = water_surface_sample(surface, 0.2, {0, 500, 0}, mirror);
  require(near(shallow.color.x, style.shallow_color.x, 0.02) && near(shallow.color.y, style.shallow_color.y, 0.02) &&
          shallow.alpha < 0.1, "shallow water keeps the turquoise tint and is nearly transparent");
  rejects([&] { (void)water_surface_sample(surface, 0, {0, 500, 0}, style); }, "zero depth rejected");
  rejects([&] { (void)water_surface_sample(surface, 5, {std::numeric_limits<double>::infinity(), 0, 0}, style); }, "infinite eye rejected");
}

}  // namespace

int main() {
  try {
    test_water_mesh_clipping();
    test_water_mesh_gradient_and_holes();
    test_upload_layout_and_pipeline();
    test_style_packing_and_validation();
    test_wave_physics_reference();
    test_flat_water_sample_reference();
    std::cout << "TerraFlux water: 6 tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "TerraFlux water test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
