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
- Weather: a gusting wind that wanders between two speed thresholds and veers around its mean, a
  solar day/night cycle with civil, nautical and astronomical twilight, stars and moonlight, seven
  cloud forms (clear, cirrus, cumulus, stratocumulus, stratus, nimbostratus, cumulonimbus) that
  announce their weather (haze, drizzle, rain or snow), thunderstorms with torrential squall bands,
  gust fronts, branching lightning channels and a cooled downdraft, and an orographic model of what
  the terrain does to a cloud forced uphill: windward thickening and rain, cap clouds and summit
  fog, foehn clearing and a rain shadow in the lee, blocking of weak winds by high barriers
- Sunrise and sunset from the side: the true sun keeps lighting the cloud layer from the side and
  from below in deepening reds after it has set for the ground, the horizon is warm toward the sun
  and purple opposite it (the Belt of Venus), and the low light draws a glitter path over the water;
  a simplified lunar ephemeris with phases (elongation and lit fraction follow the phase), a shaded
  moon disc with terminator, maria and craters, moonlight that scales with the phase, silver-lined
  moonlit clouds and a moon streak on the water
- Rain on the mountains: below the freezing level the water runs downhill along the terrain's
  drainage — seeping over gentle ground, streaming down steep rock, shooting over cliffs as
  waterfalls: ropes of white water that the crags split and deflect, accelerating and whitening
  as they fall and bursting where they strike a ledge, with a boiling plunge pool below; the
  lip sheds a drizzle, every ledge the fall strikes splashes and the pool throws up a plume —
  drops lit one by one by the sun or moon (forward-scattered sparkle, shadowed by the drops
  around them and by the cliff) among which those at the bow angle show a fragmented rainbow
  that follows the cascade down, in a haze of mist that hangs off the rock; above the freezing
  level the precipitation falls as snow that drifts, sways, is lofted and carried off by a strong
  wind, melts back to faster rain below the 0 C level and settles as a fresh snow cover on the heights
- Volumetric, living clouds: 3D-eroded cumuliform cells that stay crisp while an updraft feeds
  them and fray, thin and vanish when it dies (night, cold ground, the lee), self-shadowed from
  any angle; branching lightning channels with a white-hot core and halo; raindrops with their
  own size, terminal velocity and wind lag per cloud form; impact rings on water, sun glitter
  that widens into a glare on rough or rain-dimpled water, rainbows, puddles and flooding
- Weather coupling: the wind raises the sea state and bends the trees, rain dimples the water and
  wets the ground, and a v3 simulation contract lets air temperature, humidity and rainfall drive
  the fire itself (heat dries the fuel, rain cools it) while the viewer mirrors the replay's weather
- Two cameras: the orbiting world view and a first-person view standing at eye height on the
  terrain (W/A/S/D walk, Shift runs, arrows/mouse look), switched with Tab in either direction
  with the heading handed over, the feet following the ground, water and crags
- Walking collision: grass and knee-high things are stepped over, tree trunks, imported assets
  and ground steeper than the slope limit are solid — you slide around trunks and along cliff
  feet, nothing is tunnelled through at running speed, and `--no-collision` lifts it all
- Earth gravity for the walker: Space jumps to a configurable height with the analytic air
  time, running jumps carry momentum, ledges are fallen from, landings dip the eye and recover,
  rocks can be jumped over or landed on; the gravity constant is shared for later interaction
- A gait instead of a glide: acceleration from rest, steps whose speed pulses from footfall to
  push-off, head bob, sway and roll alternating with the stance foot, a jolt per landing foot,
  and stops that finish the step on a footfall
- A visible body: arms and hands posed for idling, walking, running, crouching, climbing, swimming,
  crawling, diving and lying down; swimming in strokes with a crawl and a dive with breath
- Hands with fingers that grip: stones lying on rock and scree are picked up (E), carried, dropped
  or thrown (Q) under the same gravity; branches are broken off trees, burning ones taken from the
  ground, lit at a fire into a torch that warms you and sets an unburned tree alight
- Tools on the belt against the hazards (1/2/3): a rope thrown over a cliff hangs down the face to
  be climbed down and up again, thrown into a crown it wraps round a branch and is climbed to a
  perch; a fire axe chops a tree down in counted chops and a chainsaw saws it through on a tank of
  fuel, both felling it away from you (a burning tree before it falls on you); branches are cut
  in halves — and a trunk that does land on you knocks you down and can kill
- A body to keep alive: health, body heat, strength and wetness bars; damage from lightning, fire,
  smoke, falls, storm debris, drowning and cold; crouching, slipping on ice, a crosswind that turns
  the heading; collapse and a fresh attempt
- Climbing: faces too steep to walk are climbed at a climbing pace and at a cost of strength, as
  steep as the surface allows (dry rock most, wet rock and soil less, snow little, ice and running
  water or a running waterfall not at all)
- Flash floods: storm rain runs down the drainage into channels with depth and current drawn as
  muddy water; the walker wades, swims, is pushed, or is swept off its feet and carried
- Storms from inside: gusts drift, slow and hurry the walker and uproot weak-rooted trees; lightning
  splits and lights the tree it hits, starts fires in dry woods, and strikes down, staggers or
  shakes a walker by distance
- Tree-level wildfire on top of the grid front: every tree has its own mass, strength and fuel,
  flames up, drops burning branches under gravity, chars, topples when burned through (onto the
  ground or against a neighbour that holds it or breaks), keeps burning as a log until the fuel is
  gone, lights neighbours by contact, embers and wind-driven radiant heat, and lies as an obstacle
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

### Trees in a wildfire: burning, falling, spreading

From the world view a burning cell is a patch of flames; from the ground the
fire has to happen to every tree. With a replay loaded the viewer runs a
tree-level fire model (`terraflux/graphics/tree_fire.hpp`) on top of the grid:
the simulation's front still decides *where* the fire arrives (a cell is active
for one frame, then burned, exactly as before, and nothing here feeds back into
propagation, the contracts or the fixtures), the model decides what each tree
does from there, in replay seconds, deterministically:

- **Every tree is its own**: a tapered trunk of its height and footprint with a
  wood density by species (conifer‑like lighter than broadleaf‑like) and a
  modulus of rupture, both varied per tree (±30 %), give it a mass, a breaking
  strength, a standing lean and a fuel load (bigger and wetter trees burn
  longer; the ground's fuel moisture is the tree's).
- **Burning**: a tree in a cell the front reaches flames up after a moment,
  is consumed over time (faster in wind) and **drops burning branches** from
  its crown that fall under the same Earth gravity as the walker, burn on the
  ground and can light the trees they land beside. The crown draws in and
  chars as it is eaten.
- **Falling**: the charred trunk's strength falls as the cube of the sound
  wood left while the load on it grows (its lean, the weight shifting toward
  the burn, the wind on the crown). Past that point it **topples** as a rigid
  rod under gravity and the wind, toward its lean and downwind. A neighbour in
  its way catches it: strong enough, the faller **leans against it** and lights
  it by contact; too weak, the neighbour **breaks and falls too**, alight at
  once. On the ground a trunk keeps burning until no fuel is left, lights what
  it lies against, and lies there as a **log the walker has to step, jump or
  walk around**.
- **Spreading**: burning trees ignite neighbours by contact, by embers and by
  radiant heat whose reach grows downwind with the wind — so among trees the
  fire runs faster with the wind than the cell front alone would show.
  `--tree-fire grid` keeps the trees strictly to their cells (they still burn,
  drop branches and fall); `--tree-fire off` restores the flat cell look.
- **Time** follows the replay: the model advances with the replay clock (2x
  plays the fire at 2x), holds while paused, and starts over from the field
  when the replay is wound back (trees in cells burned before the seek settle
  as charred snags, about half of them down).

```bash
./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json \
  --response /tmp/terraflux-response.json --view first-person --replay-speed 2
./build-gl/terraflux-terrain-viewer --request ... --response ... --tree-fire grid
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --tree-fire-test --smoke-test \
  --request /tmp/terraflux-request.json --response /tmp/terraflux-response.json
```

