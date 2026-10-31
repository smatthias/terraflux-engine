# Terrain LOD design — first bounded increment

Status: **opt-in CPU simplification implemented; error measurement and GPU integration pending**.
Reviewed against the CPU tile partitioner and TerrainTileRenderer interfaces on
30 October 2026. Existing defaults and simulation behavior remain unchanged.

## Implemented level-0 foundation

`terraflux/terrain_lod.hpp` now exposes `TerrainTileLod`, `TerrainLodOptions`,
`TerrainLodSet` and `build_terrain_lods`. Level 0 owns the exact existing partition;
`level1` is empty by default; opt-in generation is described below.
`max_vertical_error` remains empty. No selector or GPU integration is claimed. Stable IDs are vector indices in tile (Y,X) order.

The default 512 MiB logical recipe cap includes used TerrainVertex elements,
indices and source-map entries. It excludes vector capacity, container overhead,
source storage, temporary builders and future packed GPU recipes. Tile count and
element caps still apply. Per-category bounds apply while partitioning; the
combined cap is checked before publication, not as a peak-memory limit.
Overflow-safe admission uses remaining-budget division before multiplication.

A new `terraflux-terrain-lod-tests` CTest target checks curved terrain with holes
and partial edges, exact source maps/bounds/packed geometry, owned copies,
permuted grid input, empty/exact/over budgets and invalid input rejection.
Remaining API descriptions below are design proposals for later increments.

## Implemented patch eligibility

`analyze_terrain_lod_patches(source, tile_options)` returns reports sorted by
stable tile index, patch Y and patch X. Patch coordinates are 4-quad blocks relative
to the tile origin. Only blocks with triangles produce records; empty space is
not enumerated. The function does not change geometry or populate level 1.

A complete block requires both canonical oriented triangles in all 16 quads.
Cyclic rotations and triangle ordering are accepted; duplicate triangles,
reversed winding, alternate diagonals or distinct source vertices at the same
grid coordinate are noncanonical. Horizontal X/Z coordinates must exactly match
the source-origin/cell-size construction used by build_terrain_mesh. This strict
check deliberately falls back for translated or perturbed custom input.

Status precedence is irregular coordinates, noncanonical topology, incomplete
coverage, then eligible. Partial tiles and holes remain incomplete. Malformed
partition input still throws; unsupported but valid topology yields fallback.
Grid arithmetic uses 64-bit intermediates. The existing tile count/element caps
apply; reports and temporary maps are diagnostic allocations outside the recipe
byte cap. At least one triangle is required per report, bounding report count by
the source triangle count. This is not a peak-memory limit.

Tests cover full/partial/one-quad tiles, holes and missing triangles, duplicate
vertices/triangles, winding, alternate diagonals, irregular coordinates, stable
report order, reordered/cyclic triangles and both signed coordinate extremes.

## Implemented opt-in level 1

Set `TerrainLodOptions::enable_level1=true` to generate compact coarse recipes.
Each eligible patch replaces 32 triangles with a 16-triangle center fan. All 16
perimeter vertices and the existing center retain source positions, normals and
grid identity. Unsupported patches retain their original triangles. A tile with
no eligible patch stores no level-1 allocation; callers use level 0.

Level 0 remains exact and defaults are unchanged. Both variants count toward
aggregate vertex/index and logical recipe-byte budgets; candidate rejection
publishes no partial result. The analyzer reuses the existing partition rather
than allocating a second partition. Simplification uses temporary grouping and
compaction maps, which are outside the retained payload budget.

Coarse bounds conservatively retain the source tile extent. Error measurement is
still absent, not zero. The viewer does not consume these variants yet. Tests
verify 32-to-16 reduction, boundary segmentation, winding/projected area, hole
fallback, adjacent-tile packed attributes, source mappings, reordered input,
coordinate extremes and exact/over budgets covering both levels.

## Scope and integration points

