#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/grid.hpp>
#include <terraflux/terrain_mesh.hpp>
#include <terraflux/graphics/mesh_upload.hpp>
#include <array>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace terraflux::graphics;

namespace {

void require(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(what);
}

template <typename F> void must_throw(F&& action, const char* message) {
  bool threw = false;
  try { action(); }
  catch (const std::exception&) { threw = true; }
  require(threw, message);
}

void test_vertex_and_shader_contract() {
  const auto layout = terrain_shader_layout();
  validate_shader_layout(layout);
  require(layout.vertex_stride_bytes == 28 && layout.attributes.size() == 3,
          "terrain shader must expose packed position and normal");
  require(vertex_format_bytes(VertexFormat::Float4) == 16, "format sizing");
  auto reordered = layout;
  std::swap(reordered.attributes[0], reordered.attributes[1]);
  validate_shader_reflection(layout, reordered);
  reordered.attributes[0].location = 4;
  must_throw([&] { validate_shader_reflection(layout, reordered); }, "attribute location mismatch must fail");
}

void test_binding_reflection_mismatch() {
  auto expected = terrain_shader_layout();
  expected.bindings.push_back({"Fuel", ShaderResourceKind::SampledTexture, 2, kFragmentStage});
  auto reflected = expected;
  std::swap(reflected.bindings[0], reflected.bindings[1]);
  validate_shader_reflection(expected, reflected);
  reflected.bindings[0].slot = 7;
  must_throw([&] { validate_shader_reflection(expected, reflected); },
             "shader resource slot mismatch must fail");
}

void test_invalid_layouts() {
  auto a = terrain_shader_layout();
  a.attributes[1].location = a.attributes[0].location;
  must_throw([&] { validate_shader_layout(a); }, "duplicate location must fail");
  a = terrain_shader_layout();
  a.attributes[1].offset_bytes = 20;
  must_throw([&] { validate_shader_layout(a); }, "vertex stride overflow must fail");
  a = terrain_shader_layout();
  a.bindings.push_back({"CameraDuplicate", ShaderResourceKind::UniformBuffer, 0, kVertexStage});
  must_throw([&] { validate_shader_layout(a); }, "conflicting binding slots must fail");
  a = terrain_shader_layout();
  a.bindings[0].stages = 0;
  must_throw([&] { validate_shader_layout(a); }, "empty stage mask must fail");
}

terraflux::compute::GridCell cell(int x, int y, double z) {
  terraflux::compute::GridCell result{};
  result.x = x; result.y = y; result.elevation_meters = z; result.fuel_load = 1.0;
  return result;
}

TerrainUpload quad_upload() {
  terraflux::compute::Grid grid({cell(0, 0, 10), cell(1, 0, 10),
                                 cell(0, 1, 20), cell(1, 1, 20)});
  return prepare_terrain_upload(terraflux::terrain::build_terrain_mesh(grid, 30));
}

void test_terrain_upload() {
  const auto upload = quad_upload();
  require(upload.vertices.size() == 4 && upload.indices.size() == 6,
          "terrain uploader must preserve mesh topology");
  require(upload.vertices[0].position[1] == 10.0f &&
          upload.vertices[2].position[2] == -30.0f, "terrain coordinate conversion");
  require(upload.vertices[0].normal[1] > 0.0f, "terrain normal conversion");
  require(upload.vertices[0].fire_state == 0.0f, "new terrain must initialize unburned");
}

void test_create_draw_destroy() {
  const auto upload = quad_upload();
  ValidatingRenderDevice device;
  const auto vertex_bytes = std::as_bytes(std::span(upload.vertices));
  const auto index_bytes = std::as_bytes(std::span(upload.indices));
  const auto vb = device.create_buffer({BufferUsage::Vertex, vertex_bytes.size()}, vertex_bytes);
  const auto ib = device.create_buffer({BufferUsage::Index, index_bytes.size()}, index_bytes);
  const auto pipeline = device.create_pipeline({terrain_shader_layout(), true});
  device.draw_indexed({pipeline, vb, ib, 0, 6, {}, {}});
  device.draw_indexed({pipeline, vb, ib, 3, 3, {}, {}});
  require(device.draw_count() == 2, "valid indexed draws must be recorded");
  require(device.live_buffer_count() == 2 && device.live_pipeline_count() == 1,
          "device must own resources");
  device.destroy_buffer(vb);
  device.destroy_buffer(ib);
  device.destroy_pipeline(pipeline);
  require(device.live_buffer_count() == 0 && device.live_pipeline_count() == 0,
          "resources should be released");
  must_throw([&] { device.destroy_buffer(vb); }, "double destroy must be rejected");
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 3, {}, {}}); },
             "stale resource handles must not be valid");
}

