#pragma once
#include <terraflux/contracts.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terraflux::compute {

// Shared spatial grid representation for simulation, terrain meshing and future rendering.
// Indexes refer to cells in their original input order.
inline constexpr std::int32_t kAbsent = -1;
inline constexpr std::pair<int, int> kNeighbours[] = {
    {-1,-1}, {-1,0}, {-1,1}, {0,-1}, {0,1}, {1,-1}, {1,0}, {1,1}
};

struct Grid {
  std::vector<GridCell> cells;
  int min_x = 0, min_y = 0, width = 0, height = 0;
  bool dense = false;
  std::vector<std::int32_t> table;
  std::unordered_map<std::uint64_t, std::int32_t> sparse;

  static std::uint64_t key(int x, int y);
  explicit Grid(std::vector<GridCell> input);
  std::int32_t find(int x, int y) const;
};

// Fill elevation gaps in-place using only original measured elevations.
// Zero-fuel cells without elevation block lookup rays (e.g. rivers).
// Returns the number of cells whose elevations were filled.
std::size_t fill_elevation_voids(Grid& grid);

}  // namespace terraflux::compute
