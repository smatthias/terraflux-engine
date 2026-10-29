#include <terraflux/graphics/opengl_device.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/mesh_upload.hpp>
#include <terraflux/assets/obj.hpp>
#include <terraflux/scene.hpp>
#include <terraflux/grid.hpp>
#include <terraflux/math.hpp>
#include <terraflux/replay.hpp>
#include <terraflux/terrain_mesh.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct GlfwSession {
  GlfwSession() {
    if (glfwInit() != GLFW_TRUE)
      throw std::runtime_error("GLFW initialization failed (is a display server available?)");
  }
  ~GlfwSession() { glfwTerminate(); }
};
struct WindowDeleter {
  void operator()(GLFWwindow* window) const { if (window) glfwDestroyWindow(window); }
};
using Window = std::unique_ptr<GLFWwindow, WindowDeleter>;

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot open " + path);
  std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (!file.good() && !file.eof()) throw std::runtime_error("error reading " + path);
  return content;
}

struct Options {
  bool smoke_test = false;
  std::string request_file, response_file, obj_file;
};

Options parse_args(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--smoke-test") options.smoke_test = true;
    else if (arg == "--request" && i + 1 < argc) options.request_file = argv[++i];
    else if (arg == "--response" && i + 1 < argc) options.response_file = argv[++i];
    else if (arg == "--obj" && i + 1 < argc) options.obj_file = argv[++i];
    else throw std::invalid_argument(
        "usage: terraflux-terrain-viewer [--request request.json --response response.json] [--obj model.obj] [--smoke-test]");
  }
  if (options.request_file.empty() != options.response_file.empty())
    throw std::invalid_argument("--request and --response must be provided together");
  return options;
}

terraflux::compute::Grid make_demo_terrain() {
  constexpr int kSide = 72;
  std::vector<terraflux::compute::GridCell> cells;
  cells.reserve(kSide * kSide);
  for (int y = 0; y < kSide; ++y) {
    for (int x = 0; x < kSide; ++x) {
      terraflux::compute::GridCell cell{};
      cell.x = x; cell.y = y;
      cell.fuel_load = 1.0; cell.fuel_moisture = 0.15; cell.burn_susceptibility = 0.8;
      const double hill = 55.0 * std::sin(x * 0.065) * std::cos(y * 0.075);
      const double ridge = 25.0 * std::exp(-std::pow((x-48.0)/16,2) - std::pow((y-26.0)/13,2));
      cell.elevation_meters = 70.0 + hill + ridge;
      cells.push_back(cell);
    }
  }
  return terraflux::compute::Grid(std::move(cells));
}

template <class T>
std::span<const std::byte> bytes_of(const std::vector<T>& items) {
  return std::as_bytes(std::span(items));
}

struct CameraGeometry {
  terraflux::math::Vec3 target;
  double starting_distance;
};

CameraGeometry camera_geometry(const terraflux::terrain::TerrainMesh& mesh) {
  if (mesh.vertices.empty()) throw std::runtime_error("terrain mesh contains no vertices");
  auto lower = mesh.vertices.front().position;
  auto upper = lower;
  for (const auto& vertex : mesh.vertices) {
    const auto& p = vertex.position;
    lower.x = std::min(lower.x, p.x); lower.y = std::min(lower.y, p.y); lower.z = std::min(lower.z, p.z);
    upper.x = std::max(upper.x, p.x); upper.y = std::max(upper.y, p.y); upper.z = std::max(upper.z, p.z);
  }
  return {{(lower.x + upper.x) * 0.5, (lower.y + upper.y) * 0.5,
           (lower.z + upper.z) * 0.5},
          std::max(200.0, 2.1 * std::max(upper.x - lower.x, upper.z - lower.z))};
}