`TREE_FIRE_JSON` reports the stages, what lit each tree (cell, contact,
radiation, ember, collapse), the branches, and the mass and strength range.
`terraflux-tree-fire-tests` verifies the bodies, burning, gravity on the
branches, toppling against a neighbour that holds or breaks, logs as
obstacles, wind-biased spread and determinism; `--tree-fire-test` runs it on
the replay's own forest (time zero, three and eighteen replay seconds, reset
restoring the reference exactly). See
[the backend contract](docs/GRAPHICS_BACKENDS.md#tree-level-fire).

### Storms from inside: gusts that push, lightning that strikes

With the camera in the scene the weather is no longer scenery. Everything here
runs on the weather the viewer already simulates (the gusting wind, the
thunderstorm's squalls and strikes); nothing is staged.

- **Wind on the walker.** Below 12 m/s you brace and stand. Above it a gust
  drifts you downwind (0.02 × (speed − 12)² m/s, at most 4 m/s), slows you
  walking into it, hurries you with it, and in the air drag on the body takes
  you further downwind — a thunderstorm's gust front (up to +12 m/s over the
  base wind) is felt; `--wind-max 30` makes it a storm you can barely stand in.
- **Windthrow.** Every tree's roots hold a moment of `0.25 × m g h` (varied
  ±30 %); a gust whose moment on the crown (plus the standing lean) exceeds it
  uproots the tree, sound or charred, burning or not — about 2 % of a wood at
  30 m/s, 15 % at 35, 40 % at 40, 85 % at 50 — and the falling trunk crushes
  or props against its neighbours and lies as a log like any other.
- **Lightning.** A strike (the weather's own, at its own place and time)
  **splits and lights the nearest tree** within 12 m — alight at once and
  coming down — and sets the dry trees within 25 m alight with a 50 % chance,
  so a dry storm starts fires on its own (no replay needed). And it is a danger
  to you: within **15 m** you are struck down (4 s on the ground, the view
  shaking hard), within **50 m** knocked off your feet (1.5 s), within
  **200 m** the shock wave shakes the view. While down, walking and jumping
  do nothing; the title and the console say what happened and how far away.
  (`LIGHTNING_JSON` per strike.)

```bash
./build-gl/terraflux-terrain-viewer --view first-person --weather cumulonimbus --hour 15 \
  --wind-min 10 --wind-max 30            # a severe thunderstorm, in the woods
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --storm-test --smoke-test
```

`WindPush` and `lightning_effect` / `knock_down` / `shake` live in
`camera_rig.hpp`, the anchorage and `strike` in `tree_fire.hpp`; all are pure
and covered by the camera and tree fire suites. `--storm-test` raises a
thunderstorm over the demo or replay terrain, stands you ten metres from a
bright strike beside a tree (the tree split and alight and coming down, you
struck down and shaking, a warning raised), six seconds later has you up and
walking while the tree burns on the ground, and then sends a 36 m/s gust
front through (trees uprooted but not the whole wood, the standing walker
dragged downwind).

### Flash floods: rain that runs down the mountains and takes you with it

Rain does not only raise the lake. The drainage the weather already computes
for the runoff shading and the waterfalls (the catchment above every point,
the slope, the direction the water runs) now carries a **flood wave**
(`terraflux/graphics/flood.hpp`): the wave rises toward `rain / 25 mm/h` with
a quarter-hour time constant on the weather's accelerated clock and drains
with an hour's, so a thunderstorm's downpour fills the channels within
minutes while a drizzle never does. A channel's depth is
`1.5 m × catchment² × wave` (nothing below a quarter of the largest catchment),
its current runs down the drainage at Manning's speed (`depth^⅔ √slope / 0.04`,
up to 8 m/s) — deepest and fastest where the mountains' water collects. The
water is drawn as a muddy, opaque surface lying on the channels (the water
shader now takes the depth per vertex) and the lake with its flood rise is
the other water you meet; the deeper of the two counts.

You no longer walk on water; you are in it:

- **Wading**: the feet stay on the bed; you slow by 60 % per metre of depth;
  past 25 cm the current pushes you (fully by 85 cm). Crossing a ford you are
  carried downstream; walking upstream is a struggle.
- **Swimming**: from 1.3 m the feet hang under the surface and the head above
  it, you move at 1.2 m/s, cannot jump, and the current carries you entirely.
  Step off a ledge into deep water and you land afloat, not on the bed.
- **Swept away**: wading in a current of 2 m/s or more, half a metre deep or
  more, knocks you off your feet: down for 1.5 s, carried downstream, the
  title and console say so — and you cannot get up while the torrent holds
  you. The lesson is the real one: stay out of the channels when the storm
  rain comes; get to higher ground.

```bash
./build-gl/terraflux-terrain-viewer --view first-person --weather cumulonimbus --hour 14 \
  --time-scale 10                       # watch the valley floor flood within a minute
./build-gl/terraflux-terrain-viewer --view first-person --flash-flood 0.8       # the channels already running
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --flood-test --smoke-test
```

