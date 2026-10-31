#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace terraflux::compute {
// Contract history
//   v1  fuel / moisture / susceptibility / wind. elevation_meters is accepted but ignored.
//   v2  adds cell_size_meters and the terrain (slope) term.
// Both are accepted; a request is evaluated under the semantics of the version it declares.
inline constexpr const char* kContractVersionV1 = "terraflux.compute/v1";
inline constexpr const char* kContractVersionV2 = "terraflux.compute/v2";
inline constexpr const char* kContractVersion = kContractVersionV2;

inline bool is_supported_contract_version(std::string_view version) {
  return version == kContractVersionV1 || version == kContractVersionV2;
}
// Terrain was introduced in v2; v1 keeps the legacy, elevation-blind behaviour.
inline bool terrain_enabled(std::string_view version) { return version != kContractVersionV1; }

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
  // Compass degrees clockwise from +y (north). v2: meteorological, the direction the wind blows
  // FROM. v1 (frozen): the direction the wind blows TOWARD.
  double wind_direction_degrees{};
  double spread_threshold{0.5};
  // Horizontal centre-to-centre distance of orthogonal neighbours. Only used to derive slope
  // angles from GridCell::elevation_meters; irrelevant when elevations are absent.
  double cell_size_meters{30.0};
};

// Environmental driver constants. Mirrored (with rationale) in intelligence/app/spread_kernel.py;
// keep in sync. kWindWeightV1 is frozen so v1 requests replay exactly.
inline constexpr double kWindWeightV1 = 0.15;
inline constexpr double kWindWeight = 0.25;
inline constexpr double kWindHeadwindWeight = 0.10;
inline constexpr double kSlopeUphillWeight = 0.15;
inline constexpr double kSlopeDownhillWeight = 0.075;
inline constexpr double kSlopeSaturationDegrees = 30.0;
// Max ray length (cells) when filling elevation voids from the nearest known cells (v2). A quality
// bound, not a cost bound: hits are found with one linear sweep per direction.
inline constexpr int kElevationFillMaxSearchCells = 1024;
// Contract bounds on the number of grid cells (mirrors the Pydantic model).
inline constexpr std::size_t kMinCells = 1;
inline constexpr std::size_t kMaxCells = 1'000'000;

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
} // namespace terraflux::compute
