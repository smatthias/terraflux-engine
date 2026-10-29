cmake_minimum_required(VERSION 3.20)
project(terraflux_compute LANGUAGES CXX)

# Shared native terrain/grid core is independently reusable by the simulator and future renderer.
add_library(terraflux_compute STATIC
  src/grid.cpp
  src/elevation_fill.cpp
  src/math.cpp
  src/scene.cpp
  src/assets/obj.cpp
  src/terrain_mesh.cpp
)
target_include_directories(terraflux_compute PUBLIC include)
target_compile_features(terraflux_compute PUBLIC cxx_std_20)

add_executable(terraflux-compute-runner src/grid_spread_runner.cpp)
target_link_libraries(terraflux-compute-runner PRIVATE terraflux_compute)

# Device abstraction and terrain upload data: no OpenGL or platform SDK dependencies.
add_library(terraflux_graphics STATIC
  src/graphics/render_device.cpp
  src/graphics/terrain_upload.cpp
  src/replay.cpp
)
target_include_directories(terraflux_graphics PUBLIC include)
target_compile_features(terraflux_graphics PUBLIC cxx_std_20)
target_link_libraries(terraflux_graphics PUBLIC terraflux_compute)

include(CTest)
if(BUILD_TESTING)
  add_executable(terraflux-grid-tests tests/grid_tests.cpp)
  target_link_libraries(terraflux-grid-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-grid-tests COMMAND terraflux-grid-tests)

  add_executable(terraflux-math-tests tests/math_tests.cpp)
  target_link_libraries(terraflux-math-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-math-tests COMMAND terraflux-math-tests)

  add_executable(terraflux-scene-tests tests/scene_tests.cpp)
  target_link_libraries(terraflux-scene-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-scene-tests COMMAND terraflux-scene-tests)

  add_executable(terraflux-terrain-mesh-tests tests/terrain_mesh_tests.cpp)
  target_link_libraries(terraflux-terrain-mesh-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-terrain-mesh-tests COMMAND terraflux-terrain-mesh-tests)

  add_executable(terraflux-graphics-tests tests/graphics_tests.cpp)
  target_link_libraries(terraflux-graphics-tests PRIVATE terraflux_graphics)
  add_test(NAME terraflux-graphics-tests COMMAND terraflux-graphics-tests)

  add_executable(terraflux-replay-tests tests/replay_tests.cpp)
  target_link_libraries(terraflux-replay-tests PRIVATE terraflux_graphics)
  add_test(NAME terraflux-replay-tests COMMAND terraflux-replay-tests)
endif()

# Opt-in desktop GPU renderer. Headless simulation builds need no graphics packages.
option(TERRAFLUX_WITH_OPENGL "Build desktop OpenGL 4.3 terrain viewer" OFF)
if(TERRAFLUX_WITH_OPENGL)
  find_package(glfw3 3.3 REQUIRED)
  find_package(OpenGL REQUIRED)

  add_library(terraflux_opengl STATIC src/graphics/opengl_device.cpp)
  target_include_directories(terraflux_opengl PUBLIC include)
  target_compile_features(terraflux_opengl PUBLIC cxx_std_20)
  target_link_libraries(terraflux_opengl PUBLIC terraflux_graphics glfw OpenGL::GL)

  add_executable(terraflux-terrain-viewer src/graphics/terrain_viewer.cpp)
  target_link_libraries(terraflux-terrain-viewer PRIVATE terraflux_opengl)
endif()
