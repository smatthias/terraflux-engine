#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geosentinel::compute {
inline constexpr const char* kContractVersion = "geosentinel.compute/v1";

struct GridCell {
  std::int32_t x{};
  std::int32_t y{};
  double fuel_load{};
  double fuel_moisture{};
  double burn_susceptibility{};
  std::optional<double> elevation_meters{};
  std::optional<double> population{};
};

struct IgnitionCell { std::int32_t x{}; std::int32_t y{}; };

struct SpreadRequest {
  std::string grid_key{};
  std::vector<GridCell> cells{};
  IgnitionCell ignition{};
  std::int32_t steps{12};
  double wind_speed_kmh{};
  double wind_direction_degrees{};
  double spread_threshold{0.5};
};

struct SpreadFrame {
  std::int32_t step{};
  std::vector<IgnitionCell> active_cells{};
  std::vector<IgnitionCell> burned_cells{};
};

struct SpreadResult {
  std::string engine{"cpp-native"};
  std::vector<SpreadFrame> frames{};
  std::int32_t burned_cells{};
  std::int32_t completed_steps{};
};
} // namespace geosentinel::compute
