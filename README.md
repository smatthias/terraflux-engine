# TerraFlux Engine

An open-source spatial simulation engine for dynamic environments,
built with C++20 and Python.

TerraFlux provides a deterministic grid-based simulation foundation
for environmental modeling, spatial computation, and future digital
twin applications.

## Verified engine checkpoint

The [desktop baseline checkpoint](docs/ENGINE_BASELINE.md) pins implementation
revision `c64cb0470c28b2eb73dc81e0b01fedc8386676b8`, records its validation,
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

Diagnostics add `buffer_bytes`, `texture_bytes`, `total_resource_bytes` and
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

### Explicit pipeline shader identities

`PipelineDesc` now starts with a required `ShaderProgramId`, followed by layout
and depth/blend state. Built-in identities cover terrain, wildfire, untextured
objects, textured objects, instanced textured objects and both PBR variants.
Layout metadata is validated against the selected contract; binding names do
not select or redirect shader source. Missing/unknown IDs and mismatched layouts
reject before backend compilation. `ContractOnly` exists for CPU validation tests
and cannot render through OpenGL. See the [pipeline contract](docs/GRAPHICS_BACKENDS.md#explicit-shader-program-identities)
for migration and remaining module/render-pass work.

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
hours or release-readiness guarantees. The PS4 backend is unimplemented.

| Desktop engine — Phases 1–9 | Full roadmap including PS4 |
| --- | --- |
| **~85%** (previously ~84%) | **~56%** (previously ~55%) |

| Phase | Component | Estimated completion |
| --- | --- | ---: |
| 1 | Core extraction | 100% |
| 2 | Rendering abstraction | ~78% (previously ~72%) |
| 3 | OpenGL backend | 85% |
| 4 | 3D terrain renderer | 75% |
| 5 | Simulation visualization | 65% |
| 6 | Shader system | ~80% (previously 76%) |
| 7 | Scene and asset system | ~90% (previously 86%) |
| 8 | Engine runtime | ~86% |
| 9 | Performance | ~75% (previously ~70%) |
| 10 | PS4 backend | 0% |

Latest milestones: selective node reattachment and on-demand GPU mesh residency,
scene-node detachment and geometry eviction,
shared GPU texture leases, reusable CPU material/texture
resolution, serializable Motion,
shared mixed-format scene assets, isolated
GPU asset lifetimes, unload/reload with failure recovery, and independent
scene pause/resume and single-step controls. The mixed-format example includes
OBJ, glTF, GLB, transparent materials and repeated model references. Native testing comprises 14
CTest suites and 71 simulation parity fixtures; CI also exercises the viewer
under Mesa/Xvfb. See the branch's CI results for validation of each commit.

Latest abstraction increment: backend-neutral window render-target/pass descriptions,
explicit begin/end ordering and independent color/depth clear or load operations.
Terrain and object rendering now use separate passes; Mesa smoke tests verify
that a load pass preserves terrain color. The target is borrowed from the window;
it creates no engine resource allocation and presentation remains GLFW-owned.
Native tests validate descriptors and reject nested passes and draws outside a pass.
The earlier explicit shader/pipeline identities retain shared layout validation.
Next planned implementation: owned offscreen color/depth attachments and framebuffer lifetimes.
See the [prioritized gaps and acceptance criteria](docs/ENGINE_BASELINE.md#remaining-renderer-work-in-priority-order). GPU meshes are created on demand; CPU model data
and decoded image recipes remain eager and retained for reattachment. Asynchronous streaming, skeletal animation
and full glTF/IBL support remain future work.
This desktop-engine breakdown is separate from the earlier simulation-focused
[220-hour planning backlog](docs/ROADMAP.md).

## License

MIT. See [LICENSE](LICENSE).

