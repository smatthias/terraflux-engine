# Desktop engine baseline — 30 October 2026

## Pinned checkpoint

Implementation revision: [`c64cb0470c28b2eb73dc81e0b01fedc8386676b8`](https://github.com/smatthias/terraflux-engine/commit/c64cb0470c28b2eb73dc81e0b01fedc8386676b8).
This is the code checkpoint; later documentation commits do not change its scope.
[Validation run](https://github.com/smatthias/terraflux-engine/actions/runs/41029361811):
GCC/native/Python, Clang Debug/parity and OpenGL/Mesa jobs all succeeded.

| Evidence | Verified result |
| --- | --- |
| Native CTest | 14/14 suites passed |
| Python | 113 tests run; test-suite skips are reported in the job log |
| Native/Python simulation parity | 71/71 fixtures passed |
| Desktop integration | Mesa/Xvfb rendering, materials, mixed assets, controls, residency, budgets and state restore |
| Physical GPU validation | Outstanding; no hardware-performance claim |
| PS4/PSSL | No implementation or platform validation |

This is an executable development baseline, not completion of the full engine
roadmap. Progress estimates remain approximately 83% for desktop Phases 1–9
and 54% including PS4. See the [README status table](../README.md#current-development-status).

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
git worktree add --detach ../terraflux-baseline c64cb0470c28b2eb73dc81e0b01fedc8386676b8
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

| Priority / phase | Gap | Concrete next deliverable / acceptance |
| --- | --- | --- |
| P0 — 2/6 | Shader selection is tied to built-in layouts/GLSL in the OpenGL implementation | Explicit backend-neutral pipeline shader identity; migrate existing built-ins while preserving reflection, materials and all CI paths |
| P0 — 3/9 | Physical hardware evidence is missing | Run smoke/material/residency checks and repeated benchmarks on a physical GPU; record driver and results without replacing functional CI |
| P1 — 2 | No general render-target/pass abstraction | Declare color/depth attachments and pass state with validating-backend tests before adding offscreen effects |
| P1 — 4 | Static terrain/dense fire-field limits | Tile/LOD design with bounded residency and seam/bounds tests; preserve simulation outputs |
| P1 — 5 | Replay visualization has limited layer/UI composition | Reusable layer controls and deterministic replay display tests; preserve fire-grid identity |
| P1 — 6/7 | Direct-light PBR subset | Scoped IBL/shadow/material features with independent reference fixtures; do not claim full glTF compatibility |
| P1 — 7 | Static assets only | Specify animation/skin/morph/extension support explicitly before implementation; retain clear importer boundaries |
| P1 — 8/9 | Synchronous eager CPU work | Cancellable loading and CPU recipe eviction strategy with thread/lifetime tests; keep GPU calls on the context thread |
| P2 — 9 | Admission is caller-driven | Define victim selection/streaming policy and frame/resource budgets before automatic eviction |
| Separate — 10 | PS4 absent | Authorized platform toolchain/backend work and hardware validation; desktop reflection is not PSSL certification |

These are engineering follow-ups, not promises of hours or verified completion.
The next implementation increment should address the P0 shader/pipeline identity
boundary rather than expand the baseline's scene-management feature set.

## Continuing from the checkpoint

Preserve the MIT license and existing history. Make one bounded increment from
this existing baseline, document behavior/limits, and keep native/parity/Mesa
checks passing. The separate proprietary GeoSentinel3D application and its data
are outside this repository's scope. Main/PR merge decisions remain separate
from this checkpoint; no release or deployment is implied.


## Follow-up after the pinned checkpoint

The feature branch now implements the P0 explicit shader/pipeline identity
increment. The pinned code above intentionally remains unchanged as a reproducible
baseline. See [Explicit shader program identities](GRAPHICS_BACKENDS.md#explicit-shader-program-identities)
for the migrated descriptor contract. A subsequent increment adds explicit window
render-target/pass descriptions, independent color/depth clear/load operations,
and begin/end validation. See [Window render targets and render passes](GRAPHICS_BACKENDS.md#window-render-targets-and-render-passes).
The feature branch also adds owned RGBA8/Depth32Float offscreen targets, shared
budget accounting, framebuffer completeness checks and explicit lifetimes, with
a dedicated Mesa pixel/depth test. See [Owned offscreen render targets](GRAPHICS_BACKENDS.md#owned-offscreen-render-targets).
Borrowed sampled color and a reusable explicit-shader fullscreen composition pass
are also implemented. See [Sampled target color and fullscreen composition](GRAPHICS_BACKENDS.md#sampled-target-color-and-fullscreen-composition).
The next integration is opt-in viewer composition with resize recovery. The pinned
P1 gap still includes broader attachment formats and effects. Physical GPU evidence
is still outstanding.
