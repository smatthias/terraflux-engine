#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/composite.hpp>
#include <terraflux/graphics/render_pass.hpp>
#include <terraflux/graphics/layer_visibility.hpp>
#include <type_traits>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/terrain_shading.hpp>
#include <terraflux/grid.hpp>
#include <terraflux/terrain_mesh.hpp>
#include <terraflux/graphics/mesh_upload.hpp>
#include <array>
#include <algorithm>

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
  require(layout.vertex_stride_bytes == 40 && layout.attributes.size() == 4 &&
          layout.attributes[3].semantic == "SURFACE" && layout.attributes[3].format == VertexFormat::Float3 &&
          layout.attributes[3].offset_bytes == 28,
          "terrain shader must expose packed position, normal, fire state and surface attributes");
  const auto object = object_shader_layout();
  require(object.vertex_stride_bytes == 40 && object.attributes.size() == 2,
          "object shader keeps the terrain stride without fire or surface channels");
  const auto wildfire = wildfire_shader_layout();
  require(wildfire.attributes.size() == 3 && std::none_of(wildfire.attributes.begin(), wildfire.attributes.end(),
          [](const VertexAttribute& a) { return a.semantic == "FIRE_STATE"; }),
          "wildfire shader reads fire from storage but keeps surface attributes");
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
  a.attributes[1].offset_bytes = a.vertex_stride_bytes - 8;
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
  require(upload.vertices[0].surface[0] == 1.0f && upload.vertices[0].surface[1] == 0.0f,
          "fuel load and moisture travel with the vertex");
}

void test_create_draw_destroy() {
  const auto upload = quad_upload();
  ValidatingRenderDevice device;
  device.begin_render_pass({{640,480}});
  const auto vertex_bytes = std::as_bytes(std::span(upload.vertices));
  const auto index_bytes = std::as_bytes(std::span(upload.indices));
  const auto vb = device.create_buffer({BufferUsage::Vertex, vertex_bytes.size()}, vertex_bytes);
  const auto ib = device.create_buffer({BufferUsage::Index, index_bytes.size()}, index_bytes);
  const auto pipeline = device.create_pipeline({terraflux::graphics::ShaderProgramId::Terrain,terrain_shader_layout(), true});
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
  device.begin_render_pass({{640,480}});
  auto vertex_bytes = std::as_bytes(std::span(upload.vertices));
  const auto vb = device.create_buffer({BufferUsage::Vertex, vertex_bytes.size()}, vertex_bytes);
  auto index_bytes = std::as_bytes(std::span(upload.indices));
  const auto ib = device.create_buffer({BufferUsage::Index, index_bytes.size()}, index_bytes);
  const auto pipeline = device.create_pipeline({terraflux::graphics::ShaderProgramId::Terrain,terrain_shader_layout(), true});
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
  device.begin_render_pass({{640,480}});
  const std::uint32_t index = 0;
  auto bytes = std::as_bytes(std::span(&index, 1));
  must_throw([&] { (void)device.create_buffer({BufferUsage::Index, 0}, bytes); },
             "zero-size allocation must fail");
  must_throw([&] { (void)device.create_buffer({BufferUsage::Index, 16}, bytes); },
             "truncated buffer upload must fail");
}

