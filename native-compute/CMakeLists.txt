cmake_minimum_required(VERSION 3.20)
project(terraflux_compute LANGUAGES CXX)
add_library(terraflux_compute INTERFACE)
target_include_directories(terraflux_compute INTERFACE include)
target_compile_features(terraflux_compute INTERFACE cxx_std_20)

add_executable(terraflux-compute-runner src/grid_spread_runner.cpp)
target_link_libraries(terraflux-compute-runner PRIVATE terraflux_compute)