`--flash-flood 0..1` starts with the channels that full; `FLASH_FLOOD_JSON`
reports the wave, the surface triangles, the wet vertices and the deepest and
fastest water whenever the surface is rebuilt. `FloodModel` and `WaterRules`
are pure and covered by `terraflux-flood-tests` (the wave's rise and drain,
depth by catchment, Manning currents, waterfalls, the lake, the mesh; wading,
the ford's push, swimming afloat with the head above water, no jumping,
walking out, the ledge into the pool, the torrent's sweep). `--flood-test`
stands you dry in the terrain's wettest channel (reference), runs half an hour
of thunderstorm rain (surface drawn, standing in running water, swept if it
is fast and deep), has you try to walk upstream (carried, or held well short
of a dry walk), and drains the wave (reference restored exactly).

### Climbing out: rock faces, strength, and what cannot be climbed

Walking stops at 45°; climbing does not. Press **W** into a face that is too
steep to walk and, if it can be climbed, you climb it: slowly (0.6 m/s along
the face, so a 60° face gains about half a metre of height a second), and at a
cost — forty seconds of continuous climbing exhaust you; exhausted, you hang
where you are (you can always climb back down) until resting has restored
half your strength (a full recovery takes twenty seconds). What the face is
made of decides how steep a climb it allows:

| Surface | Steepest climb |
| --- | ---: |
| Dry rock | 75° |
| Wet rock (rain or runoff) | 62° |
| Soil / vegetated ground | 55° (wet 48°) |
| Snow | 40° |
| Ice | none |
| Running water in a channel, or a **waterfall that is running** | none |

A waterfall is only a wall while water runs over it (after rain or with the
flood wave up); dry, its rock is rock. The title shows `CLIMBING rock 63°`,
your strength (`strength 48%`, `EXHAUSTED`), and why a face refused you
(`cannot climb: waterfall`, `too steep`, `ice`, `exhausted`). Climbing,
wading and swimming give you the way out of a burning, flooding valley: up
the rock, not the stream bed.

```bash
./build-gl/terraflux-terrain-viewer --view first-person --view-x 240 --view-z -60 --view-yaw 300  # below the escarpment
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --climb-test --smoke-test
```

`ClimbRules`, `ClimbSurface` and the climb path of `walk` live in
`camera_rig.hpp` (the viewer supplies the surface from the terrain material,
the rain and runoff, and the flood model's waterfalls); `terraflux-camera-tests`
covers a wall without climbing, a climb on rock at the climb speed with its
surface and slope reported, refusal by steepness and by a waterfall with the
reason, exhaustion after forty seconds, hanging, climbing down while spent,
recovery and resumption, and the per-surface limits. `--climb-test` (demo
terrain: it needs a rock face steeper than the walking limit) stands you below
the steepest climbable rock facet, climbs it for twenty seconds (higher,
strength spent, the view changed), stands you under the biggest waterfall with
the flood wave running (the climb refused), and returns (reference restored
exactly).

### Staying alive: health, body heat, crouching, ice and falls

The walker now has a body that the world acts on, shown as four bars at the
bottom left in first person — **health** (red), **body heat** (orange, turning
blue as it goes), **strength** (green) and **wetness** (blue, thin) — and in
the title (`health 62% heat 80% wet (fire)`).

- **Health** is lost to lightning (struck −60 %, knocked off your feet −15 %),
  **fire** (12 %/s inside the flames of a burning tree or log or on burning
  ground, fading out by 12 m), **smoke** (1.5 %/s at full density; it lies
  downwind of the burning trees), **falls** (nothing up to 6 m/s of impact — a
  1.8 m drop — lethal at 20 m/s, a 20 m fall), **wind-borne debris** above
  32 m/s, **drowning** (swimming spends strength over 30 s; spent, 10 %/s), and
  **cold** once the body heat is below 30 % (hypothermia). At zero you collapse
  — the title says why — and Tab out to the world view and back starts a new
  attempt with a fresh body.
- **Body heat** drops below 18 °C, faster in wind (chill), faster soaked
  (×2.5) and far faster in water (×4 more); it returns in warm air and beside
  a fire at a distance — the same heat that burns you up close warms you at
  12 m. **Wetness** rises in rain (a storm soaks you in 30 s), at once in
  water, and dries in warm air (two minutes).
- **Crouch** (hold **Ctrl**): the eye comes down 0.7 m, you walk at half
  speed, cannot run, jump or climb, pass under low branches, and are a smaller
  target for lightning (a strike counts as twice as far away).
- **Ice and snow**: the ground's grip (ice 0.1, snow or a frozen fresh cover
  0.5, wet rock 0.7) slows your starts and lengthens your stops (a stop from a
  walk takes three metres on ice), on slopes over 5° you slide downhill, and a
  slide faster than 2.5 m/s on ice is a **slip** that throws you down.
- **Falls** report their impact; a **crosswind** above 15 m/s turns your
  heading downwind (0.02 rad/s per m/s over), so holding a course across a
  gale takes steering, and walking into it is slow (see the storm section).

```bash
./build-gl/terraflux-terrain-viewer --view first-person --weather cumulonimbus --hour 15   # survive a storm
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --vitals-test --smoke-test
```

`vitals.hpp` (`VitalsStyle`, `Exposure`, `advance_vitals`, `hazard_for`),
`CrouchRules`, `GripRules` and `HeadingPush` in `camera_rig.hpp`, and the
`Hud` program / `HudRenderer` (`hud.hpp`, screen-space bars) are pure or
validated and covered by `terraflux-vitals-tests` and the camera suite.
`--vitals-test` stands you fresh on a mild day (bars drawn), two metres from a
tree it sets alight (health burned, the bar changed), through half a minute of
freezing rain, wind and water (soaked, chilled toward hypothermia), a
twelve-metre fall and a direct strike (collapsed, down), and out to the world
view and back (fresh).

### Your own hands, and swimming

In first person you now have a body you can see: two arms — sleeves and
hands — posed for what you are doing (`terraflux/graphics/body_view.hpp`):
they hang at your sides and come into view when you look down, pump with
the stride when you run (the arm opposite the planted foot swings forward,
driven by the gait's own phase), reach up alternately when you climb, sweep
out and back with each breaststroke, reach ahead alternately in the crawl,
go forward together when you dive, and lie out in front of you when you are
down. They are simple blocks on two-segment arms (an elbow found by IK from a
hand target per activity) drawn with the object program just in front of the
eye; `--no-arms` hides them.

Swimming is a proper skill now. Afloat (1.3 m and deeper) you swim in
**strokes**: a breaststroke every 1.4 s at 1.2 m/s, the speed surging with
each pull and gliding between, the head lifting a little with the stroke.
**Shift** swims the **crawl** — 1.7× faster, 1.7× the strength cost (strength
drains over 30 s of swimming; spent, you are drowning). **Ctrl** **dives**:
the eye goes 1.2 m under the surface (a green-blue veil over everything), the
breath runs out in 25 s and you surface by yourself; at the surface it returns
in 8 s, and you need half a breath to dive again. Under water neither the
flames nor the smoke reach you — diving under a burning shore is a real
escape — but the cold does, faster. `--view-pitch` now also sets the
first-person look (−85..85) so a view down at your hands can be reproduced.

```bash
./build-gl/terraflux-terrain-viewer --view first-person --view-pitch -57   # look down at your hands
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --body-test --smoke-test
```

`terraflux-body-view-tests` covers the poses (lengths, mirroring, hanging
hands that come into view looking down, the running pump against the stance
foot, the alternating climb, the breaststroke sweep, the crawl, the dive, the
collapse, reach limits and smoothness), the state reduction and the box/basis
geometry; the flood suite covers the strokes, the crawl's speed and cost,
diving, breath and surfacing. `--body-test` (demo, or a replay with
`--water-level`) renders the hanging hands looking down (skin in the lower
half of the image), hides them, runs, climbs, swims (a stroke counted) and
dives (eye under the surface, breath spent), and restores the reference exactly.

### Fingers, stones, branches and a torch

The hands are hands now: each is a palm, a two-segment thumb set opposed and
four two-segment fingers, thirteen boxes per arm (`kArmSegments`), posed by a
`HandShape` — how far the fingers curl (open to a fist), how far they spread
and how the thumb curls — that `hand_shapes` chooses for what you are doing
(open and spread swimming, hooked over a hold climbing, loose at your sides,
closed around whatever you carry) and that `body_pose` blends smoothly, so the
hands close on a stone — a pebble in a fist, a big stone in an open grasp under
the palm — and open again when you let it go. A branch or torch is carried
thumb-up in a fist at the waist, the forearm turned so the stick stands up out
of it. The held thing is drawn in the right hand as a twenty-seventh segment
(`kHeldSegment`).

Things in the world can be picked up (`terraflux/graphics/items.hpp`):

- **Stones** lie where the ground is rock, scree or bare (`place_stones`: a
  jittered 7 m grid, kept by the material's chance — rock 55 %, ridge 75 %,
  bare 25 %, soil 3 % — never on water, snow or ice, 6–32 cm, deterministic
  from the terrain; `--no-stones` leaves them out; `STONES_JSON` counts them).
  Those within 90 m are drawn (`PropRenderer`), the small ones stepped over,
  the large ones walked round. Look at one within 2.3 m and **E** picks it
  up: it is gone from the ground and in your hand. **E** again puts it down
  at your feet; **Q** throws it — 12 m/s along your look lifted 12°, flown
  under the walker's gravity — and it lands as a stone again, to be picked
  up where it fell (`throw_velocity`, `advance_projectile`).
- **Branches** are broken off the tree beside you (**E** within 1.4 m of a
  trunk) or picked up where they lie; a branch the fire dropped burning can
  be taken up burning (within 1.8 m, as long as it has left to burn). Holding
  a branch where a tree's fire is hot enough (heat ≥ 0.3, a pace from the
  flames) **E** lights it into a **torch** that burns for 90 s (the title
  counts it down), warms you without burning (heat 0.18, drying and holding
  off the cold), carries its own flame, and — **E** beside an unburned tree —
  sets it alight (ignition source `ember`, so it spreads like any other tree
  fire). Burned out it is a branch again. Put down or thrown, a torch lands
  as a burning branch on the ground — and lights the trunk it lies beside —
  while a branch lands as a branch.
- The title shows what you hold and what **E** would do here
  (`holding torch (72 s) | E: set the tree alight | Q: throw`). `--hold
  stone|branch|torch` starts you holding one.

```bash
./build-gl/terraflux-terrain-viewer --view first-person --view-x 42 --view-z 12 --view-yaw -106 --view-pitch -35   # a stone at your feet: E
./build-gl/terraflux-terrain-viewer --view first-person --hold torch --view-x -350 --view-z 250          # a torch into the lakeside forest
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --items-test --smoke-test
```

`terraflux-items-tests` covers stone placement (rock against grass, the
water line, determinism, the seed, the style limits), the obstacles, reach
(the nearest in front, taken stones skipped, branches, trunks from the feet),
the throw velocity and the flight (landing where ballistics says, then at
rest), the props (nearest first, bounded) and burning branches put down and
taken up again with their remaining burn time — a burning branch laid beside
a trunk lights it. The body-view suite covers the hands (skin fingers a
finger's length from the palm, the thumb apart, a gripping hand curling in, a
stone lying in the palm, a branch held at its length and rising up through the
fist with the thumb on top). `--items-test` (demo,
or a replay with trees) stands you before a stone, picks it up (in the hand,
gone from the ground), throws it (landed ahead as a stone), breaks a branch off
a tree, lights it at that tree burning (a torch, the warmth measured), sets an
unburned tree alight with it and throws the torch to land burning.

### Tools against the hazards: rope, fire axe, chainsaw

You always carry three tools (`terraflux/graphics/tools.hpp`); **1**, **2**
and **3** take the rope, the fire axe or the chainsaw in the right hand (the
same key again puts it away), and whatever you were holding — a stone, a
branch, a torch — moves to the left hand. **E** uses the tool; with nothing
for it to do, E interacts as before (so you can pick up a stone with the axe
out). The title shows the tool, the chainsaw's fuel and what E would do.

- **Rope** (20 m, `--rope-length 2..200`). **E** throws the coil along your
  look, lifted 12°, at 10 m/s; it flies like a thrown stone, pays the rope out
  behind it and can go no further than the rope's length. Where the coil
  comes down the ground between your feet and it is walked in quarter-metre
  steps: if it falls away steeper than 45° for at least 2 m, the rope lies to
  that **lip**, **hangs down the face** to its foot (pegged at the lip — the
  title says so; there is no anchor to place) and lies on from there. On
  gentle ground it only lies there ("nothing to climb down here"). Thrown
  **up into a crown** (within a quarter of the tree's height of a standing
  trunk, between 30 % and 95 % of its height) it **wraps round a branch** and
  hangs straight down to the ground. A hung rope is drawn as a line; **E**
  within 1.5 m of it — at the lip, under its end, or beside the line — takes
  hold. On the rope **W** climbs up at 0.8 m/s (spending strength: a minute
  from fresh to exhausted; hanging still costs a third), **S** lets you down
  (free), **Space** lets go (you fall from there), and an **exhausted**
  climber slides to the bottom at 4 m/s. Letting down onto ground within
  0.5 m of the feet — the foot of the face, or a ledge the rope runs over for
  a metre — steps off onto it; at the top of a pegged rope **W** (or E)
  steps back onto the edge; at the top of a branch rope you hang in the
  crown, a **perch** out of a flood or a ground fire (a burning tree is no
  refuge). **1** within reach of either end pulls the rope in — not while
  anyone is on it. The arms pull hand over hand.
- **Fire axe**. Beside a standing trunk (within 1.6 m of the feet) **E**
  swings: the chop lands halfway through the 0.7 s swing (presses during it
  are ignored) and cuts 6 cm of the diameter; at **70 % of the diameter** the
  tree is **felled** — it topples at once **away from you** (the side you cut
  from), burning or not, a log on the ground like any fallen tree (an
  obstacle, contact fire, all of it). The hint counts the chops (a 0.5 m
  trunk takes six; the demo's big conifers up to eighteen). A trunk cut part
  way is weaker — a burning one breaks at the cut, the cut's way, before its
  uncut neighbour. On a **branch** lying in reach (or a burning one at your
  feet) the axe **cuts it in two**; pieces halve down to a quarter metre
  ("too short to cut"). Both hands are on the haft unless the left holds
  something.
- **Chainsaw** (`--chainsaw-fuel 1..36000` s, 300 by default). Hold **E**
  beside a trunk: it saws 10 cm of diameter per second (a 0.5 m trunk in
  3.5 s; the hint says how long), burns fuel only while it cuts, pushes
  forward and trembles in the hands, and fells the tree the same way; on a
  branch it cuts through in half a second. Out of fuel it is a weight.
- **A tree on you.** Any trunk that lands on you — felled toward yourself,
  windthrown, or toppled by the fire — knocks you down for three seconds and
  costs health by its mass (100 kg a tenth, a tonne and more all of it:
  `crush_damage`, cause `crushed`). Up a rope above the log you are clear.

```bash
./build-gl/terraflux-terrain-viewer --view first-person --tool rope --view-x 253 --view-z 502 --view-yaw 128 --view-pitch -30   # atop the escarpment: E throws it over
./build-gl/terraflux-terrain-viewer --view first-person --tool axe --view-x -350 --view-z 250 --view-yaw 20      # in the lakeside forest: 2 chops, 3 saws
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --tools-test --smoke-test
TERRAFLUX_SCREENSHOT_EVERY_FRAME=1 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --tools-test --smoke-test --screenshot tools.ppm        # tools-0.ppm .. tools-8.ppm
```

`terraflux-tools-tests` covers the belt, the chop and saw counts, and the rope
on a cliff (thrown, pegged at the lip with the span the face's height, taken
hold of from the edge and from the bottom, let down at the climbing speed at no
cost, climbed up at a cost, stepped off at the top and the foot, the slide when
spent, letting go, pulling in, a rope on a gentle slope only lying, a short rope
paying out its length) and up a tree (caught by the crown, hanging to the
ground, climbed to a perch with nothing to step off onto, dropping whoever
hangs on it when pulled in, drawn as a line and a coil); the tree-fire suite
covers cutting (the depth per chop, the count to fell, the fall the cut's way,
unlit for a sound tree and alight for a burning one, the saw's continuous cut,
the weakened trunk breaking first, burning branches split in halves down to a
quarter metre); the body-view suite covers the tools in the hands (both on the
haft, the swing raised then down and forward and smooth, the chainsaw's handles,
bar, push and tremble, the torch in the left hand beside the axe, the coil
hanging from the hand, nothing in hand while climbing). `--tools-test` (demo,
or a replay — on a terrain without a face the rope is thrown on the flat and
only lies there) ropes down the tallest face it finds and back, chops a tree
down in its counted chops, saws one through in its time and fuel (both falling
away), cuts a branch into halves and refuses a quarter, ropes up into a crown
to a perch, lets go, and has a tree felled toward the walker land on it.

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

### Weather: wind, daylight, clouds and what the terrain does to them

`terraflux/graphics/weather.hpp` adds a backend-neutral weather model to the
viewer. Rendering never feeds back into fire propagation or replay timing:
the fire reacts to weather only through the simulation's own v3 contract
(temperature, humidity and rain in the request), which the viewer mirrors.

- **Wind** is never constant. `--wind-min`/`--wind-max` (m/s, default 2..6) are
  the two thresholds between which the speed wanders, `--wind-swing` (default
  20 degrees) the half-width of the veer around `--wind-direction` (compass
  degrees the wind blows *toward*; defaults to the wave direction) and
  `--wind-period` (default 12 s) the time scale of the gusts. Smooth seeded
  noise drives both, so the wind actually reaches both thresholds, never leaves
  them and is reproducible (`--vegetation-seed` seeds it). The gusting wind
  steers the tree and grass sway, the flames and the smoke; clouds drift with
  its integral. `WindSample` reports the instantaneous speed, direction and
  gustiness (0 at the lull .. 1 at the gust).
- **Daylight** follows the solar position: `--latitude` (default 47),
  `--day-of-year` (default 172, the June solstice), `--hour` (local solar time
  at start, default 10) and `--time-scale` (engine seconds per simulated hour,
  default 10, so a day passes in four minutes; 0 freezes the clock, 3600 is real
  time). Day and night are not binary: the direct light reddens and fades
  through sunset, civil twilight (0 to -6 degrees), nautical twilight (-6 to
  -12) and astronomical twilight (-12 to -18) into night, the sky gradient and
  ambient light change continuously, stars come out and a full moon opposite
  the sun lights the night faintly and bluish. `DaylightPhase` names the
  current band.
- **Clouds** have forms, and each form stands for the weather an observer
  expects: `--weather clear|cirrus|cumulus|stratocumulus|stratus|nimbostratus|cumulonimbus`
  (default cumulus; `off` restores the neutral sky). `cloud_profile()` carries
  the typical base height, depth, sky cover, optical density, edge detail,
  precipitation rate, storm likelihood, surface visibility and a one-line
  forecast: fair-weather cumulus is dry and puffy, cirrus runs ahead of a
  front, stratocumulus is dull but mostly dry, stratus brings drizzle and fog on
  the hills, nimbostratus hours of steady rain or snow, cumulonimbus thunder
  showers with lightning (see below). `--cloud-cover` overrides the sky cover, `--cloud-base`
  the base altitude; otherwise the base is the lifting condensation level
  (125 m per degree of dew-point spread) above the valley floor from
  `--temperature` (default 18 C) and `--dew-point` (default: the form's typical
  base), so humid air lowers the clouds. Precipitation falls as snow where the
  air at the camera's ground is at or below 1 C (6.5 C per km lapse rate); rain
  darkens the ground; visibility shrinks to the form's haze; storms flash.
- **Terrain interaction.** A height field sampled from the render mesh and the
  current wind give an orographic field (`compute_orographic_field`) that the
  shaders read per location. Air forced uphill cools and condenses: the cloud
  thickens, its top lifts and it rains more on the windward slope ("stau").
  Where the ground rises into the layer the ground is inside the cloud — a cap
  cloud or summit fog — because terrain, sky, water and vegetation all
  evaluate the same cloud density. Air that crossed a crest descends and
  warms: in the lee the cloud evaporates for a few kilometres (foehn gap), the
  base rises and a rain shadow forms. Whether the air crosses at all depends
  on the Froude number `U / (N H)` (`--stability` sets N, default 0.01 per
  second): a weak wind against a high barrier is blocked, the cloud dams up in
  front of the slope and never reaches the lee. Clouds far above the crests
  (cirrus over hills) are decoupled. The field is recomputed whenever the wind
  veers by more than 3 degrees or changes by more than 0.75 m/s.
- **Sunrise, sunset and the moon.** The ground's light fades out at -3 degrees,
  but the cloud layer keeps the *true* sun a while longer: `cloud_light_*`
  ramps from the ordinary sun through orange at the horizon to deep red at
  -5 degrees and out at -8, and the clouds are lit along that direction — from
  the side, and from below the base once the sun is under the horizon — so
  their lit faces and undersides glow while their shaded faces keep the cool
  ambient of the sky opposite. The sky's horizon is warm toward the sun and
  cool and purplish opposite it (`horizon_away_color`, strongest around
  sunrise and sunset), and the Beckmann glitter draws a long path over the
  water toward a low sun. The moon follows a simplified ephemeris: its phase
  comes from the day of the year (29.53-day month from a new moon on day 1.5;
  `--moon-phase 0..1` overrides, 0.5 is full), its elongation from the sun
  equals the phase (a full moon rises at sunset and stands due south at
  midnight; a new moon travels with the sun) and its declination swings
  against the sun's. Moonlight is up to 0.11 of the daylight scale at full
  moon and falls steeply with the lit fraction (a half moon gives a tenth),
  bluish, only once the sun has left civil twilight and the moon is up; it
  lights the ground, the clouds (lifted, since the eye adapts, with
  silver-lined edges from the forward-scattering lobe) and draws its own
  streak on the water through the same glitter lobe. The sky draws the moon
  as a sphere shaded by the sun — so the terminator and phase follow the
  ephemeris — with procedural maria and craters (bowls with sun-facing rims)
  on the face that always turns toward us, earthshine on its night side and a
  halo; `--moon-size DEGREES` sets the apparent diameter (0.5 is true, the
  default 2 shows the craters).
- **Living, volumetric clouds.** Cumuliform cells are fed by updrafts:
  `convection` (0..1) rises with daylight and a warm ground (and with
  orographic lift on windward slopes) and a seeded lifecycle field drifting on
  the accelerated weather clock (`clock_acceleration`, the solar time scale
  capped at 60x, so a half-hour cell life passes in 30 s at the default
  `--time-scale 10`) decides which cells are fed. Fed cells keep crisp
  cauliflower edges carved by two octaves of 3D noise; when the updraft dies
  dry air mixes in: the edge transition widens, the cell thins and finally
  drops below the coverage threshold and vanishes — so cumulus fray and
  dissolve in the evening and over cold ground and re-form on a warm
  afternoon. Lighting samples the optical depth toward the sun along a cone
  (three samples), applies Beer extinction with a "powder" term that darkens
  thin edges and a darker base, so towers read as volumes from below, from
  the side and from above.
- **Thunderstorms.** A cumulonimbus is not just a cloud form: its showers arrive
  in squall bands (a third to the full 25 mm/h every minute or so), its gust
  front widens the wind thresholds by up to 12 m/s and the veer by 25 degrees
  and halves the gust period, the rain and the cold downdraft cool the surface
  air by up to eight degrees, and strikes land across the terrain — each a
  seeded, branching channel from the cloud base to the ground (`build_lightning_bolt`,
  the `Lightning` program): a white-hot core a few metres wide inside a
  gaussian blue-white halo that never drops below the camera's pixel size,
  re-lit by two return strokes, lighting the sky around it and the clouds from
  inside, throwing a point light onto the ground and a sheet flash over the
  whole scene while it decays. `--weather-time SECONDS` reproduces a moment
  (a gust, a band, a strike) including the rain that fell until then.
- **Rain physics.** Every drop obeys three forces: gravity against drag sets
  its terminal velocity from its diameter (Atlas et al.: `9.65 - 10.3 exp(-0.6 D)`
  m/s), diameters scatter exponentially around the form's mean (drizzle
  0.4 mm falling at 2 m/s, nimbostratus 1.4 mm at 6 m/s, thunderstorm drops of
  3 mm at 8 m/s), and the wind carries small drops fully while big drops lag
  the gusts. Streak length is the fall during a 1/30 s exposure, width and
  brightness follow the diameter; snow rides the wind and sways. Nimbostratus
  gives long, moderate rain (6 mm/h), a thunderstorm torrential bands of big
  drops (25 mm/h). Rain accumulates on the ground on the accelerated clock and
  drains with a six-hour time constant: after ~10 mm puddles collect in the
  hollows of gentle ground (wet, sky-reflecting by Fresnel, growing and
  merging as the ponding rises) and standing water rises 2 cm per millimetre
  (capped at 3 m): the lake surface is rebuilt at the raised level with its new
  shoreline and the floor follows (`FLOOD_JSON`), then falls again as the ground
  drains.
- **Runoff, waterfalls and the snow line.** A D8 drainage of the height field
  (`compute_runoff_field`, once per terrain) routes every texel to its
  steepest lower neighbour and accumulates the catchment, so channels form
  where the water gathers; the slope decides the regime. While it rains (or
  the ground still drains) below the freezing level — the 0 C altitude from
  the surface temperature and the lapse rate, `freezing_level_m` in
  `WEATHER_JSON` — the terrain shader wets and glosses the ground toward the
  light, scrolls streaks downhill at a speed set by the slope (a slow seep on
  gentle ground, fast on steep rock), draws bright stream threads where the
  drainage gathers on slopes, and where a stream meets a cliff steeper than
  ~45 degrees it shoots over the edge. Each cascade is traced from its lip
  down the drainage: every falling texel carries the height the water has
  dropped since the lip (`drop_meters`), the first gentle texel below is its
  plunge pool, and the field's direction channel (the smoothed downhill
  gradient, 16 bits per channel so the shading frame it sets does not step)
  gives the shader a stable along/across frame. In that frame the water runs
  in ropes a few metres wide (`waterfallRope`, shared by the ground shader and
  the spray) that the crags of the cliff — the same ridged field that
  displaces the rock — deflect sideways and that divide as the fall grows
  long; the ropes are opaque water with streaks scrolling at the fall's speed
  (`sqrt(2 g h)`, stretched as it accelerates) that whiten with entrained
  air lower down, burst white where a protruding crest of rock stands in the
  way, and boil in the plunge pool. The spray is a particle system
  (`SprayRenderer`, the `Spray` program): `waterfall_spray_emitters` turns
  every lip, every ledge the fall strikes and every plunge pool into an
  emitter (strongest first, up to 96) with the rock's frame, and the vertex
  stage derives each drop from its emitter, index and the clock — launched
  with the water over the lip, off the ledge along the face or up out of the
  pool, then relaxing to its own terminal velocity (Atlas et al. from a
  per-drop diameter: fine drizzle off the lip, coarser splash below) while the
  air the falling sheet drags down and the wind carry it; drops that fall back
  into the pool or against the rock vanish. Each drop is lit on its own: a
  white forward-scattered sparkle toward the sun or moon, darkened by the
  optical depth of the plume between it and the light (drops on the far side,
  and whole clumps, go dark), by the cliff when the face turns away from the
  light (the farther out and the higher the light, the more a drop escapes
  that shadow) and by the cloud shadow; and a drop standing 40.6–42.4 degrees
  from the anti-light point (50–53.5 for the secondary) shows a speck of the
  spectrum — tiny drops a broad pale fog bow, bigger ones a saturated one,
  each offset a little and only every other drop — so the bow is fragmented
  and follows the cascade down from ledge to ledge. The first particles of
  every emitter are mist puffs: soft camera-facing discs a few metres wide
  that swell as they thin, hang off the rock and carry the pale bow.
  `--rainfall 40` starts with that much rain already on the ground (runoff,
  puddles, a flood rise) so a sunny afternoon shows the cascades running.
  Above the freezing level nothing runs: the precipitation is snow — flakes
  falling at about a metre a second, riding the wind fully, swaying, lofted
  and carried off sideways by a wind above ~8 m/s — which melts back into
  rain through a 300 m layer below the 0 C level (sleet in between, speeding
  up to the drop's terminal velocity), and the fresh snow cover whitens the
  ground above that line as it accumulates (`snow_cover`), so a cold front
  draws a snow line across the mountains with rain and runoff below it.
- **Rain, sun and water.** Impact rings (a jittered grid of impact points on
  two scales, each repeating with its own period, spreading at 0.35 m/s with
  capillary crests that decay within a second, bigger for bigger drops)
  perturb the water normal; the sun's reflection is a Beckmann microfacet
  lobe whose roughness grows with the waves, the chop and the rain, so a calm
  lake shows a tight sun image broken by the waves and a rough or
  rain-dimpled one a broad, softer glare; rain scatters the reflected sky and
  clouds into a dull sheen. Sunshine behind the observer with rain in front
  draws a rainbow: the primary bow 40.6..42.4 degrees from the anti-solar
  point with red outside, the fainter reversed secondary at 50..53.5 and
  Alexander's dark band between, only when the sun is up and not hidden by an
  overcast.
- **Coupling.** The wind of the moment drives the sea state
  (`WeatherSimulation::water_for`: height grows about as U^1.6 from a third of
  the authored waves in a calm to the 5 m cap in a gale, with longer, faster,
  choppier waves that turn after the wind with a lag and more foam), bends the
  trees (sway grows with the square of the speed, a gale adds flutter and
  leans the crowns downwind) and steers the flames and smoke; rain dimples the
  water, darkens the ground and turns the smoke to steam.
- **Fire weather.** The surface temperature (diurnal swing, rain and downdraft
  cooling, the lapse rate to the camera's ground), the humidity from the dew
  point and the rainfall give the same `weather_term` the simulation uses under
  the v3 contract (see below): hot dry air lifts the flames, rain beats them
  down (`FireStyle::vigour`, 0.25 .. 1.6). When a replay carries a v3 weather
  series the viewer mirrors it frame by frame — temperature, humidity, rain and
  the implied cloud form (a downpour is a cumulonimbus, steady rain a
  nimbostratus, drizzle a stratus, saturated air a stratocumulus) — and the
  request's own wind seeds the viewer's wind, so the flames you see shrink in
  the same storm that stopped the simulated front. `--weather auto` enables
  this in smoke runs; an explicit form overrides the implied one; `--replay-frame N`
  starts paused on a frame.
- **Run-time keys**: **C** cycles the cloud form (printing its forecast),
  **J**/**K** move the solar clock an hour back/forward, **X** cycles wind
  presets (calm, breeze, fresh, gale), **O** toggles the weather; the title
  shows the form, the time and twilight phase and the wind. `--view-yaw`,
  `--view-pitch` and `--view-zoom` set the starting camera so a view can be
  reproduced. The fair-weather cumulus base (2.6 km above the valley floor, a
  dry continental afternoon) keeps the overview camera below the clouds over
  small terrains; humid air (`--dew-point 12`) lowers it to 750 m and the
  overview then looks down onto the cloud tops.

```bash
./build-gl/terraflux-terrain-viewer                                   # cumulus, 10:00, breeze 2..6 m/s
./build-gl/terraflux-terrain-viewer --weather stratus --wind-direction 80 --view-zoom 0.12 --view-pitch 10 --view-yaw 250
./build-gl/terraflux-terrain-viewer --weather nimbostratus --temperature 3 --hour 7 --time-scale 60
./build-gl/terraflux-terrain-viewer --weather cumulonimbus --hour 15 --weather-time 132.6 --time-scale 0   # a strike over the lake
./build-gl/terraflux-terrain-viewer --weather clear --hour 20.3 --time-scale 0 --latitude 60
./build-gl/terraflux-terrain-viewer --weather cumulus --cloud-cover 0.5 --hour 20.05 --time-scale 0 \
  --view-zoom 0.06 --view-pitch 9 --view-yaw 85 --view-x -45 --view-z -375                         # sunset over the lake
./build-gl/terraflux-terrain-viewer --weather clear --hour 0 --time-scale 0 --moon-phase 0.3 --moon-size 6 \
  --view-zoom 0.06 --view-pitch 7 --view-yaw 108 --view-x -45 --view-z -375                        # a gibbous moon with craters
./build-gl/terraflux-terrain-viewer --weather nimbostratus --hour 14 --time-scale 0 --weather-time 30 \
  --view-zoom 0.08 --view-pitch 10 --view-yaw 270 --view-x 90 --view-z -150                        # waterfalls on the escarpment
./build-gl/terraflux-terrain-viewer --weather cumulus --cloud-cover 0.1 --rainfall 40 --hour 16 --time-scale 0 \
  --view-zoom 0.03 --view-pitch 8 --view-yaw 270 --view-x 405 --view-z -234                        # cascades, spray and bow fragments after the rain
./build-gl/terraflux-terrain-viewer --weather nimbostratus --temperature 1 --hour 8 --time-scale 10 --weather-time 300 \
  --view-zoom 0.3 --view-pitch 20 --view-yaw 250                                                   # a snow line on the summit
./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json --response /tmp/terraflux-response.json \
  --weather auto --replay-frame 34 --view-zoom 0.45 --view-pitch 35 --view-yaw 30                  # the storm that slows the fire
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --weather-test --smoke-test
```

Smoke runs keep the weather off unless `--weather` is given, so every existing
reference comparison sees the neutral sky; `--smoke-test --weather cumulus`
renders one weathered frame. Smoke, profile, benchmark and residency-report
runs print `WEATHER_JSON` (form and forecast, layer, temperatures, solar time
and phase, wind sample and thresholds, orographic field statistics, the
cascade counts and `WEATHER_SPRAY_JSON` with the spray drawn and the pixels it
lit and tinted). The
state lives in the 400-byte `Atmosphere` uniform block (slot 15), the
`OrographicField` texture (slot 1) bound by every world shader and the
RGBA16 `RunoffField` texture (slot 2) bound by the ground shaders; in their
neutral state the shaders reduce exactly to the weather-free formulas, and CPU
twins (`atmosphere_shade`, `atmosphere_haze`, `atmosphere_sky_color`) mirror
the cloud-free lighting. `--weather-test` renders ten frames — weather off,
clear noon, a full-moon midnight, civil twilight, stratus at noon, cumulus at t = 0
and 120 s later, a thunderstorm at the moment of a strike over the middle of
the terrain, four weather hours of nimbostratus rain, four more in freezing
air, weather off — and checks that the sky dome covers the clear
color and is bright and blue, that the lit terrain matches the CPU twin by
day, by moonlight and at twilight, that night is dark with stars and the full
moon is its light, that the
twilight horizon glows warm, that stratus greys the sky, shadows the ground,
reaches the summit, thickens on the windward slope and draws drizzle, that the
cumulus drifts with a gusting wind that stays inside its thresholds, that the
storm widens those thresholds, draws a lightning channel, pours more than
5 mm/h, cools the surface air, turns the fire weather negative and raises the
waves, that the rain leaves puddles, runs off, raises the lake and draws
visible spray from the cascades' lips and plunge pools, that freezing air
turns it to snow with a cover on the ground, no runoff and no spray, and
that disabling the weather restores the reference exactly.
Imported OBJ/glTF assets keep their fixed studio light and are not fogged;
cloud shadows on assets, thunder, hail, wet specular ground, lee-wave clouds
and temporal filtering of the cloud march remain future work. See
[the backend contract](docs/GRAPHICS_BACKENDS.md#weather).

### World view and first person: Tab, W/A/S/D

The viewer has two cameras and **Tab** switches between them at any time:

- **World view** (the default): the orbit camera of the overview. Arrow keys
  orbit, + / - zoom, **W/A/S/D** pan the target over the ground (it follows the
  terrain, the water and now also the crag relief, so the camera never sinks
  into a displaced rock face).
- **First person**: you stand on the ground at eye height (1.7 m above the
  measured surface, or above the water surface over a flooded or submerged
  cell, plus the rock relief a crag may be displaced by). **W/S** walk forward
  and back along the way you look, **A/D** strafe, **Shift** runs (3x), the
  **arrow keys** and the **mouse** (the cursor is captured while in first
  person and released on leaving it) look around, pitch limited to 85 degrees
  up and down, and **+ / -** change the walking speed (1..60 m/s, default 2.5,
  a brisk walk; `--walk-speed` sets it; Shift runs at three times the walking
  speed). Diagonal movement is not faster. The feet follow
  the surface, so a flood rising under you lifts you with it; holes in the
  measured data and the edge of the terrain stop you instead of letting you
  fall through. A/D walk even with a scene file loaded: the node attach/detach
  keys are suspended in first person and work again in the world view.

The two views hand over to each other: entering first person stands you on the
ground under the world view's target, level with the horizon and looking the
way the orbit camera looked (if the target is over a hole the nearest ground
is used; with none in reach the switch is refused and the world view kept).
Returning puts the world view's target where you stood, behind the way you
were looking, at the orbit pitch and zoom you left with — so Tab, Tab without
moving restores the world view exactly. `--view first-person` starts on the
ground (combine with `--view-x`, `--view-z` and `--view-yaw` to pick the spot
and the heading); `CAMERA_JSON` reports every switch.

```bash
./build-gl/terraflux-terrain-viewer --view first-person --view-x -350 --view-z 250 --view-yaw 20   # in the lakeside forest
./build-gl/terraflux-terrain-viewer --view first-person --walk-speed 20 --weather cumulus
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --first-person-test --smoke-test
```

**Walking in steps.** The walker does not glide: it moves in steps. From rest
it accelerates (20 m/s²) into its first step; each step is
0.45 m + 0.22 m × speed long (1 m at the 2.5 m/s default walk, 2.1 m at the
7.5 m/s run, never shorter than a 4.5 steps/s cadence allows), and within a
step the ground speed pulses ±20 % — slowest as the swing foot lands (double
support), fastest as the stance foot pushes off mid‑step — so you feel where
every step begins and ends. The head rises over the stance leg (2–3 cm walking,
up to 7 cm running), shifts and rolls toward the stance foot (±1.5–4 cm,
±0.6°) alternating left and right as the feet alternate, and takes a small
jolt as each foot lands (harder at a run). Releasing the keys does not halt
you: the walker brakes uniformly so that it stops exactly on the next footfall
(or the one after when that would take more than 40 m/s² of braking), finishing
the step and planting the foot, and the head comes to rest exactly at eye
height. The step counter and the stance foot are tracked (`footfalls`,
`stance_foot`), so footstep sound or prints can be hung on them later. A jump
from a walk carries the gait's mean speed; landing starts the walk again from
rest. `--no-gait` restores the constant‑speed glide; `--walk-speed` sets the
pace (default 2.5 m/s, a brisk walk; Shift runs at three times that).

```bash
./build-gl/terraflux-terrain-viewer --view first-person                      # walk: feel the steps
./build-gl/terraflux-terrain-viewer --view first-person --walk-speed 1.4     # a stroll
./build-gl/terraflux-terrain-viewer --view first-person --no-gait            # the old glide
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --gait-test --smoke-test
```

`GaitStyle` in `camera_rig.hpp` holds the step, pulse, acceleration, braking,
bob, sway, roll and jolt constants (validated). `--gait-test` renders four
frames: standing (reference), mid‑step after two seconds of walking along the
most level heading clear of trunks (the walking speed reached, the per‑step
speed pulse, the footfall count and the bob/sway measured, the head up and to
the side, a different image), a second after releasing the keys (stopped on a
footfall within two step lengths, speed and phase zero, eye exactly at eye
height) and back at the start (image restored **exactly**).

**Collision.** Walking is solid: you step over grass and anything lower than
knee height (0.5 m above the feet — a stone, a tuft, a low ledge), but you
cannot walk through trees, imported assets or the mountainside. The walker is
a 0.35 m body at eye height; every placed tree is a vertical cylinder with the
footprint of its mesh at the base (the trunk of a procedural conifer or
broadleaf, the base of a custom OBJ) scaled by the tree's height, every
imported OBJ/glTF instance is the cylinder around its bounding sphere where it
is drawn this frame (so Spin/Motion objects move out of, or into, your way),
and ground that rises more steeply than `--max-slope` (default 45 degrees) is a
wall: cliffs, the escarpment's crag faces (which also carry the rock relief
allowance, so craggy rock blocks from about 30 degrees) and the steep flanks
of the ridge stop you, while soil slopes below the limit are climbed and any
slope can be descended. Motion is sub-stepped so nothing is tunnelled through
even at the running speed, you slide around trunks you hit off-centre and
along the foot of a cliff you walk into diagonally, holes in the data and the
terrain edge stop you, and entering first person among trees steps you clear
of the nearest trunk. `--no-collision` turns all of it off (a ghost walk for
inspection); `COLLISION_JSON` reports the trunk count and the body whenever
the vegetation is (re)placed.

```bash
./build-gl/terraflux-terrain-viewer --view first-person --max-slope 60       # a scrambler
./build-gl/terraflux-terrain-viewer --view first-person --no-collision       # walk through everything
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --collision-test --smoke-test
```

**Gravity and jumping.** The walker is under Earth gravity (9.81 m/s²,
`--gravity` for other worlds). Walking keeps you on the ground — steps up to
the step height and any downhill slope are walked — but stepping off a ledge
deeper than the step height starts a real fall: the feet leave the ground at
their height, keep the walking momentum, and drop with gravity (semi‑implicit
Euler at 120 Hz, terminal velocity 60 m/s) until they meet the support.
**Space** jumps (in first person; in the world view Space still pauses the
wildfire replay): a standing jump leaves the ground at √(2gh) and peaks at
`--jump-height` (default 1 m, 0 disables) after exactly the analytic air time
(0.90 s on Earth), a running jump carries the run through the air (about 2 m
at walking pace, 7 m at a run), and mid‑air steering is limited to 25 %/s of
the momentum, so you cannot turn round in flight. Landing dips the eye in
proportion to the impact speed (≈ 0.23 m from a 1 m jump, up to 0.5 m from a
fall) and the dip recovers exponentially over a few tenths of a second. While
airborne the feet are higher, so the same blocking rule lets you clear rocks
up to the jump height plus the step height, land on top of anything whose top
is within the step height of your feet (a wide rock, a ledge), and walk off it
again into a fall. Walls still stop you in the air (the momentum dies and you
drop), trunks are as solid as on the ground, and holes in the data are never
fallen into.

```bash
./build-gl/terraflux-terrain-viewer --view first-person --jump-height 2        # an athlete
./build-gl/terraflux-terrain-viewer --view first-person --gravity 1.62         # the Moon
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build-gl/terraflux-terrain-viewer --jump-test --smoke-test
```

`WalkerPhysics` in `camera_rig.hpp` holds the constants (gravity, jump
height, air control, terminal velocity, landing dip and recovery) so later
environment interaction shares the same gravity. `--jump-test` renders five
frames: standing (reference), the apex of a standing jump (feet at the jump
height ± 5 cm, a different image), the landing (back on the ground after the
analytic air time ± one tick, eye dipped), two seconds later (dip gone, image
restored **exactly**) and a running jump along the most level clear heading
(carry within 80..110 % of speed × air time).

The collision model lives in `terraflux/graphics/collision.hpp` (obstacles,
the walker body, a plan-space obstacle hash, tree and asset obstacle
builders, push-out) and the walker in `camera_rig.hpp` (`WalkEnvironment`,
`walk`, `settle`), both pure and covered by `terraflux-collision-tests`.
`--collision-test` runs on the real placement: it stands six metres from a
tree on gentle open ground and runs at it (must stop touching the trunk at body
radius), stands three metres below the terrain's steepest facet and walks up
(must not climb; skipped with a note on terrain with no facet steeper than the
limit), walks through a grass tuft in the open (must pass), and repeats the run
at the trunk with collision disabled (must pass through and render a different
image).

The camera rig lives in `terraflux/graphics/camera_rig.hpp` as pure functions
over an `OrbitCamera` and a `FirstPersonCamera` (directions, walking over a
standing-height function, look clamping, speed limits, ground placement and
the two transitions) with its own native test suite. `--first-person-test`
renders the world view with its target on the ground, enters first person and
checks the eye stands at eye height on the standing height directly under the
target with the same heading, that ground fills the bottom of the image, that a
quarter turn and a second of running change the image and move the feet by the
walked distance along the ground, and that returning restores the orbit camera
and the reference image exactly. It requires demo or replay smoke mode starting
in the world view with visible terrain and no assets or other scripted test;
tiled, resident and offscreen paths are allowed. In first-person frames a smoke
run does not require the fire to be visible (it may be behind you). Nothing
about the cameras feeds back into propagation or replay timing.

### Viewer controls

| Key | Action |
| --- | --- |
| Tab | Switch between the world view (orbit) and first person |
| Arrow keys / + / - | World view: orbit camera / zoom. First person: look around / walking speed |
| Mouse | First person: look around (cursor captured until Tab returns to the world view) |
| P | Pause/resume OBJ demo or scene Spin/Motion; camera and fire replay continue |
| N | Advance one 1/60-second scene tick and remain paused |
| [ / ] | Select the previous / next scene binding, including inactive nodes |
| D | Detach the selected scene node and evict its unused mesh resources |
| A | Activate the selected inactive node if residency budgets permit |
| U | Unload the imported asset/scene; retain terrain and wildfire playback |
| R | Reload the original asset/scene; failed loads preserve the current asset |
| Space | Pause/resume wildfire replay (world view; jumps in first person) |
| Home | Reset wildfire replay to its first frame and pause |
| Comma / period | Previous / next wildfire frame, pausing replay |
| V / B / G | Cycle vegetation density / tree meshes / grass distance |
| C / J / K / X / O | Cycle the cloud form / solar clock an hour back / forward / wind presets / toggle weather |
| W / A / S / D | World view: pan the camera target over the ground (A/D only without a scene file). First person: walk and strafe; W into a face too steep to walk climbs it if the surface allows |
| Shift | First person: run (trees, assets and slopes above `--max-slope` still stop you; `--no-collision` lifts that) |
| Ctrl | First person: crouch (hold) — lower, slower, under branches, a smaller target; afloat: dive |
| Shift (afloat) | First person: swim the crawl — faster, more tiring |
| Space | First person: jump (`--jump-height`, Earth gravity or `--gravity`), or let go of a rope; world view: pause/resume the wildfire replay |
| E | First person: use the tool in hand (throw the rope or take hold of it, chop with the axe, hold to saw with the chainsaw), else interact — pick up the stone or branch you look at, take a burning branch at your feet, break a branch off the tree beside you, light a branch at a fire, set a tree alight with the torch, or put down what you hold (the title says which) |
| Q | First person: throw what you hold (12 m/s along the look, lifted 12°); it lands as what it is |
| 1 / 2 / 3 | First person: take the rope / the fire axe / the chainsaw in hand (the same key again puts it away; 1 near a rope lying or hanging out in the world pulls it in). What you held moves to the left hand |
| W / S (on a rope) | Climb up / let yourself down; W at the top of a rope pegged at an edge steps off onto it; Space lets go |
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
textures count all generated mip levels at four (RGBA8) or eight (RGBA16) bytes per texel. Shared textures count once per
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

Current contract version: `terraflux.compute/v3`.

| Version | Status | Semantics |
| --- | --- | --- |
| `terraflux.compute/v1` | accepted (legacy) | fuel, moisture, susceptibility, wind. `elevation_meters` is accepted but **ignored**. |
| `terraflux.compute/v2` | accepted | v1 plus `cell_size_meters`, a terrain (slope) term, a signed and stronger wind term, and **meteorological wind direction** (blows *from*); see below. |
| `terraflux.compute/v3` | current | v2 plus `weather`: air temperature, relative humidity and rainfall, constant or per step, as a bounded additive term (heat dries the fuel, rain wets it); see below. |

A request is evaluated under the semantics of the version it declares and the
response echoes that version. Stored v1 requests therefore replay bit-for-bit
on both engines even if they carry elevation data; terrain is opt-in by sending
`v2`, weather by sending `v3` (a v2 request carrying a `weather` block ignores
it, and a v3 request without one reproduces v2 exactly). Requests declaring any
other version are rejected. The versions were bumped because `elevation_meters`
was already a legal v1 input and giving it an effect would otherwise have
silently changed v1 results, and because unknown keys are ignored by both
engines.

### Weather drivers: temperature, humidity and rain (v3)

```
weather = {"temperature_celsius": 26, "relative_humidity": 0.35, "precipitation_mm_h": 0}   # one sample
weather = [sample_step_0, sample_step_1, ...]                                              # per step; the last entry persists
```

```
heat    = clamp((T - 20) / 20, -1, 1)             # -1 at 0 C, 0 at 20 C, +1 at 40 C
dryness = 1 - relative_humidity
rain    = clamp(precipitation_mm_h / 10, 0, 1)     # saturates at 10 mm/h

weather = heat * (0.5 + 0.5 * dryness) * 0.10 - rain * 0.30
score   = ... + wind + uphill + downhill + weather   # appended last; exactly 0.0 without weather
```

- **Heat** shifts the propensity of fine fuel the way air temperature shifts
  its equilibrium moisture: a hot, dry afternoon adds up to +0.10 (comparable
  to a moderate tail wind), so marginal fuel burns that would not at 20 C;
  cold air subtracts up to 0.10. Humid heat dries less than dry heat.
- **Rain** wets the fuel within the step: 10 mm/h subtracts 0.30, more than
  any other single driver, so a downpour stops most running fires; only good
  fuel under a gale on a climb keeps creeping. Drizzle (1 mm/h) costs 0.03.
  The term is stateless per step; authors model persistence by keeping wet
  steps in the series (the last sample persists past its end).
- 20 C, 50 % humidity and no rain are neutral. Samples are validated to
  -60..60 C, 0..1 and 0..200 mm/h, series to 720 steps.

`examples/generate_wildfire_scenario.py` emits a v3 scenario by default: a
warm, fairly dry afternoon (26 C, 35 %) in which the fire runs, a thunderstorm
at step 30 (6 mm/h for ten steps, cooling the air by eight degrees) that slows
the front to a crawl, and cool humid air afterwards; `--storm-mm-h 14` stops it
outright, `--no-storm` keeps the weather constant, `--contract v2` drops the
term. The viewer mirrors the series while replaying (see the weather section).

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
OBJ, glTF, GLB, transparent materials and repeated model references. Native testing comprises 28
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
28 display-independent suites; the extra GL test runs explicitly under Xvfb.
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
to 19 suites. The viewer now has a first-person camera beside the orbiting
world view (Tab switches, W/A/S/D walk, Shift runs, arrows and mouse look),
standing at eye height on the ground, water and crag relief with the heading
handed over in both directions; walking collides with tree trunks, imported
assets and ground steeper than a slope limit while stepping over grass and
knee-high obstacles; the walker is under Earth gravity — Space jumps with the
analytic air time, running jumps carry momentum, ledges are fallen from and
landings dip the eye; walking is a gait of discrete steps with a per-step
speed pulse, head bob, sway, roll and footfall jolts that stops on a footfall;
the camera rig and the collision model are pure modules
with their own suites; a tree-level fire model makes every tree burn, drop
burning branches, char, topple (onto the ground or a neighbour), lie as a
burning log and light its neighbours, driven by the replay front and clock
(28 native CTest suites) with Mesa first-person, collision, jump, gait,
tree fire, storm, flood, climb, vitals, body, items and tools tests; the walker sees its
own arms and hands and swims in strokes, crawls and dives; it picks up stones
and branches, lights a torch and sets trees alight with it; it carries a rope
(down a cliff, up into a crown), a fire axe and a chainsaw that fell trees
away from it and cut branches in pieces, and a trunk that lands on it crushes it;
the walker has health, body
heat and wetness, crouches, slips on ice and is turned by crosswinds; storms reach the walker (gust drift,
windthrow, lightning that splits trees and strikes people down), storm
rain floods the drainage channels the walker wades, swims or is swept in,
and the walker climbs out over rock faces as steep as the surface allows.
Terrain renderer planning
estimate rises from ~92% to ~93%. Overall estimates remain ~93% desktop / ~64%
full roadmap; weighting remains undocumented.
See the [prioritized gaps and acceptance criteria](docs/ENGINE_BASELINE.md#remaining-renderer-work-in-priority-order). GPU meshes are created on demand; CPU model data
and decoded image recipes remain eager and retained for reattachment. Asynchronous streaming, skeletal animation
and full glTF/IBL support remain future work.
This desktop-engine breakdown is separate from the earlier simulation-focused
[220-hour planning backlog](docs/ROADMAP.md).

## License

MIT. See [LICENSE](LICENSE).