void test_uniform_buffer_updates() {
  ValidatingRenderDevice device;
  device.begin_render_pass({{640,480}});
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
  const auto pipeline = device.create_pipeline({terraflux::graphics::ShaderProgramId::Terrain,terrain_shader_layout(), true});
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
  device.begin_render_pass({{640,480}});
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
  const auto flags=LayerVisibility{}.uniform();
  const auto layers=device.create_buffer({BufferUsage::Uniform,sizeof(flags)},std::as_bytes(std::span(&flags,1)));
  const auto pipeline = device.create_pipeline({ShaderProgramId::ContractOnly,layout, true});
  device.draw_indexed({pipeline, vb, ib, 0, 6, {{10,layers}}, {{1, field}}});
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 6, {{10,layers}}, {}}); },
             "missing field storage buffer must be rejected");
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 6, {{10,layers}}, {{1, vb}}}); },
             "vertex buffer cannot be bound as shader storage");
  must_throw([&] { device.draw_indexed({pipeline, vb, ib, 0, 6, {{10,layers}}, {{2, field}}}); },
             "storage binding must match shader layout");
  device.destroy_buffer(field);
}
void test_object_uniform_bindings() {
  ValidatingRenderDevice device;
  device.begin_render_pass({{640,480}});
  const auto layout = object_shader_layout();
  validate_shader_layout(layout);
  const auto terrain = quad_upload();
  const auto vertices = std::as_bytes(std::span(terrain.vertices));
  const auto indices = std::as_bytes(std::span(terrain.indices));
  const auto vb = device.create_buffer({BufferUsage::Vertex, vertices.size()}, vertices);
  const auto ib = device.create_buffer({BufferUsage::Index, indices.size()}, indices);
  const auto pipeline = device.create_pipeline({ShaderProgramId::Object,layout, true});
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
  device.begin_render_pass({{640,480}});
  std::array<std::byte,16> pixels{};
  auto texture=device.create_texture2d({2,2},pixels);
  require(device.live_texture_count()==1,"texture allocated");
  must_throw([&]{(void)device.create_texture2d({2,2},
      std::span<const std::byte>(pixels.data(),8));},"reject incomplete RGBA data");
  must_throw([&]{(void)device.create_texture2d({5000,2},pixels);},"reject huge texture");
  const auto upload=quad_upload();
  std::vector<TexturedGpuVertex> textured_vertices(upload.vertices.size());
  const auto vbBytes=std::as_bytes(std::span(textured_vertices));
  const auto ibBytes=std::as_bytes(std::span(upload.indices));
  auto vb=device.create_buffer({BufferUsage::Vertex,vbBytes.size()},vbBytes);
  auto ib=device.create_buffer({BufferUsage::Index,ibBytes.size()},ibBytes);
  auto pipeline=device.create_pipeline({terraflux::graphics::ShaderProgramId::TexturedObject,textured_object_shader_layout(),true});
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

void test_resource_bytes() {
  ValidatingRenderDevice device;
  device.begin_render_pass({{640,480}});
  std::array<std::byte,32> buffer{};
  std::array<std::byte,60> pixels{};
  TextureDesc desc{3,5};
  desc.sampling.min=terraflux::TextureMinFilter::LinearMipLinear;
  require(rgba8_storage_bytes(desc)==72,"non-power-of-two mip chain byte accounting");
  auto no_mips=desc;no_mips.sampling.min=terraflux::TextureMinFilter::Linear;
  require(rgba8_storage_bytes(no_mips)==60,"base-level-only texture accounting");
  must_throw([&]{(void)rgba8_storage_bytes({0,1});},"invalid texture dimensions rejected");
  device.set_resource_byte_budget(104);
  const auto storage=device.create_buffer({BufferUsage::Storage,buffer.size()},buffer);
  const auto texture=device.create_texture2d(desc,pixels);
  require(device.live_resource_bytes()==ResourceBytes{32,72},"buffers and complete mip chains counted separately");
  device.orphan_storage_buffer(storage);
  device.update_buffer(storage,0,buffer);
  require(device.live_resource_bytes().total()==104,"orphan/update preserve logical capacity accounting");
  must_throw([&]{(void)device.create_buffer({BufferUsage::Vertex,1},std::span(buffer).first(1));},
             "allocation over exact byte cap rejected");
  require(device.live_buffer_count()==1 && device.live_resource_bytes().total()==104,
          "failed admission leaves no resource or byte leak");
  device.destroy_texture(texture);
  require(device.live_resource_bytes()==ResourceBytes{32,0},"texture release subtracts mip storage");
  device.set_resource_byte_budget(31);
  require(device.live_resource_bytes().total()==32,"lowering cap preserves existing allocation");
  must_throw([&]{(void)device.create_texture2d({1,1},std::span(pixels).first(4));},
             "lowered cap blocks further allocations");
  device.destroy_buffer(storage);
  require(device.live_resource_bytes().total()==0,"full destruction restores byte baseline");
  device.set_resource_byte_budget(0);
  must_throw([&]{(void)device.create_buffer({BufferUsage::Vertex,1},std::span(buffer).first(1));},
             "zero byte cap rejects positive storage");
  const auto maximum=std::numeric_limits<std::uint64_t>::max();
  check_resource_byte_budget(maximum-1,1,maximum);
  must_throw([&]{check_resource_byte_budget(maximum-1,2,maximum);},"budget addition cannot wrap");
}

void test_resource_telemetry() {
  ValidatingRenderDevice device;
  device.begin_render_pass({{640,480}});
  std::array<std::byte,32> bytes{};
  device.set_resource_byte_budget(64);
  const auto first=device.create_buffer({BufferUsage::Storage,bytes.size()},bytes);
  const auto texture=device.create_texture2d({2,2},std::span(bytes).first(16));
  auto stats=device.resource_telemetry();
  require(stats.allocation_count==2 && stats.peak_total_bytes==48 && stats.headroom_bytes==16,
          "initial allocations update peaks and headroom");
  must_throw([&]{(void)device.create_buffer({BufferUsage::Storage,bytes.size()},bytes);},
             "telemetry observes rejected allocation");
  stats=device.resource_telemetry();
  require(stats.budget_rejections==1 && stats.allocation_count==2 && stats.last_rejection &&
          stats.last_rejection->used_bytes==48 && stats.last_rejection->requested_bytes==32 &&
          stats.last_rejection->limit_bytes==64,"rejection diagnostic preserves exact request and prior usage");
  device.destroy_buffer(first);
  const auto recovered=device.create_buffer({BufferUsage::Storage,bytes.size()},bytes);
  device.orphan_storage_buffer(recovered);
  stats=device.resource_telemetry();
  require(stats.allocation_count==3 && stats.budget_rejections==1 && stats.headroom_bytes==16,
          "successful retry recovers without erasing failure history or counting orphan twice");
  device.destroy_texture(texture);
  const auto second=device.create_buffer({BufferUsage::Uniform,bytes.size()},bytes);
  stats=device.resource_telemetry();
  require(stats.peak_bytes.buffer_bytes==64 && stats.peak_bytes.texture_bytes==16 &&
          stats.peak_total_bytes==64,"category peaks need not occur at same time");
  device.set_resource_byte_budget(16);
  require(device.resource_telemetry().over_budget && !device.resource_telemetry().headroom_bytes,
          "lowered cap exposes over-budget state without unsigned headroom wrap");
  device.reset_resource_telemetry();
  stats=device.resource_telemetry();
  require(stats.peak_total_bytes==64 && !stats.budget_rejections && !stats.last_rejection &&
          !stats.allocation_count && stats.over_budget,"reset rebases peaks to live usage and clears history");
  device.destroy_buffer(recovered);device.destroy_buffer(second);
  require(device.resource_telemetry().peak_total_bytes==64 && device.resource_telemetry().headroom_bytes==16,
          "freeing resources restores headroom while retaining historical peak");
  device.reset_resource_telemetry();
  require(!device.resource_telemetry().peak_total_bytes,"empty reset clears byte peaks");
  must_throw([&]{(void)device.create_buffer({BufferUsage::Vertex,0},{});},"malformed request rejected");
  require(!device.resource_telemetry().budget_rejections,"validation error is not a budget rejection");
}

void test_layer_visibility_contract() {
  ValidatingRenderDevice device;RenderResourceOwner owner(device);
  const auto upload=quad_upload();
  const auto vb=owner.create_buffer({BufferUsage::Vertex,upload.vertices.size()*sizeof(TerrainGpuVertex)},std::as_bytes(std::span(upload.vertices)));
  const auto ib=owner.create_buffer({BufferUsage::Index,upload.indices.size()*4},std::as_bytes(std::span(upload.indices)));
  const std::array<std::uint32_t,4> cells{2,2,0,1};
  const auto field=owner.create_buffer({BufferUsage::Storage,sizeof(cells)},std::as_bytes(std::span(cells)));
  const auto pipeline=owner.create_pipeline({ShaderProgramId::Wildfire,wildfire_shader_layout()});
  auto flags=LayerVisibility{}.uniform();
  const auto layers=owner.create_buffer({BufferUsage::Uniform,sizeof(flags)},std::as_bytes(std::span(&flags,1)));
  const std::array<std::byte,8> small{};
  const auto short_buffer=owner.create_buffer({BufferUsage::Uniform,small.size()},small);
  RenderPassScope pass(device,{{64,64}});
  DrawIndexed draw{pipeline,vb,ib,0,6,{},{{1,field}}};
  must_throw([&]{device.draw_indexed(draw);},"wildfire layers require explicit uniform binding");
  draw.uniform_buffers={{10,short_buffer}};
  must_throw([&]{device.draw_indexed(draw);},"undersized layer uniform rejected");
  draw.uniform_buffers={{10,layers}};
  for(bool terrain:{false,true})for(bool fire:{false,true}) {
    const LayerVisibility state{terrain,fire};flags=state.uniform();
    require(flags.flags[0]==float(terrain) && flags.flags[1]==float(fire) &&
            state.surface_visible(true)==(terrain||fire) && state.surface_visible(false)==terrain,
            "layer uniform and surface routing preserve independent combinations");
    device.update_buffer(layers,0,std::as_bytes(std::span(&flags,1)));device.draw_indexed(draw);
  }
  require(device.draw_count()==4,"only valid layer bindings submit");
}

void test_render_pass_scopes() {
  static_assert(!std::is_copy_constructible_v<RenderPassScope> &&
                !std::is_move_constructible_v<RenderPassScope> &&
                std::is_nothrow_destructible_v<RenderPassScope>);
  ValidatingRenderDevice device;
  require(!device.active_render_pass_id(),"new device has no active pass identity");
  std::uint64_t first=0;
  {
    RenderPassScope pass(device,{{64,64}});first=device.active_render_pass_id();
    require(first && pass.active(),"scope owns its successful begin");
    must_throw([&]{RenderPassScope nested(device,{{64,64}});},"nested scope construction rejects");
    require(device.active_render_pass_id()==first && pass.active(),"failed nested begin preserves outer owner");
    pass.end();pass.end();
    require(!pass.active() && !device.active_render_pass_id(),"explicit end is idempotent");
    device.begin_render_pass({{64,64}});
  }
  require(device.active_render_pass_id()>first,"closed scope destructor leaves newer raw pass alone");
  device.end_render_pass();
  must_throw([&]{RenderPassScope invalid(device,{{0,64}});},"failed begin never acquires ownership");
  require(!device.active_render_pass_id(),"failed construction leaves pass inactive");
  std::uint64_t newer=0;
  {
    RenderPassScope stale(device,{{64,64}});
    device.end_render_pass();device.begin_render_pass({{64,64}});
    newer=device.active_render_pass_id();
    require(!stale.active(),"externally replaced pass is no longer owned");
    must_throw([&]{stale.end();},"explicit close reports raw-API ownership mismatch");
  }
  require(device.active_render_pass_id()==newer,"stale destructor cannot close somebody else's pass");
  device.end_render_pass();
  const auto baseline=device.live_resource_bytes();
  for(int failure=0;failure<3;++failure) {
    bool caught=false;
    try {
      RenderResourceOwner resources(device); // Outlives the pass during unwinding.
      const auto target=resources.create_render_target({8,8});
      RenderPassScope pass(device,{{8,8,RenderTargetKind::Offscreen,target}});
      if(failure==0)throw std::runtime_error("original scene exception");
      if(failure==1)device.draw_indexed({});
      if(failure==2) {
        device.set_resource_byte_budget(device.live_resource_bytes().total());
        resources.create_render_target({1,1});
      }
    } catch(const std::exception& error) {
      caught=true;
      if(failure==0)require(std::string(error.what())=="original scene exception",
                           "scope cleanup preserves original exception");
    }
    device.set_resource_byte_budget(std::numeric_limits<std::uint64_t>::max());
    require(caught && !device.active_render_pass_id(),"throwing work automatically ends its pass");
    require(device.live_resource_bytes()==baseline && !device.live_render_target_count() &&
            !device.live_texture_count(),"unwinding closes pass before releasing target storage");
    RenderPassScope recovered(device,{{64,64}}); // Normal implicit destructor closure.
    require(recovered.active(),"new pass starts after a drawing failure");
  }
  require(!device.active_render_pass_id(),"normal scope exit ends its pass");
}

void test_resize_recovery() {
  ValidatingRenderDevice device;
  const auto baseline=device.live_resource_bytes();
  {
    ResizableRenderTarget surface(device);
    require(!surface.resize(0,64) && !surface.target(),"minimized initial surface stays empty");
    must_throw([&]{(void)surface.color();},"empty surface cannot supply color");
    require(surface.resize(8,8),"first extent allocated");
    const auto original=*surface.target();const auto color=surface.color();
    const auto bytes=device.live_resource_bytes();
    require(!surface.resize(8,8) && !surface.resize(0,0) && surface.color()==color,
            "unchanged/minimized extents keep allocation");
    device.set_resource_byte_budget(bytes.total());
    must_throw([&]{surface.resize(16,16);},"candidate must fit alongside old target");
    require(surface.target()->handle==original.handle && surface.color()==color &&
            device.live_resource_bytes()==bytes,"failed resize preserves source and bytes");
    device.begin_render_pass({original});device.end_render_pass();
    device.set_resource_byte_budget(4096);
    device.begin_render_pass({original});
    must_throw([&]{surface.resize(16,16);},"resize cannot retire an active target");
    must_throw([&]{surface.reset();},"reset cannot retire an active target");
    require(surface.color()==color && device.live_resource_bytes()==bytes,
            "active resize rejects and cleans its candidate");
    device.end_render_pass();
    require(surface.resize(16,16),"same requested extent recovers after cap increases");
    require(surface.target()->handle!=original.handle && device.live_render_target_count()==1,
            "successful replacement retires original storage");
    must_throw([&]{(void)device.render_target_color(original.handle);},"old view invalidated on commit");
    require(device.live_resource_bytes().render_target_bytes==16*16*8,"replacement accounted once");
    must_throw([&]{surface.resize(4097,16);},"unsupported resize rejects");
    require(surface.target()->width==16,"unsupported resize preserves last successful extent");
    surface.reset();surface.reset();
    require(!surface.target() && device.live_resource_bytes()==baseline,"reset returns to baseline");
    surface.resize(3,5);
  }
  require(device.live_resource_bytes()==baseline && !device.live_texture_count(),
          "surface destruction retires storage and borrowed views");
}

void test_sampled_target_composition() {
  ValidatingRenderDevice device;
  const auto initial=device.live_resource_bytes();
  TextureHandle stale;
  {
    FullscreenComposite composite(device);
    must_throw([&]{composite.draw({});},"composition requires an active pass");
    const auto buffer_bytes=device.live_resource_bytes().buffer_bytes;
    const auto a=device.create_render_target({8,8});
    const auto b=device.create_render_target({4,4});
    const auto color=device.render_target_color(a);stale=color;
    require(color && device.render_target_color(a)==color,"stable borrowed color view");
    const auto bytes=device.live_resource_bytes();
    require(bytes==ResourceBytes{buffer_bytes,0,(64+16)*8},"borrowed views do not double count bytes");
    require(device.live_texture_count()==2,"color views tracked as bindable textures");
    must_throw([&]{device.destroy_texture(color);},"borrowed color cannot be separately destroyed");
    device.begin_render_pass({{8,8,RenderTargetKind::Offscreen,a}});
    must_throw([&]{composite.draw(color);},"active color sampling feedback rejected");
    device.end_render_pass();
    device.begin_render_pass({{4,4,RenderTargetKind::Offscreen,b}});
    composite.draw(color);
    device.end_render_pass();
    device.begin_render_pass({{64,64}});composite.draw(device.render_target_color(b));
    device.end_render_pass();
    device.destroy_render_target(a);
    must_throw([&]{(void)device.render_target_color(a);},"destroyed target cannot return color");
    device.begin_render_pass({{64,64}});
    must_throw([&]{composite.draw(stale);},"borrowed view expires with target");
    composite.draw(device.render_target_color(b)); // Rejection leaves the pass usable.
    device.end_render_pass();device.destroy_render_target(b);
    require(!device.live_texture_count(),"target destruction invalidates all borrowed views");
    require(device.draw_count()==3,"invalid source draws were not submitted");
  }
  require(device.live_resource_bytes()==initial && !device.live_pipeline_count(),
          "composite and target resources return to baseline");
  const auto layout=shader_program_layout(ShaderProgramId::Composite);
  must_throw([&]{validate_pipeline_desc({ShaderProgramId::Composite,layout,true,false,false});},
             "composite forbids depth testing");
  must_throw([&]{validate_pipeline_desc({ShaderProgramId::Composite,layout,false,false,true});},
             "composite forbids depth writes");
  auto altered=layout;altered.bindings[0].slot=3;
  must_throw([&]{validate_pipeline_desc({ShaderProgramId::Composite,altered,false,false,false});},
             "composite slot is explicit");
  device.set_resource_byte_budget(64); // Vertex allocation fits; index allocation must roll back.
  must_throw([&]{FullscreenComposite composite(device);},"composite construction obeys shared cap");
  require(device.live_resource_bytes()==initial,"failed composite construction leaves no resources");
}

void test_offscreen_targets() {
  ValidatingRenderDevice device;
  require(offscreen_storage_bytes({3,5})==120,"RGBA8 plus Depth32Float counted exactly");
  for(const auto desc:{OffscreenTargetDesc{0,1},OffscreenTargetDesc{1,0},
      OffscreenTargetDesc{4097,1},OffscreenTargetDesc{2001,2000},
      OffscreenTargetDesc{1,1,static_cast<ColorAttachmentFormat>(255)},
      OffscreenTargetDesc{1,1,ColorAttachmentFormat::Rgba8,static_cast<DepthAttachmentFormat>(255)}})
    must_throw([&]{(void)device.create_render_target(desc);},"invalid allocation descriptor rejected");
  require(device.resource_telemetry().allocation_count==0,"bad descriptors never admitted");
  device.set_resource_byte_budget(120);
  const auto target=device.create_render_target({3,5});
  require(device.live_resource_bytes()==ResourceBytes{0,0,120},"target storage has its own category");
  must_throw([&]{(void)device.create_render_target({1,1});},"shared byte cap rejects target candidate");
  require(device.live_render_target_count()==1 && device.resource_telemetry().budget_rejections==1,
          "rejected candidate leaves original target and telemetry intact");
  auto pass=RenderPassDesc{{3,5,RenderTargetKind::Offscreen,target}};
  auto invalid=pass;invalid.target.width=4;
  must_throw([&]{device.begin_render_pass(invalid);},"pass extent must match allocation");
  invalid=pass;invalid.target.kind=RenderTargetKind::Window;
  must_throw([&]{device.begin_render_pass(invalid);},"window target cannot carry owned handle");
  invalid=pass;invalid.target.handle={};
  must_throw([&]{device.begin_render_pass(invalid);},"offscreen target requires owned handle");
  device.begin_render_pass(pass);
  must_throw([&]{device.destroy_render_target(target);},"active target cannot be destroyed");
  device.end_render_pass();
  pass.color_load=AttachmentLoadOp::Load;pass.depth_load=AttachmentLoadOp::Load;
  device.begin_render_pass(pass);device.end_render_pass();
  device.set_resource_byte_budget(119);
  require(device.resource_telemetry().over_budget,"lower cap preserves existing target");
  device.destroy_render_target(target);
  must_throw([&]{device.begin_render_pass(pass);},"destroyed target cannot be rendered");
  must_throw([&]{device.destroy_render_target(target);},"double target destruction rejected");
  require(device.live_resource_bytes()==ResourceBytes{} && !device.live_render_target_count(),
          "target cleanup returns all storage to baseline");
  const auto t=device.resource_telemetry();
  require(t.peak_bytes.render_target_bytes==120 && t.peak_total_bytes==120 && t.allocation_count==1,
          "target peaks survive destruction");
  device.reset_resource_telemetry();
  require(device.resource_telemetry().peak_bytes==ResourceBytes{},"reset rebases target peak");
  device.set_resource_byte_budget(128);
  const auto larger=device.create_render_target({4,4});
  require(larger!=target,"replacement cannot revive stale handles");
  device.begin_render_pass({{4,4,RenderTargetKind::Offscreen,larger}});device.end_render_pass();
  device.destroy_render_target(larger);
}

void test_render_pass_contract() {
  ValidatingRenderDevice device;
  const auto upload=quad_upload();
  const auto vertices=std::as_bytes(std::span(upload.vertices));
  const auto indices=std::as_bytes(std::span(upload.indices));
  const auto vb=device.create_buffer({BufferUsage::Vertex,vertices.size()},vertices);
  const auto ib=device.create_buffer({BufferUsage::Index,indices.size()},indices);
  const auto pipeline=device.create_pipeline({ShaderProgramId::Terrain,terrain_shader_layout()});
  const DrawIndexed draw{pipeline,vb,ib,0,6,{}, {}};
  const auto before=device.live_resource_bytes();
  must_throw([&]{device.draw_indexed(draw);},"draw before begin rejected");
  must_throw([&]{device.end_render_pass();},"end without begin rejected");
  const RenderPassDesc valid{{640,480}};
  auto bad=valid;
  bad.target.width=0;
  must_throw([&]{device.begin_render_pass(bad);},"zero width rejected");
  bad=valid;bad.target.height=0;
  must_throw([&]{device.begin_render_pass(bad);},"zero height rejected");
  bad=valid;bad.target.width=std::numeric_limits<std::uint32_t>::max();
  must_throw([&]{device.begin_render_pass(bad);},"extent overflow rejected");
  bad=valid;bad.target.kind=static_cast<RenderTargetKind>(255);
  must_throw([&]{device.begin_render_pass(bad);},"unknown target rejected");
  bad=valid;bad.color_load=static_cast<AttachmentLoadOp>(255);
  must_throw([&]{device.begin_render_pass(bad);},"unknown color load rejected");
  bad=valid;bad.depth_load=static_cast<AttachmentLoadOp>(255);
  must_throw([&]{device.begin_render_pass(bad);},"unknown depth load rejected");
  for(const auto value:{-0.1f,1.1f,std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
    bad=valid;bad.clear_depth=value;
    must_throw([&]{device.begin_render_pass(bad);},"invalid depth rejected");
    for(std::size_t i=0;i<4;++i) {
      bad=valid;bad.clear_color[i]=value;
      must_throw([&]{device.begin_render_pass(bad);},"invalid color channel rejected");
    }
  }
  must_throw([&]{device.end_render_pass();},"failed begins leave pass inactive");
  auto clear=valid;clear.clear_color={0,1,0,1};clear.clear_depth=0;
  device.begin_render_pass(clear);
  must_throw([&]{device.begin_render_pass(valid);},"nested begin rejected");
  device.draw_indexed(draw); // Failed nesting must preserve the active pass.
  device.end_render_pass();
  must_throw([&]{device.draw_indexed(draw);},"draw after end rejected");
  must_throw([&]{device.end_render_pass();},"double end rejected");
  for(const auto color:{AttachmentLoadOp::Load,AttachmentLoadOp::Clear})
    for(const auto depth:{AttachmentLoadOp::Load,AttachmentLoadOp::Clear}) {
      device.begin_render_pass({{800,600},color,depth});
      device.draw_indexed(draw);
      device.end_render_pass();
    }
  require(device.draw_count()==5,"only draws inside valid passes counted");
  require(device.live_resource_bytes()==before,"borrowed target allocates no API storage");
  device.destroy_pipeline(pipeline);device.destroy_buffer(ib);device.destroy_buffer(vb);
}

void test_explicit_shader_identity() {
  ValidatingRenderDevice device;
  device.begin_render_pass({{640,480}});
  const std::array programs{ShaderProgramId::Terrain,ShaderProgramId::Wildfire,ShaderProgramId::Object,
      ShaderProgramId::TexturedObject,ShaderProgramId::InstancedTexturedObject,
      ShaderProgramId::PbrObject,ShaderProgramId::PbrInstanced};
  for(const auto program:programs) {
    auto layout=shader_program_layout(program);
    // Reflection/metadata order must not determine identity or validity.
    std::reverse(layout.attributes.begin(),layout.attributes.end());
    std::reverse(layout.bindings.begin(),layout.bindings.end());
    const auto pipeline=device.create_pipeline({program,layout,true});
    device.destroy_pipeline(pipeline);
  }
  require(!device.live_pipeline_count(),"all explicit built-in families validate and release");
  const auto layout=terrain_shader_layout();
  must_throw([&]{(void)device.create_pipeline({ShaderProgramId::Unspecified,layout});},
             "unspecified identity has no automatic layout fallback");
  must_throw([&]{(void)device.create_pipeline({static_cast<ShaderProgramId>(255),layout});},
             "unknown shader identity rejected");
  must_throw([&]{(void)device.create_pipeline({ShaderProgramId::PbrObject,layout});},
             "declared identity and incompatible layout rejected");
  auto changed=layout;changed.bindings[0].slot=7;
  must_throw([&]{(void)device.create_pipeline({ShaderProgramId::Terrain,changed});},
             "resource names do not silently redirect shader family");
  changed=layout;changed.attributes[0].offset_bytes=4;
  must_throw([&]{(void)device.create_pipeline({ShaderProgramId::Terrain,changed});},
             "built-in vertex packing must match its declared contract");
  const auto contract=device.create_pipeline({ShaderProgramId::ContractOnly,layout});
  device.destroy_pipeline(contract);
  must_throw([&]{validate_pipeline_desc({ShaderProgramId::ContractOnly,layout},false);},
             "contract-only identity rejected at GPU boundary");
  must_throw([&]{(void)device.create_pipeline({ShaderProgramId::PbrInstanced,
      shader_program_layout(ShaderProgramId::PbrInstanced),true,true,false});},
      "transparent instancing is outside the explicit program contract");
  const auto alpha=device.create_pipeline({ShaderProgramId::PbrObject,
      shader_program_layout(ShaderProgramId::PbrObject),true,true,false});
  device.destroy_pipeline(alpha);
  require(!device.live_pipeline_count(),"identity failures cannot leak pipeline handles");
}

bool near(double a,double b,double tolerance=1e-9) {return std::abs(a-b)<tolerance;}
bool near(terraflux::math::Vec3 a,terraflux::math::Vec3 b,double tolerance=1e-9) {
  return near(a.x,b.x,tolerance) && near(a.y,b.y,tolerance) && near(a.z,b.z,tolerance);
}

void test_terrain_style_contract() {
  // Both ground shaders declare the fragment-stage style block; objects do not.
  for(const auto program:{ShaderProgramId::Terrain,ShaderProgramId::Wildfire}) {
    const auto layout=shader_program_layout(program);
    const auto style=std::find_if(layout.bindings.begin(),layout.bindings.end(),
        [](const ShaderBinding& b){return b.name=="TerrainStyle";});
    require(style!=layout.bindings.end() && style->kind==ShaderResourceKind::UniformBuffer &&
            style->slot==kTerrainStyleSlot && style->stages==(kVertexStage|kFragmentStage),"terrain style block declared for both stages");
    validate_shader_layout(layout);
  }
  const auto object=object_shader_layout();
  require(std::none_of(object.bindings.begin(),object.bindings.end(),
          [](const ShaderBinding& b){return b.name=="TerrainStyle";}),"object shader has no terrain style");

  TerrainStyle style;style.min_elevation_meters=100;style.max_elevation_meters=600;
  style.contour_interval_meters=50;style.light_direction={0,2,0};
  const auto packed=pack_terrain_style(style);
  require(sizeof(packed)==kTerrainStyleBytes && kTerrainStyleBytes==96 && packed[0]==100.f && packed[1]==600.f &&
          near(packed[2],1.0/500,1e-9) && packed[3]==50.f && packed[4]==28.f && packed[5]==48.f &&
          packed[6]==1.f && near(packed[7],0.12,1e-6) && packed[8]==0.f && packed[9]==1.f && packed[10]==0.f &&
          near(packed[11],0.30,1e-6) && packed[12]==1.f && packed[13]==5.f && packed[14]==1.f && packed[15]==0.f &&
          packed[16]==0.f && packed[17]==-1.f && packed[18]==45.f && packed[19]==0.f &&
          packed[20]==0.25f && packed[21]==0.25f && packed[22]==0.25f && packed[23]==0.25f,
          "std140 packing: range, contours, slopes, snow line, ridge, light, ambient, detail, no water, normalized floor weights");
  auto flooded=style;flooded.submerged_level_meters=250;flooded.floor.kind=WaterFloorKind::Stones;flooded.floor.seed=3;
  flooded.floor.sand=2;flooded.floor.vegetation=0;flooded.floor.mud=1;flooded.floor.stones=1;
  const auto wet=pack_terrain_style(flooded);
  require(wet[16]==250.f && wet[17]==3.f && near(wet[19],3*37.31,1e-3) && wet[20]==0.5f && wet[21]==0.f && wet[22]==0.25f && wet[23]==0.25f,
          "submerged level, floor kind, seed offset and weights are packed");
  TerrainStyle flat;flat.min_elevation_meters=flat.max_elevation_meters=250;
  require(pack_terrain_style(flat)[2]==0.f,"flat terrain packs a zero inverse range instead of infinity");
  const auto reject=[&](auto mutate,const char* what) {
    auto bad=style;mutate(bad);
    must_throw([&]{(void)pack_terrain_style(bad);},what);
    must_throw([&]{(void)terrain_surface_color(120,{0,1,0},bad);},what);
  };
  reject([](TerrainStyle& s){s.min_elevation_meters=s.max_elevation_meters+1;},"inverted range rejected");
  reject([](TerrainStyle& s){s.max_elevation_meters=std::numeric_limits<double>::quiet_NaN();},"NaN range rejected");
  reject([](TerrainStyle& s){s.contour_interval_meters=-1;},"negative contours rejected");
  reject([](TerrainStyle& s){s.rock_slope_start_degrees=s.rock_slope_full_degrees;},"empty rock transition rejected");
  reject([](TerrainStyle& s){s.rock_slope_full_degrees=91;},"rock angle above vertical rejected");
  reject([](TerrainStyle& s){s.rock_slope_start_degrees=-1;},"negative rock angle rejected");
  reject([](TerrainStyle& s){s.light_direction={0,0,0};},"zero light rejected");
  reject([](TerrainStyle& s){s.light_direction.y=std::numeric_limits<double>::infinity();},"infinite light rejected");
  reject([](TerrainStyle& s){s.ambient=1.5;},"ambient above one rejected");
  reject([](TerrainStyle& s){s.ambient=-0.1;},"negative ambient rejected");

  terraflux::compute::Grid grid({cell(0,0,-20),cell(1,0,35),cell(0,1,10),cell(1,1,10)});
  const auto mesh=terraflux::terrain::build_terrain_mesh(grid,30);
  const auto derived=terrain_style_for(mesh,25);
  require(derived.min_elevation_meters==-20 && derived.max_elevation_meters==35 &&
          derived.contour_interval_meters==25 && derived.snow_line_fraction==1.0,
          "style follows the mesh elevation range; 55 m of relief gets no snow");
  require(terrain_style_for(mesh).contour_interval_meters==5.0,"automatic contours for 55 m of relief");
  require(auto_contour_interval(0)==0 && auto_contour_interval(-3)==0 && auto_contour_interval(578)==50 &&
          auto_contour_interval(95)==10 && auto_contour_interval(255)==25 && auto_contour_interval(1000)==100 &&
          auto_contour_interval(40)==5 && auto_contour_interval(17)==2,"1-2-2.5-5 contour spacing");
  require(auto_snow_line(299.9)==1.0 && auto_snow_line(300)==0.72 && auto_snow_line(5000)==0.72,
          "snow appears from 300 m of relief");
  reject([](TerrainStyle& s){s.snow_line_fraction=1.2;},"snow line above one rejected");
  reject([](TerrainStyle& s){s.snow_line_fraction=-0.1;},"negative snow line rejected");
  reject([](TerrainStyle& s){s.ridge_curvature=0;},"zero ridge curvature rejected");
  reject([](TerrainStyle& s){s.detail_strength=1.5;},"detail above one rejected");
  reject([](TerrainStyle& s){s.detail_strength=std::numeric_limits<double>::quiet_NaN();},"NaN detail rejected");
  reject([](TerrainStyle& s){s.rock_relief_meters=-1;},"negative rock relief rejected");
  reject([](TerrainStyle& s){s.rock_relief_meters=25;},"rock relief above twenty meters rejected");
  reject([](TerrainStyle& s){s.canopy_on_ground=1.5;},"canopy on ground above one rejected");
  auto litter=style;litter.canopy_on_ground=0.0;
  const auto floor_color=terrain_material({300,{0,1,0},0.9,0.23,0},litter);
  require(floor_color.trees>0.5 && floor_color.albedo.y<terrain_material({300,{0,1,0},0.9,0.23,0},style).albedo.y,
          "with the canopy lifted off the ground, the forest floor is darker litter");
  reject([](TerrainStyle& s){s.submerged_level_meters=std::numeric_limits<double>::infinity();},"infinite water level rejected");
  reject([](TerrainStyle& s){s.floor.sand=-1;},"negative floor weight rejected");
  reject([](TerrainStyle& s){s.floor.sand=s.floor.vegetation=s.floor.mud=s.floor.stones=0;},"all-zero floor weights rejected");
  reject([](TerrainStyle& s){s.floor.patch_size_meters=2;},"tiny floor patches rejected");
  reject([](TerrainStyle& s){s.floor.patch_size_meters=900;},"huge floor patches rejected");
  reject([](TerrainStyle& s){s.floor.kind=static_cast<WaterFloorKind>(9);},"unknown floor kind rejected");

  // The validating device accepts a complete style block and rejects a short one.
  ValidatingRenderDevice device;
  device.begin_render_pass({{64,64}});
  const auto upload=quad_upload();
  const auto vb=device.create_buffer({BufferUsage::Vertex,std::as_bytes(std::span(upload.vertices)).size()},std::as_bytes(std::span(upload.vertices)));
  const auto ib=device.create_buffer({BufferUsage::Index,std::as_bytes(std::span(upload.indices)).size()},std::as_bytes(std::span(upload.indices)));
  const auto full=device.create_buffer({BufferUsage::Uniform,sizeof(packed)},std::as_bytes(std::span(packed)));
  const std::array<float,8> half{};
  const auto truncated=device.create_buffer({BufferUsage::Uniform,sizeof(half)},std::as_bytes(std::span(half)));
  const auto pipeline=device.create_pipeline({ShaderProgramId::Terrain,terrain_shader_layout()});
  device.draw_indexed({pipeline,vb,ib,0,6,{{kTerrainStyleSlot,full}},{}});
  must_throw([&]{device.draw_indexed({pipeline,vb,ib,0,6,{{kTerrainStyleSlot,truncated}},{}});},
             "short terrain style buffer rejected");
  require(device.draw_count()==1,"styled terrain draw recorded once");
}

void test_terrain_surface_color_reference() {
  TerrainStyle style;style.min_elevation_meters=0;style.max_elevation_meters=1000;
  style.light_direction={0,1,0};style.ambient=0.3;
  const terraflux::math::Vec3 up{0,1,0};
  // Ramp: endpoints are the lowland and summit stops, interior stops are hit
  // exactly, segments are continuous and the input is clamped.
  require(near(terrain_height_tint(0),{0.16,0.40,0.22}) && near(terrain_height_tint(1),{0.88,0.86,0.82}) &&
          near(terrain_height_tint(0.5),{0.72,0.64,0.38}),"ramp stops");
  require(near(terrain_height_tint(-3),terrain_height_tint(0)) && near(terrain_height_tint(7),terrain_height_tint(1)),"ramp clamps");
  for(const double t:{0.25,0.5,0.75})
    require(near(terrain_height_tint(t-1e-9),terrain_height_tint(t+1e-9),1e-6),"ramp is continuous at stop boundaries");
  require(!near(terrain_surface_color(0,up,style),terrain_surface_color(1000,up,style),1e-3) &&
          !near(terrain_surface_color(250,up,style),terrain_surface_color(750,up,style),1e-3),
          "different heights produce different colors");
  require(near(terrain_surface_color(500,up,style),terrain_height_tint(0.5)),
          "a flat surface lit from straight above shows the unshaded ramp color");
  require(near(terrain_surface_color(-500,up,style),terrain_surface_color(0,up,style)) &&
          near(terrain_surface_color(5000,up,style),terrain_surface_color(1000,up,style)),"heights clamp to the range");
  TerrainStyle flat=style;flat.max_elevation_meters=flat.min_elevation_meters=400;
  require(near(terrain_surface_color(400,up,flat),terrain_height_tint(0)) &&
          near(terrain_surface_color(900,up,flat),terrain_height_tint(0)),"flat terrain is uniformly lowland");

  // Slope: rock blend is 0 below start, 1 at/after full, monotone between.
  require(terrain_rock_blend(0,style)==0 && terrain_rock_blend(28,style)==0 &&
          terrain_rock_blend(48,style)==1 && terrain_rock_blend(90,style)==1 && near(terrain_rock_blend(38,style),0.5),
          "rock transition endpoints");
  for(double a=0;a<90;a+=1)require(terrain_rock_blend(a,style)<=terrain_rock_blend(a+1,style),"rock blend is monotone");
  const terraflux::math::Vec3 steep=terraflux::math::Vec3{1,1,0}.normalized();   // 45 degrees
  const terraflux::math::Vec3 cliff=terraflux::math::Vec3{4,1,0}.normalized();   // ~76 degrees
  const double cliff_shade=0.3+0.7*cliff.y;
  const terraflux::math::Vec3 rock_low{0.40,0.38,0.36},rock_high{0.60,0.58,0.56};
  require(near(terrain_surface_color(500,cliff,style),(rock_low*0.5+rock_high*0.5)*cliff_shade) &&
          near(terrain_surface_color(0,cliff,style),rock_low*cliff_shade),
          "slopes beyond the full angle are pure shaded rock, darker low and paler high");
  require(terrain_material({500,cliff,1.0,0.6},style).rock==1 &&
          near(terrain_surface_color({500,cliff,1.0,0.6},style),terrain_surface_color(500,cliff,style)),
          "rock hides vegetation and the height tint");
  const auto soil=terrain_surface_color(500,up,style),partial=terrain_surface_color(500,steep,style);
  require(!near(soil,partial,1e-3) && partial.x<soil.x,"a 45-degree slope is partly rock and darker");

  // Lighting: facing the sun is brighter than facing away; ambient is the floor.
  TerrainStyle sun=style;sun.light_direction={1,1,0};
  const terraflux::math::Vec3 toward=terraflux::math::Vec3{0.3,1,0}.normalized(),away=terraflux::math::Vec3{-0.3,1,0}.normalized();
  const auto lit=terrain_surface_color(500,toward,sun),shadow=terrain_surface_color(500,away,sun);
  require(lit.x>shadow.x && lit.y>shadow.y && lit.z>shadow.z,"hillshade follows the light direction");
  const terraflux::math::Vec3 opposite=terraflux::math::Vec3{-1,0.05,0}.normalized(); // near-vertical rock, back to the sun
  require(near(terrain_surface_color(500,opposite,sun),(rock_low*0.5+rock_high*0.5)*0.3),
          "surfaces facing away from the light keep exactly the ambient floor");
  require(near(terrain_surface_color(500,{0,3,0},style),terrain_surface_color(500,up,style)),"normals are normalized");
  must_throw([&]{(void)terrain_surface_color(500,{0,0,0},style);},"zero normal rejected");
  must_throw([&]{(void)terrain_surface_color(std::numeric_limits<double>::quiet_NaN(),up,style);},"NaN elevation rejected");

  // Viewer diagnostics classify fire (red-dominant) and imported asset (blue-dominant)
  // pixels by color; no material may be mistaken for either across the style space.
  TerrainStyle alpine;alpine.min_elevation_meters=0;alpine.max_elevation_meters=1000;alpine.snow_line_fraction=0.72;
  for(double height=0;height<=1000;height+=50)
    for(double slope=0;slope<90;slope+=6)
      for(const double azimuth:{0.0,1.0,2.0,3.0,4.0,5.0})
        for(const double fuel:{0.0,0.2,0.9})
          for(const double moisture:{0.05,0.23,0.6})
            for(const double curvature:{-0.2,0.0,0.2}) {
              const double radians=slope*3.14159265358979323846/180;
              const terraflux::math::Vec3 normal{std::sin(radians)*std::cos(azimuth),std::cos(radians),std::sin(radians)*std::sin(azimuth)};
              const auto c=terrain_surface_color({height,normal,fuel,moisture,curvature},alpine);
              const int r=static_cast<int>(std::lround(c.x*255)),g=static_cast<int>(std::lround(c.y*255)),b=static_cast<int>(std::lround(c.z*255));
              require(!(r>=175 && r>g*1.7 && r>b*2.0),"no material matches the fire pixel heuristic");
              require(!(b>=45 && b*100>g*128 && b*100>r*111),"no material matches the asset pixel heuristic");
              require(c.x>=0 && c.y>=0 && c.z>=0 && c.x<=1 && c.y<=1 && c.z<=1,"colors stay normalized");
            }
}

void test_terrain_material_reference() {
  using Class=TerrainMaterialClass;
  TerrainStyle style;style.min_elevation_meters=0;style.max_elevation_meters=1000;
  style.snow_line_fraction=0.72;style.light_direction={0,1,0};
  const terraflux::math::Vec3 up{0,1,0};
  const auto classify=[&](double height,terraflux::math::Vec3 normal,double fuel,double moisture,double curvature) {
    return dominant_material(terrain_material({height*1000,normal,fuel,moisture,curvature},style));
  };
  // Vegetation moisture classes are distinct, ordered and sum to one.
  const auto dry=terrain_material({300,up,0.3,0.05},style),semi=terrain_material({300,up,0.3,0.23},style),
             wet=terrain_material({300,up,0.3,0.6},style);
  require(classify(0.3,up,0.3,0.05,0)==Class::DryVegetation && classify(0.3,up,0.3,0.23,0)==Class::SemiDryVegetation &&
          classify(0.3,up,0.3,0.6,0)==Class::WetVegetation,"dry, semi-dry and wet vegetation classes");
  for(const auto* m:{&dry,&semi,&wet})require(near(m->dry+m->semi_dry+m->wet,1.0),"moisture class weights sum to one");
  require(dry.dry==1 && semi.semi_dry==1 && wet.wet==1,"class plateaus are exact at their centers");
  require(!near(dry.albedo,semi.albedo,1e-3) && !near(semi.albedo,wet.albedo,1e-3),"moisture classes have distinct colors");
  require(dry.albedo.x>wet.albedo.x && wet.albedo.y>dry.albedo.y*0.7 && wet.albedo.x<0.3,"dry vegetation is straw, wet vegetation green");
  require(near(terrain_material({300,up,0.3,0.15},style).semi_dry,0.5),"halfway between dry and semi-dry blends evenly");
  // Fuel load: bare ground keeps the height ramp, dense fuel becomes forest
  // below the tree line and on moderate slopes only.
  require(classify(0.3,up,0.0,0.5,0)==Class::Bare && near(terrain_material({300,up,0,0.5},style).albedo,terrain_height_tint(0.3)),
          "no fuel is bare hypsometric ground");
  require(classify(0.3,up,0.9,0.23,0)==Class::Forest,"dense fuel is forest");
  require(classify(0.3,up,0.9,0.23,0)!=classify(0.3,up,0.3,0.23,0),"fuel load separates forest from grass");
  const terraflux::math::Vec3 steep=terraflux::math::Vec3{std::tan(43*3.14159265358979323846/180),1,0}.normalized();
  const terraflux::math::Vec3 moderate=terraflux::math::Vec3{std::tan(36*3.14159265358979323846/180),1,0}.normalized();
  require(terrain_material({300,steep,0.9,0.23},style).trees==0 &&
          terrain_material({300,moderate,0.9,0.23},style).trees>0.3 && terrain_material({300,moderate,0.9,0.23},style).trees<0.7,
          "forest thins out between 30 and 42 degrees");
  TerrainStyle no_snow=style;no_snow.snow_line_fraction=1.0;
  require(dominant_material(terrain_material({900,up,0.9,0.23},no_snow))!=Class::Forest &&
          terrain_material({900,up,0.9,0.23},no_snow).trees==0,"no forest above the tree line");
  // Rock, exposed ridges, snow and ice.
  const terraflux::math::Vec3 cliff=terraflux::math::Vec3{4,1,0}.normalized();
  const terraflux::math::Vec3 gentle=terraflux::math::Vec3{std::tan(20*3.14159265358979323846/180),1,0}.normalized();
  require(classify(0.3,cliff,0.9,0.5,0)==Class::Rock && classify(0.9,cliff,0.0,0.0,0)==Class::Rock,
          "cliffs are rock whatever the fuel or height; snow does not cling to them");
  require(classify(0.7,up,0.0,0.0,0.12)==Class::Ridge && classify(0.7,up,0.0,0.0,0.0)==Class::Bare,
          "a convex high crest is an exposed ridge; the same height on a plane is not");
  require(near(terrain_material({700,up,0,0,0.12},style).albedo,terraflux::math::Vec3{0.66,0.63,0.58}*0.85+terrain_height_tint(0.7)*0.15,1e-9),
          "gentle exposed ridges are scree");
  require(classify(0.8,up,0.0,0.0,0)==Class::Snow && classify(0.8,up,0.9,0.23,0)==Class::Snow &&
          classify(0.8,gentle,0.0,0.0,-0.12)==Class::Snow,"above the snow line snow covers vegetation, bare ground and gentle basins");
  require(classify(0.75,up,0.0,0.0,0.12)!=Class::Snow,"wind-blown crests near the snow line stay bare");
  require(classify(0.85,up,0.0,0.0,-0.12)==Class::Ice && classify(0.99,up,0.0,0.0,0)==Class::Ice,
          "flat high basins and summit caps are ice");
  require(classify(0.85,gentle,0.0,0.0,-0.12)==Class::Snow,"ice needs nearly level ground");
  require(classify(1.0,up,0.9,0.5,0)==Class::Snow || classify(1.0,up,0.9,0.5,0)==Class::Ice,"the summit is frozen");
  require(dominant_material(terrain_material({1000,up,0,0},no_snow))==Class::Bare,"a snow line of one disables snow and ice");
  // Names are distinct and stable.
  std::vector<std::string> names;
  for(std::size_t i=0;i<kTerrainMaterialClassCount;++i)names.push_back(terrain_material_name(static_cast<Class>(i)));
  std::sort(names.begin(),names.end());
  require(std::unique(names.begin(),names.end())==names.end() && names.front()=="bare","material names are unique");
  must_throw([&]{(void)terrain_material_name(static_cast<Class>(42));},"unknown material class rejected");
  must_throw([&]{(void)terrain_material({300,up,std::numeric_limits<double>::quiet_NaN(),0},style);},"NaN fuel rejected");
}

void test_water_floor_reference() {
  using Class=TerrainMaterialClass;
  TerrainStyle style;style.min_elevation_meters=0;style.max_elevation_meters=1000;
  style.light_direction={0,1,0};style.submerged_level_meters=100;
  const terraflux::math::Vec3 up{0,1,0};
  const auto slope=[](double degrees){return terraflux::math::Vec3{std::tan(degrees*3.14159265358979323846/180),1,0}.normalized();};
  const auto material=[&](double elevation,terraflux::math::Vec3 normal,WaterFloorKind kind) {
    auto s=style;s.floor.kind=kind;return terrain_material({elevation,normal,0.9,0.5,0},s);
  };
  // Above the line the land materials are untouched; below it the floor takes over.
  const auto land=material(101,up,WaterFloorKind::Mixed),shore=material(100,up,WaterFloorKind::Mixed),bed=material(96,up,WaterFloorKind::Mixed);
  require(land.submerged==0 && dominant_material(land)==Class::Forest,"ground above the water line is land");
  require(near(shore.submerged,0.5) && bed.submerged==1 && near(bed.depth_meters,4),"the blend spans +-0.3 m around the line");
  require(dominant_material(bed)!=Class::Forest && bed.trees>0.5,"forest under water renders as floor although the fuel is unchanged");
  TerrainStyle dry=style;dry.submerged_level_meters.reset();
  require(terrain_material({96,up,0.9,0.5,0},dry).submerged==0 && dominant_material(terrain_material({96,up,0.9,0.5,0},dry))==Class::Forest,
          "without a water level nothing is submerged");
  // Single kinds are one-hot everywhere.
  require(dominant_material(material(60,up,WaterFloorKind::Sand))==Class::FloorSand && material(60,up,WaterFloorKind::Sand).floor_sand==1 &&
          dominant_material(material(60,slope(40),WaterFloorKind::Vegetation))==Class::FloorVegetation &&
          dominant_material(material(99,up,WaterFloorKind::Mud))==Class::FloorMud &&
          dominant_material(material(99,up,WaterFloorKind::Stones))==Class::FloorStones,"single floor kinds ignore depth and slope");
  require(near(material(60,up,WaterFloorKind::Sand).albedo,{0.74,0.68,0.50}) && near(material(60,up,WaterFloorKind::Mud).albedo,{0.32,0.28,0.22}),
          "single floor albedos");
  // Mixed floors are zoned by depth and slope.
  require(dominant_material(material(99.2,up,WaterFloorKind::Mixed))==Class::FloorSand,"gentle beds under 0.8 m are sand");
  require(dominant_material(material(96.5,up,WaterFloorKind::Mixed))==Class::FloorVegetation,"gentle beds around 3.5 m grow plants");
  require(dominant_material(material(85,up,WaterFloorKind::Mixed))==Class::FloorMud,"deep flat beds are mud");
  require(dominant_material(material(96.5,slope(30),WaterFloorKind::Mixed))==Class::FloorStones &&
          dominant_material(material(85,slope(30),WaterFloorKind::Mixed))==Class::FloorStones,"steep beds are stones at any depth");
  for(const double elevation:{99.5,97.0,90.0,70.0})for(const double degrees:{0.0,10.0,25.0,45.0}) {
    const auto m=material(elevation,slope(degrees),WaterFloorKind::Mixed);
    require(near(m.floor_sand+m.floor_vegetation+m.floor_mud+m.floor_stones,1.0),"floor weights are normalized");
  }
  auto no_plants=style;no_plants.floor.vegetation=0;
  require(dominant_material(terrain_material({96.5,up,0,0,0},no_plants))!=Class::FloorVegetation,"a zero weight removes a kind");
  auto sandy=style;sandy.floor.sand=50;
  require(dominant_material(terrain_material({85,up,0,0,0},sandy))==Class::FloorSand,"weights shift the zoning");
  // Random floors are reproducible from their seed and stay within range.
  const auto a=random_water_floor(11),b=random_water_floor(11),c=random_water_floor(12);
  require(a.kind==WaterFloorKind::Mixed && a.seed==11 && a.sand==b.sand && a.vegetation==b.vegetation && a.mud==b.mud &&
          a.stones==b.stones && a.patch_size_meters==b.patch_size_meters,"random floor is a pure function of the seed");
  require(a.sand!=c.sand || a.mud!=c.mud || a.patch_size_meters!=c.patch_size_meters,"different seeds differ");
  for(const auto& f:{a,c}) {
    for(const double w:{f.sand,f.vegetation,f.mud,f.stones})require(w>=0.2 && w<=1.5,"random weights stay within 0.2..1.5");
    require(f.patch_size_meters>=25 && f.patch_size_meters<=75,"random patch sizes stay within 25..75 m");
    auto s=style;s.floor=f;(void)pack_terrain_style(s);
  }
  require(parse_water_floor("sand")==WaterFloorKind::Sand && parse_water_floor("stones")==WaterFloorKind::Stones &&
          parse_water_floor("mixed")==WaterFloorKind::Mixed && std::string(water_floor_name(WaterFloorKind::Vegetation))=="vegetation",
          "floor names round-trip");
  must_throw([&]{(void)parse_water_floor("gravel");},"unknown floor name rejected");
  must_throw([&]{(void)parse_water_floor(nullptr);},"null floor name rejected");
  // Floors seen through water must not trip the viewer's fire/asset pixel heuristics either.
  for(const auto kind:{WaterFloorKind::Sand,WaterFloorKind::Vegetation,WaterFloorKind::Mud,WaterFloorKind::Stones,WaterFloorKind::Mixed})
    for(const double elevation:{99.5,97.0,90.0,60.0})for(const double degrees:{0.0,15.0,35.0}) {
      auto s=style;s.floor.kind=kind;
      const auto c=terrain_surface_color({elevation,slope(degrees),0.5,0.3,0},s);
      const int r=static_cast<int>(std::lround(c.x*255)),g=static_cast<int>(std::lround(c.y*255)),b=static_cast<int>(std::lround(c.z*255));
      require(!(r>=175 && r>g*1.7 && r>b*2.0) && !(b>=45 && b*100>g*128 && b*100>r*111),"floor colors avoid the pixel heuristics");
    }
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
    test_resource_bytes();
    test_resource_telemetry();
    test_explicit_shader_identity();
    test_render_pass_contract();
    test_offscreen_targets();
    test_sampled_target_composition();
    test_resize_recovery();
    test_render_pass_scopes();
    test_layer_visibility_contract();
    test_terrain_style_contract();
    test_terrain_surface_color_reference();
    test_terrain_material_reference();
    test_water_floor_reference();
    std::cout << "TerraFlux graphics contracts: 25 tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "TerraFlux graphics test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
