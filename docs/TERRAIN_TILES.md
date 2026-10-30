# Terrain tile geometry contract

`terraflux/terrain_tiles.hpp` provides `tile_terrain_mesh(source, options)` in the
compute library. This is a CPU geometry partition of the existing full-resolution
TerrainMesh. The viewer currently uses the original monolithic upload. This
increment does not implement GPU tile residency, frustum submission, LOD,
decimation, skirts or on-demand source meshing.

## Geometry and ownership

Each source triangle must occupy three distinct corners of a single grid quad.
Its minimum grid coordinates identify the owning quad. Integer offsets from the
source grid origin, divided by `quads_per_side`, identify that quad's tile. The
result includes only nonempty tiles and sorts them by tile Y then X. Sparse or
disconnected surfaces do not allocate a rectangular array of intervening tiles.
No triangle is split, filled, decimated or duplicated. Original index winding and
relative triangle order within each tile are retained.

Each tile owns a compact TerrainMesh, a parallel `source_vertex_indices` map,
and inclusive `bounds_min`/`bounds_max` of its referenced geometry. Local indices
reference only that tile's vertices. Unreferenced source vertices are omitted.
Bounds describe actual triangle vertices, not a filled rectangular terrain patch.
Holes remain holes even when enclosed by a tile's conservative bounding box.

Positions, normals and grid-cell coordinates are copied verbatim from the source.
The origin and cell-size metadata also remain the same. Boundary vertices are
copied into adjacent tiles, with their globally computed normals unchanged;
recomputing normals separately at tile edges would create lighting seams. Tiles
therefore share the source's local coordinate system rather than being recentered.
The source map supports future state updates without losing simulation-grid
identity. The current wildfire shader can continue to derive its cell lookup
from these positions with the original origin/cell-size convention.

```cpp
const auto surface=terrain::build_terrain_mesh(grid,30.0);
terrain::TerrainTileOptions options;
options.quads_per_side=32;
options.max_tiles=1024;
const auto tiles=terrain::tile_terrain_mesh(surface,options);
for(const auto& tile:tiles) {
  const auto upload=graphics::prepare_terrain_upload(tile.mesh);
  // Future viewer integration can create bounded GPU resources from this upload.
}
```

## Limits and validation

`quads_per_side` must be 1..256 and defaults to 32. A valid regular tile has at
most `(side+1)^2` vertices and `6*side^2` indices. Those per-tile capacities are
also checked. Partial edge tiles contain only existing triangles.

Default aggregate limits are 4096 nonempty tiles, 4,000,000 copied vertices and
24,000,000 indices. Vertex counts include boundary copies; source vertex maps
have one entry per copied vertex. Callers can lower or raise aggregate limits.
Zero aggregate limits accept an empty draw payload and reject nonempty output.
Exceeding a count budget throws `std::length_error`; no partial result is published.

The function validates positive finite cell size, uint32-addressable source
vertices, triangle-aligned/in-range indices, finite positions/normals, grid
coordinates at or above the declared origin, and one-quad triangle footprints.
Invalid geometry/options throw `std::invalid_argument`. It expects the regular
surface topology produced by build_terrain_mesh; it is not a general arbitrary
triangle-mesh splitter. Relative tile coordinates use unsigned 32-bit values and
64-bit subtraction before division, including at signed grid-coordinate limits.

Budgets bound output element counts, not vector capacity, map overhead, CPU time
or process RSS. The complete source mesh remains required; partitioning does not
reduce initial source-mesh memory. Builders and source-to-local maps are temporary.
The Grid and source TerrainMesh are never modified. No simulation rules or
propagation outputs change.

## Validation and next integration

The `terraflux-terrain-tile-tests` display-independent CTest suite checks curved
surfaces, four-way seams, partial edges, one-quad tiles, maximum tile side,
sparse islands, missing cells/elevations, omitted isolated vertices, stable input
permutations, exact aggregate budgets, rejected candidates, malformed geometry,
and integer-coordinate edges. It reconstructs source triangles from local indices
and the source map, verifies exact multiplicity/winding, checks inclusive AABBs,
and compares packed GPU positions/normals with the original upload.

Next: optional viewer tile uploads and frustum culling, with rendered-output checks
against the monolithic path. LOD and bounded on-demand residency require additional
seam, transition and lifetime work. Physical GPU performance remains unmeasured.
