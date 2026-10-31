# TerraFlux Engine

An open-source spatial simulation engine for dynamic environments,
built with C++20 and Python.

TerraFlux provides a deterministic grid-based simulation foundation
for environmental modeling, spatial computation, and future digital
twin applications.

## Verified engine checkpoint

The [desktop baseline checkpoint](docs/ENGINE_BASELINE.md) pins implementation
revision `9196da2632abb408d93d7842453d9dc1a8fc48df`, records its validation,
provides an isolated-worktree reproduction guide and lists remaining renderer
gaps in priority order. It is a development baseline, not a completed roadmap
or physical-GPU certification.

## Current capabilities

- Native C++20 grid-based wildfire spread simulation
- Python reference implementation
- Versioned simulation contracts
- Deterministic simulation fixtures
- C++/Python parity verification
- Optional OpenGL 4.3 desktop terrain and wildfire-replay viewer
- Smooth terrain materials: relative height tints, dry/semi-dry/wet grass and forest from fuel data,
  rock on steep slopes, scree on exposed ridges, snow and ice, hillshade, contours and procedural detail
- 3D vegetation: instanced procedural or custom-OBJ trees and grass tufts placed from the fuel data,
  with per-plant size/color variation, wind sway and run-time switchable meshes and densities
- Animated 3D fire: fuel-gated flames and smoke on burning cells, crown fires in trees, charred
  vegetation and ground after the front passes; rock, water, snow and bare soil never show fire
- Animated water at a configurable level: depth-dependent color and transparency, Fresnel sky/sun
  reflection, waves with configurable height, length, speed and direction that shoal, refract toward
  the shore and break into foam on beaches and rocky shores
- OBJ/MTL and static glTF/GLB assets, textures and a direct-light PBR subset
- Scene hierarchies, shared OBJ/glTF/GLB asset references and deterministic Spin/Motion
- Scene pause/resume, single stepping and transactional asset unload/reload
- Reusable CPU material/path/image resolution and sampler-aware texture keys
- Reference-counted GPU texture leases; final material release evicts the allocation
- Headless scene residency API with callback-driven resource preparation and eviction
- Frustum culling, opaque hardware instancing, CPU/GPU timing and benchmarks

## Requirements

- C++20-compatible compiler
- CMake 3.20 or newer
- Python 3.10 or newer
- Pydantic 2.x
- libpng and libjpeg development libraries (native builds)
- Optional viewer: GLFW 3.3+, OpenGL 4.3 driver and a display server

## Build the native engine

```bash
cmake -S native-compute -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is located at `build/terraflux-compute-runner`.

## Desktop 3D viewer

On Ubuntu, install the native image and optional graphics dependencies:

```bash
sudo apt-get install cmake g++ libpng-dev libjpeg-dev libglfw3-dev libgl-dev
cmake -S native-compute -B build-gl -DCMAKE_BUILD_TYPE=Release \
  -DTERRAFLUX_WITH_OPENGL=ON
cmake --build build-gl --parallel 2
ctest --test-dir build-gl --output-on-failure
```

Launch terrain alone, a shared multi-model OBJ scene, or a glTF material preview:

```bash
./build-gl/terraflux-terrain-viewer
./build-gl/terraflux-terrain-viewer --scene examples/asset_scene.json
./build-gl/terraflux-terrain-viewer --scene examples/mixed_asset_scene.json
./build-gl/terraflux-terrain-viewer \
  --gltf examples/assets/tangent_blend.gltf --asset-preview
```

Version 2 scene JSON declares named OBJ, glTF or GLB paths relative to the
scene file. Canonical path aliases share model geometry, materials and images.
A referenced glTF/GLB expands its active authored hierarchy beneath the scene
node transform; Spin/Motion therefore move the complete referenced model.
Mixed scenes use the direct-light PBR subset, with diffuse OBJ materials
approximated as nonmetallic. Pure OBJ scenes retain the original diffuse path. Legacy version 1 scenes require their primary OBJ:

```bash
./build-gl/terraflux-terrain-viewer \
  --obj examples/assets/pyramid.obj --scene examples/scenes/twin_pyramids.json
```

### Render resolution: a 5 m lattice over the 30 m simulation grid

The simulation grid (typically 30 m cells) is never changed, but rendering does
not have to show its squares. `--render-cell METERS` (default 5; `0` keeps the
simulation lattice) refines every complete simulation quad into N x N render
quads (`refine_terrain_mesh`): heights follow a C1 bicubic Hermite surface that
passes exactly through the measured vertices (per-vertex slopes from the
measured neighbours, zero twist), normals are the analytic surface normals,
fuel/moisture/curvature interpolate bilinearly, holes stay holes and nothing is
invented beyond the measured surface. Water, tiles, residency, culling and the
material shaders all work on the refined lattice; rock relief therefore becomes
real geometry at 5 m instead of shading. `--max-render-vertices` (default
4 000 000, 1000..20 000 000) lowers the subdivision automatically for large
grids (a 1 000 x 1 000 DEM renders at 15 m, a 160 x 160 DEM at 5 m). Tile sizes
given with `--terrain-tile-quads` count simulation cells and are scaled to the
render lattice (capped at 256 render quads per side). `TERRAIN_MESH_JSON`
reports both the simulation and render counts, the render spacing and the
refinement factor. The simulation's fire grid, parity fixtures and outputs are
untouched; render vertices map back to their simulation cell through
`origin + (grid - origin) / refinement`.

```bash
./build-gl/terraflux-terrain-viewer --render-cell 2.5
./build-gl/terraflux-terrain-viewer --render-cell 0          # the raw simulation lattice
./build-gl/terraflux-terrain-viewer --request terrain-request.json \
  --response terrain-response.json --hide-fire --render-cell 5 --max-render-vertices 2000000
