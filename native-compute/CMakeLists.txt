cmake_minimum_required(VERSION 3.20)
project(terraflux_compute LANGUAGES CXX)

# Shared native terrain/grid core is independently reusable by the simulator and future renderer.
add_library(terraflux_compute STATIC
  src/grid.cpp
  src/elevation_fill.cpp
  src/math.cpp
  src/scene.cpp
  src/scene_io.cpp
  src/runtime.cpp
  src/residency_state.cpp
  src/assets/obj.cpp
  src/assets/scene_assets.cpp
  src/assets/material.cpp
  src/assets/resource_resolution.cpp
  src/assets/image.cpp
  src/assets/gltf.cpp
  src/terrain_mesh.cpp
)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
target_include_directories(terraflux_compute PUBLIC include)
target_link_libraries(terraflux_compute PUBLIC PNG::PNG JPEG::JPEG)
target_compile_features(terraflux_compute PUBLIC cxx_std_20)

add_executable(terraflux-compute-runner src/grid_spread_runner.cpp)
target_link_libraries(terraflux-compute-runner PRIVATE terraflux_compute)

# Device abstraction and terrain upload data: no OpenGL or platform SDK dependencies.
add_library(terraflux_graphics STATIC
  src/graphics/render_device.cpp
  src/graphics/terrain_upload.cpp
  src/graphics/mesh_upload.cpp
  src/graphics/visibility.cpp
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

  add_executable(terraflux-runtime-tests tests/runtime_tests.cpp)
  target_link_libraries(terraflux-runtime-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-runtime-tests COMMAND terraflux-runtime-tests
      "${CMAKE_SOURCE_DIR}/../examples/scenes/twin_pyramids.json")

  add_executable(terraflux-scene-assets-tests tests/scene_assets_tests.cpp)
  target_link_libraries(terraflux-scene-assets-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-scene-assets-tests COMMAND terraflux-scene-assets-tests
      "${CMAKE_SOURCE_DIR}/../examples/asset_scene.json"
      "${CMAKE_SOURCE_DIR}/../examples/mixed_asset_scene.json")

  add_executable(terraflux-gltf-tests tests/gltf_tests.cpp)
  target_link_libraries(terraflux-gltf-tests PRIVATE terraflux_graphics)
  add_test(NAME terraflux-gltf-tests COMMAND terraflux-gltf-tests
      "${CMAKE_SOURCE_DIR}/../examples/assets/tetra.gltf"
      "${CMAKE_SOURCE_DIR}/../examples/assets/multi_mesh.gltf"
      "${CMAKE_SOURCE_DIR}/../examples/assets/textured_multi_mesh.gltf"
      "${CMAKE_SOURCE_DIR}/../examples/assets/tangent_blend.gltf")

  add_executable(terraflux-cpu-recipe-tests tests/cpu_recipe_tests.cpp)
  target_link_libraries(terraflux-cpu-recipe-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-cpu-recipe-tests COMMAND terraflux-cpu-recipe-tests)

  add_executable(terraflux-material-tests tests/material_tests.cpp)
  target_link_libraries(terraflux-material-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-material-tests COMMAND terraflux-material-tests)

  add_executable(terraflux-mesh-upload-tests tests/mesh_upload_tests.cpp)
  target_link_libraries(terraflux-mesh-upload-tests PRIVATE terraflux_graphics)
  add_test(NAME terraflux-mesh-upload-tests COMMAND terraflux-mesh-upload-tests)

  add_executable(terraflux-terrain-mesh-tests tests/terrain_mesh_tests.cpp)
  target_link_libraries(terraflux-terrain-mesh-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-terrain-mesh-tests COMMAND terraflux-terrain-mesh-tests)

  add_executable(terraflux-graphics-tests tests/graphics_tests.cpp)
  target_link_libraries(terraflux-graphics-tests PRIVATE terraflux_graphics)
  add_test(NAME terraflux-graphics-tests COMMAND terraflux-graphics-tests)
  add_executable(terraflux-resource-owner-tests tests/resource_owner_tests.cpp)
  target_link_libraries(terraflux-resource-owner-tests PRIVATE terraflux_graphics)
  add_test(NAME terraflux-resource-owner-tests COMMAND terraflux-resource-owner-tests)
  add_executable(terraflux-visibility-tests tests/visibility_tests.cpp)
  target_link_libraries(terraflux-visibility-tests PRIVATE terraflux_graphics)
  add_test(NAME terraflux-visibility-tests COMMAND terraflux-visibility-tests)

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
