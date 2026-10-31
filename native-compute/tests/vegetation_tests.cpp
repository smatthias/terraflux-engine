#include <terraflux/graphics/vegetation.hpp>
#include <terraflux/graphics/render_pass.hpp>
#include <terraflux/assets/obj.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
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

compute::GridCell cell(int x, int y, double z, double fuel, double moisture) {
  compute::GridCell c{};
  c.x = x; c.y = y; c.elevation_meters = z; c.fuel_load = fuel; c.fuel_moisture = moisture;
  return c;
}
// 12x12 cells: west half dense wet forest, east half dry grassland, a bare strip in the middle.
terrain::TerrainMesh landscape() {
  std::vector<compute::GridCell> cells;
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 12; ++x) {
      const double fuel = x < 5 ? 0.9 : x < 7 ? 0.0 : 0.3;
      const double moisture = x < 5 ? 0.6 : 0.05;
      cells.push_back(cell(x, y, 100.0 + 0.2 * x * 30, fuel, moisture));
    }
  return terrain::build_terrain_mesh(compute::Grid(cells), 30);
}
TerrainStyle style_for(const terrain::TerrainMesh& mesh) { return terrain_style_for(mesh, 0); }

void check_mesh(const VegetationMesh& mesh, const char* what) {
  require(!mesh.vertices.empty() && mesh.indices.size() >= 3 && mesh.indices.size() % 3 == 0, what);
  double min_y = 1e9, max_y = -1e9;
  for (const auto& v : mesh.vertices) {
    min_y = std::min<double>(min_y, v.position[1]); max_y = std::max<double>(max_y, v.position[1]);
    const double n = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
    require(near(n, 1.0, 1e-4), "unit normals");
    require(v.flex >= 0 && v.flex <= 1 && v.tintable >= 0 && v.tintable <= 1, "flex and tint weights in range");
    for (const float c : v.color) require(c >= 0 && c <= 1.05f, "vertex colors normalized");
  }
  require(near(min_y, 0.0, 1e-6) && near(max_y, 1.0, 1e-6), "unit height with the base at y = 0");
  for (const auto i : mesh.indices) require(i < mesh.vertices.size(), "indices in range");
}

void test_meshes() {
  for (const auto shape : {TreeShape::Conifer, TreeShape::Broadleaf, TreeShape::Shrub}) {
    TreeMeshOptions o; o.shape = shape;
    const auto mesh = make_tree_mesh(o);
    check_mesh(mesh, "tree mesh is a valid unit model");
    require(mesh.name == tree_shape_name(shape), "mesh named after its shape");
    bool wood = false, foliage = false;
    for (const auto& v : mesh.vertices) { wood = wood || v.tintable == 0; foliage = foliage || v.tintable == 1; }
    require(wood && foliage, "trees have untinted wood and tinted foliage");
    // Flex grows with height so the tip sways more than the trunk.
    float base_flex = 1, tip_flex = 0;
    for (const auto& v : mesh.vertices) { if (v.position[1] < 0.01f) base_flex = std::min(base_flex, v.flex); if (v.position[1] > 0.99f) tip_flex = std::max(tip_flex, v.flex); }
    require(base_flex == 0 && tip_flex == 1, "flex is zero at the base and one at the tip");
  }
  TreeMeshOptions coarse, fine; coarse.segments = 3; coarse.layers = 1; fine.segments = 24; fine.layers = 6;
  require(make_tree_mesh(fine).indices.size() > 4 * make_tree_mesh(coarse).indices.size(), "segments and layers control detail");
  rejects([] { TreeMeshOptions o; o.segments = 2; (void)make_tree_mesh(o); }, "too few segments rejected");
  rejects([] { TreeMeshOptions o; o.layers = 7; (void)make_tree_mesh(o); }, "too many layers rejected");
  rejects([] { TreeMeshOptions o; o.crown_width = 3; (void)make_tree_mesh(o); }, "absurd crown rejected");
  rejects([] { TreeMeshOptions o; o.foliage_color = {2, 0, 0}; (void)make_tree_mesh(o); }, "bad color rejected");
  const auto grass = make_grass_mesh(3);
  check_mesh(grass, "grass tuft is a valid unit model");
  require(grass.indices.size() == 3 * 3 * 3 && make_grass_mesh(8).indices.size() == 8 * 9, "three triangles per blade");
  rejects([] { (void)make_grass_mesh(0); }, "zero blades rejected");
  rejects([] { (void)make_grass_mesh(9); }, "too many blades rejected");
  rejects([] { (void)tree_shape_name(static_cast<TreeShape>(9)); }, "unknown shape name rejected");
}