```

### Terrain materials: height, slope, vegetation, rock, snow and ice

The terrain and wildfire ground shaders build materials from the mesh itself
and the simulation's per-cell fuel data:

- **Height**: a five-stop hypsometric ramp (lowland green, meadow, dry grass,
  upland brown, pale summit) spans the surface's own elevation range, recorded
  by `build_terrain_mesh`; bare ground (no fuel) shows it directly.
- **Vegetation**: `fuel_load` blends bare ground to grass and grass to forest
  canopy, which thins out above the tree line (62..78 % of the range) and on
  slopes steeper than 30..42 degrees. `fuel_moisture` distinguishes **dry**
  (straw, below 0.10), **semi-dry** (olive, 0.20..0.26) and **wet** (lush green,
  above 0.40) grass and canopy, blending smoothly in between.
- **Rock and ridges**: per-pixel slope blends soil toward rock between 28 and
  48 degrees; convex crests computed from the elevation grid become scree when
  gentle and rock when steep. Rock is **flat-shaded** from the geometric facet
  normal, carved by ridged crag noise with sharp creases and crevice shadow,
  and layered by hard strata ledges; `--rock-relief 0..20` m (default 5)
  additionally displaces rock vertices vertically by the same ridged field so
  cliffs and crests get jagged, blocky silhouettes instead of smooth slopes.
  The displacement is visualization only: measured elevations, contours, fire
  lookups and the CPU reference are unaffected, it is scaled by
  `--terrain-detail`, damped under snow and ice, and suppressed below and just
  above the water line. Elsewhere vertex normals stay smooth and independent
  of the triangulated diagonal, so grass and soil show no grid grain.
- **Snow and ice**: once the relief reaches 300 m, snow covers ground above 72 %
  of the range (except cliffs and wind-blown crests), and flat high basins and
  summit caps turn to ice. `--snow-line 0..1` sets the fraction (1 disables).
- **Detail**: procedural grass tufts and patches, tree crowns grouped into
  stands with gaps, rock strata, grain and cracks, scree speckle, snow drifts
  and sparkle, ice crack lines and a gradient-based bump, all fading with
  distance. `--terrain-detail 0..1` scales it (0 shows the plain materials).
- **Contours**: anti-aliased contour lines default to a 1-2-2.5-5 spacing giving
  about ten levels; `--terrain-contours 50` sets the interval and `0` disables.
- A directional hillshade (sun from the south-west, 50 degrees up, 30 % ambient)
  lights all of it; snow and ice take a bluish tint on their shaded side.

```bash
./build-gl/terraflux-terrain-viewer
./build-gl/terraflux-terrain-viewer --terrain-contours 0 --terrain-detail 0
./build-gl/terraflux-terrain-viewer --request terrain-request.json \
  --response terrain-response.json --hide-fire --snow-line 0.8 \
  --screenshot /tmp/terrain.ppm
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --terrain-shading-test --smoke-test
```

`--screenshot PATH` writes the last presented frame as a binary PPM when the
viewer exits (the single frame in smoke mode). The camera starts where the
terrain's bounding sphere fills the vertical field of view. Smoke, profile,
benchmark and residency-report runs print `TERRAIN_MESH_JSON` with vertex and
triangle counts, the elevation range, maximum/mean slope, curvature extremes,
vegetated vertices and the active style. The demo terrain is authored with a wet
riparian strip, semi-dry lowland, dry plateau, irregular forest stands, a lake, a
two-band escarpment with craggy faces and a scree crest, and a snow-capped ridge
broken by ridged crags, so every class is visible.

The style lives in `terraflux/graphics/terrain_shading.hpp` as a validated
64-byte `TerrainStyle` uniform block shared by both ground shaders, with a CPU
reference of the material classification (`terrain_material`,
`dominant_material`) and lit color (`terrain_surface_color`) for detail
strength zero. `--terrain-shading-test` renders the plain materials and compares
the GPU pixel at every interior, unburned vertex against that reference per
material class, checks the height ramp, hillshade range and visible rock, then
toggles contours and full detail and checks their pixel statistics. It requires
demo or replay smoke mode with both layers visible, default style options and no
assets; tiled, resident and offscreen paths are allowed. Fire/burn overlays,
layer visibility, fire-grid identity and replay timing are unchanged, and no
material affects fire propagation. See
[the backend contract](docs/GRAPHICS_BACKENDS.md#terrain-materials-height-slope-vegetation-rock-snow-and-ice).

### 3D vegetation: trees and grass as run-time configurable meshes

Trees and grass are no longer painted onto the ground: they are instanced 3D
meshes placed from the material model (`place_vegetation`) and drawn by the
`Vegetation` shader. Each plant has its own position on the rendered surface,
height (0.7..1.3 x the nominal height, taller on wet ground), rotation, sway
phase, lean on steep ground and **color tint** (species by moisture class — dry
ground grows olive conifers, wet ground lush broadleaf trees — plus per-plant
brightness/hue jitter), so no two trees in a forest look the same. The ground
under 3D trees shows forest-floor litter instead of a second painted canopy.

- **Meshes** (`vegetation.hpp`): procedural unit-height models with vertex
  colors and a wind "flex" weight — `make_tree_mesh` (conifer tiers, lobed
  broadleaf crowns or shrubs; segments, layers, trunk height, crown width and
  colors are parameters) and `make_grass_mesh` (1..8 tapered blades) — or any
  OBJ converted by `vegetation_mesh_from_model` (normalized to unit height,
  section materials become vertex colors, green materials take the species tint).
- **Placement**: seeded jittered-grid sampling over the render lattice with an
  exact surface lookup (`TerrainSampler`), trees where the canopy cover, tree
  line, slope, snow and water allow, grass where vegetation grows outside dense
  canopy; instances are grouped into 120 m chunks for frustum and distance
  culling and refilled into instance storage each frame (trees to 6 km, grass
  to its draw distance, shrinking away instead of popping).
- **Wind**: the wave direction/speed drive a gusting sway that moves tips more
  than trunks; `--wave-direction`/`--wave-speed` therefore also steer the trees.

```bash
./build-gl/terraflux-terrain-viewer                               # medium density, mixed species
./build-gl/terraflux-terrain-viewer --vegetation high --tree-style conifer --grass-distance 600
./build-gl/terraflux-terrain-viewer --tree-style custom --tree-obj my_tree.obj --tree-height 20
./build-gl/terraflux-terrain-viewer --vegetation off
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --vegetation-test --smoke-test
```

`--vegetation off|low|medium|high` sets density presets (tree spacing 18/12/8 m,
grass off/300 m/450 m); `--tree-spacing 4..200`, `--grass-spacing 0.5..20`,
`--grass-distance 0..5000`, `--tree-height 1..80` override them;
`--tree-style conifer|broadleaf|mixed|custom` picks the meshes (`custom` needs
`--tree-obj`); `--vegetation-seed` changes the layout. At run time **V** cycles
the density, **B** cycles the tree meshes (conifer, broadleaf, mixed, custom),
**G** cycles the grass distance, and **W/A/S/D** pan the camera target over the
ground (it follows the terrain, so you can walk among the trees; zoom now goes
down to 3 % of the framing distance). A rejected change keeps the previous
state. `VEGETATION_JSON` reports counts, kinds, chunks, spacing and bytes;
`VEGETATION_DRAW_JSON` the per-frame visible chunks, instances, draws and
triangles. `--vegetation-test` renders vegetation off, on, with swapped tree
meshes, 1.5 s later and off again, and checks that plants change a substantial
share of the image, rise above the terrain silhouette, show at least two dozen
distinct colors, differ after the mesh swap and after the wind step, and that
disabling restores the reference exactly. Vegetation is a visualization of the
fuel inputs and does not affect fire propagation; smoke runs freeze the sway.

### Animated fire that respects fuel and fire breaks

The simulation alone decides which cells ignite and burn (fuel-free cells never
do; see the contract below). The viewer now turns that into a scene-aware
visualisation instead of a flat square per cell:

- **Flames** are instanced, camera-facing, noise-eroded tongues that flicker and
  lean with the wind. Each burning cell gets up to `flames_per_cell x fuel`
  ground flames at seeded sub-cell positions — and only where the sampled
  ground actually carries fuel: the `fire_fuel_factor` gate (fuel above the
  threshold and neither rock, water, snow nor ice) rejects rock outcrops,
  shorelines, snow and bare soil inside a burning cell. Flame height and count
  scale with the fuel load, so thin grass smoulders low while forest burns high.
- **Crown fires**: every 3D tree standing in a burning cell carries a flame in its
  canopy, and the plants themselves change state through the same fire field the
  ground uses — burning foliage glows and flickers orange, burned trees are left
  charred, burned grass is reduced to stubble.
- **Smoke**: plumes rise from burning cells (more and denser with more fuel),
  drift downwind and are lit warm at their base; burned cells keep sparse,
  thin smouldering plumes.
- **Ragged fronts**: flames, charring and the ground footprint all follow one
  continuous front field — the cell states blurred over a 5 x 5 neighbourhood
  and cut by world-space noise at three scales (broad bulges, lobes and a fine
  fringe) — instead of cell membership, so the outline wanders up to about a
  cell past the squares, leaves occasional unburned islands, and never appears
  where no burning or burned cell is nearby. The burn footprint is gated by
  the same fuel/material rule and fainter where fuel is thin, so fire breaks
  such as rivers (zero fuel), rock and water stay visibly unburned.
  Note that the large-scale shape still comes from the simulation: the
  8-neighbour threshold automaton spreads at equal cell counts along axes and
  diagonals (a Chebyshev metric), so in uniform fuel without wind or slope it
  grows a square; the renderer can soften that, not remove it. Real fuel,
  moisture, slope and wind variation break it up, and a distance-corrected or
  stochastic spread rule would be a simulation (v3 contract) change.

```bash
./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json \
  --response /tmp/terraflux-response.json --flame-scale 1.5 --smoke-scale 0.8
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --fire-test --smoke-test \
  --request /tmp/terraflux-request.json --response /tmp/terraflux-response.json
