#include <terraflux/terrain_mesh.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace terraflux::terrain {
namespace {

void add_triangle(TerrainMesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  mesh.indices.push_back(a);
  mesh.indices.push_back(b);
  mesh.indices.push_back(c);
  const math::Vec3& p0 = mesh.vertices[a].position;
  const math::Vec3& p1 = mesh.vertices[b].position;
  const math::Vec3& p2 = mesh.vertices[c].position;
  const math::Vec3 face = (p1 - p0).cross(p2 - p0);
  mesh.vertices[a].normal = mesh.vertices[a].normal + face;
  mesh.vertices[b].normal = mesh.vertices[b].normal + face;
  mesh.vertices[c].normal = mesh.vertices[c].normal + face;
}

}  // namespace

TerrainMesh build_terrain_mesh(const compute::Grid& grid, double cell_size_meters) {
  if (!(cell_size_meters > 0.0) || !std::isfinite(cell_size_meters))
    throw std::invalid_argument("terrain cell size must be positive and finite");

  TerrainMesh mesh;
  mesh.cell_size_meters = cell_size_meters;
  mesh.origin_grid_x = grid.min_x;
  mesh.origin_grid_y = grid.min_y;

  // Stabilize output across input permutations. The simulation's indices refer
  // to input order, so maintain an explicit input-index -> mesh-vertex mapping.
  std::vector<std::size_t> ordered(grid.cells.size());
  std::iota(ordered.begin(), ordered.end(), 0);
  std::sort(ordered.begin(), ordered.end(), [&grid](std::size_t a, std::size_t b) {
    const auto& lhs = grid.cells[a];
    const auto& rhs = grid.cells[b];
    if (lhs.y != rhs.y) return lhs.y < rhs.y;
    return lhs.x < rhs.x;
  });

  constexpr auto kInvalid = std::numeric_limits<std::uint32_t>::max();
  std::vector<std::uint32_t> vertex_for_cell(grid.cells.size(), kInvalid);
  mesh.vertices.reserve(grid.cells.size());

  for (const std::size_t cell_index : ordered) {
    const auto& cell = grid.cells[cell_index];
    if (!cell.elevation_meters) continue;  // do not invent height for voids
    if (!std::isfinite(*cell.elevation_meters))
      throw std::invalid_argument("terrain elevation must be finite");
    const double x = static_cast<double>(
        static_cast<std::int64_t>(cell.x) - mesh.origin_grid_x) * cell_size_meters;
    const double z = -static_cast<double>(
        static_cast<std::int64_t>(cell.y) - mesh.origin_grid_y) * cell_size_meters;
    if (!std::isfinite(x) || !std::isfinite(z))
      throw std::invalid_argument("terrain coordinates overflow");
    vertex_for_cell[cell_index] = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{x, *cell.elevation_meters, z}, {}, cell.x, cell.y});
  }

  // Each complete square becomes two CCW triangles, viewed from +Y.
  // Missing cells/elevations intentionally create holes rather than a bridge.
  for (const std::size_t base_index : ordered) {
    const auto& cell = grid.cells[base_index];
    if (cell.x == std::numeric_limits<std::int32_t>::max() ||
        cell.y == std::numeric_limits<std::int32_t>::max())
      continue;
    const auto br = grid.find(cell.x + 1, cell.y);
    const auto tl = grid.find(cell.x, cell.y + 1);
    const auto tr = grid.find(cell.x + 1, cell.y + 1);
    if (br == compute::kAbsent || tl == compute::kAbsent || tr == compute::kAbsent)
      continue;
    const auto a = vertex_for_cell[base_index];
    const auto b = vertex_for_cell[static_cast<std::size_t>(br)];
    const auto c = vertex_for_cell[static_cast<std::size_t>(tl)];
    const auto d = vertex_for_cell[static_cast<std::size_t>(tr)];
    if (a == kInvalid || b == kInvalid || c == kInvalid || d == kInvalid)
      continue;
    add_triangle(mesh, a, b, c);
    add_triangle(mesh, b, d, c);
  }

  for (auto& vertex : mesh.vertices) {
    const double len = vertex.normal.length();
    // Isolated vertices and numerically degenerate faces use a safe up normal.
    vertex.normal = len > 0.0 && std::isfinite(len)
        ? vertex.normal / len : math::Vec3{0.0, 1.0, 0.0};
  }
  return mesh;
}

}  // namespace terraflux::terrain