void update_camera(terraflux::graphics::RenderDevice& device,
                   terraflux::graphics::BufferHandle uniform, int width, int height,
                   const CameraGeometry& camera_geometry,
                   double yaw, double pitch, double distance,
                   double cell_size, std::uint32_t fire_width,
                   std::uint32_t fire_height, double now) {
  using terraflux::math::Vec3;
  using terraflux::math::Mat4;
  const Vec3 target = camera_geometry.target;
  const Vec3 eye{
      target.x + distance * std::sin(yaw) * std::cos(pitch),
      target.y + distance * std::sin(pitch),
      target.z + distance * std::cos(yaw) * std::cos(pitch)
  };
  constexpr double kPi = 3.14159265358979323846;
  const Mat4 view = Mat4::lookAtRH(eye, target, {0, 1, 0});
  const Mat4 proj = Mat4::perspectiveRH_NO(kPi / 3.0,
      static_cast<double>(width) / static_cast<double>(height), 1.0,
      std::max(12000.0, distance * 8.0));
  const Mat4 vp = proj * view;
  std::array<float, 20> camera{};
  for (std::size_t i = 0; i < 16; ++i) camera[i] = static_cast<float>(vp.values[i]);
  camera[16] = static_cast<float>(cell_size);
  camera[17] = static_cast<float>(fire_width);
  camera[18] = static_cast<float>(fire_height);
  camera[19] = static_cast<float>(now);
  device.update_buffer(uniform, 0, std::as_bytes(std::span(camera)));
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_args(argc, argv);

    std::optional<terraflux::replay::Playback> playback;
    std::optional<terraflux::compute::Grid> demo;
    if (!options.request_file.empty())
      playback.emplace(terraflux::replay::load(read_file(options.request_file),
                                              read_file(options.response_file)));
    else
      demo.emplace(make_demo_terrain());
    auto& grid = playback ? playback->grid : *demo;
    const double cell_size = playback ? playback->cell_size_meters : 30.0;

    // This is visualization-only preprocessing. Native compute outputs and
    // regression semantics are unchanged. Unmeasured rivers remain holes.
    terraflux::compute::fill_elevation_voids(grid);
    for (auto& cell : grid.cells)
      if (!cell.elevation_meters && cell.fuel_load > 0) cell.elevation_meters = 0.0;
    const auto mesh = terraflux::terrain::build_terrain_mesh(grid, cell_size);
    auto upload = terraflux::graphics::prepare_terrain_upload(mesh);
    std::optional<terraflux::replay::FireField> fire_field;
    if (playback) fire_field.emplace(terraflux::replay::make_fire_field(grid));
    if (upload.indices.empty()) throw std::runtime_error("scenario terrain has no drawable triangles");
    const auto scene_camera = camera_geometry(mesh);

    GlfwSession glfw;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, options.smoke_test ? GLFW_FALSE : GLFW_TRUE);
    Window window(glfwCreateWindow(1280, 800, "TerraFlux 3D Wildfire Viewer", nullptr, nullptr));
    if (!window) throw std::runtime_error("OpenGL 4.3 context could not be created");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(options.smoke_test ? 0 : 1);

    using namespace terraflux::graphics;
    OpenGLRenderDevice device;
    const auto vertices = bytes_of(upload.vertices);
    const auto indices = bytes_of(upload.indices);
    const auto vb = device.create_buffer({BufferUsage::Vertex, vertices.size()}, vertices);
    const auto ib = device.create_buffer({BufferUsage::Index, indices.size()}, indices);
    std::optional<BufferHandle> fire_buffer;
    if (fire_field) {
      const auto fire_bytes = std::as_bytes(std::span(fire_field->states));
      fire_buffer = device.create_buffer({BufferUsage::Storage, fire_bytes.size()}, fire_bytes);
    }
    std::array<float, 20> initial_camera{};
    const auto camera_bytes = std::as_bytes(std::span(initial_camera));
    const auto camera = device.create_buffer({BufferUsage::Uniform, camera_bytes.size()}, camera_bytes);
    const auto pipeline = device.create_pipeline({
        playback ? wildfire_shader_layout() : terrain_shader_layout(), true});

    // Two independent world transforms share the original asset VBO/IBO.
    // Their own std140 object UBOs are refreshed every frame (not mesh data).
    struct ImportedGpu {
      BufferHandle vb, ib, object_uniform_left, object_uniform_right;
      PipelineHandle pipeline;
      std::uint32_t indices = 0;
      terraflux::scene::Scene scene;
      terraflux::scene::NodeId left_pivot = 0, right_pivot = 0;
      terraflux::scene::NodeId left_mesh = 0, right_mesh = 0;
      double separation = 1.0;
      terraflux::math::Vec3 local_center{};
    };
    std::optional<ImportedGpu> imported;
    if (!options.obj_file.empty()) {
      std::ifstream model_file(options.obj_file);
      if (!model_file) throw std::runtime_error("cannot open OBJ " + options.obj_file);
      const auto object = terraflux::assets::import_obj(model_file);
      auto min_pos = object.vertices.front().position;
      auto max_pos = min_pos;
      for (const auto& vertex : object.vertices) {
        const auto& p = vertex.position;
        min_pos.x = std::min(min_pos.x, p.x); min_pos.y = std::min(min_pos.y, p.y);
        min_pos.z = std::min(min_pos.z, p.z);
        max_pos.x = std::max(max_pos.x, p.x); max_pos.y = std::max(max_pos.y, p.y);
        max_pos.z = std::max(max_pos.z, p.z);
      }
      const double extent = std::max({max_pos.x-min_pos.x, max_pos.y-min_pos.y,
                                      max_pos.z-min_pos.z});
      if (!(extent > 0.0)) throw std::runtime_error("OBJ has no spatial extent");
      const double scale = (scene_camera.starting_distance * 0.095) / extent;
      const auto geometry = prepare_asset_upload(object);  // local-space, uploaded once
      const auto vertex_bytes = bytes_of(geometry.vertices);
      const auto index_bytes = bytes_of(geometry.indices);
      ImportedGpu obj{};
      obj.indices = static_cast<std::uint32_t>(geometry.indices.size());
      obj.separation = extent * 0.95;
      obj.local_center = {(min_pos.x + max_pos.x)*0.5, min_pos.y,
                          (min_pos.z + max_pos.z)*0.5};
      const auto anchor = obj.scene.create_node("Terrain anchor");
      obj.left_pivot = obj.scene.create_node("Rotating left", anchor);
      obj.right_pivot = obj.scene.create_node("Rotating right", anchor);
      obj.left_mesh = obj.scene.create_node("Mesh left", obj.left_pivot);
      obj.right_mesh = obj.scene.create_node("Mesh right", obj.right_pivot);
      obj.scene.set_transform(anchor, {
          scene_camera.target, {}, {scale,scale,scale}});
      obj.scene.set_transform(obj.left_mesh, {
          obj.local_center * -1.0, {}, {1,1,1}});
      obj.scene.set_transform(obj.right_mesh, {
          obj.local_center * -1.0, {}, {1,1,1}});
      obj.vb = device.create_buffer({BufferUsage::Vertex,vertex_bytes.size()},vertex_bytes);
      obj.ib = device.create_buffer({BufferUsage::Index,index_bytes.size()},index_bytes);
      const auto init_object = make_object_uniform(terraflux::math::Mat4::identity());
      const auto init_bytes = std::as_bytes(std::span(&init_object,1));
      obj.object_uniform_left = device.create_buffer(
          {BufferUsage::Uniform,init_bytes.size()},init_bytes);
      obj.object_uniform_right = device.create_buffer(
          {BufferUsage::Uniform,init_bytes.size()},init_bytes);
      obj.pipeline = device.create_pipeline({object_shader_layout(),true});
      imported = std::move(obj);
      std::cout << "Imported OBJ: " << geometry.indices.size()/3
                << " triangles, two independent scene objects sharing geometry\n";
    }

    double yaw = 0.45, pitch = 0.75, distance = scene_camera.starting_distance;
    double previous_time = glfwGetTime(), last_step_time = previous_time;
    std::size_t playback_frame = 0;
    std::size_t uploaded_frame = static_cast<std::size_t>(-1);
    bool playing = playback.has_value();
    bool previous_space = false, previous_home = false, previous_back = false, previous_forward = false;
    unsigned rendered_frames = 0;

    if (playback && options.smoke_test && playback->frames.size() > 1)
      playback_frame = 1; // exercise non-initial fire state in GPU smoke test

    while (!glfwWindowShouldClose(window.get())) {
      glfwPollEvents();
      if (glfwGetKey(window.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
      const double now = glfwGetTime();
      const double delta = std::clamp(now - previous_time, 0.0, 0.1);
      previous_time = now;
      const bool space = glfwGetKey(window.get(), GLFW_KEY_SPACE) == GLFW_PRESS;
      const bool home = glfwGetKey(window.get(), GLFW_KEY_HOME) == GLFW_PRESS;
      const bool back = glfwGetKey(window.get(), GLFW_KEY_COMMA) == GLFW_PRESS;
      const bool forward = glfwGetKey(window.get(), GLFW_KEY_PERIOD) == GLFW_PRESS;
      if (playback) {
        if (space && !previous_space) {
          if (!playing && playback_frame + 1 == playback->frames.size()) playback_frame = 0;
          playing = !playing;
          last_step_time = now;
        }
        if (home && !previous_home) { playback_frame = 0; playing = false; }
        if (back && !previous_back) {
          playback_frame = playback_frame == 0 ? 0 : playback_frame - 1; playing = false;
        }
        if (forward && !previous_forward) {
          playback_frame = std::min(playback_frame + 1, playback->frames.size() - 1); playing = false;
        }
        if (playing && now - last_step_time >= 0.35) {
          if (playback_frame + 1 < playback->frames.size()) ++playback_frame;
          else playing = false;
          last_step_time = now;
        }
        if (playback_frame != uploaded_frame) {
          terraflux::replay::apply_frame(playback->frames[playback_frame], *fire_field);
          device.update_buffer(*fire_buffer, 0, std::as_bytes(std::span(fire_field->states)));
          uploaded_frame = playback_frame;
          const auto& current = playback->frames[playback_frame];
          std::string title = "TerraFlux - " + playback->grid_key +
              " | step " + std::to_string(current.step + 1) + "/" +
              std::to_string(playback->frames.size()) +
              " | active " + std::to_string(current.active.size()) +
              " | burned " + std::to_string(current.burned.size()) +
              " | Space pause | Home reset | , . step | arrows orbit | +/- zoom";
          glfwSetWindowTitle(window.get(), title.c_str());
        }
      }
      previous_space = space; previous_home = home;
      previous_back = back; previous_forward = forward;

      if (glfwGetKey(window.get(), GLFW_KEY_LEFT) == GLFW_PRESS) yaw -= delta;
      if (glfwGetKey(window.get(), GLFW_KEY_RIGHT) == GLFW_PRESS) yaw += delta;
      if (glfwGetKey(window.get(), GLFW_KEY_UP) == GLFW_PRESS) pitch += delta;
      if (glfwGetKey(window.get(), GLFW_KEY_DOWN) == GLFW_PRESS) pitch -= delta;
      pitch = std::clamp(pitch, 0.1, 1.48);
      if (glfwGetKey(window.get(), GLFW_KEY_EQUAL) == GLFW_PRESS ||
          glfwGetKey(window.get(), GLFW_KEY_KP_ADD) == GLFW_PRESS)
        distance -= scene_camera.starting_distance * 0.65 * delta;
      if (glfwGetKey(window.get(), GLFW_KEY_MINUS) == GLFW_PRESS ||
          glfwGetKey(window.get(), GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS)
        distance += scene_camera.starting_distance * 0.65 * delta;
      distance = std::clamp(distance, scene_camera.starting_distance * 0.2,
                           scene_camera.starting_distance * 4.0);

      int width = 0, height = 0;
      glfwGetFramebufferSize(window.get(), &width, &height);
      if (width <= 0 || height <= 0) {
        if (options.smoke_test) throw std::runtime_error("no framebuffer in smoke test");
        continue;
      }
      update_camera(device, camera, width, height, scene_camera, yaw, pitch, distance,
                    cell_size, fire_field ? fire_field->width : 1,
                    fire_field ? fire_field->height : 1, now);
      device.clear_frame(width, height);
      DrawIndexed command{pipeline, vb, ib, 0,
          static_cast<std::uint32_t>(upload.indices.size()), {{0, camera}}, {}};
      if (fire_buffer) command.storage_buffers.push_back({1, *fire_buffer});
      device.draw_indexed(command);
      if (imported) {
        using terraflux::scene::Transform;
        const double spread = imported->separation;
        // Per-object animation updates only UBO state; geometry remains fixed.
        imported->scene.set_transform(imported->left_pivot,
            Transform{{-spread, 0.0, 0.0}, {0.0, now*0.7, 0.0}, {1.0,1.0,1.0}});
        imported->scene.set_transform(imported->right_pivot,
            Transform{{spread, 0.10*spread*std::sin(now*1.4), 0.0},
                      {0.0, -now*0.43, 0.0}, {1.0,1.0,1.0}});
        const auto left = make_object_uniform(imported->scene.world_matrix(imported->left_mesh),
                                              {0.16,0.57,0.95});
        const auto right = make_object_uniform(imported->scene.world_matrix(imported->right_mesh),
                                               {0.79,0.43,0.92});
        device.update_buffer(imported->object_uniform_left, 0,
                             std::as_bytes(std::span(&left,1)));
        device.update_buffer(imported->object_uniform_right, 0,
                             std::as_bytes(std::span(&right,1)));
        DrawIndexed left_draw{imported->pipeline, imported->vb, imported->ib,
            0, imported->indices, {{0,camera},{2,imported->object_uniform_left}}, {}};
        DrawIndexed right_draw{imported->pipeline, imported->vb, imported->ib,
            0, imported->indices, {{0,camera},{2,imported->object_uniform_right}}, {}};
        device.draw_indexed(left_draw);
        device.draw_indexed(right_draw);
      }
      if (options.smoke_test && playback && !playback->frames[playback_frame].active.empty()) {
        const auto visible_fire = device.count_fire_pixels(width, height);
        if (visible_fire == 0)
          throw std::runtime_error("smoke test found no visible fire pixels");
        std::cout << "Visible fire pixels: " << visible_fire << '\n';
      }
      glfwSwapBuffers(window.get());
      ++rendered_frames;
      if (options.smoke_test) break;
    }

    if (imported) {
      device.destroy_pipeline(imported->pipeline);
      device.destroy_buffer(imported->object_uniform_right);
      device.destroy_buffer(imported->object_uniform_left);
      device.destroy_buffer(imported->ib);
      device.destroy_buffer(imported->vb);
    }
    device.destroy_pipeline(pipeline);
    device.destroy_buffer(camera);
    if (fire_buffer) device.destroy_buffer(*fire_buffer);
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    if (options.smoke_test)
      std::cout << "OpenGL wildfire replay smoke test rendered " << rendered_frames << " frame(s)\n";
  } catch (const std::exception& e) {
    std::cerr << "TerraFlux viewer error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
