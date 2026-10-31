#include <terraflux/terrain_mesh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terraflux::terrain {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Unnormalized, area-weighted face normal of CCW triangle (p0, p1, p2).
math::Vec3 face_normal(const math::Vec3& p0, const math::Vec3& p1, const math::Vec3& p2) {
  return (p1 - p0).cross(p2 - p0);
}

}  // namespace

TerrainMesh build_terrain_mesh(const compute::Grid& grid, double cell_size_meters) {
  if (!(cell_size_meters > 0.0) || !std::isfinite(cell_size_meters))
    throw std::invalid_argument("terrain cell size must be positive and finite");

  TerrainMesh mesh;
  mesh.cell_size_meters = cell_size_meters;
  mesh.simulation_cell_size_meters = cell_size_meters;
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
    if (!std::isfinite(cell.fuel_load) || !std::isfinite(cell.fuel_moisture))
      throw std::invalid_argument("terrain fuel attributes must be finite");
    vertex_for_cell[cell_index] = static_cast<std::uint32_t>(mesh.vertices.size());
    if (mesh.vertices.empty()) {
      mesh.min_elevation_meters = mesh.max_elevation_meters = *cell.elevation_meters;
    } else {
      mesh.min_elevation_meters = std::min(mesh.min_elevation_meters, *cell.elevation_meters);
      mesh.max_elevation_meters = std::max(mesh.max_elevation_meters, *cell.elevation_meters);
    }
    TerrainVertex vertex{{x, *cell.elevation_meters, z}, {}, cell.x, cell.y};
    vertex.fuel_load = std::clamp(cell.fuel_load, 0.0, 1.0);
    vertex.fuel_moisture = std::clamp(cell.fuel_moisture, 0.0, 1.0);
    mesh.vertices.push_back(vertex);
  }

  // Convexity from the measured neighbours only. Each axis with both neighbours
  // contributes (h- - 2h + h+) / cell; the sign is flipped so crests are positive.
  const auto height_at = [&](std::int64_t x, std::int64_t y) -> const double* {
    if (x < std::numeric_limits<std::int32_t>::min() || x > std::numeric_limits<std::int32_t>::max() ||
        y < std::numeric_limits<std::int32_t>::min() || y > std::numeric_limits<std::int32_t>::max())
      return nullptr;
    const auto index = grid.find(static_cast<int>(x), static_cast<int>(y));
    if (index == compute::kAbsent) return nullptr;
    const auto& elevation = grid.cells[static_cast<std::size_t>(index)].elevation_meters;
    return elevation ? &*elevation : nullptr;
  };
  for (auto& vertex : mesh.vertices) {
    double laplacian = 0.0;
    int axes = 0;
    const double h = vertex.position.y;
    const std::int64_t gx = vertex.grid_x, gy = vertex.grid_y;
    const auto accumulate_axis = [&](std::int64_t dx, std::int64_t dy) {
      const double* forward = height_at(gx + dx, gy + dy);
      const double* backward = height_at(gx - dx, gy - dy);
      if (!forward || !backward) return;
      laplacian += (*forward - 2.0 * h + *backward) / cell_size_meters;
      ++axes;
    };
    accumulate_axis(1, 0);
    accumulate_axis(0, 1);
    vertex.curvature = axes ? -laplacian / axes : 0.0;
    if (!std::isfinite(vertex.curvature)) throw std::invalid_argument("terrain curvature overflow");
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
    // Rendered geometry always uses the b-c diagonal.
    mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});

    // Shading normals accumulate both diagonal splits of the quad so that the
    // smooth normal has no preferred diagonal direction. Each corner receives
    // the face normals of the triangles that contain it in either split.
    const auto& pa = mesh.vertices[a].position;
    const auto& pb = mesh.vertices[b].position;
    const auto& pc = mesh.vertices[c].position;
    const auto& pd = mesh.vertices[d].position;
    const math::Vec3 abc = face_normal(pa, pb, pc), bdc = face_normal(pb, pd, pc);  // b-c split
    const math::Vec3 abd = face_normal(pa, pb, pd), adc = face_normal(pa, pd, pc);  // a-d split
    mesh.vertices[a].normal = mesh.vertices[a].normal + abc + abd + adc;
    mesh.vertices[b].normal = mesh.vertices[b].normal + abc + bdc + abd;
    mesh.vertices[c].normal = mesh.vertices[c].normal + abc + bdc + adc;
    mesh.vertices[d].normal = mesh.vertices[d].normal + bdc + abd + adc;
  }

  for (auto& vertex : mesh.vertices) {
    const double len = vertex.normal.length();
    // Isolated vertices and numerically degenerate faces use a safe up normal.
    vertex.normal = len > 0.0 && std::isfinite(len)
        ? vertex.normal / len : math::Vec3{0.0, 1.0, 0.0};
  }
  return mesh;
}

