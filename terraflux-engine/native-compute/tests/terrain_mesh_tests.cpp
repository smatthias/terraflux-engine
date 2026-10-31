#include <terraflux/terrain_mesh.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

using terraflux::compute::Grid;
using terraflux::compute::GridCell;
using terraflux::compute::fill_elevation_voids;
using terraflux::terrain::TerrainMesh;
using terraflux::terrain::build_terrain_mesh;

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) < tol; }

GridCell cell(int x, int y, std::optional<double> elevation, double fuel = 1.0) {
  GridCell c{};
  c.x = x; c.y = y; c.elevation_meters = elevation;
  c.fuel_load = fuel; c.fuel_moisture = 0.1; c.burn_susceptibility = 1.0;
  return c;
}

void test_flat_quad_and_winding() {
  Grid grid({cell(1, 1, 4), cell(0, 0, 4), cell(0, 1, 4), cell(1, 0, 4)});
  const TerrainMesh mesh = build_terrain_mesh(grid, 30.0);
  require(mesh.vertices.size() == 4 && mesh.indices.size() == 6, "flat quad topology");
  require(mesh.indices == std::vector<std::uint32_t>({0, 1, 2, 1, 3, 2}), "CCW indices and stable vertex order");
  require(mesh.vertices[0].grid_x == 0 && mesh.vertices[0].grid_y == 0, "vertex grid metadata");
  require(near(mesh.vertices[0].position.x, 0) && near(mesh.vertices[0].position.z, 0), "quad origin");
  require(near(mesh.vertices[3].position.x, 30) && near(mesh.vertices[3].position.z, -30), "north-to-negative-Z mapping");
  for (const auto& v : mesh.vertices)
    require(near(v.normal.x, 0) && near(v.normal.y, 1) && near(v.normal.z, 0), "flat normal points up");
}

void test_sloped_normals() {
  Grid grid({cell(0, 0, 0), cell(1, 0, 10), cell(0, 1, 0), cell(1, 1, 10)});
  const auto mesh = build_terrain_mesh(grid, 30);
  require(mesh.indices.size() == 6, "sloped quad triangles");
  for (const auto& v : mesh.vertices) {
    require(v.normal.x < 0 && v.normal.y > 0 && near(v.normal.z, 0), "uphill normal direction");
    require(near(v.normal.length(), 1.0), "normal must be unit length");
  }
}

void test_incomplete_quad_does_not_bridge() {
  Grid grid({cell(0, 0, 0), cell(1, 0, 0), cell(0, 1, 0)});
  const auto mesh = build_terrain_mesh(grid, 30);
  require(mesh.vertices.size() == 3 && mesh.indices.empty(), "missing corner must not be bridged");
  for (const auto& v : mesh.vertices) require(near(v.normal.y, 1), "isolated normal defaults up");
}

void test_disconnected_grids() {
  Grid grid({cell(0, 0, 10), cell(1, 0, 10), cell(0, 1, 10), cell(1, 1, 10),
             cell(10, 0, 20), cell(11, 0, 20), cell(10, 1, 20), cell(11, 1, 20)});
  const auto mesh = build_terrain_mesh(grid, 30);
  require(mesh.vertices.size() == 8 && mesh.indices.size() == 12, "disconnected surfaces must not bridge gap");
}

void test_missing_elevation_and_filling() {
  Grid grid({cell(0, 0, 100), cell(1, 0, 100), cell(0, 1, 100), cell(1, 1, std::nullopt)});
  const auto before = build_terrain_mesh(grid, 30);
  require(before.vertices.size() == 3 && before.indices.empty(), "unknown terrain must not become geometry");
  require(fill_elevation_voids(grid) == 1, "existing interpolation fills a reachable void");
  const auto after = build_terrain_mesh(grid, 30);
  require(after.vertices.size() == 4 && after.indices.size() == 6, "filled terrain becomes renderable");
  require(near(after.vertices[3].position.y, 100), "filled elevation carried into geometry");
}

void test_unmeasured_river_stays_hole() {
  Grid grid({cell(0, 0, 100), cell(1, 0, 100), cell(0, 1, 100),
             cell(1, 1, std::nullopt, 0.0)});
  require(fill_elevation_voids(grid) == 0, "zero-fuel unmeasured river must not fill");
  require(build_terrain_mesh(grid, 30).indices.empty(), "unmeasured river must not create a surface");
}