```

`--flame-scale 0..3` and `--smoke-scale 0..3` size the effects; the **F** layer
key hides them together with the ground fire. Emitters are rebuilt when the
replay frame or the vegetation changes (`FIRE_EFFECTS_JSON` reports burning and
burned cells, flames, crown flames, plumes and samples rejected for lack of
burnable ground; `FIRE_DRAW_JSON` the per-frame instances). `--fire-test`
renders the ground fire alone, then the effects at two times, then hides them,
and requires flames and smoke to be drawn, pixels to change well above the
ground footprint, the effects to animate and the reference to be restored
exactly. None of this feeds back into propagation or replay timing; smoke runs
freeze the animation clock.

### Water: level, depth, transparency, reflection and waves

`--water-level METERS` places a still-water surface over the terrain (the demo
terrain has an authored lake at 34 m; replay data gets water only on request).
`build_water_mesh` clips the plane to every terrain triangle with a submerged
corner and carries the bed height and bed gradient with each vertex, so the
shader knows the exact depth beneath every pixel and which way the shore lies.
Holes in the terrain stay holes; nothing is invented beyond the measured surface.

- **Depth and transparency**: the color absorbs from turquoise shallows to deep
  blue (63 % at 6 m) and the opacity rises with depth (63 % at 2.5 m), so beaches,
  contour lines and vegetation remain visible through shallow water.
- **Reflection**: a Fresnel-weighted procedural sky (horizon to zenith) plus sun
  glitter from the same light as the terrain; grazing views turn opaque and sky
  colored. There is no planar reflection of the terrain or assets.
- **Waves**: `--wave-height 0..5` (amplitude, m), `--wave-length 1..1000` (m),
  `--wave-speed 0..50` (deep-water phase speed, m/s; 0 freezes) and
  `--wave-direction 0..360` (compass degrees the waves travel toward, 0 north,
  90 east). The primary wave is joined by a long irregular swell, two cross
  waves and ripples; gusts modulate the amplitude and a slow warp bends the
  fronts. Over shallow ground waves slow, shorten and grow (shoaling), bend
  toward rising ground (refraction) and are capped by the depth they travel in.
- **Foam and breaking**: waves whose height the depth can no longer carry break
  into foam at their crests; a surging swash line runs up the beach with each
  arriving crest; rocky (steep) shores throw animated splashes. Flat water
  (height 0) has no foam.
- **Lake/sea floor**: ground below the water line renders as floor instead of
  land, visible through shallow water (`--water-clarity 0.5..50` sets the depth
  at which the surface becomes 63 % opaque; default 4 m). `--water-floor` selects
  `sand`, `vegetation` (aquatic plants), `mud`, `stones`, `mixed` (default) or
  `random`. The mixed floor is zoned by depth and bed slope — sand on gentle beds
  down to about 2 m, plants in the sunlit 1.5–6 m band, mud deeper and flat,
  stones on steep beds and in the surf — and broken into seeded patches
  (`--floor-seed N`, `--floor-patch-size 5..500` m). `random` derives the kind
  weights and patch size from a seed (random unless `--floor-seed` is given) and
  prints it in `WATER_FLOOR_JSON`, so a floor you like can be reproduced. Detail
  adds sand grain and ripple marks, plant clumps, mud mottling and pebbles with
  dark gaps.
- A long swell also lifts the coarse surface mesh itself, dying out toward the
  shore so water never climbs onto land. Water belongs to the terrain layer
  (hidden with T) and does not affect fire propagation or replay timing.

```bash
./build-gl/terraflux-terrain-viewer --wave-height 1.2 --wave-direction 300
./build-gl/terraflux-terrain-viewer --water-floor random --water-clarity 8
./build-gl/terraflux-terrain-viewer --water-floor stones --floor-seed 7 --floor-patch-size 20
./build-gl/terraflux-terrain-viewer --request terrain-request.json \
  --response terrain-response.json --hide-fire --water-level 80 --wave-length 40
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --water-test --smoke-test
```

Smoke, profile, benchmark and residency-report runs print `WATER_JSON` (level,
triangles, submerged vertices, maximum depth and wave settings). The style lives
in `terraflux/graphics/water_shading.hpp` as a validated 128-byte `WaterStyle`
uniform block with CPU references for the shallow-water phase-speed ratio,
refraction toward the shore, shoaling/breaking limits and the flat-water color
and opacity. `--water-test` renders flat water and compares the blend over the
ground at every visible surface vertex deeper than 0.3 m against that reference,
then checks that waves change the surface and produce foam at time zero, animate
0.7 s later, never touch land, and that flat water restores the reference
exactly. It requires demo or replay smoke mode (replay with `--water-level`),
moving waves, both layers visible and no assets or other scripted test; tiled,
resident and offscreen paths are allowed. Smoke runs freeze the water clock so
frame comparisons stay deterministic. See
[the backend contract](docs/GRAPHICS_BACKENDS.md#water-surface).

### Viewer controls

| Key | Action |
| --- | --- |
| Arrow keys / + / - | Orbit camera / zoom |
| P | Pause/resume OBJ demo or scene Spin/Motion; camera and fire replay continue |
| N | Advance one 1/60-second scene tick and remain paused |
| [ / ] | Select the previous / next scene binding, including inactive nodes |
| D | Detach the selected scene node and evict its unused mesh resources |
| A | Activate the selected inactive node if residency budgets permit |
| U | Unload the imported asset/scene; retain terrain and wildfire playback |
| R | Reload the original asset/scene; failed loads preserve the current asset |
| Space | Pause/resume wildfire replay |
| Home | Reset wildfire replay to its first frame and pause |
| Comma / period | Previous / next wildfire frame, pausing replay |
| V / B / G | Cycle vegetation density / tree meshes / grass distance |
| W / A / S / D | Pan the camera target over the ground (A/D only without a scene file) |
| Escape | Exit |

P/N are edge-triggered: holding a key does not repeat it. Paused wall time
never accumulates; N discards a fractional pending tick. Successful reload
resets scene animation to authored transforms and resumes its clock. Reload
is synchronous and temporarily holds the old and new GPU scopes together.
Static glTF hierarchies have no animation for P/N to advance.

### Residency diagnostics and budgets

```bash
./build-gl/terraflux-terrain-viewer --scene examples/mixed_asset_scene.json \
  --residency-report --max-resident-meshes 7 --max-scene-instances 17
