# Terrain tile geometry contract

`terraflux/terrain_tiles.hpp` provides `tile_terrain_mesh(source, options)` in the
compute library. This is a CPU geometry partition of the existing full-resolution
TerrainMesh. The viewer defaults to the original monolithic upload and offers
an optional tiled upload/frustum path described below. LOD, decimation, skirts and on-demand source meshing remain future work. Selective
GPU residency is described below.

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
monolithic path. LOD and on-demand source meshing still require additional seam
and transition work. Physical GPU performance remains unmeasured.


## Optional viewer tile uploads and culling

`--terrain-tiles` enables TerrainTileRenderer, using 32 quads per side by default.
`--terrain-tile-quads N` also enables tiled mode and selects 1..256 quads per side.
The CPU partition's default aggregate caps still apply: a very small tile size
can exceed the 4096-tile cap and reject startup. Source geometry and simulation
rules are unchanged. The default tiled mode uploads all tiles eagerly. The
additional --terrain-residency mode below selects GPU residents per frame. LOD
selection remains unimplemented.

TerrainTileRenderer owns one vertex/index buffer pair per nonempty tile through
RenderResourceOwner. Partial construction releases every buffer already created.
Successful draws share the existing terrain/wildfire pipeline, Camera uniform
and optional FireState SSBO. The wildfire lookup uses the same coordinates and
original dense fire-field mapping. Tile GPU buffers are independent of imported
asset lifetimes and included in the viewer's unload baseline and shared byte cap.
Temporary double-precision tile meshes are released after construction. Packed
TerrainUpload recipes are now retained in both tiled modes for rehydration; the
viewer also retains its full source mesh.

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
and plane-distance magnitude. In eager mode culling skips submissions while retaining allocations. It is
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

Selective GPU residency and budget tests are described below.
More tile draws can increase CPU/submission overhead; these correctness tests do
not demonstrate a speedup. Compare repeated benchmarks on actual GPU hardware
before making performance claims.


## Selective GPU tile residency

`--terrain-residency` implies tiled mode and creates TerrainTileRenderer with
initially_resident=false. It retains all packed CPU recipes but creates no tile
GPU buffers until the first visible-set synchronization. Before each terrain
pass the viewer calls synchronize_visible using the same frustum/culling policy
as draw. Hidden buffers expire according to the retention policy below (immediate
by default), common residents keep their handles, and
returning tiles are re-uploaded synchronously on the context thread. Eager tiled
mode remains available. No loading threads or simulation changes are introduced.

The public API also supports explicit set_resident_tiles(span<tile ID>),
resident_tiles, resident_tile_count, resident_bytes and a diagnostic
resident_vertex_buffer query. IDs are stable indices in the coordinate-sorted
tile list. Invalid or duplicate IDs reject. set_residency_limit controls the
committed set count; --max-resident-terrain-tiles accepts 0..4096 and enables
selective viewer mode. Lowering a limit preserves existing residents and prevents
future sets larger than the new limit. Geometry recipe count budgets remain
independent of the resident count limit.

Residency changes require ended render passes. A transition first validates the
requested set/count, then stages every missing buffer pair with per-tile RAII
owners. It publishes those residents and evicts unwanted old tiles only after
all candidates succeed. Failure frees staged buffers and preserves the previous
set, live handles and bytes. Peaks/admission history may still reflect candidates.
The global resource byte cap includes old plus staged storage. The count limit
applies to the final committed set, not transient staging. No automatic LRU,
early eviction to make room, or capacity-guaranteed streaming is implied.

Draw preflights every visible tile's residency before submitting any terrain
triangle. A missing visible tile rejects rather than rendering an incomplete
surface. The viewer reports visible-set admission failures through its existing
error handler and exits. An API caller can raise a cap, shrink a requested set or
release other resources and retry. Existing valid resident sets remain usable.
With culling disabled synchronization requests all tiles.

TERRAIN_JSON adds resident_tiles, resident_geometry_bytes, resident_tile_limit,
admitted and evicted. geometry_bytes still describes all retained packed recipes
and the fully resident geometry payload. Residents have two GPU buffers each;
shared camera/fire buffers are excluded from these per-terrain byte totals.
Diagnostics emit on residency changes when --residency-report is enabled, with
periodic/smoke records as before. BENCHMARK_JSON adds terrain_residency,
terrain_resident_tiles, terrain_resident_geometry_bytes, terrain_admitted_per_frame
and terrain_evicted_per_frame. Viewer asset-unload baselines account for the
current tile residents dynamically.