void test_model_conversion(const char* obj_path) {
  std::ifstream file(obj_path);
  require(static_cast<bool>(file), "fixture OBJ opens");
  auto model = assets::import_obj(file);
  assets::MaterialLibrary materials;
  assets::Material green; green.base_color = {0.2, 0.6, 0.2};
  assets::Material brown; brown.base_color = {0.4, 0.25, 0.1};
  if (!model.sections.empty()) materials[model.sections.front().material_name] = green;
  if (model.sections.size() > 1) materials[model.sections.back().material_name] = brown;
  const auto mesh = vegetation_mesh_from_model(model, materials, {0.3, 0.5, 0.3}, "pyramid");
  check_mesh(mesh, "converted model is a valid unit model");
  require(mesh.vertices.size() == model.vertices.size() && mesh.indices == model.indices && mesh.name == "pyramid",
          "topology is preserved");
  std::set<int> tint_weights;
  for (const auto& v : mesh.vertices) tint_weights.insert(static_cast<int>(v.tintable));
  require(tint_weights.count(1) == 1, "green materials are tintable foliage");
  assets::Mesh flat = model;
  for (auto& v : flat.vertices) v.position.y = 0;
  rejects([&] { (void)vegetation_mesh_from_model(flat, materials, {0.3, 0.5, 0.3}, "flat"); }, "flat model rejected");
  assets::Mesh empty;
  rejects([&] { (void)vegetation_mesh_from_model(empty, materials, {0.3, 0.5, 0.3}, "empty"); }, "empty model rejected");
  auto wide = model;
  for (auto& v : wide.vertices) v.position.x *= 1000;
  rejects([&] { (void)vegetation_mesh_from_model(wide, materials, {0.3, 0.5, 0.3}, "wide"); }, "over-wide model rejected");
}