```

`RESIDENCY_JSON` reports live draw instances, distinct referenced meshes,
detached nodes/saved instances, configured limits and live OpenGL API resource
counts (including terrain). Reports appear at initial admission and detach/
reattach/reload; `--profile` also reports every 120 frames. Limits default to
512 meshes and 1024 instances; zero intentionally rejects nonempty residency.
These budgets apply to scene-file GPU residency, not CPU data, texture bytes,
physical VRAM or automatic LRU eviction. Initial admission and reattachment
reject over-budget draw sets before GPU preparation; existing state remains
available if an interactive reattachment/reload is rejected.

To open a scene inactive and activate only one node within a smaller budget:

```bash
./build-gl/terraflux-terrain-viewer --scene examples/mixed_asset_scene.json \
  --start-inactive --activate-node 2 --max-resident-meshes 1 \
  --max-scene-instances 1 --residency-report
```

`--select-node ID` selects without activating. The title shows node ID, name,
and active/inactive state; brackets cycle through sorted binding IDs. Selection
survives detachment. A rejected activation preserves current bindings and
resources; no automatic victim eviction occurs. Inactive startup retains CPU
recipes but uploads no drawable geometry. `--activate-node` applies once at
startup; R reload reapplies inactive-start settings without auto-activation.

### Logical resource-byte accounting

```bash
./build-gl/terraflux-terrain-viewer --scene examples/mixed_asset_scene.json \
  --resource-budget-mib 64 --residency-report
```

An optional device-wide byte cap covers terrain, imported geometry, material/
camera uniforms, shared textures, instance buffers and temporary reload
candidates. The default is unlimited; the CLI accepts 0..16384 MiB. Allocations
that would exceed the cap fail before backend creation. Existing resources are
not automatically evicted. Reload/activation may need extra temporary capacity;
initialization or draw-time stream allocation can fail if the cap is too small.

Diagnostics add `buffer_bytes`, `texture_bytes`, `render_target_bytes`, `total_resource_bytes` and
`resource_budget_bytes` (null when unlimited). Buffers count their capacity;
RGBA8 textures count all generated mip levels. Shared textures count once per
live allocation; orphaning keeps the same logical capacity. Destruction and
full asset unload return counts to the terrain baseline.

These are logical API payload bytes, not measured physical VRAM. Driver
alignment/metadata, shader programs, query/framebuffer storage, deferred orphan
copies and retained CPU data are excluded. No hardware memory availability or
performance improvement is inferred from these counts.

### Peaks, rejection history and recovery

Residency reports include independent buffer/texture byte peaks, a combined
`peak_total_bytes`, allocation-admission count, rejection count, remaining logical
headroom and whether current usage exceeds a lowered cap. `last_byte_rejection`
records the usage, request size and limit at the last rejection, or null.
Freeing resources restores headroom without erasing history. Per-category peaks
may occur at different times and must not be summed into a combined peak.

RenderDevice telemetry can be reset explicitly: peaks rebase to current usage
and admission/rejection history clears. These are validation-mirror admissions,
including temporary reservations before driver creation; not physical VRAM peaks.
Orphaning does not count as a new logical allocation.

```bash
./build-gl/terraflux-terrain-viewer --smoke-test --resource-recovery-test \
  --residency-report --scene examples/mixed_asset_scene.json
```

The recovery check sets a cap at current logical usage, verifies a rejection
preserves all active resource counts/bytes, detaches all drawables, then
reattaches and renders one model within the same cap. It reports recovery;
it does not introduce automatic eviction or retry policy into normal operation.

### Retained CPU recipe payload

```bash
./build-gl/terraflux-terrain-viewer --scene examples/mixed_asset_scene.json \
  --cpu-recipe-budget-mib 64 --resource-budget-mib 64 --residency-report
```

The optional CPU cap (0..16384 MiB; unlimited by default) limits logical retained
vertex/index payload and decoded glTF image bytes. Reports expose category and
combined `cpu_recipe_bytes` plus `cpu_recipe_budget_bytes`. Canonical path aliases
share recipes and count once; separate imported files count separately. Selective GPU
eviction/detachment does not reduce these retained recipes, which support reactivation.

The scene loader checks accumulated payload after each canonical model, and the
viewer checks its completed recipe before GPU upload. Parsing/decoding is still
eager: transient parser buffers, source JSON/binary data, vector spare capacity,
material strings/maps, allocator overhead and temporary upload layouts are
excluded. This is a retained-payload admission limit, not a hard process-memory
or peak-RSS limit. File-backed OBJ textures are decoded on demand and are not
retained as CPU image recipes. Over-budget candidate loads reject without
replacing an already active imported scope.

### Persist selection and inactive bindings

```bash
./build-gl/terraflux-terrain-viewer --scene examples/mixed_asset_scene.json \
  --start-inactive --activate-node 2 --save-state /tmp/terraflux-state.json
./build-gl/terraflux-terrain-viewer --scene examples/mixed_asset_scene.json \
  --load-state /tmp/terraflux-state.json --max-resident-meshes 1 --max-scene-instances 1
```

`--save-state` writes a separate versioned JSON file on clean exit. It records
selected and inactive node IDs, with a compatibility fingerprint of the authored
scene and legacy primary OBJ argument. Writes close successfully before replacing
the destination by rename; failures preserve the prior destination. The source
scene is unchanged. State is validated before residency admission, and changed
scenes, invalid bindings, duplicate IDs and unsupported versions reject clearly.

CLI `--select-node` overrides saved selection; `--start-inactive` makes every
binding inactive, and one-time `--activate-node` runs afterward. R reload rereads
the configured state file. Full asset unload retains the last residency snapshot
for saving. Budgets, animation time, runtime transforms and camera state are not
saved; the fingerprint is a compatibility check, not an asset-integrity signature.

### Terrain tile geometry groundwork

`terrain::tile_terrain_mesh` partitions the existing terrain mesh into nonempty
CPU tiles, defaulting to 32 quads per side. Each tile has a compact mesh,
source-vertex mapping and inclusive bounds. Adjacent tiles copy the same globally
computed positions/normals, including after GPU packing; holes and winding are
preserved. Aggregate tile/vertex/index limits reject over-budget candidates.
`--terrain-tiles` enables eager tile uploads and camera-frustum culling;
`--terrain-tile-quads 8` also enables that mode with smaller tiles. Monolithic
rendering remains the default. `--terrain-residency` additionally releases hidden tiles and admits the visible set
before each frame. LOD remains future work. See the [API and limits](docs/TERRAIN_TILES.md).

```bash
./build-gl/terraflux-terrain-viewer --terrain-tiles --scene examples/mixed_asset_scene.json
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --terrain-tile-quads 8 --smoke-test --terrain-tile-test
```

Tiled smoke mode retains an extra monolithic reference upload and compares terrain
pixels before assets are drawn; normal tiled mode creates only tile geometry
buffers. The three-camera `--terrain-tile-test` requires the demo terrain, tiled
smoke mode and culling, and fails if no tiles are culled. `--no-culling` disables
both terrain and asset culling and can be used in smoke/profile/benchmark runs.
TERRAIN_JSON reports tile counts, draws, triangles and tile buffer bytes. Benchmark
JSON also identifies tiled mode and reports terrain draws/culled tiles per frame.
These functional checks do not establish a hardware speedup.

```bash
./build-gl/terraflux-terrain-viewer --terrain-residency --terrain-tile-quads 16 \
  --scene examples/mixed_asset_scene.json --residency-report
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --terrain-residency --terrain-tile-quads 8 \
  --terrain-residency-test --smoke-test
