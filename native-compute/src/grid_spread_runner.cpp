#include <terraflux/contracts.hpp>

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

struct Json {
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value;
};

// Recursive-descent JSON parser. Generic values build a small Json tree; the "cells" array of the
// request - which is the bulk of the payload - is parsed straight into GridCell structs.
class Parser {
 public:
  explicit Parser(const std::string& input) : input_(input) {}

  // Parses the root request object. Every field except "cells" is returned in the Json object;
  // the cells go to `cells`. `saw_cells` tells whether the key was present at all.
  Json::Object parse_request(std::vector<GridCell>& cells, bool& saw_cells) {
    saw_cells = false;
    expect('{'); Json::Object result; whitespace();
    if (position_ < input_.size() && input_[position_] == '}') { ++position_; finish(); return result; }
    while (true) {
      const auto key = string(); expect(':');
      if (key == "cells" && !saw_cells) { saw_cells = true; cells_array(cells); }
      else result.emplace(key, value());
      whitespace(); const char delimiter = take();
      if (delimiter == '}') { finish(); return result; }
      if (delimiter != ',') throw std::runtime_error("invalid JSON object");
    }
  }

 private:
  const std::string& input_;
  std::size_t position_ = 0;

  void finish() { whitespace(); if (position_ != input_.size()) throw std::runtime_error("unexpected trailing JSON"); }
  void whitespace() { while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_; }
  char take() { if (position_ >= input_.size()) throw std::runtime_error("unexpected end of JSON"); return input_[position_++]; }
  char peek() { whitespace(); if (position_ >= input_.size()) throw std::runtime_error("unexpected end of JSON"); return input_[position_]; }
  void expect(char expected) { whitespace(); if (take() != expected) throw std::runtime_error("unexpected JSON token"); }
  void literal(const std::string& expected) { if (input_.compare(position_, expected.size(), expected) != 0) throw std::runtime_error("invalid JSON literal"); position_ += expected.size(); }

  Json value() {
    const char next = peek();
    if (next == '{') return object();
    if (next == '[') return array();
    if (next == '"') return Json{string()};
    if (next == 't') { literal("true"); return Json{true}; }
    if (next == 'f') { literal("false"); return Json{false}; }
    if (next == 'n') { literal("null"); return Json{nullptr}; }
    return Json{number()};
  }
  std::string string() {
    expect('"'); std::string output;
    while (true) {
      const char c = take();
      if (c == '"') return output;
      if (c != '\\') { output += c; continue; }
      const char escaped = take();
      if (escaped == '"' || escaped == '\\' || escaped == '/') output += escaped;
      else if (escaped == 'b') output += '\b';
      else if (escaped == 'f') output += '\f';
      else if (escaped == 'n') output += '\n';
      else if (escaped == 'r') output += '\r';
      else if (escaped == 't') output += '\t';
      else if (escaped == 'u') { for (int i = 0; i < 4; ++i) take(); output += '?'; }
      else throw std::runtime_error("invalid JSON escape");
    }
  }
  double number() {
    whitespace(); const char* begin = input_.c_str() + position_; char* end = nullptr;
    const double result = std::strtod(begin, &end);
    // strtod also accepts "inf"/"nan"/overflowing exponents; none of those are JSON numbers.
    if (end == begin || !std::isfinite(result)) throw std::runtime_error("invalid JSON number");
    position_ += static_cast<std::size_t>(end - begin); return result;
  }
  Json object() {
    expect('{'); Json::Object result; whitespace(); if (position_ < input_.size() && input_[position_] == '}') { ++position_; return Json{result}; }
    while (true) { const auto key = string(); expect(':'); result.emplace(key, value()); whitespace(); const char delimiter = take(); if (delimiter == '}') return Json{result}; if (delimiter != ',') throw std::runtime_error("invalid JSON object"); }
  }
  Json array() {
    expect('['); Json::Array result; whitespace(); if (position_ < input_.size() && input_[position_] == ']') { ++position_; return Json{result}; }
    while (true) { result.push_back(value()); whitespace(); const char delimiter = take(); if (delimiter == ']') return Json{result}; if (delimiter != ',') throw std::runtime_error("invalid JSON array"); }
  }

  // null -> nullopt, number -> value, anything else -> error (same acceptance as the Pydantic model).
  std::optional<double> optional_number_value() { if (peek() == 'n') { literal("null"); return std::nullopt; } return number(); }

