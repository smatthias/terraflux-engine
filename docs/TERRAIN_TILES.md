# Terrain tile geometry contract

`terraflux/terrain_tiles.hpp` provides `tile_terrain_mesh(source, options)` in the
compute library. This is a CPU geometry partition of the existing full-resolution
TerrainMesh. The viewer defaults to the original monolithic upload and offers
an optional tiled upload/frustum path described below. LOD, selective GPU tile
residency, decimation, skirts and on-demand source meshing remain future work.

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
  // The optional viewer path creates bounded GPU resources from these uploads.
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

The optional viewer integration below adds rendered-output checks against the
monolithic path. LOD and on-demand residency still require additional seam,
transition and lifetime work. Physical GPU performance remains unmeasured.


## Optional viewer tile uploads and culling

`--terrain-tiles` enables TerrainTileRenderer, using 32 quads per side by default.
`--terrain-tile-quads N` also enables tiled mode and selects 1..256 quads per side.
The CPU partition's default aggregate caps still apply: a very small tile size
can exceed the 4096-tile cap and reject startup. Source geometry and simulation
rules are unchanged. All tiles are uploaded eagerly and remain GPU resident.
There is no residency eviction or LOD selection in this increment.

TerrainTileRenderer owns one vertex/index buffer pair per nonempty tile through
RenderResourceOwner. Partial construction releases every buffer already created.
Successful draws share the existing terrain/wildfire pipeline, Camera uniform
and optional FireState SSBO. The wildfire lookup uses the same coordinates and
original dense fire-field mapping. Tile GPU buffers are independent of imported
asset lifetimes and included in the viewer's unload baseline and shared byte cap.
Temporary CPU tile/upload arrays are released after construction; the viewer
still retains its full source mesh.

Normal tiled mode creates no monolithic GPU geometry buffers. Tiled smoke mode
additionally creates and renders the monolithic reference, so its allocation and
frame timings differ from ordinary/benchmark rendering. Each smoke frame compares
the complete terrain color image exactly before object submission. This exercises
the same active fire field and camera state for both paths. Pixel readback and
reference drawing are diagnostic-only work.

Tiles use AABBs of their packed float32 positions. Frustum::intersects_aabb tests
all six inward-facing planes using each plane's positive support vertex. Bounds
must be finite and ordered. Plane touches and a scale-aware float rounding margin
are retained conservatively; some invisible tiles may therefore still draw. The
margin is 64 float epsilons times the larger of the coordinate scale (at least 1)
and plane-distance magnitude. Culling skips submissions, not allocations. It is
frustum culling, not occlusion culling. `--no-culling` disables it for both tiles
and assets; the flag is accepted in smoke/profile/benchmark modes.

TerrainTileDrawStats reports total/visible/culled tile counts, draws and submitted
triangles. TERRAIN_JSON emits those counts plus tile geometry_bytes in smoke mode
and periodically during profiling. BENCHMARK_JSON adds terrain_tiled, terrain_tiles,
terrain_draws_per_frame, terrain_culled_per_frame and terrain_geometry_bytes.
Buffer bytes include copied boundary vertices. A fully hidden tile set draws zero
terrain triangles; the destination pass still performs its configured clear.

`--terrain-tile-test --smoke-test --terrain-tile-quads 8` runs three demo-terrain
camera views, including closer orbit views. It requires culling and rejects
imported assets, replay input or another scripted test. It compares monolithic
pixels for every view and requires at least one skipped tile draw. It can combine
with `--offscreen`. CI also tests tiled wildfire replay, mixed-format asset lifecycle
through offscreen composition, and disabled culling. The native tile suite checks
six-plane AABB rejection/touches, expected visible and fully hidden submissions,
byte accounting, cleanup and partial-construction budget rejection.

Next: selective tile GPU residency with explicit lifetimes and budget tests.
More tile draws can increase CPU/submission overhead; these correctness tests do
not demonstrate a speedup. Compare repeated benchmarks on actual GPU hardware
before making performance claims.