```

Selective mode retains packed CPU recipes, starts without tile GPU buffers and
synchronizes residency with the conservative visible set between passes. An
unchanged resident tile keeps its handles. Missing buffers are staged before any
old resident is evicted; a failed upload preserves the previous set and bytes.
Global byte admission therefore needs old plus staged storage temporarily.
`--max-resident-terrain-tiles 0..4096` also enables selective mode and limits the
committed set. Lowering an API limit retains existing storage but blocks oversized
future sets. A visible-set admission failure reports an error and stops the viewer;
it never silently renders terrain holes. API callers can raise a cap or request a
smaller set and retry. Disabled culling requires every tile to be resident.

CPU terrain recipes remain eager and retained in both tiled modes; the imported
asset CPU recipe budget does not include terrain recipes. TERRAIN_JSON distinguishes
all-tile geometry_bytes from resident_tiles/resident_geometry_bytes and reports
admissions/evictions. The five-frame residency smoke test forces rejection and
recovery, then full eviction and cold readmission, with monolithic pixel checks.

### Terrain residency retention window

```bash
./build-gl/terraflux-terrain-viewer --terrain-retain-frames 30 --terrain-tile-quads 16
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --terrain-retain-frames 2 --terrain-tile-quads 8 \
  --terrain-retention-test --smoke-test
```

`--terrain-retain-frames 0..600` enables selective residency. The default is zero
(immediate eviction). With N > 0, a resident tile remains eligible for N successful
visibility synchronizations after it becomes hidden. A return resets its age and
reuses its GPU handles; hidden retained tiles are still culled from drawing.
Required visible tiles take priority. Count and final-set byte pressure trim
optional hidden tiles, preferring the most recently visible and then stable tile
IDs. Staging still requires old plus new bytes; failed transitions preserve both
residents and ages. Minimized windows do not advance the policy.

TERRAIN_JSON adds retention_frames, retained_hidden and pressure_trimmed; benchmark
JSON adds the retention setting and per-frame retention/trim averages. The six-frame
retention test checks return without uploads, age reset and expiry, with matching
monolithic pixels. It requires demo smoke mode, a two-update window and default
budgets, and can use `--offscreen`. The earlier five-frame residency test requires
zero retention. This avoids extra uploads on brief returns; physical GPU timing
benefits remain unmeasured.

### Optional offscreen viewer rendering

```bash
./build-gl/terraflux-terrain-viewer --offscreen --scene examples/mixed_asset_scene.json
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --offscreen --smoke-test \
  --offscreen-resize-test --scene examples/mixed_asset_scene.json
```

`--offscreen` renders terrain and objects into an RGBA8/Depth32Float target, then
copies its color to the window with nearest sampling. Direct window rendering
remains the default. A successful frame adds one fullscreen draw; profiling/GPU
queries include it. `BENCHMARK_JSON.offscreen` records the requested mode.

Resize allocates a candidate before releasing the old target, so the resource
budget must accommodate both temporarily. Failure keeps the previous target and
scales its image into the window; initial failure falls back to direct rendering.
A failed request is retried when window dimensions, live resource bytes or the
budget limit change. Zero-sized/minimized windows skip rendering and keep storage.
Target limits remain 4096 per axis and 4,000,000 pixels. OFFSCREEN_JSON reports
allocation/recovery events and fallback, including the retained target dimensions.
Assets unload independently of the compositor and current target.

The four-frame `--offscreen-resize-test` forces a budget rejection, checks retention,
raises the cap, retries the same requested size, resizes again and releases target
storage. It requires `--offscreen --smoke-test`, disallows other scripted tests or
an explicit resource byte cap, and disables instancing to isolate target admission
from instance-ring growth. Mesa CI also checks initial fallback, mixed-format asset
unload/reload and wildfire rendering. Pixel comparisons run only in smoke mode;
ordinary frames perform no CPU framebuffer readback for composition.

### Explicit pipeline shader identities

`PipelineDesc` now starts with a required `ShaderProgramId`, followed by layout
and depth/blend state. Built-in identities cover terrain, wildfire, untextured
objects, textured objects, instanced textured objects, both PBR variants and fullscreen composition.
Layout metadata is validated against the selected contract; binding names do
not select or redirect shader source. Missing/unknown IDs and mismatched layouts
reject before backend compilation. `ContractOnly` exists for CPU validation tests
and cannot render through OpenGL. See the [pipeline contract](docs/GRAPHICS_BACKENDS.md#explicit-shader-program-identities)
for migration and remaining shader-module work.

### Exception-safe render-pass scopes

Include `terraflux/graphics/render_pass.hpp` and declare the pass after its target
and resource owners, so unwinding closes the pass before freeing their storage:

```cpp
RenderResourceOwner resources(device);
const auto target=resources.create_render_target({640,480});
{
  RenderPassScope pass(device,{{640,480,RenderTargetKind::Offscreen,target}});
  // Submit draws; an exception here still ends the pass on scope exit.
} // Target storage remains alive until resources is destroyed.
```

A successful explicit `pass.end()` closes early and is idempotent. Destruction is
nonthrowing and preserves the original drawing exception. Each successful begin
has a distinct identity; stale scopes leave newer passes alone, while explicit
closure reports an ownership mismatch. The device/context must outlive the scope.
Viewer passes use these guards in both rendering modes. Rendering errors still
propagate to the viewer's existing error handler; native/Mesa recovery tests also
verify that the device can start a new pass after the failure and cleanup.

### Terrain and fire layer visibility

T toggles terrain shading and F toggles fire/burn marks; each key press toggles
once. Startup flags `--hide-terrain` and `--hide-fire` select the same states.
Both layers are visible by default. Window titles show their states and identify
fire as unavailable in the terrain-only demo. LAYERS_JSON records requested states,
fire availability and whether the terrain/fire surface is submitted.

```bash
./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json \
  --response /tmp/terraflux-response.json --hide-terrain
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json \
  --response /tmp/terraflux-response.json --layers-test --smoke-test
```

Terrain-only mode omits both flame and burned-area shading. Fire-only mode keeps
active/burned footprints at their terrain elevation, discarding unaffected ground
so it writes neither color nor depth. Hiding both skips surface draws while
imported objects remain visible. Without replay data, fire visibility has no visual
effect and hiding terrain skips the demo surface. Visibility changes do not pause,
reset or edit replay or scene state. Fire-state buffers continue to receive replay
updates while hidden. Selective tile mode releases surface buffers when neither
layer needs them, and rehydrates on return; eager tiled mode keeps its buffers.

The five-frame `--layers-test` requires replay smoke mode without assets or other
scripted tests, with visible active fire and unaffected terrain in view. It checks
both, terrain-only, fire-only, hidden and restored images. Shader animation time is
fixed only for that diagnostic; engine/replay clocks continue at deterministic
60 Hz, and the fire field/frame are checked unchanged over the short sequence.
CI covers direct and resident/offscreen paths plus objects with both layers hidden.

### Replay speed and timing controls

Use keys **1–5** for **0.25x, 0.5x, 1x, 2x, 4x**, or set `--replay-speed` to one
of those numeric values. The flag requires replay input. Titles and REPLAY_JSON
show the selected rate, frame and play state; benchmark JSON records replay_speed.

```bash
./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json \
  --response /tmp/terraflux-response.json --replay-speed 2
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json \
  --response /tmp/terraflux-response.json --replay-controls-test --smoke-test