namespace {
// Cubic Hermite basis and derivatives on [0, 1].
struct Hermite { double a0, a1, b0, b1, da0, da1, db0, db1; };
Hermite hermite(double t) {
  const double t2 = t * t, t3 = t2 * t;
  return {2 * t3 - 3 * t2 + 1, -2 * t3 + 3 * t2, t3 - 2 * t2 + t, t3 - t2,
          6 * t2 - 6 * t, -6 * t2 + 6 * t, 3 * t2 - 4 * t + 1, 3 * t2 - 2 * t};
}
}  // namespace

std::uint32_t refinement_for_cell_size(const TerrainMesh& source, double target_cell_meters,
                                       std::size_t max_vertices) {
  if (!std::isfinite(target_cell_meters) || !(target_cell_meters > 0.0))
    throw std::invalid_argument("render cell size must be positive and finite");
  if (!(source.cell_size_meters > 0.0) || !std::isfinite(source.cell_size_meters))
    throw std::invalid_argument("source mesh has an invalid cell size");
  const double ratio = source.cell_size_meters / target_cell_meters;
  auto n = static_cast<std::uint32_t>(std::clamp(std::ceil(ratio - 1e-9), 1.0, 64.0));
  // Each complete quad contributes n*n new vertices at most; isolated vertices stay.
  const std::size_t quads = source.indices.size() / 6;
  while (n > 1 && source.vertices.size() + quads * (static_cast<std::size_t>(n) * n + 2 * n) > max_vertices) --n;
  return n;
}