void test_stable_under_input_permutation() {
  Grid a({cell(0, 0, 0), cell(1, 0, 5), cell(0, 1, 10), cell(1, 1, 15)});
  Grid b({cell(1, 1, 15), cell(0, 1, 10), cell(1, 0, 5), cell(0, 0, 0)});
  const auto left = build_terrain_mesh(a, 30);
  const auto right = build_terrain_mesh(b, 30);
  require(left.indices == right.indices, "triangulation must be independent of input order");
  for (std::size_t i = 0; i < left.vertices.size(); ++i) {
    const auto& v = left.vertices[i]; const auto& w = right.vertices[i];
    require(v.grid_x == w.grid_x && v.grid_y == w.grid_y, "vertex order must be coordinate-stable");
    require(near(v.position.x, w.position.x) && near(v.position.y, w.position.y) &&
            near(v.position.z, w.position.z), "vertex positions must match");
    require(near(v.normal.x, w.normal.x) && near(v.normal.y, w.normal.y) &&
            near(v.normal.z, w.normal.z), "normal order must match");
  }
}

void test_local_origin() {
  Grid grid({cell(-10, 25, 8), cell(-9, 25, 8), cell(-10, 26, 8), cell(-9, 26, 8)});
  const auto mesh = build_terrain_mesh(grid, 15);
  require(mesh.origin_grid_x == -10 && mesh.origin_grid_y == 25, "origin metadata");
  require(near(mesh.vertices[0].position.x, 0) && near(mesh.vertices[0].position.z, 0), "local origin");
  require(near(mesh.vertices[3].position.x, 15) && near(mesh.vertices[3].position.z, -15), "local spacing");
}

void test_invalid_input_and_int_max() {
  Grid grid({cell(std::numeric_limits<std::int32_t>::max(), 0, 5)});
  const auto mesh = build_terrain_mesh(grid, 1);
  require(mesh.vertices.size() == 1 && mesh.indices.empty(), "max integer coordinate safe");
  bool invalid = false;
  try { (void)build_terrain_mesh(grid, 0); } catch (const std::invalid_argument&) { invalid = true; }
  require(invalid, "zero cell size must be rejected");
  invalid = false;
  try { (void)build_terrain_mesh(grid, std::numeric_limits<double>::infinity()); }
  catch (const std::invalid_argument&) { invalid = true; }
  require(invalid, "infinite cell size must be rejected");
}

void test_elevation_range_metadata() {
  Grid grid({cell(0, 0, 12.5), cell(1, 0, -3.0), cell(0, 1, 40.0), cell(1, 1, std::nullopt),
             cell(9, 9, 7.0)});
  const auto mesh = build_terrain_mesh(grid, 30);
  require(near(mesh.min_elevation_meters, -3.0) && near(mesh.max_elevation_meters, 40.0),
          "range covers every vertex, including isolated ones, and ignores voids");
  Grid flat({cell(0, 0, 250), cell(1, 0, 250), cell(0, 1, 250), cell(1, 1, 250)});
  const auto flat_mesh = build_terrain_mesh(flat, 30);
  require(near(flat_mesh.min_elevation_meters, 250) && near(flat_mesh.max_elevation_meters, 250),
          "flat terrain has a zero-width range");
  Grid voids({cell(0, 0, std::nullopt), cell(1, 0, std::nullopt)});
  const auto empty = build_terrain_mesh(voids, 30);
  require(empty.vertices.empty() && empty.min_elevation_meters == 0 && empty.max_elevation_meters == 0,
          "meshes without vertices report a zero range");
}

