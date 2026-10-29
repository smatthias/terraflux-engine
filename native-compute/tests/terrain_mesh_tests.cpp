#include <terraflux/terrain_mesh.hpp>

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
    std::cout << "TerraFlux terrain mesh: 9 tests passed\n";
  } catch (const std::exception& e) {
    std::cerr << "TerraFlux terrain mesh test failed: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
