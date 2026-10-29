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

// 0 = unburned, 0.5 = burned, 1 = actively burning. Only affects GPU data;
// never mutates the simulation, grid or stored frame history.
void apply_frame(const terrain::TerrainMesh& mesh, const Frame& frame,
                 graphics::TerrainUpload& upload);
}  // namespace terraflux::replay