void test_style_and_layout() {
  VegetationStyle style;
  const auto u = pack_vegetation_style(style, {1, 2, 3}, 4.5);
  require(u.size() == 24 && near(u[0], 1) && near(u[1], 0) && u[2] == 3.f && u[3] == 4.5f && near(u[5], 0.766, 1e-3) &&
          u[7] == 0.35f && u[8] == 1.f && near(u[9], 180) && u[10] == 300.f && u[11] == 6000.f && u[12] == 1.f && u[14] == 3.f &&
          u[19] == 0.f, "std140 packing: wind, light, ranges, eye, no fire grid");
  VegetationFireGrid grid; grid.present = true; grid.cell_size_meters = 30; grid.width = 4; grid.height = 3; grid.origin_offset_x = 1; grid.origin_offset_y = 0;
  const auto with_fire = pack_vegetation_style(style, {0, 0, 0}, 0, grid);
  require(with_fire[16] == 30.f && with_fire[17] == 4.f && with_fire[18] == 3.f && with_fire[19] == 1.f && with_fire[20] == 1.f,
          "fire grid packs after the eye");
  const std::vector<std::uint32_t> states{0, 0, 2, 0,  1, 0, 0, 3,  0, 0, 0, 0};
  require(vegetation_fire_state(grid, states, 30.0, 0.0) == 2 && vegetation_fire_state(grid, states, -30.0, -30.0) == 1 &&
          vegetation_fire_state(grid, states, 44.0, -14.0) == 2 && vegetation_fire_state(grid, states, 0.0, 0.0) == 0 &&
          vegetation_fire_state(grid, states, 500.0, 0.0) == 0 && vegetation_fire_state(grid, states, -90.0, 0.0) == 0,
          "plants look up the simulation cell they stand in, rounding to the nearest center");
  VegetationFireGrid absent;
  require(vegetation_fire_state(absent, states, 30.0, 0.0) == 0, "no fire grid means no burn state");
  VegetationFireGrid bad = grid; bad.width = 0;
  rejects([&] { (void)pack_vegetation_style(style, {0, 0, 0}, 0, bad); }, "empty fire grid rejected");
  const auto reject = [&](auto mutate, const char* what) {
    auto bad = style; mutate(bad);
    rejects([&] { (void)pack_vegetation_style(bad, {0, 0, 0}, 0); }, what);
    rejects([&] { (void)place_vegetation(landscape(), style_for(landscape()), bad); }, what);
  };
  reject([](VegetationStyle& s) { s.tree_spacing_meters = 1; }, "tree spacing below four meters");
  reject([](VegetationStyle& s) { s.grass_spacing_meters = 0.1; }, "grass spacing below half a meter");
  reject([](VegetationStyle& s) { s.grass_distance_meters = -1; }, "negative grass distance");
  reject([](VegetationStyle& s) { s.tree_distance_meters = 10; }, "tree distance below 100 m");
  reject([](VegetationStyle& s) { s.tree_height_meters = 100; }, "tree height above 80 m");
  reject([](VegetationStyle& s) { s.sway_amplitude = 5; }, "sway above three");
  reject([](VegetationStyle& s) { s.wind_speed_mps = 40; }, "wind above 30 m/s");
  reject([](VegetationStyle& s) { s.color_variation = 0.9; }, "variation above a half");
  reject([](VegetationStyle& s) { s.ambient = 2; }, "ambient above one");
  reject([](VegetationStyle& s) { s.light_direction = {0, 0, 0}; }, "zero light");
  rejects([&] { (void)pack_vegetation_style(style, {0, 0, 0}, -1); }, "negative time");
  const auto layout = vegetation_shader_layout();
  validate_shader_layout(layout);
  require(layout.vertex_stride_bytes == sizeof(VegetationVertex) && layout.attributes.size() == 5 && layout.bindings.size() == 4 &&
          layout.bindings[2].name == "InstanceData" && layout.bindings[2].slot == 4 && layout.bindings[3].name == "PlantFireState" &&
          shader_program_layout(ShaderProgramId::Vegetation).vertex_stride_bytes == 44, "vegetation layout contract");
}

void test_sampler() {
  const auto mesh = landscape();
  const TerrainSampler sampler(mesh);
  for (const auto& v : mesh.vertices) {
    const auto s = sampler.sample(v.position.x, v.position.z);
    require(s && near(s->position.y, v.position.y) && near(s->fuel_load, v.fuel_load) && near(s->normal.x, v.normal.x),
            "vertices sample exactly");
  }
  // Inside a triangle the height is the linear interpolation of its corners.
  const auto& a = mesh.vertices[0];
  const auto s = sampler.sample(a.position.x + 10.0, a.position.z - 5.0);  // inside the (a, b, c) triangle of the first quad
  require(s && near(s->position.y, 100.0 + 0.2 * 10.0, 1e-9), "planar interpolation inside a triangle");
  require(!sampler.sample(-1, 0) && !sampler.sample(0, 1) && !sampler.sample(1e6, 0) && !sampler.sample(std::nan(""), 0),
          "outside the lattice samples nothing");
  // Holes: a missing quad returns nothing even inside the bounds.
  std::vector<compute::GridCell> cells;
  for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x) if (!(x == 1 && y == 1)) cells.push_back(cell(x, y, 5, 1, 0.2));
  const auto holed = terrain::build_terrain_mesh(compute::Grid(cells), 30);
  const TerrainSampler holed_sampler(holed);
  require(!holed_sampler.sample(45, -45) && !holed_sampler.sample(15, -15), "quads touching the hole are not triangulated");
  rejects([] { (void)TerrainSampler(terrain::TerrainMesh{}); }, "empty mesh rejected");
}