The current CPU partitioner preserves every source triangle and returns stable
coordinate-sorted tile IDs, source vertex mappings and global normals. The GPU
renderer retains one packed recipe per tile and stages replacements between passes.
LOD adds an optional recipe variant, not a new simulation mesh.

Implemented CPU generation supports two levels per tile:
level 0 is the existing exact mesh; level 1 simplifies eligible complete 4x4-quad
patches. No viewer flag is implemented by this design. Screen-space selection,
GPU integration and benchmark conclusions are subsequent increments.

## Geometry algorithm

Partition each tile's quad domain into nonoverlapping 4x4 patches aligned to the
tile's minimum grid coordinate. A patch is eligible only when all 16 source
quads have the expected two triangles from build_terrain_mesh, with consistent
winding, unique grid vertices and regular horizontal spacing. Check actual
source topology, not merely the presence of corner vertices.

For each eligible patch, retain all 16 perimeter vertices and its existing center
vertex at offset (2,2). Replace its 32 source triangles with a consistently wound
16-triangle fan from that center to consecutive perimeter vertices. Copy vertex
positions, normals, grid coordinates and source IDs verbatim. Compact unreferenced
vertices only after generating the complete variant.

Keep original triangles for any ineligible patch, including partial tile edges,
holes, absent elevations, noncanonical diagonals or unsupported geometry.
Do not create triangles over missing source coverage. An all-fallback tile may
alias level 0 instead of storing a duplicate recipe.

All patch boundaries retain every original grid vertex and segment. Thus a coarse
patch meets a fine patch, another coarse patch or a neighboring tile with the
same boundary polyline, including nonplanar edges. No skirts, T-junctions or
neighbor-level restrictions are needed for this two-level increment. This claim
must be checked structurally and after float32 packing, not inferred from screenshots.

For complete eligible patches the triangle count halves. Boundary-heavy or sparse
terrain may simplify little or not at all. This is not a performance promise.
The method cannot simply be recursively reapplied to its output: further levels
need a separate design.

## Proposed CPU contract

Add a separate terrain_lod API rather than changing tile_terrain_mesh semantics.
Names below are proposed, not available interfaces.

- TerrainTileLod: stable tile coordinates/ID, level-0 recipe, optional level-1
  recipe, source mappings, conservative source bounds and measured error.
- TerrainLodOptions: enable level 1, maximum aggregate vertices, indices and
  logical recipe bytes. All stored levels count toward these limits.
- build_terrain_lods: accept the source mesh plus tile options; validate source
  once, retain deterministic (tile Y, tile X, patch Y, patch X) processing.
- Expose source and simplified triangle counts and fallback-patch counts.
  Do not label an aliased level 0 as reduced geometry.

Source mesh, grid and simulation output must remain unchanged. Level 0 must match
the existing partition exactly. Do not send simplified triangles back through
tile_terrain_mesh: its one-quad triangle contract rejects coarse triangles.

## Error and bounds

Report the maximum absolute vertical difference between the original piecewise
linear surface and the proposed fan surface. Sampling source vertices alone is
insufficient: triangulations can cross between them.

For each eligible patch, overlay source and fan triangles in grid XY. Evaluate
the height difference at vertices of every overlap polygon: source vertices,
fan vertices and edge intersections. The difference is affine on each overlap
polygon, so its absolute maximum occurs at one of those vertices. Use double
precision, finite-result checks and documented numerical tolerance. Validate
the implementation against an independent dense sampling oracle in tests.

Bounds used for visibility retain the full-resolution source extent even when a
removed source vertex was a height extremum. Also include all packed candidate
vertices using the current conservative float32 AABB policy. Never shrink the
selection bound in a way that could cull a detailed surface prematurely.

## Holes, normals and wildfire identity

Copy global normals; do not recompute them per tile or per LOD. Interior shading
may change as interpolation changes, while shared-edge inputs remain identical.
Copy original origin/cell-size metadata. Preserve world XY/grid registration so
the wildfire shader uses the same dense field and cell lookup. LOD must not
aggregate fire cells, modify burned lists or alter simulation/replay timing.

