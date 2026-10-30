#include <terraflux/grid.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace terraflux::compute {

std::uint64_t Grid::key(int x, int y) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32)
       | static_cast<std::uint32_t>(y);
}

// Preserve the existing dense/sparse storage choice and coordinate ordering.
// An intentionally narrow extraction: numerical behavior belongs to the existing engine.
Grid::Grid(std::vector<GridCell> input) : cells(std::move(input)) {
  if (cells.empty()) return;
  int max_x = cells[0].x, max_y = cells[0].y; min_x = max_x; min_y = max_y;
  for (const auto& c : cells) {
    min_x = std::min(min_x, c.x); max_x = std::max(max_x, c.x);
    min_y = std::min(min_y, c.y); max_y = std::max(max_y, c.y);
  }
  const std::int64_t w = static_cast<std::int64_t>(max_x) - min_x + 1;
  const std::int64_t h = static_cast<std::int64_t>(max_y) - min_y + 1;
  dense = w * h <= 4 * static_cast<std::int64_t>(cells.size());
  if (dense) {
    width = static_cast<int>(w); height = static_cast<int>(h);
    table.assign(static_cast<std::size_t>(w * h), kAbsent);
    for (std::size_t i = 0; i < cells.size(); ++i) {
      auto& slot = table[static_cast<std::size_t>(cells[i].x - min_x) * height
                       + (cells[i].y - min_y)];
      if (slot != kAbsent) throw std::runtime_error("duplicate grid cell");
      slot = static_cast<std::int32_t>(i);
    }
  } else {
    sparse.reserve(cells.size() * 2);
    for (std::size_t i = 0; i < cells.size(); ++i) {
      if (!sparse.emplace(key(cells[i].x, cells[i].y), static_cast<std::int32_t>(i)).second)
        throw std::runtime_error("duplicate grid cell");
    }
  }
}

std::int32_t Grid::find(int x, int y) const {
  if (dense) {
    const std::int64_t xi = static_cast<std::int64_t>(x) - min_x;
    const std::int64_t yi = static_cast<std::int64_t>(y) - min_y;
    if (xi < 0 || yi < 0 || xi >= width || yi >= height) return kAbsent;
    return table[static_cast<std::size_t>(xi) * height + static_cast<std::size_t>(yi)];
  }
  const auto found = sparse.find(key(x, y));
  return found == sparse.end() ? kAbsent : found->second;
}

}  // namespace terraflux::compute