  void cells_array(std::vector<GridCell>& cells) {
    expect('['); whitespace();
    if (position_ < input_.size() && input_[position_] == ']') { ++position_; return; }
    while (true) {
      cells.push_back(cell_object());
      if (cells.size() > terraflux::compute::kMaxCells) throw std::runtime_error("too many cells");
      whitespace(); const char delimiter = take();
      if (delimiter == ']') return;
      if (delimiter != ',') throw std::runtime_error("invalid JSON array");
    }
  }
  GridCell cell_object() {
    expect('{'); GridCell cell; bool seen[5] = {false, false, false, false, false};
    static const char* const kRequired[5] = {"x", "y", "fuel_load", "fuel_moisture", "burn_susceptibility"};
    whitespace();
    if (position_ < input_.size() && input_[position_] == '}') throw std::runtime_error("missing field: x");
    while (true) {
      const auto key = string(); expect(':');
      if (key == "x" && !seen[0]) { cell.x = static_cast<std::int32_t>(number()); seen[0] = true; }
      else if (key == "y" && !seen[1]) { cell.y = static_cast<std::int32_t>(number()); seen[1] = true; }
      else if (key == "fuel_load" && !seen[2]) { cell.fuel_load = number(); seen[2] = true; }
      else if (key == "fuel_moisture" && !seen[3]) { cell.fuel_moisture = number(); seen[3] = true; }
      else if (key == "burn_susceptibility" && !seen[4]) { cell.burn_susceptibility = number(); seen[4] = true; }
      else if (key == "elevation_meters" && !cell.elevation_meters) cell.elevation_meters = optional_number_value();
      else if (key == "population" && !cell.population) cell.population = optional_number_value();
      else value();  // unknown or repeated key: parse and ignore (Pydantic ignores extras; first value wins)
      whitespace(); const char delimiter = take();
      if (delimiter == '}') break;
      if (delimiter != ',') throw std::runtime_error("invalid JSON object");
    }
    for (int i = 0; i < 5; ++i) if (!seen[i]) throw std::runtime_error(std::string("missing field: ") + kRequired[i]);
    return cell;
  }
};

const Json::Object& object(const Json& json) { if (!std::holds_alternative<Json::Object>(json.value)) throw std::runtime_error("JSON object required"); return std::get<Json::Object>(json.value); }
const Json& field(const Json::Object& object, const std::string& key) { const auto found = object.find(key); if (found == object.end()) throw std::runtime_error("missing field: " + key); return found->second; }
double number(const Json& json) { if (!std::holds_alternative<double>(json.value)) throw std::runtime_error("JSON number required"); return std::get<double>(json.value); }
std::string string(const Json& json) { if (!std::holds_alternative<std::string>(json.value)) throw std::runtime_error("JSON string required"); return std::get<std::string>(json.value); }
double optional_number(const Json::Object& value, const std::string& key, double fallback) { const auto found = value.find(key); return found == value.end() || std::holds_alternative<std::nullptr_t>(found->second.value) ? fallback : number(found->second); }

constexpr std::pair<int, int> kNeighbours[] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}};
constexpr std::int32_t kAbsent = -1;

// Cells in input order plus an (x, y) -> index lookup: a dense bounding-box table when the grid
// fills its bounding box reasonably (O(1), cache friendly), otherwise a hash map.
struct Grid {
  std::vector<GridCell> cells;
  int min_x = 0, min_y = 0, width = 0, height = 0;
  bool dense = false;
  std::vector<std::int32_t> table;
  std::unordered_map<std::uint64_t, std::int32_t> sparse;

  static std::uint64_t key(int x, int y) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y); }

  explicit Grid(std::vector<GridCell> input) : cells(std::move(input)) {
    if (cells.empty()) return;
    int max_x = cells[0].x, max_y = cells[0].y; min_x = max_x; min_y = max_y;
    for (const auto& c : cells) { min_x = std::min(min_x, c.x); max_x = std::max(max_x, c.x); min_y = std::min(min_y, c.y); max_y = std::max(max_y, c.y); }
    const std::int64_t w = static_cast<std::int64_t>(max_x) - min_x + 1, h = static_cast<std::int64_t>(max_y) - min_y + 1;
    dense = w * h <= 4 * static_cast<std::int64_t>(cells.size());
    if (dense) {
      width = static_cast<int>(w); height = static_cast<int>(h);
      table.assign(static_cast<std::size_t>(w * h), kAbsent);
      for (std::size_t i = 0; i < cells.size(); ++i) {
        auto& slot = table[static_cast<std::size_t>(cells[i].x - min_x) * height + (cells[i].y - min_y)];
        if (slot != kAbsent) throw std::runtime_error("duplicate grid cell");
        slot = static_cast<std::int32_t>(i);
      }
    } else {
      sparse.reserve(cells.size() * 2);
      for (std::size_t i = 0; i < cells.size(); ++i)
        if (!sparse.emplace(key(cells[i].x, cells[i].y), static_cast<std::int32_t>(i)).second) throw std::runtime_error("duplicate grid cell");
    }
  }
  std::int32_t find(int x, int y) const {
    if (dense) {
      const std::int64_t xi = static_cast<std::int64_t>(x) - min_x, yi = static_cast<std::int64_t>(y) - min_y;
      if (xi < 0 || yi < 0 || xi >= width || yi >= height) return kAbsent;
      return table[static_cast<std::size_t>(xi) * height + static_cast<std::size_t>(yi)];
    }
    const auto found = sparse.find(key(x, y)); return found == sparse.end() ? kAbsent : found->second;
  }
};

