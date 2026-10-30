#include <terraflux/contracts.hpp>
#include <terraflux/grid.hpp>
#include <terraflux/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {
using terraflux::compute::GridCell;
using terraflux::compute::Grid;
using terraflux::compute::kAbsent;
using terraflux::compute::kNeighbours;
using terraflux::compute::fill_elevation_voids;

using terraflux::json::Parser;
using terraflux::json::Json;
using terraflux::json::object;
using terraflux::json::field;
using terraflux::json::number;
using terraflux::json::string;
using terraflux::json::optional_number;

std::string quote(const std::string& value) {
  std::string output = "\"";
  for (const char c : value) { if (c == '"' || c == '\\') output += '\\'; output += c; }
  return output + "\"";
}
void coordinates(std::ostringstream& output, const std::set<std::pair<int, int>>& values) {
  output << '['; bool first = true;
  for (const auto& [x, y] : values) { if (!first) output << ','; first = false; output << '[' << x << ',' << y << ']'; }
  output << ']';
}
}  // namespace

int main() {
  try {
    std::stringstream input; input << std::cin.rdbuf();
    const std::string payload = input.str();
    std::vector<GridCell> parsed; bool saw_cells = false;
    const auto root = Parser(payload).parse_request(parsed, saw_cells);
    if (!saw_cells) throw std::runtime_error("missing field: cells");
    if (parsed.size() < terraflux::compute::kMinCells) throw std::runtime_error("cells must not be empty");

    const std::string contract_version = string(field(root, "contract_version"));
    if (!terraflux::compute::is_supported_contract_version(contract_version)) throw std::runtime_error("unsupported contract_version");
    const bool use_terrain = terraflux::compute::terrain_enabled(contract_version);

    std::size_t cells_with_elevation = 0;
    for (const auto& cell : parsed) if (cell.elevation_meters) ++cells_with_elevation;
    Grid grid(std::move(parsed));

    const auto& ignition_json = object(field(root, "ignition"));
    const int ignition_x = static_cast<int>(number(field(ignition_json, "x"))), ignition_y = static_cast<int>(number(field(ignition_json, "y")));
    const std::int32_t ignition = grid.find(ignition_x, ignition_y);
    if (ignition == kAbsent) throw std::runtime_error("ignition cell is not present in cells");

    const int steps = static_cast<int>(optional_number(root, "steps", 12));
    const double wind_speed = optional_number(root, "wind_speed_kmh", 0);
    const double wind_direction = optional_number(root, "wind_direction_degrees", 0);
    const double threshold = optional_number(root, "spread_threshold", .5);
    const double cell_size = optional_number(root, "cell_size_meters", 30.0);
    // Mirror the Pydantic bounds so raw-JSON clients are accepted/rejected exactly like Python clients.
    if (steps < 1 || steps > 720 || threshold < 0 || threshold > 1 || !(cell_size > 0) || cell_size > 10000
        || wind_speed < 0 || wind_speed > 300 || wind_direction < 0 || wind_direction >= 360) throw std::runtime_error("invalid simulation parameters");

    const std::size_t cells_with_filled_elevation = (use_terrain && cells_with_elevation) ? fill_elevation_voids(grid) : 0;
    const auto passable = [&grid](int x, int y) { const auto i = grid.find(x, y); return i != kAbsent && grid.cells[i].fuel_load > 0; };
    const auto clamp = [](double value, double low, double high) { return std::max(low, std::min(high, value)); };

    // v2: igniting a cell without fuel does not take (one empty frame, burned_cells = 0). v1 is frozen.
    const bool ignition_takes = !(use_terrain && grid.cells[ignition].fuel_load <= 0);
    std::vector<char> is_burned(grid.cells.size(), 0);
    std::vector<std::int32_t> burning;
    std::set<std::pair<int, int>> burned_sorted;
    if (ignition_takes) { burning.push_back(ignition); is_burned[ignition] = 1; burned_sorted.emplace(ignition_x, ignition_y); }

    std::vector<std::tuple<int, std::set<std::pair<int, int>>, std::set<std::pair<int, int>>>> frames;
    for (int step = 0; step < steps; ++step) {
      std::vector<std::int32_t> next;
      std::set<std::pair<int, int>> active_sorted;
      for (const auto source_index : burning) {
        const auto& source = grid.cells[source_index];
        const int x = source.x, y = source.y;
        for (const auto& [dx, dy] : kNeighbours) {
          const auto target = grid.find(x + dx, y + dy);
          if (target == kAbsent || is_burned[target]) continue;
          const auto& cell = grid.cells[target];
          if (use_terrain) {
            // v2: a cell without fuel cannot ignite, whatever the wind and slope do. v1 is frozen.
            if (cell.fuel_load <= 0) continue;
            // v2: no corner cutting through two impassable cells (diagonal rivers / fire breaks).
            if (dx != 0 && dy != 0 && !passable(x + dx, y) && !passable(x, y + dy)) continue;
          }
          const double bearing = std::fmod(std::atan2(static_cast<double>(dx), static_cast<double>(dy)) * 180.0 / std::acos(-1.0) + 360.0, 360.0);
          // Cosine between the spread bearing and the wind angle as written in the request.
          const double cos_toward = std::cos((bearing - wind_direction) * std::acos(-1.0) / 180.0);
          const double wind_norm = clamp(wind_speed / 80.0, 0, 1);
          // Mirrors wind_term()/slope_term()/spread_score() in spread_kernel.py, where the constants'
          // rationale lives. Operation order matters for bit-level parity.
          double wind = 0.0;
          double terrain = 0.0;
          if (use_terrain) {
            // v2: meteorological convention - the wind blows FROM wind_direction_degrees, so it
            // pushes the fire toward +180 deg; cos(x - 180deg) == -cos(x).
            const double wind_cos = -cos_toward;
            const double tail_drive = wind_norm * std::max(wind_cos, 0.0);
            const double head_drive = wind_norm * std::max(-wind_cos, 0.0);
            wind = tail_drive * terraflux::compute::kWindWeight - head_drive * terraflux::compute::kWindHeadwindWeight;
            if (source.elevation_meters && cell.elevation_meters) {
              const double run = cell_size * ((dx != 0 && dy != 0) ? std::sqrt(2.0) : 1.0);
              const double rise = *cell.elevation_meters - *source.elevation_meters;
              const double angle = std::atan(rise / run) * 180.0 / std::numbers::pi;
              const double steepness = clamp(angle / terraflux::compute::kSlopeSaturationDegrees, -1.0, 1.0);
              terrain = steepness >= 0 ? steepness * terraflux::compute::kSlopeUphillWeight
                                       : steepness * terraflux::compute::kSlopeDownhillWeight * (1.0 - tail_drive);
            }
          } else {
            // Frozen v1 expression, evaluated in the original order. v1 treated
            // wind_direction_degrees as the direction the wind blows TOWARD.
            const double alignment = (cos_toward + 1.0) / 2.0;
            wind = wind_norm * alignment * terraflux::compute::kWindWeightV1;
          }
          const double score = clamp(cell.fuel_load * .35 + (1.0 - cell.fuel_moisture) * .25 + cell.burn_susceptibility * .25 + wind + terrain, 0, 1);
          if (score >= threshold) { next.push_back(target); is_burned[target] = 1; active_sorted.emplace(cell.x, cell.y); burned_sorted.emplace(cell.x, cell.y); }
        }
      }
      burning = std::move(next);
      frames.emplace_back(step, std::move(active_sorted), burned_sorted);
      if (burning.empty()) break;
    }

    std::ostringstream output;
    output << "{\"contract_version\":\"" << contract_version << "\",\"engine\":\"cpp-native-v1\",\"deterministic\":true,\"grid_key\":" << quote(string(field(root, "grid_key"))) << ",\"frames\":[";
    for (std::size_t index = 0; index < frames.size(); ++index) {
      if (index) output << ',';
      const auto& [step, active, consumed] = frames[index];
      output << "{\"step\":" << step << ",\"active_cells\":"; coordinates(output, active);
      output << ",\"burned_cells\":"; coordinates(output, consumed);
      output << ",\"active_count\":" << active.size() << ",\"burned_count\":" << consumed.size() << '}';
    }
    output << "],\"metrics\":{\"burned_cells\":" << burned_sorted.size() << ",\"completed_steps\":" << frames.size();
    if (use_terrain) output << ",\"cells_with_elevation\":" << cells_with_elevation << ",\"cells_with_filled_elevation\":" << cells_with_filled_elevation;  // v1 metrics are frozen
    output << "}}";
    std::cout << output.str() << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 2;
  }
}