```

At 1x a source frame lasts 0.35 seconds of fixed engine time. Speed changes retain
partial-frame progress and affect the next 60 Hz tick. Space pauses/resumes; Home
selects frame zero and pauses; comma/period move one frame and pause. These actions
clear pending partial progress, so paused time creates no replay backlog. Reaching
the final frame stops immediately; Space at the end restarts from frame zero.
A single-frame replay stays paused. Reset and stepping preserve the selected rate.
Engine and scene clocks, camera input and flame animation timing retain their own
rates. The compute response and its source frame data are unchanged.

`--replay-controls-test` requires replay smoke mode with at least three frames,
default initial speed, no assets and no other scripted test. Its seven frames use
six fixed ticks per render to check rate change, pause, forward/back, resume and
reset. Shader animation is frozen only for pixel comparisons; selected fire data
and independent engine time are checked on every frame. CI runs monolithic and
resident/offscreen variants. See the [replay clock contract](docs/GRAPHICS_BACKENDS.md#fixed-tick-replay-clock).

### Wildfire replay and runtime checks

```bash
python3 examples/generate_wildfire_scenario.py > /tmp/terraflux-request.json
./build-gl/terraflux-compute-runner < /tmp/terraflux-request.json > /tmp/terraflux-response.json
./build-gl/terraflux-terrain-viewer \
  --request /tmp/terraflux-request.json --response /tmp/terraflux-response.json \
  --scene examples/asset_scene.json
```

For automated viewer checks, use these with a display or `xvfb-run -a`:

```bash
./build-gl/terraflux-terrain-viewer --smoke-test --scene-controls-test \
  --scene examples/asset_scene.json
./build-gl/terraflux-terrain-viewer --smoke-test --asset-lifecycle-test \
  --scene examples/asset_scene.json
```

The controls check verifies pause, one tick, a paused hold and resume against
expected serialized scene state, while the engine clock advances independently.
The lifecycle check covers failed replacement, preserved rendering, unload,
a terrain-only frame, reload, replacement and final cleanup. Imported GPU
resources and streamed instance buffers return to the terrain-only baseline.
Live API resource counts are not physical VRAM measurements.

`--profile` reports CPU and asynchronous GPU timings. See
[scene/assets documentation](docs/SCENE_ASSETS.md) for supported material
features, scene formats and culling/instancing benchmarks, and
[graphics backends](docs/GRAPHICS_BACKENDS.md) for backend boundaries.
Mesa/Xvfb CI verifies functionality; physical GPU validation and representative
hardware performance measurements remain outstanding.

## Python setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r intelligence/requirements.txt
```

## Run tests

```bash
ctest --test-dir build --output-on-failure
PYTHONPATH=intelligence \
  python -m unittest discover -s intelligence/tests -v
```

## Verify C++/Python parity

```bash
PYTHONPATH=intelligence \
  python intelligence/scripts/verify_native_parity.py \
    --runner build/terraflux-compute-runner \
    --fixtures intelligence/tests/fixtures/grid_spread_cases.json
```

## Simulation contract

Current contract version: `terraflux.compute/v2`.

| Version | Status | Semantics |
| --- | --- | --- |
| `terraflux.compute/v1` | accepted (legacy) | fuel, moisture, susceptibility, wind. `elevation_meters` is accepted but **ignored**. |
| `terraflux.compute/v2` | current | v1 plus `cell_size_meters`, a terrain (slope) term, a signed and stronger wind term, and **meteorological wind direction** (blows *from*); see below. |

A request is evaluated under the semantics of the version it declares and the
response echoes that version. Stored v1 requests therefore replay bit-for-bit
on both engines even if they carry elevation data; terrain is opt-in by sending
`v2`. Requests declaring any other version are rejected. The version was bumped
because `elevation_meters` was already a legal v1 input and giving it an effect
would otherwise have silently changed v1 results.

### Environmental drivers: wind and terrain (v2)

Each `GridCell` may carry an optional `elevation_meters`. Under `v2`, when both
the burning cell and its neighbour have an elevation, the slope angle between
them is

```
angle = atan((z_target - z_source) / run) * 180 / pi
run   = cell_size_meters            (orthogonal neighbour)
      = cell_size_meters * sqrt(2)  (diagonal neighbour)
```

The spread score is then

```
wind_norm  = clamp(wind_speed_kmh / 80, 0, 1)
wind_cos   = -cos(bearing - wind_direction)           # -1 head wind, 0 cross wind, +1 tail wind
tail_drive = wind_norm * max( wind_cos, 0)            # 0..1
head_drive = wind_norm * max(-wind_cos, 0)            # 0..1

wind     = tail_drive * 0.25 - head_drive * 0.10
uphill   = +0.15  * clamp( angle / 30, 0, 1)
downhill = -0.075 * clamp(-angle / 30, 0, 1) * (1 - tail_drive)

score = fuel_load * 0.35 + (1 - fuel_moisture) * 0.25 + burn_susceptibility * 0.25
      + wind + uphill + downhill
```

Wind and slope are each projected onto the direction of spread and summed,
the scalar form of the FARSITE / Rothermel wind–slope vector combination.

#### Wind direction convention

Angles are compass degrees, clockwise from the grid's **+y axis = north**:
`0` = north (+y), `90` = east (+x), `180` = south (−y), `270` = west (−x).
`bearing` is the compass direction from the burning cell to its neighbour.

| Version | `wind_direction_degrees` means | `270` does |
| --- | --- | --- |
| `v2` | **meteorological — the direction the wind blows FROM**, as in weather data (METAR, NOAA, ECMWF) | a west wind: pushes fire east (+x) |
| `v1` (frozen) | the direction the wind blows *toward* | pushes fire west (−x) |

Real weather feeds can therefore be passed to `v2` unchanged. **Migrating a
v1 request to v2 is not just a version-string change:** either rotate its
wind angles by 180° (if they were pre-converted to "blows toward"), or leave
them as they are if they were raw meteorological values — in which case v1
was pushing the fire the wrong way. If your grid's +y axis points south (as
raster row indices usually do), flip `y` before building the request or the
north–south component will be reversed.

- **Tail wind** is the strongest single environmental term (weight 0.25;
  v1 used 0.15). On open flat ground it is the only driver and can carry
  marginal fuel on its own.
- **Head wind** penalises spread (a backing fire), but modestly (0.10):
  backing fires are slower than calm fires yet keep spreading in continuous
  fuel. v1 treated a head wind as neutral.
- **Cross wind** has no component along the spread path and contributes
  nothing to that step. v1's unsigned alignment counted it as half a tail
  wind; v2 does not. Consequently a wind blowing along the contour leaves the
  uphill bonus fully intact (the fuel above is still preheated from below) and
  gives no relief to the downhill penalty. Quartering winds are scaled by the
  cosine.
- **Uphill** adds up to +0.15 at 30°. Fire climbs fast because the flames tilt
  into the slope and preheat the fuel above, which then ignites more easily;
  the field rule of thumb is a doubling of spread rate per ~10°, expressed
  here linearly in angle. Wind never reduces this bonus; a head wind competes
  with it additively — a steep slope beats a gale, a gentle slope loses to it.
- **Downhill is deliberately not the mirror image.** The penalty is half the
  uphill bonus (−0.075 at −30°) and is scaled by `(1 - tail_drive)`: a strong
  tail wind tilts the flames back into the fuel and drives fire downhill at
  essentially full strength. This sits between Rothermel (no downslope effect)
  and McArthur (symmetric), in line with Sullivan et al. (2014).
- **Flat** terrain, or a pair of cells where *either* elevation is missing,
  contributes exactly `0.0`.

#### Partial elevation coverage (engine-side void filling)

