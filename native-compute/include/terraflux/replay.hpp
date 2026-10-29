#pragma once
#include <terraflux/grid.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace terraflux::replay {
using Point = std::pair<std::int32_t, std::int32_t>;

struct Frame {
  int step = 0;
  std::vector<Point> active;
  std::vector<Point> burned;
};

struct Playback {
  compute::Grid grid;
  double cell_size_meters = 30.0;
  std::string grid_key;
  std::vector<Frame> frames;
};

// Takes the original request and its corresponding native runner's JSON
// response; verifies matching contract/grid and the cumulative frame shape.
Playback load(const std::string& request_json, const std::string& response_json);

// Dense GPU-visible cell field, distinct from terrain vertices. A 3x3 shader
// neighborhood of rounded fire footprints avoids square vertex interpolation.
// 0 = valid and unburned, 1 = burned, 2 = active, 3 = absent cell.
// Coordinates are always checked against the *actual* simulation grid.
struct FireField {
  std::int32_t min_x = 0, min_y = 0;
  std::uint32_t width = 0, height = 0;
  std::vector<std::uint32_t> states;
};

FireField make_fire_field(const compute::Grid& grid);
void apply_frame(const Frame& frame, FireField& field);
std::uint32_t state_at(const FireField& field, std::int32_t x, std::int32_t y);

}  // namespace terraflux::replay