void test_placement() {
  const auto mesh = landscape();
  auto terrain = style_for(mesh);
  VegetationStyle style; style.tree_spacing_meters = 6; style.grass_spacing_meters = 2; style.seed = 3;
  const auto placement = place_vegetation(mesh, terrain, style);
  require(!placement.trees.empty() && !placement.grass.empty() && !placement.chunks.empty(), "forest and grassland are populated");
  const auto same = place_vegetation(mesh, terrain, style);
  require(same.trees.size() == placement.trees.size() && same.grass.size() == placement.grass.size() &&
          std::memcmp(same.trees.data(), placement.trees.data(), placement.trees.size() * sizeof(VegetationInstance)) == 0,
          "placement is deterministic for a seed");
  VegetationStyle other = style; other.seed = 4;
  const auto different = place_vegetation(mesh, terrain, other);
  require(different.trees.size() != placement.trees.size() ||
          std::memcmp(different.trees.data(), placement.trees.data(), std::min(different.trees.size(), placement.trees.size()) * sizeof(VegetationInstance)) != 0,
          "another seed places differently");
  const TerrainSampler sampler(mesh);
  std::set<std::uint32_t> tints;
  std::size_t west = 0;
  for (const auto& t : placement.trees) {
    const auto ground = sampler.sample(t.position[0], t.position[2]);
    require(ground && near(ground->position.y, t.position[1], 1e-4), "trees stand exactly on the rendered surface");
    require(t.position[0] < 5 * 30 + 1e-6, "trees grow only where the fuel load is dense (the west)");
    require(t.scale >= 0.7 * 16 * 0.85 - 1e-3 && t.scale <= 1.3 * 16 + 1e-3, "tree heights vary around the nominal height");
    require(t.rotation >= 0 && t.rotation <= 2 * 3.15, "rotation is an angle");
    ++west;
    tints.insert(static_cast<std::uint32_t>(t.tint[0] * 255) << 16 | static_cast<std::uint32_t>(t.tint[1] * 255) << 8 | static_cast<std::uint32_t>(t.tint[2] * 255));
  }
  require(west == placement.trees.size() && tints.size() > placement.trees.size() / 4, "tints differ from tree to tree");
  require(placement.trees_per_kind[1] > placement.trees_per_kind[0], "wet ground grows mostly broadleaf-like trees");
  std::size_t east_grass = 0, bare_grass = 0;
  for (const auto& g : placement.grass) {
    if (g.position[0] >= 7 * 30) ++east_grass;
    if (g.position[0] >= 5 * 30 && g.position[0] <= 6 * 30) ++bare_grass;  // both corners of these quads carry no fuel
    require(g.scale > 0 && g.scale <= 3, "grass heights are plausible");
  }
  require(east_grass > 0 && bare_grass == 0, "grass covers the grassland but not the bare strip");
  // Chunks partition the instances and bound them.
  std::size_t counted = 0;
  for (const auto& c : placement.chunks) {
    for (std::uint32_t i = c.tree_first; i < c.tree_first + c.tree_count; ++i) {
      const auto& t = placement.trees[i];
      require(t.position[0] >= c.lo.x && t.position[0] <= c.hi.x && t.position[2] >= c.lo.z && t.position[2] <= c.hi.z &&
              t.position[1] + t.scale <= c.hi.y + 1e-3, "chunk bounds enclose their trees and their height");
    }
    counted += c.tree_count;
  }
  require(counted == placement.trees.size(), "every tree belongs to exactly one chunk");
  // Water, snow and a disabled layer remove plants.
  auto flooded = terrain; flooded.submerged_level_meters = 1000;
  require(place_vegetation(mesh, flooded, style).trees.empty(), "nothing grows under water");
  VegetationStyle no_trees = style; no_trees.trees = false;
  require(place_vegetation(mesh, terrain, no_trees).trees.empty() && !place_vegetation(mesh, terrain, no_trees).grass.empty(), "trees can be disabled alone");
  VegetationStyle denser = style; denser.tree_spacing_meters = 4;
  require(place_vegetation(mesh, terrain, denser).trees.size() > placement.trees.size(), "closer spacing plants more trees");
  rejects<std::length_error>([&] { (void)place_vegetation(mesh, terrain, style, 10); }, "instance budget is enforced");
}