DEM data is routinely incomplete (sensor voids, water bodies, tile edges,
corrupt values), so under `v2` the engine fills elevation voids itself before
simulating. For each cell without elevation it finds the nearest cell *with*
elevation along each of the 8 compass rays (passing through other voids,
stopping at grid holes or after 1024 cells) and combines the hits by inverse
distance — the same scheme as GDAL's `FillNodata`. The hits are found with
one linear sweep per direction, so the cost is O(cells) however sparse the
data; the 1024-cell limit is a quality bound, not a cost bound. This is exact linear interpolation
across 1-D gaps, exact for an isolated void on a planar slope, gives a water
body its shore level, and never extrapolates beyond a DEM edge (edge voids
take the nearest known value). Known cells are never modified. Voids that
cannot be reached — nothing known at all, cut off by grid holes, or beyond
the search limit — stay void and are evaluated flat, pair by pair.

**Rays do not cross unmeasured blockers.** A zero-fuel cell without elevation
(typically a river, which is NoData in many DEMs) ends a ray like a grid
hole. Otherwise the far bank would pull shoreline cells toward its own
height — in a gorge this invented a saturated 34° slope on a flat plateau —
and bias spread along the bank and into any gap in the line. A blocker
*with* a measured elevation (a road with DEM values) is a legitimate hit.
Blocker voids themselves are not filled: their elevation is never read.
Filled values are convex combinations of real measurements, so the fill can
produce wrong *gradients* but never values outside the range of its inputs,
and the slope term saturates at ±30° in any case.

Filling can never let fire cross zero fuel: the no-fuel rule and the corner
rule are evaluated before the score, so a blocker's elevation — measured or
estimated — never takes part in a spread decision. This is pinned by
adversarial tests (a 30° climb into a dry blocker in a 300 km/h tail wind).

Why this matters: without filling, a void was flat on both the step into it
and the step out of it, which acted as a barrier on climbs and a bridge on
descents (including 2-D routing around a penalised slope). Filling removes
those artifacts; what remains is genuine information loss — on a planar
slope with 5 % voids, ~88 % of runs are identical to full coverage, and the
residual grows with void cluster size and local terrain noise.

**What is recoverable.** Scattered voids recover well down to very low
coverage: on 300 m-amplitude synthetic hills, 10 % coverage is filled
completely with ~5 m RMSE, 2 % with ~27 m. A large contiguous hole is filled
completely too (a plane is recovered exactly, whatever the hole size), but
only the *trend* across it: features smaller than the hole are gone — a
300×300-cell hole over 120-cell-wavelength hills fills with ~65 m RMSE. The
fill is a convex combination of real values, so it never invents peaks or
pits outside the data range, and the slope term saturates at ±30° anyway.

**Scale.** The contract allows up to 1 000 000 cells (raised from 100 000
after measurement). Measured on one core, 1M cells, 50 % voids plus a 300²
hole: native runner ≈ 4 s and ≈ 300 MB; Python reference ≈ 12 s to simulate
plus ≈ 20 s of Pydantic validation. Memory and output size are dominated by
the response, which carries the cumulative `burned_cells` list in *every*
frame: 120 steps × 50k burned cells is already a 21 MB response. For large
fires over many steps this — not the fill — is the limit (a v3 could emit
per-frame deltas).

The v2 response reports `metrics.cells_with_elevation` (measured) and
`metrics.cells_with_filled_elevation` (estimated by the engine); the
remainder, if any, was evaluated flat. Note that filling does **not** make
the engine a substitute for a proper DEM: estimated terrain is still a guess.

#### Fire blockers: rivers, rock, roads, fuel breaks

A blocker is a cell with `fuel_load = 0` or a cell absent from the grid.
Under `v2`:

- Blocker cells never ignite and never appear in `burned_cells`, whatever
  the wind or slope (v1 lets a dry, "susceptible" zero-fuel cell score 0.5).
- A one-cell-wide blocker line holds against any wind: there is **no
  spotting/ember model**, so fire never jumps an unbroken gap. A gap in the
  line funnels the fire through.
- **No corner cutting.** A diagonal step is refused when *both* cells at the
  corner are blockers, so a diagonal river or fire break that is continuous
  on the ground is continuous in the model. (With plain 8-connectivity, v1
  leaks through every corner of a diagonal river.) A diagonal step with at
  least one passable corner is allowed.
- A *weak* fuel break (low but non-zero fuel) is a normal cell: it holds in
  calm and against cross or head winds and fails when a tail wind and/or an
  uphill slope lift its score over the threshold — which is how real breaks
  fail.

#### Saturation and limits

- Wind saturates at **80 km/h**: 150 km/h and 300 km/h (the contract maximum)
  behave exactly like 80 km/h. Slope saturates at **±30°**: a 60° cliff or a
  5 000 m rise per cell behaves like 30°. Both are deliberate and pinned by
  tests; neither extrapolates.
- The score is clamped to `[0, 1]`. Perfect fuel (0.85) still spreads into a
  gale head wind down a cliff (0.675); environment alone never extinguishes
  maximal fuel. A steep climb (≥ ~45°) outweighs any head wind; a gentle one
  (≤ ~8°) loses to it.
- `elevation_meters` must be finite; `inf`/`nan` are rejected by both engines.
  The engine fills voids (see above) in linear time.
  The native runner enforces the same wind bounds as the Python contract
  (0–300 km/h, 0–359.99°) so raw-JSON clients see one contract.

`SpreadRequest.cell_size_meters` (default `30.0`, matching common 30 m DEM
products) is the horizontal centre-to-centre distance of orthogonal neighbours.
It only matters when elevations are supplied.

