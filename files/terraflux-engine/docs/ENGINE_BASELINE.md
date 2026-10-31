# Desktop engine baseline — 30 October 2026

## Pinned checkpoint

Implementation revision: [`9196da2632abb408d93d7842453d9dc1a8fc48df`](https://github.com/smatthias/terraflux-engine/commit/9196da2632abb408d93d7842453d9dc1a8fc48df).
This is the code checkpoint; later documentation commits do not change its scope.
[Validation run](https://github.com/smatthias/terraflux-engine/actions/runs/41087103164):
GCC/native/Python, Clang Debug/parity and OpenGL/Mesa jobs all succeeded.

| Evidence | Verified result |
| --- | --- |
| Native CTest | 15 registered suites; native test steps passed in GCC, Clang and OpenGL jobs |
| Python | Python test step passed; consult the run log for counts and skips |
| Native/Python simulation parity | Parity steps passed in GCC and Clang; README documents 71 fixtures |
| Desktop integration | Mesa/Xvfb rendering, materials, mixed assets, controls, residency, budgets and state restore |
| Physical GPU validation | Outstanding; no hardware-performance claim |
| PS4/PSSL | No implementation or platform validation |

This is an executable development baseline, not completion of the full engine
roadmap. Progress estimates remain approximately 93% for desktop Phases 1–9
and 64% including PS4. These are existing planning estimates, not measured
coverage or an arithmetic average of the phase percentages; weighting is not documented. See the [README status table](../README.md#current-development-status).

Previous checkpoint: `c64cb0470c28b2eb73dc81e0b01fedc8386676b8`, validated by
[run 41029361811](https://github.com/smatthias/terraflux-engine/actions/runs/41029361811).
The refreshed checkpoint includes the later renderer increments listed below.
Validation evidence here comes from the linked CI run, not a new local test run.

## Included behavior and boundaries

| Area | Baseline |
| --- | --- |
| Simulation | Deterministic wildfire grid propagation, versioned v1/v2 contracts, terrain/wind rules and replay parity |
| Desktop | OpenGL 4.3, terrain mesh, orbit camera and wildfire replay |
| Assets | OBJ/MTL, static glTF/GLB, images/samplers and the documented direct-light PBR subset |
| Scene composition | Versioned scenes, hierarchy, shared canonical models, authored glTF transforms and mixed-format references |
| Runtime | Fixed-step Spin/Motion, independent scene pause/step, selection, detach/reattach and synchronous GPU residency |
| Ownership | Independent geometry scopes, shared texture leases, staged replacement and cleanup on failure |
| Persistence | Versioned sidecar for selected/inactive bindings with authored-scene compatibility checks |
| Diagnostics | Mesh/instance admission, logical GPU bytes/peaks/rejections and retained CPU recipe payload limits |
| Shader contract | Explicit built-in shader identities and layout validation; broader shader modules remain future work |
| Render passes | Window/offscreen passes, RGBA8/Depth32Float targets, sampled color composition, resize recovery and exception-safe scopes |
| Terrain residency | CPU tiles, frustum culling, transactional GPU admission/eviction and bounded retention |
| Replay display | Independent terrain/fire layers and exact fixed-tick playback rates with bounded stepping |
| Performance foundation | Frustum culling, opaque instancing, streamed SSBO ring, CPU profiling and asynchronous GPU elapsed queries |

The public simulation remains a simplified threshold cellular automaton, not a
calibrated operational hazard forecast. GPU byte totals exclude driver overhead,
deferred orphan storage and pipeline/framebuffer/query allocations. CPU recipe
limits exclude parser peaks, container capacity and allocator metadata. Model
parsing/decoding and GPU reactivation are synchronous; retained CPU recipes are
eager. None of these counters measure physical VRAM or process RSS.

## Reproduce in an isolated worktree

From an existing repository checkout, create a separate worktree so current
uncommitted development stays separate from the pinned baseline:

```bash
git worktree add --detach ../terraflux-baseline 9196da2632abb408d93d7842453d9dc1a8fc48df
cd ../terraflux-baseline
```

Requirements: C++20, CMake 3.20+, Python 3.10+, libpng/libjpeg development
libraries; the desktop viewer additionally needs GLFW 3.3+, OpenGL 4.3 and a
display. On Ubuntu:

```bash
sudo apt-get install cmake g++ libpng-dev libjpeg-dev libglfw3-dev libgl-dev xvfb xauth
python3 -m venv .venv
. .venv/bin/activate
pip install -r intelligence/requirements.txt
cmake -S native-compute -B build-gl -DCMAKE_BUILD_TYPE=Release -DTERRAFLUX_WITH_OPENGL=ON
cmake --build build-gl --parallel 2
ctest --test-dir build-gl --output-on-failure
PYTHONPATH=intelligence python -m unittest discover -s intelligence/tests -v
PYTHONPATH=intelligence python intelligence/scripts/verify_native_parity.py \
  --runner build-gl/terraflux-compute-runner \
  --fixtures intelligence/tests/fixtures/grid_spread_cases.json
```

For a simulation/headless build, omit `-DTERRAFLUX_WITH_OPENGL=ON`; native
image libraries are still required. See the [CI workflow](../.github/workflows/ci.yml)
for the complete integration sequence, including scenario generation, GLB,
state save/restore, exact/over budgets and instancing comparisons.

## Minimum interactive review

```bash
./build-gl/terraflux-terrain-viewer --scene examples/mixed_asset_scene.json --residency-report
```

- Orbit/zoom and inspect the mixed OBJ/glTF/GLB scene.
- P pauses scene animation; N advances one tick; camera input remains responsive.
- Brackets select a binding. D detaches it; A restores it. Shared meshes remain
  valid and cold geometry returns after all bindings are detached.
- U unloads the imported scope; R reloads it while keeping terrain available.
- Repeat with a saved partial residency state and explicit limits using the
  [README commands](../README.md#persist-selection-and-inactive-bindings).

For software integration checks, prefix viewer commands with `xvfb-run -a`
and set `LIBGL_ALWAYS_SOFTWARE=1`. Useful standalone checks:

```bash
xvfb-run -a ./build-gl/terraflux-terrain-viewer --smoke-test --scene-controls-test \
  --scene examples/mixed_asset_scene.json
xvfb-run -a ./build-gl/terraflux-terrain-viewer --smoke-test --scene-residency-test \
  --scene examples/mixed_asset_scene.json
xvfb-run -a ./build-gl/terraflux-terrain-viewer --smoke-test --resource-recovery-test \
  --scene examples/mixed_asset_scene.json --residency-report
```

Mesa results verify functionality only. Repeat on physical GPU hardware,
recording the driver/GPU and commands/results before marking hardware validation
complete. Use the existing culling/instancing benchmark scripts for repeated
hardware measurements; do not infer performance from software-renderer timings.

## Remaining renderer work, in priority order

| Priority / phase | Remaining gap | Concrete next deliverable / acceptance |
| --- | --- | --- |
| P0 — 3/9 | Physical hardware evidence | Run smoke/material/residency checks and repeated benchmarks on a physical GPU; record GPU, driver, commands and results |
| P1 — 4 | Terrain LOD | [Design complete](TERRAIN_LOD.md); implement bounded CPU variants, measured error and invariant tests before viewer integration |
| P1 — 2/6 | Broader shader modules and attachment formats | Define one bounded extension with descriptor, lifetime and failure tests; retain existing built-ins |
| P1 — 5 | Replay UI composition | Specify the next scoped display control; preserve exact timing, layer isolation and fire-grid identity |
| P1 — 6/7 | Direct-light PBR subset | Scope IBL or shadows separately with reference fixtures; do not claim full glTF compatibility |
| P1 — 7 | Static assets only | Specify animation/skin/morph/extension support before implementation |
| P1 — 8/9 | Synchronous eager CPU loading | Define cancellation, ownership and thread handoff; keep GPU calls on the context thread |
| P1 — 8/9 | Retained CPU recipes | Define eviction and reload behavior with payload budgets and lifetime tests |
| P2 — 9 | General automatic admission/streaming | Define victim selection and per-frame upload budgets; retain transactional failure behavior |
| Separate — 10 | PS4 absent | Authorized platform toolchain/backend work and hardware validation |

Explicit shader identities, basic render-target/pass abstraction, offscreen
composition, terrain tiling/residency/retention, independent layer visibility and
fixed-tick replay controls are implemented at this checkpoint. They are no longer
listed as wholly missing. LOD and general asynchronous streaming remain open.

### Next ten steps

This is the proposed execution sequence, not ten equally sized completion units.
Physical GPU evidence requires hardware access; independent implementation work
can continue while that evidence is pending.

Completed: [physical-GPU validation checklist](GPU_VALIDATION.md), including logging,
acceptance criteria and repeated benchmark commands. Hardware execution remains pending.
Also completed: [terrain LOD specification](TERRAIN_LOD.md). ↑ Design milestone;
implementation percentages remain unchanged.

1. Execute hardware smoke checks and record GPU/driver/results.
2. Run the interactive hardware review, including resize and lifecycle controls.
3. Run repeated hardware benchmarks and record frame-time and resource behavior.
4. Implement overlay-based geometric error measurement; CPU simplification is complete.
5. Expand error, seam/hole and aggregate-budget invariants.
6. Integrate LOD selection with transactional terrain residency.
7. Verify LOD rendering, layer isolation and replay across direct/offscreen paths.
8. Specify cancellable CPU loading and context-thread upload ownership.
9. Implement a bounded loading increment with cancellation/lifetime tests.
10. Run regressions and refresh the checkpoint, completion table and remaining gaps.

Broader shader/material work, animation and PS4 remain in the full backlog above.
Each implementation step must be scoped against the actual code before editing.

## Continuing from the checkpoint

Preserve the MIT license and existing history. Make one bounded increment from
this existing baseline, document behavior/limits, and keep native/parity/Mesa
checks passing. The separate proprietary GeoSentinel3D application and its data
are outside this repository's scope. Main/PR merge decisions remain separate
from this checkpoint; no release or deployment is implied.


## Follow-up implementation

The branch now adds the CPU LOD level-0 foundation with exact geometry/source-map
preservation and a combined logical recipe-byte cap. It registers a sixteenth
native suite. The pinned checkpoint and its 15-suite evidence above remain
historical; they do not certify this later change. Simplification and GPU LOD
selection remain pending. Deterministic patch eligibility is now implemented,
with explicit fallback reasons. Opt-in CPU simplification now preserves every
patch boundary while reducing eligible patches from 32 to 16 triangles.
Both levels count toward element/byte caps; measured error and GPU integration remain pending. See [LOD implementation status](TERRAIN_LOD.md).

The terrain renderer increment after that adds height/slope shading: the mesh
generator records the elevation range and produces diagonal-independent smooth
normals with slope and summary helpers; a validated 48-byte `TerrainStyle`
uniform block shared by the terrain and wildfire shaders maps a hypsometric ramp
onto the surface's own range, blends steep ground toward rock, applies a
directional hillshade and optionally draws anti-aliased contours. The viewer
frames terrain from its bounding sphere, prints `TERRAIN_MESH_JSON`, writes
`--screenshot` frames and runs the Mesa `--terrain-shading-test`, which compares
rendered pixels with the CPU color reference. Native suites remain 16 (graphics
contracts grow to 23 tests and terrain mesh tests to 14). See
[terrain shading](GRAPHICS_BACKENDS.md#terrain-height-and-slope-shading).
Physical-GPU evidence is still pending for this increment as well.

The following increment turns the shading into materials: terrain vertices carry
fuel load, fuel moisture and curvature (`SURFACE`, stride 40); the shared ground
shaders classify dry/semi-dry/wet grass, forest, rock, scree on exposed ridges,
snow and ice from height, slope, convexity and fuel data, add distance-faded
procedural detail and automatic contours. `TerrainStyle` grows to 64 bytes with
snow line, ridge curvature and detail strength. The CPU material reference
(`terrain_material`/`dominant_material`) is verified per class by the Mesa
shading test; graphics contracts grow to 24 tests and terrain mesh tests to 15.
See [terrain materials](GRAPHICS_BACKENDS.md#terrain-materials-height-slope-vegetation-rock-snow-and-ice).

The water increment adds `build_water_mesh` (a level-clipped surface with per-vertex
bed depth and gradient), the `Water` shader identity with a validated 128-byte
`WaterStyle` block, depth-dependent color/opacity, Fresnel sky/sun reflection,
configurable shoaling/refracting/breaking waves with foam, and the Mesa
`--water-test`; native testing grows to a seventeenth suite (`terraflux-water-tests`).
Submerged ground renders as a configurable lake/sea floor (sand, aquatic
vegetation, mud, stones, depth/slope-zoned mixed or seeded random) seen through
the shallows; `TerrainStyle` grows to 96 bytes. Rock is flat-shaded from facet
normals, carved by ridged crag noise with hard strata ledges and displaced by a
configurable, visualization-only vertical relief (`--rock-relief`, tile culling
bounds padded accordingly). Rendering uses a C1 bicubic-Hermite refinement of
the untouched simulation lattice (`refine_terrain_mesh`, default 5 m, vertex
budget 4M) so relief and shorelines resolve below the simulation cell; terrain
mesh tests grow to 16. Trees and grass are instanced 3D meshes (procedural or
OBJ, `vegetation.hpp`) placed from the fuel data with per-plant variation, wind
sway and run-time switchable meshes/densities; an eighteenth suite
(`terraflux-vegetation-tests`) and the Mesa `--vegetation-test` cover them. Fire
is visualised as fuel-gated 3D flames and smoke with crown fires and charred
plants (`fire_effects.hpp`, a nineteenth suite and the Mesa `--fire-test`). See
[the water surface contract](GRAPHICS_BACKENDS.md#water-surface).

## Implementation references

- [Shader identities, render passes, offscreen targets and replay clock](GRAPHICS_BACKENDS.md)
- [Terrain tiling, residency and retention](TERRAIN_TILES.md)
- [Scenes and asset boundaries](SCENE_ASSETS.md)
- [Current phase completion table](../README.md#current-development-status)

The checkpoint includes the DEM import scripts in `examples/`; their presence
does not establish physical-GPU validation or LOD support.