void test_renderer() {
  const auto mesh = landscape();
  const auto terrain = style_for(mesh);
  VegetationStyle style; style.tree_spacing_meters = 6; style.grass_spacing_meters = 2;
  ValidatingRenderDevice device;
  RenderResourceOwner shared(device);
  const std::array<float, 16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const auto camera = shared.create_buffer({BufferUsage::Uniform, sizeof(identity)}, std::as_bytes(std::span(identity)));
  const auto packed = pack_vegetation_style(style, {0, 50, 0}, 0);
  const auto style_buffer = shared.create_buffer({BufferUsage::Uniform, sizeof(packed)}, std::as_bytes(std::span(packed)));
  const auto pipeline = shared.create_pipeline({ShaderProgramId::Vegetation, vegetation_shader_layout(), true});
  const std::uint32_t no_fire = 0;
  const auto fire_states = shared.create_buffer({BufferUsage::Storage, sizeof(no_fire)}, std::as_bytes(std::span(&no_fire, 1)));
  const auto baseline = device.live_resource_bytes();
  const auto buffers = device.live_buffer_count();
  {
    VegetationRenderer renderer(device, place_vegetation(mesh, terrain, style),
                                {make_tree_mesh({TreeShape::Conifer}), make_tree_mesh({TreeShape::Broadleaf})}, make_grass_mesh(3),
                                style.grass_distance_meters, style.grass_spacing_meters);
    require(renderer.geometry_bytes() > 0 && renderer.grass_capacity() > 0, "renderer allocated instance storage");
    rejects<std::logic_error>([&] { renderer.draw(pipeline, camera, style_buffer, fire_states, Frustum::from_view_projection(math::Mat4::identity()), {0, 50, 0}, style); },
                              "drawing needs an active pass");
    const auto everything = Frustum::from_view_projection(math::Mat4::identity());
    RenderPassScope pass(device, {{64, 64}});
    const auto all = renderer.draw(pipeline, camera, style_buffer, fire_states, everything, {0, 50, 0}, style, false);
    require(all.tree_instances == renderer.placement().trees.size() && all.draws >= 2 && all.triangles > 0 &&
            all.chunks_visible == all.chunks_total, "unculled draw submits every tree");
    VegetationStyle far = style; far.grass_distance_meters = 0;
    const auto no_grass = renderer.draw(pipeline, camera, style_buffer, fire_states, everything, {0, 50, 0}, far, false);
    require(no_grass.grass_instances == 0 && no_grass.tree_instances == all.tree_instances, "grass obeys its draw distance");
    const auto far_away = renderer.draw(pipeline, camera, style_buffer, fire_states, everything, {1e5, 50, 0}, style, false);
    require(far_away.tree_instances == 0 && far_away.draws == 0, "trees beyond their distance are skipped");
    const auto none = renderer.draw(pipeline, camera, style_buffer, fire_states, Frustum::from_view_projection(math::Mat4::translation({100, 100, 100})), {0, 50, 0}, style);
    require(none.chunks_visible == 0 && none.draws == 0, "frustum culling removes every chunk");
    // Meshes and placement swap at run time; a failed swap keeps the old meshes.
    const auto before = device.live_buffer_count();
    renderer.set_tree_meshes({make_tree_mesh({TreeShape::Shrub})});
    require(device.live_buffer_count() == before - 2 && renderer.tree_meshes().size() == 1, "mesh swap releases the old buffers");
    rejects([&] { renderer.set_tree_meshes({}); }, "empty mesh list rejected");
    rejects([&] { renderer.set_tree_meshes({VegetationMesh{}, VegetationMesh{}, VegetationMesh{}}); }, "three meshes rejected");
    VegetationMesh broken = make_grass_mesh(2); broken.indices.push_back(99);
    rejects([&] { renderer.set_grass_mesh(broken); }, "broken grass mesh rejected");
    require(renderer.tree_meshes().size() == 1 && renderer.tree_meshes()[0].name == "shrub", "failed swaps keep the current meshes");
    VegetationStyle sparse = style; sparse.tree_spacing_meters = 20;
    renderer.set_placement(place_vegetation(mesh, terrain, sparse), sparse.grass_distance_meters, sparse.grass_spacing_meters);
    const auto fewer = renderer.draw(pipeline, camera, style_buffer, fire_states, everything, {0, 50, 0}, sparse, false);
    require(fewer.tree_instances < all.tree_instances && fewer.tree_instances == renderer.placement().trees.size(), "re-placement takes effect");
  }
  require(device.live_resource_bytes() == baseline && device.live_buffer_count() == buffers, "renderer releases everything");
  // Validator: vegetation instance records are 48 bytes, not 144.
  RenderPassScope pass(device, {{64, 64}});
  const auto grass = make_grass_mesh(1);
  const auto vb = shared.create_buffer({BufferUsage::Vertex, std::as_bytes(std::span(grass.vertices)).size()}, std::as_bytes(std::span(grass.vertices)));
  const auto ib = shared.create_buffer({BufferUsage::Index, std::as_bytes(std::span(grass.indices)).size()}, std::as_bytes(std::span(grass.indices)));
  std::vector<VegetationInstance> ten(10);
  const auto instances = shared.create_buffer({BufferUsage::Storage, ten.size() * sizeof(VegetationInstance)}, std::as_bytes(std::span(ten)));
  device.draw_indexed({pipeline, vb, ib, 0, 3, {{0, camera}, {kVegetationStyleSlot, style_buffer}}, {{4, instances}, {kVegetationFireSlot, fire_states}}, {}, 10});
  rejects<std::out_of_range>([&] { device.draw_indexed({pipeline, vb, ib, 0, 3, {{0, camera}, {kVegetationStyleSlot, style_buffer}}, {{4, instances}, {kVegetationFireSlot, fire_states}}, {}, 11}); },
                             "instance count beyond the 48-byte records rejected");
  rejects([&] { device.draw_indexed({pipeline, vb, ib, 0, 3, {{0, camera}, {kVegetationStyleSlot, style_buffer}}, {{4, instances}}, {}, 1}); },
          "the fire state buffer is required even without a fire");
  const std::array<float, 8> short_style{};
  const auto truncated = shared.create_buffer({BufferUsage::Uniform, sizeof(short_style)}, std::as_bytes(std::span(short_style)));
  rejects([&] { device.draw_indexed({pipeline, vb, ib, 0, 3, {{0, camera}, {kVegetationStyleSlot, truncated}}, {{4, instances}, {kVegetationFireSlot, fire_states}}, {}, 1}); },
          "short vegetation style buffer rejected");
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::runtime_error("usage: terraflux-vegetation-tests <pyramid_materials.obj>");
    test_meshes();
    test_model_conversion(argv[1]);
    test_style_and_layout();
    test_sampler();
    test_placement();
    test_renderer();
    std::cout << "TerraFlux vegetation: 6 tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "TerraFlux vegetation test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