This is a threshold cellular automaton, not a rate-of-spread model: slower
spread cannot be expressed, only spread / no spread per step, which is why the
physical rate multipliers are mapped onto bounded additive score terms. Known
simplifications: wind response is linear in speed (Rothermel's is superlinear)
and all weights are modelling parameters, not calibrated constants. Constants live in
`intelligence/app/spread_kernel.py` and `native-compute/include/terraflux/contracts.hpp`
and must be kept in sync.

## Current development status

Planning snapshot: 30 October 2026, desktop viewer feature branch.
Percentages are rough engineering estimates, not measured coverage, approved
hours or release-readiness guarantees. Overall figures preserve the existing
planning estimates; their weighting is undocumented and they are not arithmetic
averages of the phase percentages. The PS4 backend is unimplemented.

| Desktop engine — Phases 1–9 | Full roadmap including PS4 |
| --- | --- |
| **~93%** (previously ~92%) | **~64%** (previously ~63%) |

| Phase | Component | Estimated completion |
| --- | --- | ---: |
| 1 | Core extraction | 100% |
| 2 | Rendering abstraction | ~89% (previously ~88%) |
| 3 | OpenGL backend | ~91% (previously ~90%) |
| 4 | 3D terrain renderer | ~93% ↑ (previously ~92%) |
| 5 | Simulation visualization | ~80% (previously ~75%) |
| 6 | Shader system | ~84% (previously ~82%) |
| 7 | Scene and asset system | ~90% (previously 86%) |
| 8 | Engine runtime | ~91% (previously ~90%) |
| 9 | Performance | ~83% (previously ~82%) |
| 10 | PS4 backend | 0% |

Latest milestones: selective node reattachment and on-demand GPU mesh residency,
scene-node detachment and geometry eviction,
shared GPU texture leases, reusable CPU material/texture
resolution, serializable Motion,
shared mixed-format scene assets, isolated
GPU asset lifetimes, unload/reload with failure recovery, and independent
scene pause/resume and single-step controls. The mixed-format example includes
OBJ, glTF, GLB, transparent materials and repeated model references. Native testing comprises 19
CTest suites and 71 simulation parity fixtures; CI also exercises the viewer
under Mesa/Xvfb. See the branch's CI results for validation of each commit.

Latest abstraction increment: backend-neutral window render-target/pass descriptions,
explicit begin/end ordering and independent color/depth clear or load operations.
Terrain and object rendering now use separate passes; Mesa smoke tests verify
that a load pass preserves terrain color. The target is borrowed from the window;
window storage creates no engine resource allocation and presentation remains GLFW-owned.
Native tests validate descriptors and reject nested passes and draws outside a pass.
The earlier explicit shader/pipeline identities retain shared layout validation.
Owned offscreen targets now provide RGBA8 color and Depth32Float depth attachments,
validated pass handles/extents, shared byte-budget admission, telemetry and scope
cleanup. OpenGL creation checks framebuffer completeness and rolls back partial
allocations. A dedicated Mesa test covers clear/load, depth occlusion, geometry,
target switching, budget rejection and repeated cleanup. Native CTest now comprises
19 display-independent suites; the extra GL test runs explicitly under Xvfb.
Offscreen color now exposes a borrowed sampled texture view, with no duplicate
byte charge and invalidation when its target is destroyed. The reusable
`FullscreenComposite` helper copies it into a separate offscreen/window pass
through an explicit Composite shader; feedback sampling of the active target
rejects. Mesa tests check exact pixels, orientation, nearest scaling, depth
preservation and subsequent geometry state restoration. The viewer now supports
`--offscreen` with transactional target resize, retained-target scaling on failure,
and direct rendering if the first target cannot be admitted. Smoke tests validate
composited pixels, actual window resize recovery, asset lifecycle and wildfire replay.
Viewer terrain, object and compositing passes now use `RenderPassScope`. Scope
exit ends the owned pass before resource cleanup, including when drawing throws.
Monotonic pass IDs keep stale scopes from ending newer passes. Native/Mesa tests
exercise exceptions, failed draws, budget rejection, cleanup and subsequent rendering.
The compute library now partitions full-resolution terrain into bounded, nonempty
CPU tiles with local indices, source-vertex maps and inclusive geometry bounds.
Tiles preserve global positions/normals, holes and exact triangle coverage, with
configurable aggregate count budgets. The viewer now optionally uploads these tiles and culls their packed-geometry
bounds against the camera frustum. Tiled smoke renders match monolithic reference
pixels, including fire-grid lookup, offscreen composition and asset lifecycle.
Optional `--terrain-residency` now evicts hidden tile buffers, stages missing
visible tiles transactionally and preserves the old set on admission failure.
CPU recipes remain retained; LOD is future work. See [terrain tile geometry](docs/TERRAIN_TILES.md).
Optional `--terrain-retain-frames N` now keeps recently hidden GPU tiles for up
to N successful visibility updates, with visible-first count/byte trimming and
transactional age updates. A six-frame Mesa test checks reuse, expiry and exact
monolithic pixels. Independent terrain/fire visibility is now available through T/F keys and
`--hide-terrain` / `--hide-fire`. Fire-only rendering discards unaffected ground;
hiding fire removes active/burned shading without pausing replay. The five-state
layer test checks isolation, exact restoration, unchanged fire data and continued
replay timing through monolithic, tiled/resident and offscreen paths.
Replay now uses an exact fixed-tick controller with 0.25x/0.5x/1x/2x/4x rates,
pause/resume, bounded stepping and endpoint restart. A seven-frame Mesa check
verifies displayed fire data, backward/reset pixel restoration and independent
engine timing. Native replay tests now cover nine groups.
Baseline checkpoint and remaining-work list refreshed against successful CI.
The [physical-GPU validation checklist](docs/GPU_VALIDATION.md) is ready, with
logged smoke checks, interactive review and repeated benchmark commands.
Hardware execution is pending. The [terrain LOD design](docs/TERRAIN_LOD.md)
is complete: boundary-preserving 4x4 patch simplification, hole fallback,
error measurement and transactional integration criteria. LOD implementation
now supports opt-in CPU simplification; geometric error measurement is next.
Progress this task: ↑ opt-in CPU level-1 geometry now reduces each eligible 4x4
patch from 32 to 16 triangles, preserving boundaries and source attributes.
Fallback patches remain full resolution; combined budgets include both variants.
Geometric error measurement and viewer integration are next.
Progress this task: ↑ terrain generation now records the elevation range and
produces diagonal-independent smooth normals with slope/summary helpers; the
terrain and wildfire shaders share a validated `TerrainStyle` block that maps a
hypsometric ramp onto the surface's own range, blends steep ground toward rock,
applies a directional hillshade and optionally draws anti-aliased contours. The
viewer frames the terrain from its bounding sphere, logs `TERRAIN_MESH_JSON`,
writes `--screenshot` frames and runs the Mesa `--terrain-shading-test`, which
compares rendered pixels against the CPU color reference.
Progress this task: ↑ terrain vertices now carry fuel load, fuel moisture and
curvature; the shared ground shaders classify dry/semi-dry/wet grass, forest,
rock, scree on exposed ridges, snow and ice from height, slope, convexity and
fuel data, add procedural detail (tufts, crowns, strata, cracks, drifts,
sparkle, bump) that fades with distance, and draw automatic contours. The
CPU material reference is verified per class by the Mesa shading test.
Progress this task: ↑ a clipped still-water surface now carries bed depth and
gradient per vertex; the Water shader blends depth-dependent color and opacity
over the ground, reflects a procedural sky and the sun by Fresnel weight, and
animates configurable waves (height, length, speed, direction) that shoal,
refract toward the shore and break into foam, with swash lines on beaches and
splashes on rocky shores. Submerged ground renders as a configurable lake/sea
floor (sand, aquatic vegetation, mud, stones, depth/slope-zoned mixed or a
seeded random floor) seen through the shallows. The CPU water and floor
references are verified by the Mesa water and shading tests, and native
testing grows to 17 suites. Rock now renders as flat-shaded, ridged, strata-
layered crags with configurable vertical relief, and the demo escarpment became
a craggy two-band cliff. Rendering now uses a C1-refined 5 m lattice over the
untouched 30 m simulation grid (`--render-cell`, budget-bounded), so relief,
crags, shorelines and contours are resolved far below the simulation cell.
Trees and grass are now instanced 3D meshes (procedural conifer/broadleaf/shrub
or any OBJ), placed from the fuel data with per-plant size and color variation,
wind sway and run-time switchable meshes and densities. Fire is now animated:
fuel-gated flames and smoke rise from burning cells, trees carry crown fires and
char, and rock, water, snow and bare soil never show fire; native testing grows
to 19 suites. Terrain renderer planning
estimate rises from ~92% to ~93%. Overall estimates remain ~93% desktop / ~64%
full roadmap; weighting remains undocumented.
See the [prioritized gaps and acceptance criteria](docs/ENGINE_BASELINE.md#remaining-renderer-work-in-priority-order). GPU meshes are created on demand; CPU model data
and decoded image recipes remain eager and retained for reattachment. Asynchronous streaming, skeletal animation
and full glTF/IBL support remain future work.
This desktop-engine breakdown is separate from the earlier simulation-focused
[220-hour planning backlog](docs/ROADMAP.md).

## License

MIT. See [LICENSE](LICENSE).