TerrainMesh refine_terrain_mesh(const TerrainMesh& source, std::uint32_t subdivisions, std::size_t max_vertices) {
  if (subdivisions == 0 || subdivisions > 64) throw std::invalid_argument("terrain refinement must be 1..64 subdivisions");
  if (!(source.cell_size_meters > 0.0) || !std::isfinite(source.cell_size_meters))
    throw std::invalid_argument("source mesh has an invalid cell size");
  if (subdivisions == 1) return source;
  if (source.indices.size() % 3 != 0) throw std::invalid_argument("source mesh has a partial triangle");

  // Grid lookup of source vertices; positions must sit on the lattice.
  std::unordered_map<std::uint64_t, std::uint32_t> at;
  const auto key = [](std::int64_t x, std::int64_t y) {
    return (static_cast<std::uint64_t>(x) << 32) ^ static_cast<std::uint32_t>(y);
  };
  at.reserve(source.vertices.size());
  for (std::size_t i = 0; i < source.vertices.size(); ++i) {
    const auto& v = source.vertices[i];
    const double x = static_cast<double>(static_cast<std::int64_t>(v.grid_x) - source.origin_grid_x) * source.cell_size_meters;
    const double z = -static_cast<double>(static_cast<std::int64_t>(v.grid_y) - source.origin_grid_y) * source.cell_size_meters;
    if (std::abs(v.position.x - x) > 1e-6 * std::max(1.0, std::abs(x)) ||
        std::abs(v.position.z - z) > 1e-6 * std::max(1.0, std::abs(z)) || !std::isfinite(v.position.y))
      throw std::invalid_argument("terrain refinement requires vertices on the grid lattice");
    if (!at.emplace(key(v.grid_x, v.grid_y), static_cast<std::uint32_t>(i)).second)
      throw std::invalid_argument("terrain refinement requires unique grid vertices");
  }
  const auto find = [&](std::int64_t x, std::int64_t y) -> const TerrainVertex* {
    const auto it = at.find(key(x, y));
    return it == at.end() ? nullptr : &source.vertices[it->second];
  };
  // Per-vertex slopes in height per simulation cell, east (+x) and north (+y).
  std::vector<double> slope_x(source.vertices.size(), 0.0), slope_y(source.vertices.size(), 0.0);
  for (std::size_t i = 0; i < source.vertices.size(); ++i) {
    const auto& v = source.vertices[i];
    const auto difference = [&](std::int64_t dx, std::int64_t dy) {
      const auto* forward = find(v.grid_x + dx, v.grid_y + dy);
      const auto* backward = find(v.grid_x - dx, v.grid_y - dy);
      if (forward && backward) return (forward->position.y - backward->position.y) * 0.5;
      if (forward) return forward->position.y - v.position.y;
      if (backward) return v.position.y - backward->position.y;
      return 0.0;
    };
    slope_x[i] = difference(1, 0);
    slope_y[i] = difference(0, 1);
  }

  // Only triangulated quads are refined: mirror build_terrain_mesh's rule.
  std::vector<std::array<std::uint32_t, 4>> quads;  // a (x,y), b (x+1,y), c (x,y+1), d (x+1,y+1)
  for (std::size_t t = 0; t + 5 < source.indices.size(); t += 6) {
    const std::uint32_t a = source.indices[t], b = source.indices[t + 1], c = source.indices[t + 2];
    const std::uint32_t b2 = source.indices[t + 3], d = source.indices[t + 4], c2 = source.indices[t + 5];
    if (a >= source.vertices.size() || b >= source.vertices.size() || c >= source.vertices.size() || d >= source.vertices.size() ||
        b2 != b || c2 != c)
      throw std::invalid_argument("terrain refinement requires build_terrain_mesh quad triangulation");
    const auto& va = source.vertices[a];
    if (source.vertices[b].grid_x != va.grid_x + 1 || source.vertices[b].grid_y != va.grid_y ||
        source.vertices[c].grid_x != va.grid_x || source.vertices[c].grid_y != va.grid_y + 1 ||
        source.vertices[d].grid_x != va.grid_x + 1 || source.vertices[d].grid_y != va.grid_y + 1)
      throw std::invalid_argument("terrain refinement requires build_terrain_mesh quad triangulation");
    quads.push_back({a, b, c, d});
  }
  const std::size_t n = subdivisions;
  const std::size_t estimate = source.vertices.size() + quads.size() * (n * n + 2 * n);
  if (estimate > max_vertices) throw std::length_error("terrain refinement exceeds the render vertex budget");

  TerrainMesh fine;
  fine.origin_grid_x = source.origin_grid_x;
  fine.origin_grid_y = source.origin_grid_y;
  fine.cell_size_meters = source.cell_size_meters / static_cast<double>(n);
  fine.simulation_cell_size_meters = source.simulation_cell_size_meters;
  fine.refinement = static_cast<std::uint32_t>(n) * source.refinement;
  if (fine.refinement > 64 || fine.refinement < n) throw std::invalid_argument("terrain refinement must be 1..64 subdivisions");
  fine.vertices.reserve(estimate);
  fine.indices.reserve(quads.size() * n * n * 6);
  std::unordered_map<std::uint64_t, std::uint32_t> fine_at;
  fine_at.reserve(estimate);
  const std::int64_t scale = static_cast<std::int64_t>(n);
  const auto fine_key = [&](std::int64_t gx, std::int64_t gy) { return key(gx, gy); };

  // Isolated measured vertices (no triangle) are kept so the range is unchanged.
  std::vector<bool> referenced(source.vertices.size(), false);
  for (const auto& q : quads) for (const auto i : q) referenced[i] = true;

  const auto emit = [&](std::int64_t gx, std::int64_t gy, math::Vec3 position, math::Vec3 normal,
                        double fuel, double moisture, double curvature) -> std::uint32_t {
    const auto [it, inserted] = fine_at.emplace(fine_key(gx, gy), static_cast<std::uint32_t>(fine.vertices.size()));
    if (!inserted) return it->second;
    if (gx < std::numeric_limits<std::int32_t>::min() || gx > std::numeric_limits<std::int32_t>::max() ||
        gy < std::numeric_limits<std::int32_t>::min() || gy > std::numeric_limits<std::int32_t>::max())
      throw std::invalid_argument("terrain refinement grid index overflow");
    TerrainVertex v{position, normal, static_cast<std::int32_t>(gx), static_cast<std::int32_t>(gy)};
    v.fuel_load = fuel; v.fuel_moisture = moisture; v.curvature = curvature;
    if (fine.vertices.empty()) fine.min_elevation_meters = fine.max_elevation_meters = position.y;
    fine.min_elevation_meters = std::min(fine.min_elevation_meters, position.y);
    fine.max_elevation_meters = std::max(fine.max_elevation_meters, position.y);
    fine.vertices.push_back(v);
    return it->second;
  };
  const auto fine_index = [&](const TerrainVertex& v, std::int64_t i, std::int64_t j) {
    return std::pair{source.origin_grid_x + (static_cast<std::int64_t>(v.grid_x) - source.origin_grid_x) * scale + i,
                     source.origin_grid_y + (static_cast<std::int64_t>(v.grid_y) - source.origin_grid_y) * scale + j};
  };
  for (std::size_t i = 0; i < source.vertices.size(); ++i) {
    if (referenced[i]) continue;
    const auto& v = source.vertices[i];
    const auto [gx, gy] = fine_index(v, 0, 0);
    emit(gx, gy, v.position, v.normal, v.fuel_load, v.fuel_moisture, v.curvature);
  }

  std::vector<std::uint32_t> lattice((n + 1) * (n + 1));
  for (const auto& q : quads) {
    const TerrainVertex* corner[4] = {&source.vertices[q[0]], &source.vertices[q[1]], &source.vertices[q[2]], &source.vertices[q[3]]};
    const double h[4] = {corner[0]->position.y, corner[1]->position.y, corner[2]->position.y, corner[3]->position.y};
    const double hx[4] = {slope_x[q[0]], slope_x[q[1]], slope_x[q[2]], slope_x[q[3]]};
    const double hy[4] = {slope_y[q[0]], slope_y[q[1]], slope_y[q[2]], slope_y[q[3]]};
    const auto [gx0, gy0] = fine_index(*corner[0], 0, 0);
    for (std::size_t j = 0; j <= n; ++j) {
      const double v = static_cast<double>(j) / static_cast<double>(n);
      const Hermite hv = hermite(v);
      for (std::size_t i = 0; i <= n; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(n);
        const Hermite hu = hermite(u);
        // Ferguson patch (zero twist): tensor product of cubic Hermite bases.
        const double au[2] = {hu.a0, hu.a1}, bu[2] = {hu.b0, hu.b1}, dau[2] = {hu.da0, hu.da1}, dbu[2] = {hu.db0, hu.db1};
        const double av[2] = {hv.a0, hv.a1}, bv[2] = {hv.b0, hv.b1}, dav[2] = {hv.da0, hv.da1}, dbv[2] = {hv.db0, hv.db1};
        double height = 0.0, du = 0.0, dv = 0.0;
        for (int cj = 0; cj < 2; ++cj) for (int ci = 0; ci < 2; ++ci) {
          const int c = cj * 2 + ci;
          height += h[c] * au[ci] * av[cj] + hx[c] * bu[ci] * av[cj] + hy[c] * au[ci] * bv[cj];
          du += h[c] * dau[ci] * av[cj] + hx[c] * dbu[ci] * av[cj] + hy[c] * dau[ci] * bv[cj];
          dv += h[c] * au[ci] * dav[cj] + hx[c] * bu[ci] * dav[cj] + hy[c] * au[ci] * dbv[cj];
        }
        // Corners reproduce the measured heights exactly.
        if ((i == 0 || i == n) && (j == 0 || j == n)) height = h[(j == n ? 2 : 0) + (i == n ? 1 : 0)];
        const double gx_world = du / source.cell_size_meters;   // d height / d east
        const double gy_north = dv / source.cell_size_meters;   // d height / d north
        const math::Vec3 normal = math::Vec3{-gx_world, 1.0, gy_north}.normalized();
        const auto bilinear = [&](double v00, double v10, double v01, double v11) {
          return (v00 * (1 - u) + v10 * u) * (1 - v) + (v01 * (1 - u) + v11 * u) * v;
        };
        const double fuel = bilinear(corner[0]->fuel_load, corner[1]->fuel_load, corner[2]->fuel_load, corner[3]->fuel_load);
        const double moisture = bilinear(corner[0]->fuel_moisture, corner[1]->fuel_moisture, corner[2]->fuel_moisture, corner[3]->fuel_moisture);
        const double curvature = bilinear(corner[0]->curvature, corner[1]->curvature, corner[2]->curvature, corner[3]->curvature);
        const std::int64_t gx = gx0 + static_cast<std::int64_t>(i), gy = gy0 + static_cast<std::int64_t>(j);
        const math::Vec3 position{static_cast<double>(gx - source.origin_grid_x) * fine.cell_size_meters, height,
                                  -static_cast<double>(gy - source.origin_grid_y) * fine.cell_size_meters};
        if (!std::isfinite(height) || !std::isfinite(position.x) || !std::isfinite(position.z))
          throw std::invalid_argument("terrain refinement produced a non-finite position");
        lattice[j * (n + 1) + i] = emit(gx, gy, position, normal, fuel, moisture, curvature);
      }
    }
    for (std::size_t j = 0; j < n; ++j) for (std::size_t i = 0; i < n; ++i) {
      const std::uint32_t a = lattice[j * (n + 1) + i], b = lattice[j * (n + 1) + i + 1];
      const std::uint32_t c = lattice[(j + 1) * (n + 1) + i], d = lattice[(j + 1) * (n + 1) + i + 1];
      fine.indices.insert(fine.indices.end(), {a, b, c, b, d, c});
    }
  }
  if (fine.vertices.size() > max_vertices) throw std::length_error("terrain refinement exceeds the render vertex budget");
  return fine;
}

