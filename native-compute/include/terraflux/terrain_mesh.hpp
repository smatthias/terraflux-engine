#pragma once

#include <terraflux/grid.hpp>
#include <terraflux/math.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraflux::terrain {

// GPU-backend-independent triangulated surface. Coordinates use a local origin:
// +X east, +Y elevation (meters), -Z north. Grid cell indices remain available
// for associating a vertex with a simulation's wildfire state.
struct TerrainVertex {
  math::Vec3 position;
  math::Vec3 normal;
  std::int32_t grid_x = 0, grid_y = 0;
  // Surface attributes copied from the simulation cell for material shading:
  // fuel_load (0 bare .. 1 dense vegetation) and fuel_moisture (0 dry .. 1 wet).
  double fuel_load = 0.0, fuel_moisture = 0.0;
  // Convexity: minus the discrete Laplacian of elevation in slope units per
  // cell (tan change per cell). Positive on crests and ridges, negative in
  // basins and gullies, zero on planes and where no axis has both neighbours.
  double curvature = 0.0;
};

struct TerrainMesh {
  std::vector<TerrainVertex> vertices;
  std::vector<std::uint32_t> indices;  // CCW triangles when viewed from above
  std::int32_t origin_grid_x = 0, origin_grid_y = 0;
  double cell_size_meters = 30.0;
  // Inclusive elevation range over all vertices, including vertices that belong
  // to no triangle. Both are zero for a mesh without vertices. Renderers map
  // height colors onto this range so the highest and lowest terrain are always
  // distinguishable, whatever the absolute elevations of the data set.
  double min_elevation_meters = 0.0, max_elevation_meters = 0.0;
  // Render quads per simulation cell along each axis (see refine_terrain_mesh).
  // 1 means vertices are simulation cells; otherwise grid_x/grid_y index the
  // finer render lattice and cell_size_meters is the render spacing. The
  // simulation cell of a vertex is origin + (grid - origin) / refinement.
  std::uint32_t refinement = 1;
  double simulation_cell_size_meters = 30.0;
};

// Tessellate each complete 2x2 block of grid cells into two triangles.
// Cells with unavailable elevations have no vertex; missing cells and voids
// prevent the corresponding quad from being triangulated. No mesh triangles
// may bridge gaps. Pre-fill elevations with compute::fill_elevation_voids when
// interpolation is desired; measured elevations are never changed here.
// Vertex/triangle order is coordinate-stable regardless of original cell order.
//
// Vertex normals are smooth shading normals: every complete quad touching a
// vertex contributes the area-weighted face normals of both of its possible
// diagonal splits. The result is therefore independent of the diagonal that is
// actually triangulated, exact for planar terrain, symmetric under grid
// rotation/mirroring, and never influenced by holes or missing neighbours.
// Vertices touching no complete quad use the +Y up normal.
//
// Curvature uses the east/west and north/south neighbours that have
// elevations; an axis missing either neighbour is skipped, so holes and
// borders never invent relief.
//
// This is geometry only: it does not affect fire propagation or create a
// rendering context, GPU resources, or proprietary platform dependencies.
TerrainMesh build_terrain_mesh(const compute::Grid& grid, double cell_size_meters);

// Render-resolution refinement of a simulation-resolution mesh. Every complete
// simulation quad becomes `subdivisions` x `subdivisions` render quads whose
// heights follow a C1 bicubic Hermite surface (per-vertex slopes from central
// or one-sided differences over the measured neighbours, zero twist) that
// passes exactly through the measured vertices; normals are the analytic
// surface normals, fuel/moisture/curvature interpolate bilinearly, holes stay
// holes and nothing extends beyond the measured surface. The result keeps the
// source origin, records `refinement` and the simulation spacing, and uses
// cell_size_meters / subdivisions as its own spacing. Simulation data, the
// source mesh and fire lookups (plan positions) are unchanged. subdivisions 1
// returns a copy of the source. Rejects 0 or more than 64 subdivisions, a
// non-grid source (vertex not at its grid position) and results beyond
// max_vertices (length_error, nothing published).
TerrainMesh refine_terrain_mesh(const TerrainMesh& source, std::uint32_t subdivisions,
                                std::size_t max_vertices = 4'000'000);
// Largest subdivision count (1..64) whose render spacing is at most
// target_cell_meters, reduced until the refined vertex estimate fits
// max_vertices. Returns 1 when the target is not finer than the source.
std::uint32_t refinement_for_cell_size(const TerrainMesh& source, double target_cell_meters,
                                       std::size_t max_vertices = 4'000'000);

// Surface inclination in degrees from horizontal, 0 (flat) .. 90 (vertical),
// derived from a vertex's shading normal. The normal need not be unit length;
// a zero or non-finite normal reports 0. Downward-facing normals are treated
// as their upward mirror image, so the result never exceeds 90 degrees.
double vertex_slope_degrees(const TerrainVertex& vertex);

// Read-only description of a built mesh for logs, tests and tools. Slope
// statistics consider only vertices referenced by at least one triangle;
// isolated vertices do not represent a rendered surface.
struct TerrainMeshSummary {
  std::size_t vertex_count = 0, triangle_count = 0;
  double min_elevation_meters = 0.0, max_elevation_meters = 0.0;
  double max_slope_degrees = 0.0, mean_slope_degrees = 0.0;
  double max_curvature = 0.0, min_curvature = 0.0;   // strongest crest / basin
  std::size_t vegetated_vertices = 0;                // fuel_load > 0 among surface vertices
};
TerrainMeshSummary summarize_terrain_mesh(const TerrainMesh& mesh);

}  // namespace terraflux::terrain