Pixel equality with the full-resolution surface is required for level 0.
For level 1, geometry and shading can legitimately differ. Test no cracks,
correct world-coordinate fire lookup and bounded geometric error rather than
requiring whole-image equality for a changed surface.

## Budgets and transactional GPU integration

CPU budgets must include both levels, source maps and retained packed recipes;
document excluded allocator/container/transient memory. Check size arithmetic for
overflow before allocation. Reject a candidate without publishing partial output.
The source mesh remains retained, so this is not source streaming or an RSS cap.

In the later GPU increment, each stable tile ID has at most one committed level.
Use (tile ID, level) for recipe/resource identity; do not treat a level switch as
a new spatial tile. An unchanged pair reuses handles. Stage all required new
buffer pairs before publishing the new set or releasing previous resources.
A failure preserves old levels, handles, ages and committed selection. Global
byte admission includes old plus staged buffers; resident count limits count
spatial tiles. Hidden retention preserves its last committed level.

Draw only from the committed selection. If selection changes fail, the old
complete visible coverage can remain drawable only after verifying it covers
the current visible set; otherwise retain the existing explicit failure path.
Never silently omit newly visible terrain to fit a cap.

## Selection policy — later increment

Start integration with an explicit fixed-level override (0 or 1) for reproducible
tests. Automatic selection follows only after that path is validated.

Use a documented geometric-error threshold and camera parameters; account for
vertical field of view, viewport height and conservative nearest depth of the
source bounds. A bound crossing the near plane forces level 0. A projected-error
heuristic must not be advertised as a strict pixel-error bound without a proof
covering perspective and off-axis geometry. Add separate refine/coarsen thresholds
to prevent oscillation; commit selection history only on successful residency
transitions. Define units, threshold defaults and parameter validation in that
implementation change. This design does not yet freeze an automatic selector API.

## Acceptance matrix

| Area | Required evidence before marking implemented |
| --- | --- |
| Level 0 | Exact mesh, source map, winding and packed output parity with current tiles |
| Flat/planar patch | 32 to 16 triangles; zero error within declared numerical tolerance |
| Curved/saddle patch | Finite measured error; overlay result bounds dense oracle samples |
| Topology | Positive projected triangle area, no overlap/gaps, exact perimeter coverage |
| Mixed levels | Identical boundary segments/positions/normals before and after packing |
| Holes | Missing quad/triangle/elevation forces local fallback; no filled coverage |
| Partial/sparse tiles | Tiny, non-multiple-of-four and disconnected regions retain valid coverage |
| Extremes | Signed coordinate edges, steep heights, invalid/duplicate topology and overflow |
| Determinism | Stable geometry for equivalent supported source ordering; explicit mapping semantics |
| Limits | Empty/exact/over budgets across both levels; no partial candidate publication |
| Residency (later) | Handle reuse, level switch, staging failure, age preservation and cleanup |
| Rendering (later) | Fixed levels, mixed levels, direct/offscreen, layers and replay; level-0 exact reference |
| Performance (later) | Repeated hardware trials with triangles, draw counts, bytes and timing; no assumed speedup |

## Implementation sequence

1. Add CPU data structures, eligibility detection and exact level-0 preservation.
2. Generate boundary-preserving patch fans and compact source mappings.
3. Compute overlay-based error and conservative bounds.
4. Add topology, seam, hole, determinism and aggregate-budget tests.
5. Integrate fixed-level GPU recipes and transactional replacement.
6. Validate direct/offscreen and wildfire behavior before automatic selection.
7. Add camera selection with hysteresis and explicit diagnostics.
8. Measure actual hardware impact.

Items 1 and 2 are implemented. Error measurement and further geometric
invariants remain next; GPU integration is still pending. Shader modules, animation,
general asynchronous loading and PS4 remain separate workstreams.