double vertex_slope_degrees(const TerrainVertex& vertex) {
  const auto& n = vertex.normal;
  const double len = n.length();
  if (!(len > 0.0) || !std::isfinite(len)) return 0.0;
  const double cosine = std::clamp(std::abs(n.y) / len, 0.0, 1.0);
  return std::acos(cosine) * 180.0 / kPi;
}

TerrainMeshSummary summarize_terrain_mesh(const TerrainMesh& mesh) {
  TerrainMeshSummary summary;
  summary.vertex_count = mesh.vertices.size();
  summary.triangle_count = mesh.indices.size() / 3;
  summary.min_elevation_meters = mesh.min_elevation_meters;
  summary.max_elevation_meters = mesh.max_elevation_meters;
  std::vector<bool> referenced(mesh.vertices.size(), false);
  for (const auto index : mesh.indices) {
    if (index >= mesh.vertices.size())
      throw std::invalid_argument("terrain mesh contains an out-of-range triangle index");
    referenced[index] = true;
  }
  std::size_t surface_vertices = 0;
  double slope_sum = 0.0;
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    if (!referenced[i]) continue;
    const auto& vertex = mesh.vertices[i];
    const double slope = vertex_slope_degrees(vertex);
    summary.max_slope_degrees = std::max(summary.max_slope_degrees, slope);
    summary.max_curvature = std::max(summary.max_curvature, vertex.curvature);
    summary.min_curvature = std::min(summary.min_curvature, vertex.curvature);
    if (vertex.fuel_load > 0.0) ++summary.vegetated_vertices;
    slope_sum += slope;
    ++surface_vertices;
  }
  if (surface_vertices) summary.mean_slope_degrees = slope_sum / static_cast<double>(surface_vertices);
  return summary;
}

}  // namespace terraflux::terrain