// Analytic plane z = a*x + b*y: every complete quad is coplanar, so smooth
// normals must equal the plane normal exactly, at edges and corners too.
void test_planar_normals_are_exact_everywhere() {
  const double a = 0.35, b = -0.2, size = 10.0;
  std::vector<GridCell> cells;
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 7; ++x) cells.push_back(cell(x, y, a * x * size + b * y * size));
  const auto mesh = build_terrain_mesh(Grid(cells), size);
  // World: +X east, +Y up, -Z north => d(height)/dX = a, d(height)/d(-Z) = b.
  const terraflux::math::Vec3 expected = terraflux::math::Vec3{-a, 1.0, b}.normalized();
  for (const auto& v : mesh.vertices)
    require(near(v.normal.x, expected.x, 1e-12) && near(v.normal.y, expected.y, 1e-12) &&
            near(v.normal.z, expected.z, 1e-12), "planar normal must be exact at every vertex");
  const double expected_slope = std::atan(std::hypot(a, b)) * 180.0 / 3.14159265358979323846;
  for (const auto& v : mesh.vertices)
    require(near(terraflux::terrain::vertex_slope_degrees(v), expected_slope, 1e-9),
            "slope follows the analytic plane gradient");
}

// A radially symmetric bump must yield normals that follow the grid's 90-degree
// rotational symmetry. Single-diagonal face accumulation breaks this symmetry.
void test_normals_are_diagonal_independent() {
  constexpr int kSide = 9, kCenter = 4;
  std::vector<GridCell> cells;
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x) {
      const double r2 = (x - kCenter) * (x - kCenter) + (y - kCenter) * (y - kCenter);
      cells.push_back(cell(x, y, 120.0 * std::exp(-r2 / 6.0)));
    }
  const auto mesh = build_terrain_mesh(Grid(cells), 30);
  const auto at = [&](int x, int y) -> const terraflux::terrain::TerrainVertex& {
    return mesh.vertices[static_cast<std::size_t>(y) * kSide + static_cast<std::size_t>(x)];
  };
  require(at(3, 5).grid_x == 3 && at(3, 5).grid_y == 5, "row-major vertex order for a dense grid");
  // Rotating the grid by 90 degrees about the center maps (dx, dy) to (-dy, dx);
  // world X is grid x and world Z is -grid y, so (nx, nz) maps to (nz, -nx).
  for (int dy = -kCenter; dy <= kCenter; ++dy)
    for (int dx = -kCenter; dx <= kCenter; ++dx) {
      const auto& n = at(kCenter + dx, kCenter + dy).normal;
      const auto& r = at(kCenter - dy, kCenter + dx).normal;
      require(near(r.x, n.z, 1e-9) && near(r.y, n.y, 1e-9) && near(r.z, -n.x, 1e-9),
              "smooth normals must be invariant under grid rotation (no diagonal bias)");
    }
  const auto& apex = at(kCenter, kCenter).normal;
  require(near(apex.x, 0, 1e-12) && near(apex.z, 0, 1e-12) && near(apex.y, 1),
          "the bump apex normal points straight up");
  require(at(kCenter + 1, kCenter).normal.x > 0.3 && at(kCenter - 1, kCenter).normal.x < -0.3,
          "flank normals lean outward, away from the apex");
  const auto summary = terraflux::terrain::summarize_terrain_mesh(mesh);
  require(summary.max_slope_degrees > 30 && summary.max_slope_degrees < 80 &&
          summary.mean_slope_degrees > 0 && summary.mean_slope_degrees < summary.max_slope_degrees,
          "bump slopes are steep on the flanks but not everywhere");
}

void test_normals_ignore_holes() {
  // Two complete quads west of a hole column and one isolated eastern vertex.
  Grid grid({cell(0, 0, 0), cell(1, 0, 30), cell(2, 0, std::nullopt), cell(3, 0, 900),
             cell(0, 1, 0), cell(1, 1, 30), cell(2, 1, std::nullopt),
             cell(0, 2, 0), cell(1, 2, 30)});
  const auto mesh = build_terrain_mesh(grid, 30);
  require(mesh.indices.size() == 12, "two complete quads");
  const terraflux::math::Vec3 expected = terraflux::math::Vec3{-1, 1, 0}.normalized();
  for (const auto& v : mesh.vertices) {
    if (v.grid_x == 3) {
      require(near(v.normal.y, 1.0), "a vertex without complete quads keeps the up normal");
      continue;
    }
    require(near(v.normal.x, expected.x, 1e-12) && near(v.normal.y, expected.y, 1e-12) &&
            near(v.normal.z, 0, 1e-12), "the hole and the isolated vertex never influence neighbours");
  }
  const auto summary = terraflux::terrain::summarize_terrain_mesh(mesh);
  require(summary.vertex_count == 7 && summary.triangle_count == 4, "summary counts");
  require(near(summary.max_slope_degrees, 45.0, 1e-9) && near(summary.mean_slope_degrees, 45.0, 1e-9),
          "slope statistics exclude the isolated vertex");
  require(near(summary.min_elevation_meters, 0) && near(summary.max_elevation_meters, 900),
          "summary range still includes the isolated vertex");
}