void test_draw_range_checks() {
  auto upload = quad_upload();
  ValidatingRenderDevice device;
  auto vertex_bytes = std::as_bytes(std::span(upload.vertices));
  const auto vb = device.create_buffer({BufferUsage::Vertex, vertex_bytes.size()}, vertex_bytes);
  auto index_bytes = std::as_bytes(std::span(upload.indices));
  const auto ib = device.create_buffer({BufferUsage::Index, index_bytes.size()}, index_bytes);
  const auto pipeline = device.create_pipeline({terrain_shader_layout(), true});
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 3, 6, {}, {}}); },
             "out of range index count must be rejected");
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 4, {}, {}}); },
             "draw must consist of full triangles");
  must_throw([&] { device.draw_indexed({pipeline, ib, vb, 0, 3, {}, {}}); },
             "index/vertex buffer usage mismatch must fail");
  upload.indices[0] = 99;
  auto invalid_bytes = std::as_bytes(std::span(upload.indices));
  const auto bad_ib = device.create_buffer({BufferUsage::Index, invalid_bytes.size()}, invalid_bytes);
  must_throw([&] { device.draw_indexed({pipeline, vb, bad_ib, 0, 6, {}, {}}); },
             "index referencing missing vertex must be rejected");
}

void test_reject_incomplete_buffers() {
  ValidatingRenderDevice device;
  const std::uint32_t index = 0;
  auto bytes = std::as_bytes(std::span(&index, 1));
  must_throw([&] { (void)device.create_buffer({BufferUsage::Index, 0}, bytes); },
             "zero-size allocation must fail");
  must_throw([&] { (void)device.create_buffer({BufferUsage::Index, 16}, bytes); },
             "truncated buffer upload must fail");
}

void test_uniform_buffer_updates() {
  ValidatingRenderDevice device;
  const std::uint32_t values[] = {11, 22, 33, 44};
  const auto initial = std::as_bytes(std::span(values));
  const auto uniform = device.create_buffer({BufferUsage::Uniform, initial.size()}, initial);
  const std::uint32_t replacement = 55;
  const auto bytes = std::as_bytes(std::span(&replacement, 1));
  device.update_buffer(uniform, sizeof(std::uint32_t), bytes);
  must_throw([&] { device.update_buffer(uniform, initial.size() - 2, bytes); },
             "uniform buffer update must reject overrun");
  const auto upload = quad_upload();
  const auto vb_bytes = std::as_bytes(std::span(upload.vertices));
  const auto ib_bytes = std::as_bytes(std::span(upload.indices));
  const auto vb = device.create_buffer({BufferUsage::Vertex, vb_bytes.size()}, vb_bytes);
  const auto ib = device.create_buffer({BufferUsage::Index, ib_bytes.size()}, ib_bytes);
  const auto pipeline = device.create_pipeline({terrain_shader_layout(), true});
  device.draw_indexed({pipeline, vb, ib, 0, 6, {{0, uniform}}, {}});
  require(device.draw_count() == 1, "camera buffer draw must validate");
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 6, {{1, uniform}}, {}}); },
             "undeclared uniform slot must fail");
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 6, {{0, vb}}, {}}); },
             "incorrect uniform buffer usage must fail");
  device.destroy_buffer(uniform);
  must_throw([&] { device.update_buffer(uniform, 0, bytes); }, "destroyed uniform buffer must reject update");
}