// Mirrors fill_elevation_voids() in spread_kernel.py (rationale lives there): for each void fuel
// cell, the nearest cell with known elevation along each of the 8 rays, inverse-distance weighted.
// Rays pass through void fuel cells and stop at grid holes, at zero-fuel voids, or after
// kElevationFillMaxSearchCells. Every void is estimated from the ORIGINAL known cells only, so the
// result is order independent, and the 8 contributions are summed in kNeighbours order so the
// floating-point result is bit-identical to the reference. Returns the number of cells filled.
std::size_t fill_by_ray_cast(Grid& grid) {
  std::vector<std::pair<std::int32_t, double>> filled;
  for (std::int32_t i = 0; i < static_cast<std::int32_t>(grid.cells.size()); ++i) {
    const auto& cell = grid.cells[i];
    if (cell.elevation_meters || cell.fuel_load <= 0) continue;
    double numerator = 0.0, denominator = 0.0;
    for (const auto& [dx, dy] : kNeighbours) {
      const double length = (dx != 0 && dy != 0) ? std::sqrt(2.0) : 1.0;
      for (int k = 1; k <= terraflux::compute::kElevationFillMaxSearchCells; ++k) {
        const auto probe = grid.find(cell.x + dx * k, cell.y + dy * k);
        if (probe == kAbsent) break;
        const auto& other = grid.cells[probe];
        if (!other.elevation_meters) { if (other.fuel_load <= 0) break; continue; }
        const double weight = 1.0 / (k * length);
        numerator += weight * *other.elevation_meters;
        denominator += weight;
        break;
      }
    }
    if (denominator > 0) filled.emplace_back(i, numerator / denominator);
  }
  for (const auto& [i, z] : filled) grid.cells[i].elevation_meters = z;
  return filled.size();
}

// Same hits and arithmetic as the ray cast, found with one sweep per direction over the dense
// table: visiting v + d before v, the nearest hit of v is v + d itself if known, else the hit of
// v + d one step further. O(cells) regardless of how far the hits are.
std::size_t fill_by_line_sweep(Grid& grid) {
  const std::size_t size = grid.table.size();
  std::vector<double> numerator(size, 0.0), denominator(size, 0.0);
  std::vector<int> hit_k(size);
  std::vector<double> hit_z(size);
  const auto at = [&grid](std::size_t slot) -> const GridCell* { const auto i = grid.table[slot]; return i == kAbsent ? nullptr : &grid.cells[i]; };
  const auto fillable = [](const GridCell* c) { return c && !c->elevation_meters && c->fuel_load > 0; };
  for (const auto& [dx, dy] : kNeighbours) {
    std::fill(hit_k.begin(), hit_k.end(), 0);  // per direction: a skipped cell must not keep a stale hit
    const double length = (dx != 0 && dy != 0) ? std::sqrt(2.0) : 1.0;
    const int x_begin = dx > 0 ? grid.width - 1 : 0, x_step = dx > 0 ? -1 : 1;
    const int y_begin = dy > 0 ? grid.height - 1 : 0, y_step = dy > 0 ? -1 : 1;
    for (int n = 0, xi = x_begin; n < grid.width; ++n, xi += x_step) {
      const int ux = xi + dx;
      if (ux < 0 || ux >= grid.width) continue;
      for (int m = 0, yi = y_begin; m < grid.height; ++m, yi += y_step) {
        const std::size_t i = static_cast<std::size_t>(xi) * grid.height + yi;
        if (!fillable(at(i))) { hit_k[i] = 0; continue; }
        const int uy = yi + dy;
        if (uy < 0 || uy >= grid.height) { hit_k[i] = 0; continue; }
        const std::size_t u = static_cast<std::size_t>(ux) * grid.height + uy;
        const GridCell* other = at(u);
        if (!other) { hit_k[i] = 0; continue; }
        int k; double z;
        if (other->elevation_meters) { k = 1; z = *other->elevation_meters; }
        else if (other->fuel_load <= 0 || hit_k[u] == 0 || hit_k[u] + 1 > terraflux::compute::kElevationFillMaxSearchCells) { hit_k[i] = 0; continue; }
        else { k = hit_k[u] + 1; z = hit_z[u]; }
        hit_k[i] = k; hit_z[i] = z;
        const double weight = 1.0 / (k * length);
        numerator[i] += weight * z;
        denominator[i] += weight;
      }
    }
  }
  std::size_t count = 0;
  for (auto& cell : grid.cells) {
    if (cell.elevation_meters || cell.fuel_load <= 0) continue;
    const std::size_t i = static_cast<std::size_t>(cell.x - grid.min_x) * grid.height + (cell.y - grid.min_y);
    if (denominator[i] > 0) { cell.elevation_meters = numerator[i] / denominator[i]; ++count; }
  }
  return count;
}

std::size_t fill_elevation_voids(Grid& grid) { return grid.dense ? fill_by_line_sweep(grid) : fill_by_ray_cast(grid); }

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