void test_slope_helper_edge_cases() {
  terraflux::terrain::TerrainVertex v{};
  v.normal = {0, 1, 0};
  require(near(terraflux::terrain::vertex_slope_degrees(v), 0), "flat");
  v.normal = {0, 3, 0};
  require(near(terraflux::terrain::vertex_slope_degrees(v), 0), "length is irrelevant");
  v.normal = {1, 0, 0};
  require(near(terraflux::terrain::vertex_slope_degrees(v), 90), "vertical");
  v.normal = {0, -1, 0};
  require(near(terraflux::terrain::vertex_slope_degrees(v), 0), "downward normals mirror upward");
  v.normal = {0, 0, 0};
  require(near(terraflux::terrain::vertex_slope_degrees(v), 0), "degenerate normal is flat, not NaN");
  v.normal = {1, 1, 0};
  require(near(terraflux::terrain::vertex_slope_degrees(v), 45), "45 degrees");
  terraflux::terrain::TerrainMesh broken;
  broken.vertices.push_back(v);
  broken.indices = {0, 0, 7};
  bool rejected = false;
  try { (void)terraflux::terrain::summarize_terrain_mesh(broken); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "summary rejects out-of-range indices");
}

void test_surface_attributes_and_curvature() {
  // A symmetric ridge along y: crest vertices are convex, the foot is concave,
  // the flanks are planar; fuel attributes are copied and clamped per vertex.
  std::vector<GridCell> cells;
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 7; ++x) {
      auto c = cell(x, y, 30.0 * (3 - std::abs(x - 3)), x == 5 ? 1.4 : 0.25 * x);
      c.fuel_moisture = x == 6 ? -0.5 : 0.1 * x;
      cells.push_back(c);
    }
  const auto mesh = build_terrain_mesh(Grid(cells), 30);
  for (const auto& v : mesh.vertices) {
    const double expected_fuel = std::min(1.0, v.grid_x == 5 ? 1.4 : 0.25 * v.grid_x);
    const double expected_moisture = v.grid_x == 6 ? 0.0 : 0.1 * v.grid_x;
    require(near(v.fuel_load, expected_fuel) && near(v.fuel_moisture, expected_moisture),
            "fuel load and moisture are copied from the cell and clamped to [0, 1]");
    // Row y=1 has both north/south neighbours (planar along y, zero contribution).
    if (v.grid_y == 1) {
      if (v.grid_x == 3) require(near(v.curvature, (60.0 / 30.0) / 2.0), "crest convexity -(60-180+60)/cell averaged with the flat axis");
      else if (v.grid_x == 0 || v.grid_x == 6) require(near(v.curvature, 0.0), "edge columns lack one east/west neighbour");
      else require(near(v.curvature, 0.0), "planar flanks have zero curvature");
    } else {
      // Border rows have only the east/west axis.
      if (v.grid_x == 3) require(near(v.curvature, 60.0 / 30.0), "crest curvature from the single available axis");
      else require(near(v.curvature, 0.0), "borders never invent relief");
    }
  }
  // A basin is the mirror image: negative curvature of the same magnitude.
  std::vector<GridCell> basin;
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 3; ++x) basin.push_back(cell(x, y, (x == 1 && y == 1) ? 0.0 : 15.0));
  const auto pit = build_terrain_mesh(Grid(basin), 30);
  require(near(pit.vertices[4].curvature, -(15.0 + 15.0) / 30.0), "a pit is concave on both axes");
  // Holes beside a vertex remove that axis from the estimate instead of bridging.
  std::vector<GridCell> holed = basin;
  holed.erase(std::remove_if(holed.begin(), holed.end(), [](const GridCell& c) { return c.x == 2 && c.y == 1; }), holed.end());
  const auto partial = build_terrain_mesh(Grid(holed), 30);
  for (const auto& v : partial.vertices)
    if (v.grid_x == 1 && v.grid_y == 1) require(near(v.curvature, -(15.0 + 15.0) / 30.0), "the remaining north/south axis alone defines the pit");
  const auto summary = terraflux::terrain::summarize_terrain_mesh(mesh);
  require(near(summary.max_curvature, 2.0) && near(summary.min_curvature, 0.0) && summary.vegetated_vertices == 18,
          "summary reports crest/basin extremes and vegetated surface vertices");
  auto bad = cells;
  bad[0].fuel_load = std::numeric_limits<double>::quiet_NaN();
  bool rejected = false;
  try { (void)build_terrain_mesh(Grid(bad), 30); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "non-finite fuel attributes are rejected");
}