void test_storage_buffer_validation() {
  ValidatingRenderDevice device;
  auto layout = wildfire_shader_layout();
  validate_shader_layout(layout);
  const std::uint32_t empty[4] = {0,0,0,0};
  const auto initial = std::as_bytes(std::span(empty));
  const auto field = device.create_buffer({BufferUsage::Storage, initial.size()}, initial);
  const auto upload = quad_upload();
  const auto vb = device.create_buffer(
      {BufferUsage::Vertex, std::as_bytes(std::span(upload.vertices)).size()},
      std::as_bytes(std::span(upload.vertices)));
  const auto ib = device.create_buffer(
      {BufferUsage::Index, std::as_bytes(std::span(upload.indices)).size()},
      std::as_bytes(std::span(upload.indices)));
  const auto pipeline = device.create_pipeline({layout, true});
  device.draw_indexed({pipeline, vb, ib, 0, 6, {}, {{1, field}}});
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 6, {}, {}}); },
             "missing field storage buffer must be rejected");
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 6, {}, {{1, vb}}}); },
             "vertex buffer cannot be bound as shader storage");
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 6, {}, {{2, field}}}); },
             "storage binding must match shader layout");
  device.destroy_buffer(field);
}
void test_object_uniform_bindings() {
  ValidatingRenderDevice device;
  const auto layout = object_shader_layout();
  validate_shader_layout(layout);
  const auto terrain = quad_upload();
  const auto vertices = std::as_bytes(std::span(terrain.vertices));
  const auto indices = std::as_bytes(std::span(terrain.indices));
  const auto vb = device.create_buffer({BufferUsage::Vertex, vertices.size()}, vertices);
  const auto ib = device.create_buffer({BufferUsage::Index, indices.size()}, indices);
  const auto pipeline = device.create_pipeline({layout, true});
  std::array<float, 16> camera{};
  std::array<float, 36> object_data{};
  const auto camera_bytes = std::as_bytes(std::span(camera));
  const auto object_bytes = std::as_bytes(std::span(object_data));
  const auto cb = device.create_buffer({BufferUsage::Uniform, camera_bytes.size()}, camera_bytes);
  const auto ob = device.create_buffer({BufferUsage::Uniform, object_bytes.size()}, object_bytes);
  device.draw_indexed({pipeline,vb,ib,0,6,{{0,cb},{2,ob}},{}});
  require(device.draw_count()==1, "per-object UBO draw succeeds");
  must_throw([&]{ device.draw_indexed({pipeline,vb,ib,0,6,{{0,cb},{2,ob},{2,ob}}, {}}); },
             "duplicate object UBO slot rejected");
  must_throw([&]{ device.draw_indexed({pipeline,vb,ib,0,6,{{0,cb},{7,ob}}, {}}); },
             "undeclared object UBO slot rejected");
}

void test_texture_resources() {
  ValidatingRenderDevice device;
  std::array<std::byte,16> pixels{};
  auto texture=device.create_texture2d({2,2},pixels);
  require(device.live_texture_count()==1,"texture allocated");
  must_throw([&]{(void)device.create_texture2d({2,2},
      std::span<const std::byte>(pixels.data(),8));},"reject incomplete RGBA data");
  must_throw([&]{(void)device.create_texture2d({5000,2},pixels);},"reject huge texture");
  const auto upload=quad_upload();
  const auto vbBytes=std::as_bytes(std::span(upload.vertices));
  const auto ibBytes=std::as_bytes(std::span(upload.indices));
  auto vb=device.create_buffer({BufferUsage::Vertex,vbBytes.size()},vbBytes);
  auto ib=device.create_buffer({BufferUsage::Index,ibBytes.size()},ibBytes);
  auto pipeline=device.create_pipeline({textured_object_shader_layout(),true});
  must_throw([&]{device.draw_indexed({pipeline,vb,ib,0,6,{}, {}, {}});},
             "must bind textured material");
  device.draw_indexed({pipeline,vb,ib,0,6,{}, {}, {{3,texture}}});
  require(device.draw_count()==1,"textured draw accepted");
  must_throw([&]{device.draw_indexed({pipeline,vb,ib,0,6,{}, {},
             {{3,texture},{3,texture}}});},"duplicate texture slots rejected");
  must_throw([&]{device.draw_indexed({pipeline,vb,ib,0,6,{}, {},
             {{1,texture}}});},"undeclared texture unit rejected");
  device.destroy_texture(texture);
  must_throw([&]{device.draw_indexed({pipeline,vb,ib,0,6,{}, {},
             {{3,texture}}});},"stale texture rejected");
}
void test_bad_mesh_upload() {
  auto upload = quad_upload();
  terraflux::terrain::TerrainMesh bad;
  bad.vertices.resize(1);
  bad.vertices[0].position.x = std::numeric_limits<double>::infinity();
  must_throw([&] { (void)prepare_terrain_upload(bad); },
             "unrepresentable terrain vertex must be rejected");
  bad.vertices[0].position.x = 0.0;
  bad.indices.push_back(1);
  must_throw([&] { (void)prepare_terrain_upload(bad); },
             "out-of-range terrain triangle must be rejected");
  require(upload.vertices.size() == 4, "sanity check");
}

}  // namespace

int main() {
  try {
    test_vertex_and_shader_contract();
    test_binding_reflection_mismatch();
    test_invalid_layouts();
    test_terrain_upload();
    test_create_draw_destroy();
    test_draw_range_checks();
    test_reject_incomplete_buffers();
    test_bad_mesh_upload();
    test_uniform_buffer_updates();
    test_storage_buffer_validation();
    test_object_uniform_bindings();
    test_texture_resources();
    std::cout << "TerraFlux graphics contracts: 12 tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "TerraFlux graphics test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
