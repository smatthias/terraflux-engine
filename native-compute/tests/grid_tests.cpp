#include <terraflux/grid.hpp>

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using terraflux::compute::Grid;
using terraflux::compute::GridCell;
using terraflux::compute::fill_elevation_voids;
using terraflux::compute::kAbsent;

namespace {

void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
GridCell cell(int x, int y, double fuel, std::optional<double> elevation = std::nullopt) {
  GridCell result{};
  result.x = x;
  result.y = y;
  result.fuel_load = fuel;
  result.fuel_moisture = 0.25;
  result.burn_susceptibility = 1.0;
  result.elevation_meters = elevation;
  return result;
}

void test_dense_coordinates() {
  Grid grid({cell(-1, -1, 1), cell(0, -1, 1), cell(-1, 0, 1), cell(0, 0, 1)});
  require(grid.dense, "contiguous grid must use dense indexing");
  require(grid.find(-1, -1) == 0 && grid.find(0, -1) == 1, "dense coordinates must retain input order");
  require(grid.find(-1, 0) == 2 && grid.find(0, 0) == 3, "dense 2D indexing incorrect");
  require(grid.find(0, 1) == kAbsent && grid.find(-2, -1) == kAbsent, "dense bounds incorrect");
}

void test_sparse_coordinates() {
  Grid grid({cell(-1000, 0, 1), cell(1000, 1000, 1), cell(0, -1000, 1)});
  require(!grid.dense, "widely separated grid must use sparse indexing");
  require(grid.find(-1000, 0) == 0 && grid.find(1000, 1000) == 1, "sparse lookup incorrect");
  require(grid.find(0, -1000) == 2 && grid.find(1, 1) == kAbsent, "sparse missing cell incorrect");
}

void test_duplicate_coordinates() {
  bool threw = false;
  try { Grid grid({cell(5, 3, 1), cell(5, 3, 1)}); }
  catch (const std::runtime_error&) { threw = true; }
  require(threw, "duplicate coordinates must be rejected");
}

void test_elevation_interpolation() {
  Grid grid({cell(0, 0, 1, 100.0), cell(1, 0, 1), cell(2, 0, 1, 120.0)});
  require(fill_elevation_voids(grid) == 1, "one gap must be filled");
  require(grid.cells[1].elevation_meters.has_value(), "interpolated elevation missing");
  require(std::abs(*grid.cells[1].elevation_meters - 110.0) < 1e-12, "linear terrain interpolation changed");
  require(*grid.cells[0].elevation_meters == 100.0 && *grid.cells[2].elevation_meters == 120.0,
          "measured elevation must remain unchanged");
}

void test_river_breaks_interpolation() {
  Grid grid({cell(0, 0, 1, 100.0), cell(1, 0, 0), cell(2, 0, 1),
             cell(3, 0, 1, 30.0)});
  require(fill_elevation_voids(grid) == 1, "river-side gap must be filled");
  require(!grid.cells[1].elevation_meters.has_value(), "unmeasured zero-fuel river must not be filled");
  require(std::abs(*grid.cells[2].elevation_meters - 30.0) < 1e-12,
          "elevation lookup must not cross unmeasured river");
}

void test_missing_elevation_fallback() {
  Grid grid({cell(0, 0, 1), cell(1, 0, 1)});
  require(fill_elevation_voids(grid) == 0, "all-void grid cannot be filled");
  require(!grid.cells[0].elevation_meters && !grid.cells[1].elevation_meters,
          "unrecoverable terrain must remain missing");
}

void test_sparse_elevation() {
  Grid grid({cell(1, 5, 1, 10.0), cell(2, 5, 1), cell(3, 5, 1, 30.0),
             cell(900, 900, 1, 999.0)});
  require(!grid.dense, "test must exercise sparse path");
  require(fill_elevation_voids(grid) == 1, "sparse grid gap must be filled");
  require(std::abs(*grid.cells[1].elevation_meters - 20.0) < 1e-12,
          "sparse and dense interpolation must agree");
}

}  // namespace

int main() {
  try {
    test_dense_coordinates();
    test_sparse_coordinates();
    test_duplicate_coordinates();
    test_elevation_interpolation();
    test_river_breaks_interpolation();
    test_missing_elevation_fallback();
    test_sparse_elevation();
    std::cout << "TerraFlux grid and elevation core: 7 tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "TerraFlux grid test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
