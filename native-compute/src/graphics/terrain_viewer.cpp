#include <terraflux/graphics/opengl_device.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/grid.hpp>
#include <terraflux/math.hpp>
#include <terraflux/terrain_mesh.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
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

terraflux::compute::Grid make_demo_terrain() {
  constexpr int kSide = 72;
  std::vector<terraflux::compute::GridCell> cells;
  cells.reserve(kSide * kSide);
  for (int y = 0; y < kSide; ++y) {
    for (int x = 0; x < kSide; ++x) {
      terraflux::compute::GridCell cell{};
      cell.x = x;
      cell.y = y;
      cell.fuel_load = 1.0;
      cell.fuel_moisture = 0.15;
      cell.burn_susceptibility = 0.8;
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

void update_camera(terraflux::graphics::RenderDevice& device,
                   terraflux::graphics::BufferHandle uniform, int width, int height,
                   double yaw, double pitch, double distance) {
  using terraflux::math::Vec3;
  using terraflux::math::Mat4;
  const Vec3 target{1065.0, 60.0, -1065.0};
  const Vec3 eye{
      target.x + distance * std::sin(yaw) * std::cos(pitch),
      target.y + distance * std::sin(pitch),
      target.z + distance * std::cos(yaw) * std::cos(pitch)
  };
  constexpr double kPi = 3.14159265358979323846;
  const Mat4 view = Mat4::lookAtRH(eye, target, {0, 1, 0});
  const Mat4 proj = Mat4::perspectiveRH_NO(kPi / 3.0,
      static_cast<double>(width) / static_cast<double>(height), 1.0, 12000.0);
  const Mat4 vp = proj * view;
  std::array<float, 16> camera{};
  for (std::size_t i = 0; i < 16; ++i) camera[i] = static_cast<float>(vp.values[i]);
  device.update_buffer(uniform, 0, std::as_bytes(std::span(camera)));
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const bool smoke_test = argc == 2 && std::string(argv[1]) == "--smoke-test";
    if (argc > 1 && !smoke_test)
      throw std::invalid_argument("usage: terraflux-terrain-viewer [--smoke-test]");

    GlfwSession glfw;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, smoke_test ? GLFW_FALSE : GLFW_TRUE);

    Window window(glfwCreateWindow(1280, 800, "TerraFlux Terrain | arrows orbit, +/- zoom, Esc exit", nullptr, nullptr));
    if (!window) throw std::runtime_error("OpenGL 4.3 context could not be created");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(smoke_test ? 0 : 1);

    terraflux::graphics::OpenGLRenderDevice device;
    const auto grid = make_demo_terrain();
    const auto mesh = terraflux::terrain::build_terrain_mesh(grid, 30.0);
    const auto upload = terraflux::graphics::prepare_terrain_upload(mesh);
    if (upload.indices.empty()) throw std::runtime_error("demo terrain produced no triangles");

    using namespace terraflux::graphics;
    const auto vertices = bytes_of(upload.vertices);
    const auto indices = bytes_of(upload.indices);
    const auto vb = device.create_buffer({BufferUsage::Vertex, vertices.size()}, vertices);
    const auto ib = device.create_buffer({BufferUsage::Index, indices.size()}, indices);
    std::array<float, 16> initial_camera{};
    const auto camera_bytes = std::as_bytes(std::span(initial_camera));
    const auto camera = device.create_buffer({BufferUsage::Uniform, camera_bytes.size()}, camera_bytes);
    const auto pipeline = device.create_pipeline({terrain_shader_layout(), true});

    double yaw = 0.45, pitch = 0.75, distance = 3300.0;
    double previous_time = glfwGetTime();
    unsigned frames = 0;
    while (!glfwWindowShouldClose(window.get())) {
      glfwPollEvents();
      if (glfwGetKey(window.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
      const double now = glfwGetTime();
      const double delta = std::clamp(now - previous_time, 0.0, 0.1);
      previous_time = now;
      if (glfwGetKey(window.get(), GLFW_KEY_LEFT) == GLFW_PRESS) yaw -= delta;
      if (glfwGetKey(window.get(), GLFW_KEY_RIGHT) == GLFW_PRESS) yaw += delta;
      if (glfwGetKey(window.get(), GLFW_KEY_UP) == GLFW_PRESS) pitch += delta;
      if (glfwGetKey(window.get(), GLFW_KEY_DOWN) == GLFW_PRESS) pitch -= delta;
      pitch = std::clamp(pitch, 0.1, 1.48);
      if (glfwGetKey(window.get(), GLFW_KEY_EQUAL) == GLFW_PRESS ||
          glfwGetKey(window.get(), GLFW_KEY_KP_ADD) == GLFW_PRESS)
        distance -= 1900.0 * delta;
      if (glfwGetKey(window.get(), GLFW_KEY_MINUS) == GLFW_PRESS ||
          glfwGetKey(window.get(), GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS)
        distance += 1900.0 * delta;
      distance = std::clamp(distance, 700.0, 8000.0);

      int width = 0, height = 0;
      glfwGetFramebufferSize(window.get(), &width, &height);
      if (width <= 0 || height <= 0) {
        if (smoke_test) throw std::runtime_error("no visible framebuffer in smoke test");
        continue;
      }
      update_camera(device, camera, width, height, yaw, pitch, distance);
      device.clear_frame(width, height);
      device.draw_indexed({pipeline, vb, ib, 0,
          static_cast<std::uint32_t>(upload.indices.size()), {{0, camera}}});
      glfwSwapBuffers(window.get());
      ++frames;
      if (smoke_test) break;
    }
    // All graphics resources must be destroyed while the context still exists.
    device.destroy_pipeline(pipeline);
    device.destroy_buffer(camera);
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    if (smoke_test) std::cout << "OpenGL terrain smoke test rendered " << frames << " frame(s)\n";
  } catch (const std::exception& e) {
    std::cerr << "TerraFlux viewer error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
