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
//   v3  adds weather (air temperature, relative humidity, rainfall; constant or per step) as a
//       bounded additive term. Absent weather is v2.
// All are accepted; a request is evaluated under the semantics of the version it declares.
inline constexpr const char* kContractVersionV1 = "terraflux.compute/v1";
inline constexpr const char* kContractVersionV2 = "terraflux.compute/v2";
inline constexpr const char* kContractVersionV3 = "terraflux.compute/v3";
inline constexpr const char* kContractVersion = kContractVersionV3;

inline bool is_supported_contract_version(std::string_view version) {
  return version == kContractVersionV1 || version == kContractVersionV2 || version == kContractVersionV3;
}
// Terrain was introduced in v2; v1 keeps the legacy, elevation-blind behaviour.
inline bool terrain_enabled(std::string_view version) { return version != kContractVersionV1; }
// The weather term was introduced in v3; earlier versions ignore a weather block.
inline bool weather_enabled(std::string_view version) { return version == kContractVersionV3; }

// v3 air conditions for one step. 20 C, 50 % and no rain are neutral (term exactly 0.0).
struct WeatherSample {
  double temperature_celsius{20.0};   // -60 .. 60
  double relative_humidity{0.5};      // 0 .. 1
  double precipitation_mm_h{0.0};     // 0 .. 200
};

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
  // v3: empty means no weather; one sample applies to every step; a series keeps its last
  // sample for the remaining steps. Ignored under v1 and v2.
  std::vector<WeatherSample> weather{};
};

// Environmental driver constants. Mirrored (with rationale) in intelligence/app/spread_kernel.py;
// keep in sync. kWindWeightV1 is frozen so v1 requests replay exactly.
inline constexpr double kWindWeightV1 = 0.15;
inline constexpr double kWindWeight = 0.25;
inline constexpr double kWindHeadwindWeight = 0.10;
inline constexpr double kSlopeUphillWeight = 0.15;
inline constexpr double kSlopeDownhillWeight = 0.075;
inline constexpr double kSlopeSaturationDegrees = 30.0;
// v3 weather term: heat * (0.5 + 0.5 * dryness) * kHeatWeight - rain * kRainWeight, with
// heat = clamp((T - 20) / 20, -1, 1), dryness = 1 - RH and rain = clamp(P / 10, 0, 1).
inline constexpr double kHeatWeight = 0.10;
inline constexpr double kRainWeight = 0.30;
inline constexpr double kWeatherNeutralTemperature = 20.0;
inline constexpr double kWeatherHeatScaleDegrees = 20.0;
inline constexpr double kWeatherRainSaturationMmH = 10.0;
inline constexpr std::size_t kMaxWeatherSteps = 720;
// Mirrors weather_term() in spread_kernel.py; operation order matters for bit-level parity.
inline double weather_term(const WeatherSample& sample) {
  const auto clamp = [](double value, double low, double high) { return value < low ? low : (value > high ? high : value); };
  const double heat = clamp((sample.temperature_celsius - kWeatherNeutralTemperature) / kWeatherHeatScaleDegrees, -1, 1);
  const double dryness = 1 - sample.relative_humidity;
  const double rain = clamp(sample.precipitation_mm_h / kWeatherRainSaturationMmH, 0, 1);
  return heat * (0.5 + 0.5 * dryness) * kHeatWeight - rain * kRainWeight;
}
inline bool valid_weather_sample(const WeatherSample& s) {
  return s.temperature_celsius >= -60 && s.temperature_celsius <= 60 && s.relative_humidity >= 0 &&
         s.relative_humidity <= 1 && s.precipitation_mm_h >= 0 && s.precipitation_mm_h <= 200;
}
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