void test_refinement() {
  using terraflux::terrain::refine_terrain_mesh;
  using terraflux::terrain::refinement_for_cell_size;
  // Plane: the refined surface is exactly planar with exact normals everywhere.
  const double a = 0.2, b = -0.35;
  std::vector<GridCell> cells;
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 5; ++x) cells.push_back(cell(x, y, a * x * 30 + b * y * 30, 0.25 * x));
  const auto coarse = build_terrain_mesh(Grid(cells), 30);
  const auto fine = refine_terrain_mesh(coarse, 6);
  require(fine.refinement == 6 && near(fine.cell_size_meters, 5) && near(fine.simulation_cell_size_meters, 30) &&
          fine.origin_grid_x == coarse.origin_grid_x && fine.origin_grid_y == coarse.origin_grid_y, "refined metadata");
  require(fine.vertices.size() == (4 * 6 + 1) * (3 * 6 + 1) && fine.indices.size() == 12 * 36 * 6,
          "shared lattice vertices and n*n quads per simulation quad");
  const terraflux::math::Vec3 expected = terraflux::math::Vec3{-a, 1.0, b}.normalized();
  for (const auto& v : fine.vertices) {
    const double east = v.position.x, north = -v.position.z;
    require(near(v.position.y, a * east + b * north, 1e-9), "planar heights are reproduced between the measured vertices");
    require(near(v.normal.x, expected.x, 1e-9) && near(v.normal.y, expected.y, 1e-9) && near(v.normal.z, expected.z, 1e-9),
            "analytic normals of the interpolated surface");
    require(near(v.fuel_load, 0.25 * east / 30.0, 1e-9), "attributes interpolate bilinearly");
    const auto sim_x = fine.origin_grid_x + (v.grid_x - fine.origin_grid_x) / static_cast<std::int32_t>(fine.refinement);
    require(sim_x >= 0 && sim_x <= 4 && near(v.position.x, (v.grid_x - fine.origin_grid_x) * 5.0), "render lattice indices map back to simulation cells");
  }
  require(fine.min_elevation_meters == coarse.min_elevation_meters && fine.max_elevation_meters == coarse.max_elevation_meters,
          "a planar refinement keeps the elevation range");
  // A bump: measured vertices are reproduced exactly, the surface stays bounded and smooth.
  std::vector<GridCell> bump;
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 5; ++x) bump.push_back(cell(x, y, (x == 2 && y == 2) ? 60.0 : 0.0));
  const auto hill = build_terrain_mesh(Grid(bump), 30);
  const auto smooth = refine_terrain_mesh(hill, 4);
  std::size_t exact = 0;
  for (const auto& v : smooth.vertices) {
    if ((v.grid_x - smooth.origin_grid_x) % 4 == 0 && (v.grid_y - smooth.origin_grid_y) % 4 == 0) {
      const bool apex = v.grid_x - smooth.origin_grid_x == 8 && v.grid_y - smooth.origin_grid_y == 8;
      require(near(v.position.y, apex ? 60.0 : 0.0), "measured vertices are reproduced exactly");
      ++exact;
    }
    require(v.position.y > -20.0 && v.position.y <= 60.0 + 1e-9, "the cubic surface stays bounded");
    require(near(v.normal.length(), 1.0), "unit normals");
  }
  require(exact == 25, "every measured vertex appears once in the refined lattice");
  const auto at = [&](int gx, int gy) {
    for (const auto& v : smooth.vertices)
      if (v.grid_x - smooth.origin_grid_x == gx && v.grid_y - smooth.origin_grid_y == gy) return v;
    throw std::runtime_error("missing refined vertex");
  };
  require(at(6, 8).position.y > 20.0 && at(6, 8).position.y < 60.0 && at(6, 8).position.y > at(5, 8).position.y,
          "the flank rises smoothly toward the apex");
  require(near(at(8, 8).normal.x, 0.0, 1e-9) && near(at(8, 8).normal.z, 0.0, 1e-9), "the apex normal is vertical");
  require(at(6, 8).normal.x < -0.3, "the west flank normal leans west");
  // Holes stay holes; isolated measured vertices survive; identity for one subdivision.
  auto holed = bump;
  holed[12].elevation_meters = std::nullopt;  // remove the apex cell
  holed.push_back(cell(40, 40, 5.0));
  const auto gapped = build_terrain_mesh(Grid(holed), 30);
  const auto gapped_fine = refine_terrain_mesh(gapped, 3);
  require(gapped_fine.indices.size() == gapped.indices.size() * 9, "only triangulated quads are refined");
  for (std::size_t t = 0; t < gapped_fine.indices.size(); t += 3)
    for (int k = 0; k < 3; ++k) {
      const auto& v = gapped_fine.vertices[gapped_fine.indices[t + k]];
      require(!(v.grid_x - gapped_fine.origin_grid_x == 6 && v.grid_y - gapped_fine.origin_grid_y == 6), "no geometry at the hole");
    }
  require(std::any_of(gapped_fine.vertices.begin(), gapped_fine.vertices.end(),
          [&](const auto& v) { return v.grid_x - gapped_fine.origin_grid_x == 120; }), "isolated vertices are kept");
  const auto same = refine_terrain_mesh(coarse, 1);
  require(same.vertices.size() == coarse.vertices.size() && same.indices == coarse.indices && same.refinement == 1, "one subdivision is the identity");
  // Budgets and choice of subdivision count.
  require(refinement_for_cell_size(coarse, 5) == 6 && refinement_for_cell_size(coarse, 30) == 1 &&
          refinement_for_cell_size(coarse, 7) == 5 && refinement_for_cell_size(coarse, 0.1) == 64 &&
          refinement_for_cell_size(coarse, 45) == 1, "subdivisions from the target spacing");
  require(refinement_for_cell_size(coarse, 1, 600) < refinement_for_cell_size(coarse, 1), "the vertex budget lowers the subdivision count");
  require(refine_terrain_mesh(coarse, refinement_for_cell_size(coarse, 1, 600), 600).vertices.size() <= 600, "the chosen count fits the budget");
  bool rejected = false;
  try { (void)refine_terrain_mesh(coarse, 6, 100); } catch (const std::length_error&) { rejected = true; }
  require(rejected, "over-budget refinement is rejected without publishing");
  for (const auto n : {0U, 65U}) {
    rejected = false;
    try { (void)refine_terrain_mesh(coarse, n); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "subdivision count outside 1..64 is rejected");
  }
  auto skewed = coarse;
  skewed.vertices[3].position.x += 1.0;
  rejected = false;
  try { (void)refine_terrain_mesh(skewed, 2); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "vertices off the lattice are rejected");
  rejected = false;
  try { (void)refinement_for_cell_size(coarse, 0); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, "zero target spacing is rejected");
}

}  // namespace
int main() {
  try {
    test_flat_quad_and_winding();
    test_sloped_normals();
    test_incomplete_quad_does_not_bridge();
    test_disconnected_grids();
    test_missing_elevation_and_filling();
    test_unmeasured_river_stays_hole();
    test_stable_under_input_permutation();
    test_local_origin();
    test_invalid_input_and_int_max();
    test_elevation_range_metadata();
    test_planar_normals_are_exact_everywhere();
    test_normals_are_diagonal_independent();
    test_normals_ignore_holes();
    test_slope_helper_edge_cases();
    test_surface_attributes_and_curvature();
    test_refinement();
    std::cout << "TerraFlux terrain mesh: 16 tests passed\n";
  } catch (const std::exception& e) {
    std::cerr << "TerraFlux terrain mesh test failed: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
