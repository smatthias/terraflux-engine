cmake_minimum_required(VERSION 3.20)
project(terraflux_compute LANGUAGES CXX)

# Shared native terrain/grid core is independently reusable by the simulator and future renderer.
add_library(terraflux_compute STATIC
  src/grid.cpp
  src/elevation_fill.cpp
)
target_include_directories(terraflux_compute PUBLIC include)
target_compile_features(terraflux_compute PUBLIC cxx_std_20)

add_executable(terraflux-compute-runner src/grid_spread_runner.cpp)
target_link_libraries(terraflux-compute-runner PRIVATE terraflux_compute)

include(CTest)
if(BUILD_TESTING)
  add_executable(terraflux-grid-tests tests/grid_tests.cpp)
  target_link_libraries(terraflux-grid-tests PRIVATE terraflux_compute)
  add_test(NAME terraflux-grid-tests COMMAND terraflux-grid-tests)
endif()
