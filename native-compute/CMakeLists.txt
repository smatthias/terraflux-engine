cmake_minimum_required(VERSION 3.20)
project(terraflux_compute LANGUAGES CXX)

# Shared native terrain/grid core is independently reusable by the simulator and future renderer.
add_library(terraflux_compute STATIC
  src/grid.cpp
  src/elevation_fill.cpp
  src/math.cpp
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

  add_executable(terraflux-terrain-mesh-tests tests/terrain_mesh_tests.cpp)
  target_link_libraries(terraflux-terrain-mesh-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-terrain-mesh-tests COMMAND terraflux-terrain-mesh-tests)

  add_executable(terraflux-graphics-tests tests/graphics_tests.cpp)
  target_link_libraries(terraflux-graphics-tests PRIVATE terraflux_graphics)
  add_test(NAME terraflux-graphics-tests COMMAND terraflux-graphics-tests)
endif()
