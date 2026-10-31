#include <terraflux/grid.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace terraflux::compute {
namespace {

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


}  // namespace

// Public entry point; retains the existing dense/sparse selection and exact arithmetic.
std::size_t fill_elevation_voids(Grid& grid) {
  return grid.dense ? fill_by_line_sweep(grid) : fill_by_ray_cast(grid);
}

}  // namespace terraflux::compute
