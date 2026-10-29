#include <terraflux/replay.hpp>
#include <terraflux/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraflux::replay {
namespace {
using namespace terraflux::json;

int integer(const Json& value, const char* label) {
  const double n = number(value);
  if (n < std::numeric_limits<std::int32_t>::min() ||
      n > std::numeric_limits<std::int32_t>::max() || n != std::trunc(n))
    throw std::invalid_argument(std::string("invalid integer: ") + label);
  return static_cast<int>(n);
}

const Json::Array& array(const Json& value) {
  if (!std::holds_alternative<Json::Array>(value.value))
    throw std::invalid_argument("JSON array required");
  return std::get<Json::Array>(value.value);
}

std::vector<Point> points(const Json& values, const compute::Grid& grid, const char* label) {
  const auto& entries = array(values);
  if (entries.size() > grid.cells.size())
    throw std::invalid_argument(std::string(label) + " exceeds grid size");
  std::vector<Point> result;
  result.reserve(entries.size());
  std::set<Point> unique;
  for (const auto& entry : entries) {
    const auto& pair = array(entry);
    if (pair.size() != 2) throw std::invalid_argument("coordinate must have x and y");
    Point point{integer(pair[0], "x"), integer(pair[1], "y")};
    if (grid.find(point.first, point.second) == compute::kAbsent ||
        !unique.insert(point).second)
      throw std::invalid_argument(std::string("duplicate or off-grid ") + label + " coordinate");
    result.push_back(point);
  }
  return result;
}

}  // namespace

Playback load(const std::string& request_json, const std::string& response_json) {
  std::vector<compute::GridCell> cells;
  bool had_cells = false;
  const auto request = Parser(request_json).parse_request(cells, had_cells);
  if (!had_cells || cells.empty() || cells.size() > compute::kMaxCells)
    throw std::invalid_argument("request must have at least one cell");

  const std::string version = string(field(request, "contract_version"));
  if (!compute::is_supported_contract_version(version))
    throw std::invalid_argument("unsupported compute contract");
  const std::string key = string(field(request, "grid_key"));
  const double size = optional_number(request, "cell_size_meters", 30.0);
  if (!(size > 0.0 && size <= 10000.0))
    throw std::invalid_argument("invalid cell size");

  Playback playback{compute::Grid(std::move(cells)), size, key, {}};
  const auto response = object(Parser(response_json).parse_document());
  if (string(field(response, "grid_key")) != key ||
      string(field(response, "contract_version")) != version)
    throw std::invalid_argument("request and response refer to different simulations");

  const auto& raw_frames = array(field(response, "frames"));
  if (raw_frames.empty() || raw_frames.size() > 720)
    throw std::invalid_argument("replay must contain 1..720 frames");
  std::set<Point> previous_burned;
  for (std::size_t i = 0; i < raw_frames.size(); ++i) {
    const auto& f = object(raw_frames[i]);
    Frame frame;
    frame.step = integer(field(f, "step"), "step");
    if (frame.step != static_cast<int>(i))
      throw std::invalid_argument("nonsequential replay step");
    frame.active = points(field(f, "active_cells"), playback.grid, "active");
    frame.burned = points(field(f, "burned_cells"), playback.grid, "burned");
    std::set<Point> burned(frame.burned.begin(), frame.burned.end());
    if (!std::includes(burned.begin(), burned.end(), previous_burned.begin(), previous_burned.end()))
      throw std::invalid_argument("cumulative burned cells decreased");
    for (const Point& point : frame.active)
      if (!burned.contains(point))
        throw std::invalid_argument("active cell missing from burned set");
    if (integer(field(f, "burned_count"), "burned_count") != static_cast<int>(frame.burned.size()) ||
        integer(field(f, "active_count"), "active_count") != static_cast<int>(frame.active.size()))
      throw std::invalid_argument("frame counts do not match coordinates");
    previous_burned = std::move(burned);
    playback.frames.push_back(std::move(frame));
  }
  return playback;
}

void apply_frame(const terrain::TerrainMesh& mesh, const Frame& frame,
                 graphics::TerrainUpload& upload) {
  if (mesh.vertices.size() != upload.vertices.size())
    throw std::invalid_argument("mesh/upload vertex count mismatch");
  // Updating every vertex is deliberately simple for the first viewer.
  // Later stages can add sparse GPU updates using frame deltas.
  std::set<Point> active(frame.active.begin(), frame.active.end());
  std::set<Point> burned(frame.burned.begin(), frame.burned.end());
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    const Point point{mesh.vertices[i].grid_x, mesh.vertices[i].grid_y};
    upload.vertices[i].fire_state = active.contains(point) ? 1.0f :
        burned.contains(point) ? 0.5f : 0.0f;
  }
}
}  // namespace terraflux::replay