This trades GPU storage for CPU retention: every packed vertex/index recipe stays
available in both tiled modes. Recipes are bounded by partition element counts,
but are outside the imported-asset CPU recipe budget. Ordinary selective mode has
no monolithic GPU copy; smoke mode still retains that extra reference allocation.
No performance gain is established by these functional checks.

The five-frame --terrain-residency-test uses demo terrain, tiled residency smoke
mode, zero retention, culling and default budgets, without other scripted tests. It verifies a
smaller resident set in a close view, rejects readmission under a fixed byte cap,
checks preserved counts/bytes, raises the cap, recovers the original set with cold
handles, fully evicts, then rehydrates. Every rendered frame matches monolithic
pixels. CI runs direct/offscreen variants plus wildfire, mixed-scene lifecycle,
disabled culling and a zero resident-count rejection. Native tests additionally
exercise overlapping set handle reuse, duplicate/invalid IDs, lowered count caps,
partial multi-tile staging failure, fail-before-partial-draw behavior and cleanup.

Residency hysteresis is described below.
Physical GPU measurements, LOD, source-mesh streaming and CPU recipe eviction
remain outstanding.


## Bounded retention hysteresis

set_retention_frames accepts 0..600; --terrain-retain-frames selects the same
value and enables selective viewer mode. Zero is the default and preserves
immediate eviction. A visible tile has age zero. Each successful visibility
synchronization increments the age of hidden residents and may keep them while
age <= the configured window. A two-update window therefore retains hidden
updates one and two, then expires on update three. A return to visibility resets
age and reuses the same buffers. Retained hidden tiles are never added to draw
submission just because they remain allocated.

Visibility synchronization first computes the complete required set and candidate
ages. Recently hidden residents are optional. It prefers lower age (more recently
visible) and then ascending stable tile ID, adding optional residents only when
they fit the committed count limit and available final-set bytes. Available bytes
subtract other owners' current resources from the device's effective cap. Count
or byte pressure drops optional retention before required visible tiles. Expired
tiles are not counted as pressure trims. Required-visible count overflow still
rejects the transition.

The policy commits ages only after the transactional resident-set transition
succeeds. Validation or staged allocation failure preserves ages, residents and
handles, so retries do not silently consume retention time. An explicit
set_resident_tiles call bypasses hysteresis and resets age history after success;
it remains available for deterministic eviction and manual lifetime control.

Trimming retention is not early eviction for staging. Old + missing-new buffers
must still fit global byte admission before old resources are released. A failed
required upload therefore preserves the previous complete set even if a different
set could fit after destructive eviction. If a byte cap is lowered below already
resident required geometry, the existing admission contract retains that geometry;
optional hidden retention is dropped. This policy does not promise a hard physical
VRAM limit or guaranteed streaming under every cap.

Ages count successful synchronize_visible calls, not simulation ticks or elapsed
seconds. The viewer invokes it once per drawable frame; minimized frames and failed
synchronizations do not age residents. CPU recipes remain retained regardless of
GPU expiry. No asynchronous loading or GPU/CPU memory measurement is added.

TerrainResidencyChange and TERRAIN_JSON expose retained/retained_hidden (the number
of optional hidden residents kept) and trimmed/pressure_trimmed (recent hidden
candidates dropped for count/byte pressure), plus retention_frames. BENCHMARK_JSON
adds terrain_retention_frames, terrain_retained_hidden_per_frame and
terrain_pressure_trimmed_per_frame. Admission/eviction counters remain available
for direct comparisons; retaining tiles can increase steady GPU storage.

Native tests cover exact expiry boundaries, return handle reuse, zero-window
behavior, deterministic ties and recency preference, visible-first count limits,
byte trimming with another resource owner, failure-preserved ages and cleanup.
The six-frame --terrain-retention-test uses the demo, a two-update window, culling
and default budgets. It holds hidden tiles, returns without uploads, resets ages,
then waits for expiry and releases storage. Every rendered frame matches the
monolithic pixel reference. CI runs direct/offscreen variants and a mixed-scene
asset lifecycle with retention enabled. The earlier immediate-eviction test
requires a zero window.

Independent terrain/fire layer controls now skip surface submissions and explicitly
release selective tile buffers when neither layer needs the surface; returning
visibility rehydrates them. Layer-skipped tiles are reported separately from
frustum rejections. See [Terrain/fire layer visibility](GRAPHICS_BACKENDS.md#terrainfire-layer-visibility).
Fixed-tick replay speed controls now share these renderer paths. The baseline
checkpoint has been refreshed. The [first LOD design](TERRAIN_LOD.md) specifies
boundary-preserving patch fans and local fallback; implementation is pending. Physical GPU timing,
LOD, source streaming and CPU recipe eviction remain outstanding.
