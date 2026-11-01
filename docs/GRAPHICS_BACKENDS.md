# TerraFlux graphics backend contract

## Boundary

The simulation/grid/terrain mesh remains in `terraflux_compute`. The graphics
abstraction is implemented in the independent `terraflux_graphics` static
library. Its public API uses no OpenGL, windowing-library or PlayStation SDK
types. The implementation includes a CPU validating device and an **optional desktop
OpenGL 4.3 terrain backend**. The platform API stays outside the shared header.

## Coordinate and data formats

The 3D scene uses +X east, +Y up, -Z north. Terrain geometry is generated in
double precision, then deliberately packed into interleaved `float32`
position/normal attributes, with `uint32` CCW triangle indices. The renderer
must respect the local terrain-grid origin and the existing `cell_size_meters`.

The current terrain upload layout is:
- `POSITION`, location 0, three floats at byte offset 0
- `NORMAL`, location 1, three floats at byte offset 12
- `FIRE_STATE`, location 2, one float at byte offset 24 (legacy terrain input)
- `SURFACE`, location 3, three floats at byte offset 28: fuel load, fuel moisture and
  vertex curvature (crests positive), copied from the simulation grid for material shading
- stride 40 bytes; wildfire rendering uses the same upload stride but reads fire state from an SSBO
- `Camera` uniform buffer at vertex-stage binding slot 0
- `TerrainStyle` uniform buffer at fragment-stage binding slot 11 (terrain and wildfire ground shading)

Buffers are initialized fully at creation; subranges can subsequently be updated,
for example to animate the camera uniform buffer. Index draws currently use unsigned 32-bit indices and triangles.

## Cross-platform shader metadata

`ShaderLayout` is a declared contract, **not actual compiled shader
reflection**. Backend implementations must extract compiled GLSL or PSSL
resource metadata and call `validate_shader_reflection(expected, actual)`.
The comparison is order independent but checks names, locations, data formats,
offsets, resource kinds, slots, and stage visibility.

The OpenGL implementation compiles its initial built-in terrain GLSL 4.30
vertex/fragment shaders, reflects active vertex inputs and uniform blocks
through GL_PROGRAM_INPUT / GL_UNIFORM_BLOCK, and compares them with
ShaderLayout. Vertex stride and offsets are *not* properties that OpenGL
shader reflection exposes: they are validated from the declared layout and
configured in the vertex array. The backend also supports object and PBR pipelines, texture sampling
(`TextureDesc::format` selects four 8-bit channels, the default, or four 16-bit little-endian
channels — `TextureFormat::Rgba16`, eight bytes per texel in the byte accounting and the
validator's pixel-size check — for fields whose interpolated values steer shading frames),
material uniforms, storage buffers, transparency and indexed instancing.
Shader source selection now uses explicit engine program IDs. Built-in GLSL
remains embedded in the backend; external shader-module support is still pending.

The GLFW demo creates a procedural elevation grid using the existing compute
types, builds the shared TerrainMesh, uploads GPU buffers and renders the mesh
with an orbit camera. The viewer also imports saved wildfire request/response frames, composes static
OBJ/glTF/GLB assets and supports scene/runtime controls. See the pinned
[engine baseline](ENGINE_BASELINE.md) for current evidence and outstanding gaps.
Actual PS4 GNM/GNMX and
PSSL support depends on authorized Sony SDK access and hardware validation.
Do not assume that validating GLSL here proves PSSL compatibility.

## Verification

Both GCC Release and Clang Debug CI build the graphics library and exercise
the CTest graphics-contract test executable. Simulation parity must remain
unchanged. The validating device checks basic resource lifetime, bound vertex
and index buffer sizes, triangle draw ranges, and referenced vertex indices,
but it does not render pixels.

## Desktop build and run (Linux)

Install OpenGL development headers and GLFW 3.3+ (for example, on Ubuntu:
`sudo apt install libglfw3-dev libgl-dev libpng-dev libjpeg-dev`). A GPU/driver supporting OpenGL 4.3
is required (Mesa software rendering can be used for CI).

```bash
cmake -S native-compute -B build-gl -DCMAKE_BUILD_TYPE=Release -DTERRAFLUX_WITH_OPENGL=ON
cmake --build build-gl --parallel 2
./build-gl/terraflux-terrain-viewer
```

Arrow keys orbit the camera, + and - zoom, Escape exits. Run
`xvfb-run -a ./build-gl/terraflux-terrain-viewer --smoke-test` to compile,
link, reflect, upload and render one frame headlessly on a suitable Mesa image.

Building without `TERRAFLUX_WITH_OPENGL=ON` requires no OpenGL or GLFW and
continues to run the existing simulation and parity tests. PS4/PSSL code is
not included and cannot be certified by desktop OpenGL tests.

## Wildfire playback (native compute output)

The viewer can now replay the **unchanged** `terraflux.compute/v1` or `v2`
JSON request and response produced by the existing native runner. It validates
grid identity and contract version, steps and coordinates before rendering.
Active fire appears orange, previously burned terrain charcoal, and unburned
terrain retains slope lighting. The baseline demo without arguments still works.

```bash
python3 examples/generate_wildfire_scenario.py > /tmp/terraflux-request.json
./build-gl/terraflux-compute-runner < /tmp/terraflux-request.json > /tmp/terraflux-response.json
./build-gl/terraflux-terrain-viewer --request /tmp/terraflux-request.json --response /tmp/terraflux-response.json
```

Controls: Space pauses/resumes; Home restarts at the first frame while paused;
comma/period step backward/forward; arrow keys orbit; +/- zoom; Escape exits.
The window title reports current step, actively burning cell count and
cumulative burned count. `--smoke-test` renders one frame and exits.

Unknown terrain elevations are interpolated for visualization where possible;
remaining unmeasured burnable cells use a flat 0m visualization fallback,
without changing the compute engine's input or responses. Zero-fuel cells with
unknown elevation remain unrendered. This initial implementation updates the
whole vertex buffer when stepping frames; large-grid optimization is future
work. Frame JSON can itself be large at long durations because each step lists
cumulative burned cells. Playback currently loads the complete JSON in memory.

**Note:** This is a deterministic visualization of the existing simplified
cellular automaton, not a calibrated wildfire forecast. PS4/PSSL remains
separate, subject to authorized platform tooling.

## Rounded wildfire fronts and terrain bounds

Fire visualization now uses a separate `FireField` grid packed into an OpenGL
4.3 shader storage buffer (SSBO), instead of interpolated fire-state vertex
colors. Each fragment checks a 3x3 neighborhood of cell centers to render
rounded, blended active-fire and burned footprints. Tiny edge variation and
color pulsation soften the cellular appearance while preserving **exact
simulation frames and coordinates**. Holes are marked absent and out-of-grid
queries are rejected on the CPU; the shader bounds-checks all lookups.
The rasterized fire surface cannot extend beyond the terrain geometry.

Only the storage buffer is updated for each replay step. The geometry vertex
and index buffers remain static; shader-lit terrain color is unchanged.
No visual smoothing modifies the scientific/simulation fire spread model.

The GPU field uses a bounded dense rectangle of at most 4 million cells; very
wide sparse grids should later be tiled rather than allocating unbounded GPU
buffers. The OpenGL backend reflects `GL_SHADER_STORAGE_BLOCK` alongside
vertex attributes and uniform blocks. This API remains a desktop GPU backend;
the PS4/PSSL shader can later consume the same declared `FireState` layout.


## Explicit shader program identities

`ShaderProgramId` is a backend-neutral engine identity, not a GL program handle.
`PipelineDesc` declares it explicitly before the shader layout and depth/blend
state. The built-in contract registry maps each identity to its expected layout;
the OpenGL backend maps the same identity to the appropriate GLSL stages.
No resource-name scan chooses a shader family. Compiled-driver reflection still
checks the resulting program against the declared layout.

```cpp
using namespace terraflux::graphics;
auto pipeline = device.create_pipeline({
    ShaderProgramId::Terrain, terrain_shader_layout(), true});
```

| Identity | Contract |
| --- | --- |
| Terrain | Terrain vertex layout, camera, TerrainStyle and Atmosphere uniforms, OrographicField sampler |
| Wildfire | Terrain upload stride, camera, FireState storage, LayerVisibility, TerrainStyle and Atmosphere uniforms, OrographicField sampler |
| Object | Local untextured geometry and object uniform |
| TexturedObject | Textured local geometry, object uniform and albedo sampler |
| InstancedTexturedObject | Textured geometry with InstanceData storage |
| PbrObject | Textured per-object PBR material/resources; optional sorted alpha blend |
| PbrInstanced | Opaque instanced PBR material/resources |
| Composite | Fullscreen POSITION/TEXCOORD geometry and SourceColor sampler; depth/blend disabled |
| Water | POSITION/BED/BED_GRADIENT surface, camera, WaterStyle and Atmosphere uniforms, OrographicField sampler; alpha blend, depth test, no depth write |
| Vegetation | POSITION/NORMAL/COLOR/FLEX/TINT_WEIGHT plant meshes, camera, VegetationStyle and Atmosphere uniforms, OrographicField sampler, 48-byte InstanceData records and PlantFireState storage |
| Fire | POSITION/TEXCOORD billboard quad, camera, FireStyle and Atmosphere uniforms, OrographicField sampler, 48-byte InstanceData records; alpha blend, depth test, no depth write |
| Sky | Fullscreen POSITION triangle, Atmosphere uniform and OrographicField sampler; depth test at the far depth, no depth write, no blend |
| Precipitation | CORNER/PARTICLE streak quads, camera and Atmosphere uniforms (both stages), OrographicField sampler; alpha blend, depth test, no depth write |
| Lightning | POSITION/GLOW channel strips, camera and Atmosphere (fragment) uniforms; alpha blend, depth test, no depth write |
| Spray | CORNER/PARTICLE/ORIGIN/FLOW/FACE/SHAPE drop quads (64-byte vertices), camera and Atmosphere uniforms (both stages), OrographicField sampler; alpha blend, depth test, no depth write |
| ContractOnly | CPU validation of custom layouts; rejected by GPU creation |

Unspecified and unknown IDs reject. A declared built-in must match its full
layout (stride, attribute format/offset/location and binding name/kind/slot/
stages), independent of declaration order. Transparent blending requires
PbrObject, Water, Fire, Precipitation, Lightning or Spray with depth testing enabled
and depth writes disabled (Water, Fire, Precipitation, Lightning and Spray additionally
require blending; Sky must neither blend nor write depth); transparent
instancing remains unsupported. Both devices use the shared descriptor validator
before allocating a pipeline handle; OpenGL also rejects ContractOnly before
compilation. Existing callers must supply an identity when constructing a
PipelineDesc; there is deliberately no automatic layout-based fallback.

This increment replaces the checkpoint's implicit shader-selection gap. It does
not add runtime shader compilation inputs, file hot reload, arbitrary shader
modules, offscreen render targets or PS4/PSSL support. Native tests cover all built-in
identities, invalid/missing IDs, layout mismatch, metadata ordering and contract-only
boundaries; existing Mesa paths exercise actual compile/link/reflection and draws.


## Window render targets and render passes

`RenderDevice::begin_render_pass(RenderPassDesc)` and `end_render_pass()` now
bound every draw submission. `RenderTargetDesc` identifies a borrowed window
framebuffer and its pixel width/height. It owns no storage; the caller supplies
the current actual framebuffer extent, skips minimized/zero-sized frames, and
presents through the platform after ending all passes. Resizes are reflected in
the next pass descriptor. The window context must provide color and depth buffers.

`AttachmentLoadOp::Clear` and `Load` independently control color and depth.
Clear uses normalized finite RGBA/depth values (depth defaults to 1); Load keeps
the attachment contents from earlier work in the same framebuffer. Every pass
preserves its results at end. Loading a newly created or swapped window buffer
has no defined initial contents; begin the frame with Clear. No discard/store
selection, subpasses, stencil, MSAA or custom viewport are exposed. The later
offscreen increment below adds owned attachment handles.

Both backends reject invalid target kinds, zero or greater-than-INT32_MAX extents,
unknown load operations, invalid clear values, nested begins, unmatched ends and
draws outside an active pass. Descriptor/order validation happens before OpenGL
state changes; rejected begins leave the previous pass state intact. Resource
creation, updates and destruction remain legal between or within passes; the
existing resource/draw validation rules still apply.

For window targets the OpenGL backend binds framebuffer zero, sets the full-target viewport, applies
only the requested clears, and restores writable depth before a depth clear,
keeping its depth-write cache synchronized after transparent rendering. It owns
GL state within the viewer context. End does not flush, swap or allocate. The
old OpenGL-only `clear_frame()` entry point is removed; callers now construct a
pass descriptor. GPU timing remains an independent diagnostic around the passes.

The viewer clears/draws terrain in the first pass, then loads both attachments
for opaque and transparent objects in the second pass. PBR/lifecycle smoke paths
compare terrain pixels before and immediately after the Load begin, in addition
to their existing final rendering checks. Native tests cover pass ordering,
recovery after rejected begins, every clear/load combination, extent/value
validation and unchanged resource bytes. The CPU backend validates contracts;
it does not emulate attachment pixels.

Owned offscreen targets are described below. Physical GPU validation and PS4/PSSL
remain outstanding.


## Owned offscreen render targets

`create_render_target(OffscreenTargetDesc)` creates immutable, single-sample
RGBA8 color and Depth32Float depth attachments as one owned resource. Both are
required. The initial contents are undefined: clear before loading. Allocation
accepts positive extents no larger than 4096 per axis and 4,000,000 pixels,
with an additional OpenGL device-limit check. Unsupported format enums reject.
There are no stencil, HDR, mipmaps or independently shared attachments yet.

```cpp
RenderResourceOwner resources(device);
const auto target=resources.create_render_target({640,480});
device.begin_render_pass({{640,480,RenderTargetKind::Offscreen,target}});
// Submit ordinary indexed draws here.
device.end_render_pass();
// End all passes before the owning scope is reset or destroyed.
```

A window pass carries no owned handle. An offscreen pass requires a live
RenderTargetHandle and dimensions exactly matching the allocation. Invalid
handles or extents reject before changing pass state or GL bindings. Destroying
the active target rejects; end the pass first. Creation of another target during
a pass restores the previous read/draw framebuffer, renderbuffer and active-unit
texture bindings.
Pass end preserves the attachment storage and leaves it bound for GL diagnostic
readback. A later window pass explicitly rebinds framebuffer zero. Destruction
of an inactive bound framebuffer follows OpenGL's unbinding behavior.

OpenGL owns one framebuffer, an RGBA8 color texture and a Depth32Float
renderbuffer per target. Storage allocation
checks GL error status and framebuffer completeness before publication. Failure
restores bindings, deletes partial objects and removes the validating mirror's
live allocation. RenderResourceOwner tracks targets for reverse-order cleanup;
the device also deletes any remaining target objects before context destruction.
Color sampling is available through the borrowed handle described below; depth
remains a renderbuffer and cannot be sampled.

`ResourceBytes::render_target_bytes` accounts for width × height × 8 bytes,
separately from independently owned sampled texture bytes. Color views appear in
live texture counts but do not charge texture_bytes again. Storage contributes to total bytes, shared
budget admission, category/total peaks, allocation count and headroom. One target
is one logical admission. OpenGL's mirror admits before driver allocation, so
failed driver creation may remain in admission/peak history even though current
bytes return to baseline. These numbers exclude driver overhead and do not
measure physical VRAM. Window framebuffer storage remains outside engine bytes.
Viewer RESIDENCY_JSON adds live_render_targets, render_target_bytes and
peak_render_target_bytes; total_resource_bytes includes the new category.

Resizing means constructing a new target before releasing the old one. The
budget must admit both while both are live; a failed candidate preserves the
original. Target formats/extents cannot be mutated in place.

Native graphics tests cover formats/extents, exact accounting, cap rejection,
invalid and stale handles, active-destruction rejection and reallocation. Ownership
tests cover failed construction and idempotent reset. Build the extra display test
with `-DTERRAFLUX_WITH_OPENGL=ON` and `BUILD_TESTING=ON`, then run:

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ./build-gl/terraflux-offscreen-tests
```

This test is separate from CTest so the 15 native suites remain display-independent.
It reads back color and float depth, checks independent target storage, color-only
and depth-only clears, preserved depth occlusion, actual triangle drawing,
creation while a pass is active, window restoration, budget rejection and repeated
replacement/cleanup. The existing viewer continues to render directly to the
window. Physical GPU testing
is still required before hardware performance conclusions.


## Sampled target color and fullscreen composition

`render_target_color(RenderTargetHandle)` returns a stable, borrowed TextureHandle
for its RGBA8 color attachment. It accepts only a live target. Destruction of the
target invalidates the view; calling destroy_texture on a live borrowed view
rejects. Keep the owner alive for every draw that samples it. No ownership is
transferred, no allocation occurs on retrieval, and the same width × height × 8
budget charge continues to cover color and depth. Never put the borrowed handle
in an independent owning texture cache.

The color texture has one mip level, nearest min/mag filtering, and clamp-to-edge
wrapping. Its initial contents are undefined until cleared or rendered. A later
pass may sample it as a normal texture binding. Sampling the active destination's
color is rejected before the backend submits a draw, for every shader family;
use two targets for effects that read their previous output. Sequential OpenGL
framebuffer writes followed by draws sampling a different target need no CPU
readback or explicit finish. Cross-context synchronization is not supported.

`ShaderProgramId::Composite` is an explicit built-in program, with a 16-byte
vertex stride: POSITION float2 at location 0/offset 0, TEXCOORD float2 at location
1/offset 8, and SourceColor sampler at fragment slot 0. The pipeline requires
depth testing, depth writes and blending all disabled. GLSL reflection validates
this contract just like the other built-ins.

`FullscreenComposite` owns one reusable pipeline and a fullscreen indexed quad.
It constructs transactionally through RenderResourceOwner and submits one draw
inside the caller's active destination pass:

```cpp
FullscreenComposite copy(device); // Reuse across frames.
// Render scene into source_target, then end that pass.
device.begin_render_pass({{window_width,window_height}});
copy.draw(device.render_target_color(source_target));
device.end_render_pass();
// Platform presentation follows here.
```

The helper copies RGBA without blending, tone mapping, gamma conversion or a
vertical flip. Destination dimensions may differ; target color views use nearest
scaling. Destination depth is left intact by the draw (the pass descriptor still
controls its clear/load operation). It does not copy depth or present the window.
Color texture creation restores the previous texture binding and invalidates the
backend's texture binding cache; destruction likewise clears that cache. Ordinary
geometry after composition restores its own cached depth, pipeline and vertex state.

Native tests cover stable view retrieval, single charging, feedback rejection,
stale views, separate-destruction rejection, state contracts and failed helper
construction. The explicit Mesa/Xvfb test additionally checks an asymmetric
rendered image copied between targets, exact RGBA equality, nearest enlargement
into the window, unchanged destination depth and geometry drawn after the copy.
The viewer integration described below adds opt-in rendering through this path.
HDR, sampled depth and general post-processing effects remain future work.


## Viewer offscreen mode and transactional resizing

The viewer's `--offscreen` flag uses a persistent FullscreenComposite and a
ResizableRenderTarget. Terrain and object passes share the offscreen attachments;
a third pass copies color to the current window framebuffer before presentation.
Camera aspect and scene diagnostics use the actual scene-target dimensions,
including when the last successful target is retained across a window resize.
Window depth is cleared for the copy and is not populated with scene geometry.
The direct path remains the default.

ResizableRenderTarget::resize returns false for zero/minimized or unchanged
extents. A different positive extent allocates its candidate first, retires the
old target only after candidate creation succeeds, then publishes the new extent.
A failed admission, unsupported extent or active-target retirement preserves the
old allocation and destroys the candidate. Reset is explicit/idempotent and the
destructor provides best-effort cleanup. Resize/reset must run between passes.
Destroying the owning helper during an active pass remains a caller error;
exception-safe pass scopes below provide the intended ownership order.

The viewer catches resize failures before drawing. It retains and scales the old
target, or draws directly if there is no allocation yet. It retries after changes
to requested extent, live bytes or effective cap, rather than emitting the same
failed request every stable frame. A transient driver failure with none of those
changes therefore requires an extent/resource/cap change to retry. Minimized
frames skip presentation and retain target storage.

The shared budget includes the helper's 88 geometry-buffer bytes and target
attachments, plus assets and terrain. Resize needs old plus new attachment
storage simultaneously. Asset-unload validation computes the current viewer
baseline including the compositor and any live target. OFFSCREEN_JSON emits
successful allocation/recovery events when diagnostics/smoke are enabled, and
failure events always, with retained width/height, resize_failures and fallback.
RESIDENCY_JSON continues to include current target bytes/counts and peaks.

Composition adds one draw and is included in CPU/GPU render measurements.
BENCHMARK_JSON.offscreen records the requested mode; inspect fallback diagnostics
before comparing measurements. Native tests cover no-op/minimized extents,
failed admission, preserved handles/bytes, active-pass rejection, recovery,
unsupported dimensions, reset and destruction. Smoke mode compares scene pixels
against nearest-scaled window pixels; ordinary rendering has no such readback.

The dedicated four-frame viewer test uses actual GLFW window resizes under
Mesa/Xvfb, forces a cap rejection, verifies old-target retention, raises the cap,
recovers at the same requested size, resizes again and checks cleanup. It turns
off instancing to avoid unrelated ring allocations during the admission check.
CI separately exercises first-allocation fallback, mixed-format asset lifecycle
and wildfire replay through the offscreen path. Display-independent CTest now
has 15 suites. Physical GPU validation remains outstanding.


## Exception-safe render-pass scopes

Include `terraflux/graphics/render_pass.hpp` for RenderPassScope. Its constructor
begins a pass; failed begins acquire no ownership. Declare it after every resource
owner needed by the pass. Its nonthrowing destructor ends the owned active pass,
so stack unwinding closes it before target, texture or geometry storage is released.
The device and GL context must outlive the guard. Copying and moving are disabled.

`end()` supports early closure and is idempotent after success. It propagates an
end failure and keeps the guard armed for destructor cleanup. Destruction catches
cleanup failures to preserve the original exception; this is best-effort cleanup,
not recovery of a lost device/context. Existing backends end a valid owned pass
without issuing fallible driver commands. They retain normal nesting and draw
validation rules.

RenderDevice::active_render_pass_id() is a noexcept query: zero means inactive,
and each successful begin receives a nonzero, monotonically increasing identity.
Invalid descriptors or nested begins leave identity/state unchanged. ID exhaustion
rejects a new begin rather than recycling identity. The validating backend owns
this state; OpenGL delegates to its validating mirror.

The guard checks identity before closure. If raw calls ended/replaced its pass,
`active()` returns false, explicit `end()` reports the mismatch, and destruction
leaves any newer pass alone. Mixing raw calls with guards is therefore detectable;
ordinary callers should let the guard manage its pass. A successfully closed guard
also cannot interfere with a later pass in the same lexical scope.

All viewer terrain, object and composite passes use RenderPassScope, retaining
explicit close points for pass boundaries and readback. Rendering exceptions
propagate to the existing top-level error handler after pass/resource unwinding.
They are not silently retried. Native tests cover normal implicit exit, explicit
and repeated end, invalid/nested construction, stale ownership, original exception
preservation, invalid draw/admission failures and a subsequent successful pass.
Mesa tests additionally inject scene exceptions, invalid draws, feedback sampling
and budget rejection inside owned offscreen passes; they check that target and
composite resources return to baseline and that a later window pass renders pixels.
These are controlled validation failures, not simulated hardware device loss.

Terrain tiling groundwork is now available in the compute library; see
[Terrain tile geometry contract](TERRAIN_TILES.md) for copied boundary normals,
source mappings, bounds and aggregate budgets. The viewer now has optional tile uploads/frustum culling and exact monolithic
reference pixel checks in smoke mode; see that contract for CLI flags, diagnostics
and eager-residency limits. Selective terrain GPU residency now stages visible-set changes transactionally
and releases hidden buffer pairs; retained CPU recipes support cold readmission.
See the terrain contract for final-set count limits, transient byte admission,
complete-surface preflight and recovery tests. Bounded hidden-tile retention now
reduces re-upload churn on brief returns, with visible-first pressure trimming and
transactional ages. Native/Mesa checks cover handle reuse, expiry and pixel parity.
Independent terrain/fire controls now isolate shading through a reflected layer
uniform and preserve replay state; see the contract below.
Physical GPU validation remains outstanding.


## Terrain/fire layer visibility

`layer_visibility.hpp` defines LayerVisibility with independent terrain/fire flags
and a 16-byte, 16-aligned LayerVisibilityUniform containing one float4. X enables
ground shading, Y enables active/burned fire visualization, and Z/W are reserved
zero. surface_visible(has_fire_field) routes the CPU draw: ground needs geometry,
and a fire-only surface needs geometry only when a replay fire field exists.

The Wildfire shader contract adds a required fragment UBO named LayerVisibility
at slot 10. Camera remains 80 bytes and FireState remains storage slot 1. The
shared validator rejects missing/undersized layer uniforms; OpenGL reflects the
block name, binding and 16-byte size before publication. Like PbrMaterial, this
known fragment-only block uses the backend's fragment-stage reflection fallback
for Mesa's inconsistent block-stage reporting. Existing Wildfire API callers must
bind the layer UBO; TerrainTileRenderer::draw accepts it as the final optional
argument. Other shader families do not declare this binding.

With both flags enabled, existing wildfire ground/flame/burn shading is preserved.
Disabling fire returns clean terrain shading. Fire-only mode discards fragments
outside active/burned footprints (a 0.001 contribution cutoff), then draws those
patches without the underlying ground shading. Discarded fragments write no depth,
so imported objects behind unaffected hidden ground remain visible. This is a
surface visualization, not volumetric flame geometry or a new simulation model.
The CPU skips the surface entirely when neither enabled layer has data to draw.

Viewer T/F controls are edge-triggered; --hide-terrain and --hide-fire set initial
states. Replay application, fire SSBO updates, independent scene/engine clocks and
asset visibility remain separate. The shared geometry/camera/fire-grid origin is
unchanged. In selective residency mode an entirely hidden surface explicitly
releases all tile buffer pairs (bypassing retention), while keeping CPU recipes
and fire data. Returning layers rehydrate before drawing. Eager residency stays
allocated. Empty terrain passes still clear their target; object/composite passes
continue normally.

LAYERS_JSON reports terrain/fire requests, fire_available and surface_visible.
TERRAIN_JSON adds surface_visible and layer_skipped: a surface skipped by visibility
is not counted as a frustum rejection. Benchmark JSON records terrain_visible and
fire_visible, and counts actual terrain submissions. Layer state is session-only
and is not added to the scene/residency sidecar in this increment.

Native graphics tests cover the uniform contract and all CPU routing combinations.
The five-frame Mesa replay test checks both layers, clean ground, fire-only patches,
uniform sky with both hidden, and exact restoration. It freezes shader animation
only for this comparison, while checking a 60 Hz engine/replay accumulator, a
stable selected replay frame and unchanged fire-state cells. Monolithic and
resident/tiled/offscreen paths are exercised; the latter rehydrates after full
layer hiding. Separate smoke checks verify objects with both surface layers hidden.
The diagnostic requires visible active fire plus unaffected ground in view.

Replay speed controls are described below. Physical GPU validation remains outstanding.

## Render-resolution refinement

`refine_terrain_mesh(source, n, max_vertices)` (compute library) turns a
simulation-resolution `TerrainMesh` into a render-resolution one: each complete
simulation quad (detected from `build_terrain_mesh`'s `a b c / b d c`
triangulation) becomes n x n render quads on a shared lattice with spacing
`cell / n`, same origin, `refinement = n` and `simulation_cell_size_meters`
recorded. Heights come from a Ferguson patch (tensor-product cubic Hermite with
zero twist) using per-vertex slopes from central differences over the measured
neighbours (one-sided at borders and holes, zero when none), so the surface is
C1 across quad edges, reproduces every measured vertex exactly and never
extends over holes; isolated measured vertices are kept so the elevation range
is preserved. Normals are the analytic partial derivatives of the patch;
fuel, moisture and curvature interpolate bilinearly. n = 1 returns the source;
0 or more than 64 subdivisions, vertices off the lattice, duplicate lattice
vertices and a result beyond `max_vertices` (length_error, nothing published)
are rejected. `refinement_for_cell_size(source, target, max_vertices)` picks the
largest n whose spacing is at most the target and whose vertex estimate
(`vertices + quads (n^2 + 2n)`) fits the budget. Tiles and LOD copy the new
metadata; the tile partition, water mesh, residency and culling simply operate
on the finer lattice (the viewer scales `--terrain-tile-quads` by the
refinement, capped at 256). Consumers that need the simulation cell of a render
vertex use `origin + (grid - origin) / refinement`; the viewer's shading test
does so to find interior, unburned vertices. Fire lookups use plan positions
and are unaffected. Mesa renders the demo's 363k-triangle refined mesh in about
160 ms per frame; the 160 x 160 bundled DEM refines to 1.8M triangles within the
default 4M-vertex budget.

## Terrain materials: height, slope, vegetation, rock, snow and ice

`terrain_shading.hpp` defines the backend-neutral `TerrainStyle`: the surface's
elevation range, a contour interval, the slope angles at which soil starts to
blend toward rock and is fully rock (defaults 28 and 48 degrees), the world-space
direction toward the light (default south-west, 50 degrees up), an ambient
floor, the relative snow line (`auto_snow_line`: 72 % of the range once the
relief reaches 300 m, otherwise disabled), the vertex curvature that counts as a
fully exposed crest (0.12 slope units per cell) and the procedural detail
strength (0..1). `pack_terrain_style` validates every field and packs a 96-byte
std140 block of six float4s: `elevationRange` (min, max, inverse range or zero
when flat, contour interval), `slopeStyle` (rock start/full degrees, snow line,
ridge curvature), `sunLight` (unit direction, ambient), `detail` (strength),
`waterFloor` (submerged level, kind index or -1, patch size, seed offset) and
`floorWeights` (normalized sand, vegetation, mud, stones). `detail.y` carries
`rock_relief_meters` (0..20, default 5).
`terrain_style_for(mesh, contours)` reads the range recorded by
`build_terrain_mesh` (tiles and LOD variants copy it verbatim), chooses
`auto_contour_interval` (a 1-2-2.5-5 step giving about ten levels) unless a
non-negative interval is given, and the automatic snow line.

Both ground shaders (Terrain and Wildfire) declare the uniform block
`TerrainStyle` at slot 11 in both stages, read the `SURFACE` vertex attribute and
share one GLSL material function, `terrainMaterial()`, whose CPU twin is
`terrain_material()`:

- **Height**: a five-stop hypsometric ramp (lowland green, meadow, dry grass,
  upland brown, pale summit) across the *relative* range colors bare ground, so
  a DEM without fuel data still reads as a relief map.
- **Vegetation**: fuel load blends bare ground toward grass (from 0.04 to 0.25)
  and grass toward forest canopy (from 0.45 to 0.80); canopy thins out above
  62..78 % of the range and on slopes steeper than 30..42 degrees. Fuel moisture
  selects **dry** (straw, below 0.10), **semi-dry** (olive, 0.20..0.26) and
  **wet** (lush green, above 0.40) grass and canopy palettes with smooth blends
  between the classes; `TerrainMaterial` reports the class weights and
  `dominant_material()` names the winner.
- **Rock and exposed ridges**: slope blends the surface toward rock between the
  start and full angles; convex crests (`curvature / ridge_curvature` from 0.2 to
  0.7, above 30..60 % of the range) become scree when gentle and rock when steep,
  and rock is paler at height.
- **Snow and ice**: above the snow line (±5 % blend) snow covers everything that
  is not steeper than 42..60 degrees; wind-blown crests lose 12 % of their
  effective height. Flat ground (below 6..14 degrees) in concave basins or on
  summit caps above 93 % turns to ice.
- **Lake/sea floor**: when `submerged_level_meters` is set, ground below it
  blends (over ±0.3 m) from the land albedo to a floor albedo. `WaterFloorStyle`
  selects sand, vegetation, mud, stones or mixed; single kinds are one-hot, the
  mixed floor zones the configured weights by depth and bed slope
  (`floor_weights()` / GLSL `floorWeightsFor()`: sand on gentle beds down to
  about 2 m, plants in 1.5..6 m, mud deep and flat, stones on steep beds and in
  the surf) and normalizes them. `random_water_floor(seed)` derives weights in
  0.2..1.5 and a patch size in 25..75 m from the seed alone.
- **Lighting**: `ambient + (1 - ambient) * max(dot(normal, light), 0)`.

Composition order is shared exactly: ground = mix(ramp, grass, vegetation);
albedo = mix(mix(ground, canopy, trees x vegetation), stone, rock), then snow,
then ice, then mix(albedo, floor, submerged). `terrain_surface_color()` is the lit CPU reference for detail strength
zero; `terrainBaseColor()` is its GLSL twin.

With detail enabled (default 1.0) the GPU adds texture that needs screen-space
derivatives and therefore has no CPU twin: analytic-gradient value noise gives
grass patches and tufts; forests are built from eight-meter crowns grouped into
stands and clumps with gaps revealing the ground beneath, rounded by a
gradient-based bump, and blending to stand- and clump-scale coverage as crowns
drop below the pixel footprint; rock shows elevation-following strata, grain,
dark cracks and a bump, scree a speckle; snow has soft drifts, sparse sparkle
and a bluish ambient tint on its shaded side; ice is smooth with dark crack
lines; the lake/sea floor breaks the zoned mix into seeded patches (cubed
noise-weighted kinds) and shows sand grain and ripple marks, plant clumps over
sand, mud mottling and pebbles with dark gaps and a rounded bump. Every
frequency fades out as its wavelength approaches the pixel footprint
(`terrainDetailFade`), so distant terrain stays calm.

Rock gets three further treatments so cliffs read as rock rather than hills.
(1) **Relief**: the vertex stage (both ground shaders now include the material
snippet, which has no derivative functions) adds `rockRelief()` to the vertex
height: `detail.x * detail.y * rock * (1 - 0.6 max(snow, ice)) * (1 - submerged)
* (2 ridgedFbm(p * 0.045) - 1)`, further multiplied by `smoothstep(level, level +
2 relief, elevation)` when water is present so no gap can open under the water
surface. `ridgedFbm` is three octaves of `(1 - |2 n - 1|)^2` with analytic
gradients. The displacement is vertical only (grid/fire lookups keep their plan
position), deterministic in world space (tile seams stay exact) and zero at
detail zero, so the CPU reference and the pixel tests are untouched; the
fragment stage receives both the displaced position and the measured
`baseElevation` for materials, contours and water. `TerrainTileRenderer::
set_bounds_padding(meters)` (0..100) inflates culling bounds by the relief so
displaced tiles never pop at the frustum edge. (2) **Faceting**: the shading
normal blends `rock * (1 - snowy) * (1 - submerged) * 0.85 * detail` toward the
geometric facet normal `normalize(cross(dFdx(worldPosition), dFdy(worldPosition)))`,
so displaced crags are flat-shaded with hard edges. (3) **Surface**: the same
ridged field plus a finer octave perturb the normal with slopes consistent with
the relief, creases darken the albedo (`crevice * 0.32`), strata become hard
ledges (`smoothstep(0.38, 0.62, strataNoise)`), and grain, cracks and scree
speckle remain. The Mesa shading test tolerates up to 3 % silhouette pixels
changing in its detail frame (relief moves rock edges); contour and restoration
frames must leave the sky untouched. Contour lines
(`applyTerrainContours`) are anti-aliased with `fwidth`, darken light ground and
lift very dark forest so they remain visible, and fade where they would merge.

The Mesa `--terrain-shading-test` renders four frames: (0) detail and contours
off, comparing the pixel at every interior, unburned, on-screen vertex with the
CPU reference (12/255 tolerance; a majority must match overall and within every
material class with at least twenty firm candidates — vertices not within the
shoreline blend and, on the floor, with a dominant zone weight of at least 0.75,
since neighbouring render vertices are only a few pixels apart — the matched set must span the
ramp and a hillshade range, vertices where rock visibly changes the color must
match, and the demo must exhibit at least seven material classes); (1) eight
contour levels, which may change 0.5..40 % of terrain pixels and must mostly
darken; (2) exact restoration; (3) full detail, which must change at least half
of the terrain pixels with a mean absolute difference between 0.6 and 40 levels
while leaving the sky untouched. Fire/burn overlays, layer visibility, fire-grid
identity and replay timing are unchanged; the shader animation clock is frozen
only during that diagnostic. `TERRAIN_MATERIAL_JSON` lists candidates/matches per
class; `TERRAIN_CONTOUR_JSON` and `TERRAIN_DETAIL_JSON` report the pixel statistics.

The shared validator rejects a bound `TerrainStyle` buffer shorter than 96 bytes
and OpenGL reflects the block name, binding and size before publication. Like
LayerVisibility, this fragment-only block uses the fragment-stage reflection
fallback. `TerrainTileRenderer::draw` accepts the style buffer as its final
optional argument; every tile draw binds it. Object, textured and PBR shaders
declare neither the block nor the `SURFACE` attribute.

Smooth normals come from the mesh generator: each complete quad contributes the
area-weighted face normals of both of its diagonal splits to its four corners,
so the shading normal is independent of the rendered diagonal, exact for planar
terrain, symmetric under grid rotation and never influenced by holes or missing
neighbours. Curvature is minus the discrete Laplacian over the measured
east/west and north/south neighbours, skipping any axis that lacks one. Rendered
geometry still uses one diagonal per quad; very sharp steps narrower than a few
cells can still show the triangulation in the rock blend because the data, not
the shading, is under-sampled. Material classes are a visualization of the
simulation's fuel inputs, not a land-cover model: a request without fuel data
renders bare hypsometric ground, and no vegetation, snow or rock affects fire
propagation.


## Water surface

`terrain_water.hpp` (compute library) builds a still-water plane at a given
level from the terrain mesh: every terrain triangle with at least one corner
below the level is copied with its winding, each vertex moved to the level and
annotated with the bed height and the bed gradient (from the terrain's smooth
normal, capped at 87 degrees). Interpolating those across the same triangles
recovers the exact piecewise-linear bed, so depth = level - bed is exact per
pixel and the land corners of shoreline triangles (depth <= 0) are discarded.
Holes stay holes; the terrain is not modified; a non-finite level is rejected.

`water_shading.hpp` (graphics library) defines `WaterStyle` and packs it into a
128-byte std140 block of eight float4s at slot 12 (`surface`: level, wave height,
wave length, wave speed; `travel`: unit direction in world xz, time, foam depth;
`shallow`/`deep`: color and absorption/clarity depth; `eye`: camera position and
choppiness; `horizon`/`zenith`: sky colors with specular power and reflectivity;
`sun`: light direction and specular intensity). Validation covers every range
documented in the header. Compass degrees map to world xz as 0 -> (0, -1) north
and 90 -> (1, 0) east. The `Water` program identity uses a 24-byte
POSITION/BED/BED_GRADIENT vertex stream, the Camera block at slot 0 and the
style block in both stages; the pipeline must blend, depth-test and not write
depth, and the validator rejects a style buffer shorter than 128 bytes.

The fragment shader computes, per pixel: the phase-speed ratio
`sqrt(tanh(k d))` (clamped to [0.3, 1]); the travel direction refracted toward
the bed gradient by `0.75 (1 - ratio) min(20 |grad|, 1)`; for each wave the local
wave number `k / ratio`, Green's-law growth `1 / sqrt(ratio)` (capped at 1.8), a
depth limit `0.6 d` on the amplitude and a breaking weight as the limit engages;
the primary wave (gust-modulated, front-warped), a long swell at 1.9 wavelengths,
two cross waves at ±32 degrees and 0.55 wavelengths, and noise ripples, all
accumulated into a height slope that perturbs the normal. Foam is the sum of a
shoreline band inside the foam depth with a swash line advancing at the wave
period, splashes where the bed gradient exceeds 0.35..0.9 near the shore, and
breaking crests; it vanishes for zero wave height. Color mixes the absorbed body
color (`exp(-d / absorption)`), a horizon-to-zenith sky by Schlick Fresnel times
reflectivity, and a sun highlight; opacity is `max(1 - exp(-d / clarity),
reflection)` and foam is opaque. The vertex shader lifts the mesh by a swell six
wavelengths long, attenuated by `1 - exp(-d / (2 H + 0.05))`.

CPU twins: `wave_shallowness`, `wave_travel_direction`, `wave_shoaling` and the
flat-water `water_surface_sample` (wave height zero, no foam) mirror the GLSL
exactly; the animated wave field, ripples and foam need screen-space derivatives
and time and have no CPU twin. The Mesa `--water-test` renders four frames:
(0) flat water at t = 0, comparing `blend(before, sample)` with the GPU pixel at
every visible surface vertex deeper than 0.3 m (12/255 tolerance, majority
match, at least fifty candidates, a depth range of at least one meter and at
least five hundred covered pixels); (1) configured waves at t = 0, which must
change at least 30 % of covered pixels and show bright foam while changing at
most 1 % as many pixels outside the three-pixel-dilated coverage mask; (2) the
same waves at t = 0.7 s, which must differ from frame 1 on at least 30 % of the
surface; (3) flat water again, which must restore frame 0 exactly. Smoke runs
freeze the water clock; interactive runs animate with the engine clock.

The viewer draws the water at the end of the terrain pass, after the tile and
shading diagnostics and before the asset snapshot, so existing asset checks
compare against the complete ground. Objects therefore draw over water rather
than being refracted or blended beneath it; planar scene reflections, caustics,
shadows and per-cell water bodies (rivers without a level) remain future work.
Submerged cells are visualization only and do not affect fire propagation.
The viewer passes the water level and the `--water-floor` configuration into
the terrain style, so the terrain pass already draws the floor that the
transparent water then tints; `WATER_FLOOR_JSON` records kind, seed, weights and
patch size. The shading test's per-class comparison covers the floor classes
(`floor_sand`, `floor_vegetation`, `floor_mud`, `floor_stones`).


## 3D vegetation

`vegetation.hpp` (graphics library) turns the material model's canopy and
vegetation cover into instanced 3D plants. **Meshes** are unit-height models
(`VegetationVertex`: position, normal, color, flex 0..1 from base to tip, and
a tint weight that is 1 on foliage/blades and 0 on wood) produced by
`make_tree_mesh` (conifer: tapered trunk plus `layers` stacked cones with closed
undersides; broadleaf/shrub: trunk plus a lobed low-poly ellipsoid; `segments`
3..24, `layers` 1..6, trunk height 0.05..0.6, crown width 0.1..1.5, colors),
`make_grass_mesh` (1..8 tapered two-sided blades) or `vegetation_mesh_from_model`
(any `assets::Mesh`, normalized to unit height with its base at y = 0, section
base colors as vertex colors, green materials tintable; empty, flat or over-wide
models are rejected). **Placement** (`place_vegetation`) samples a seeded
jittered grid at the tree and grass spacings over the render lattice through
`TerrainSampler` (barycentric lookup on the drawn triangle, so plants stand
exactly on the surface; outside the lattice and over holes it returns nothing),
evaluates `terrain_material` at each sample and keeps a tree with probability
`trees x vegetation x (1 - snow)(1 - ice)(1 - submerged)(1 - rock)` and a grass
tuft with `vegetation (1 - 0.7 trees) ...`; species kind (0 conifer-like,
1 broadleaf-like) follows the moisture classes (wet 85 % broadleaf, semi-dry
25 %, dry 5 %), tints are multipliers on the mesh foliage color (dry olive,
semi-dry unchanged, wet lush) with per-plant brightness and hue jitter
(`color_variation`), heights vary 0.7..1.3 x the nominal height, and instances
are bucketed into 120 m chunks with height-inclusive bounds. The budget
(`max_instances`, default 3M) and the sample-grid limit reject oversized
requests without publishing.

The `Vegetation` program uses a 44-byte vertex stream, the Camera block, the
64-byte `VegetationStyle` block at slot 13 in both stages (wind direction and
speed, time, light and ambient, sway amplitude, grass fade start/end, tree fade
end, eye) and 48-byte std430 `InstanceData` records (position and height,
rotation, phase, bend, kind, tint) at slot 4; the validator sizes instance
capacity by that record for this program (144 bytes for objects). The vertex
stage rotates and scales the plant, shrinks it to nothing across its fade band
(no blending), sways it by `flex^2` with a gust that travels along the wind and
leans it on steep ground; the fragment stage uses two-sided wrapped lighting and
darkens toward the base. `VegetationRenderer` owns the mesh buffers per slot
(two tree kinds, grass) and one instance storage buffer per slot sized to the
kind's total (trees) or to the densest grass disc within the draw distance;
each frame it culls chunks by frustum and distance nearest first, refills the
buffers and issues one instanced draw per slot (batches of at most 65535).
`set_tree_meshes`, `set_grass_mesh` and `set_placement` replace resources at
run time; a failed replacement keeps the current ones.

The viewer wires the wave direction/speed into the wind, lowers the terrain's
painted canopy to forest-floor litter (`canopy_on_ground` 0.35) while 3D trees
are drawn, draws vegetation at the start of the object pass (after the terrain
snapshot, so asset diagnostics are unchanged), freezes the sway in smoke runs and
exposes V/B/G and W/A/S/D controls. The Mesa `--vegetation-test` renders five
frames (off, on, swapped tree meshes, the same meshes 1.5 s later, off) from a
close camera and requires trees and grass to be drawn, at least 2 % of pixels to
change, formerly-sky pixels to be covered (plants have height), at least 24
distinct color buckets, visible differences after the mesh swap and the wind
step, and exact restoration. Vegetation does not affect fire propagation; planar
shadows, billboard impostors and per-species LOD remain future work.


## Animated fire effects

`fire_effects.hpp` (graphics library) visualises the replay's fire field as 3D
flames and smoke without touching propagation. `build_fire_emitters` walks the
field: for every burning cell it draws `ceil(flames_per_cell)` seeded sample
points inside the cell, looks each up on the render surface (`TerrainSampler`)
and evaluates `terrain_material`; `fire_fuel_factor` (mirrored in the ground
shader) is `smoothstep(threshold, threshold + 0.1, fuel) (1 - rock)(1 -
submerged)(1 - max(snow, ice))` and a sample with factor zero is counted as
fuelless and skipped, so rock, water, snow, ice and bare soil never carry
flames. Surviving samples are kept with probability ~1.2 x fuel, sized
`flame_height x flame_scale x (0.35 + 0.65 fuel) x factor` with ±30 % jitter,
and given the fuel as intensity; every tree of the optional placement standing
in the cell adds a crown flame at 55 % of its height (at most eight per cell);
one to three plumes rise from fuel-bearing spots; burned cells keep a thin
smouldering plume in about a third of cases. Budgets keep crown flames and
plumes first and trim ground flames. The `Fire` program draws a shared
billboard quad per 48-byte std430 record (position, height, phase, intensity,
kind 0/1/2, seed) with the 64-byte `FireStyle` block (wind, time, eye, flame
scale, smoke scale/height, draw distance, light) at slot 14; the vertex stage
builds a cylindrical billboard, flickers and sways flames and leans smoke
downwind, and the fragment stage erodes a narrowing tongue with rising noise
(core, orange and red bands by heat) or a widening grey plume that thins with
height and is lit warm at its base. `FireEffectsRenderer` refills one instance
buffer per draw with the records inside the draw distance and frustum, smoke
before flames, in batches of 65535; the pipeline must blend, depth-test and not
write depth.

All three layers share one **continuous front field** instead of cell
membership, so fronts are ragged rather than square. `fire_front_at` (CPU;
GLSL twins in the wildfire and vegetation shaders) sums, with saturation, the
radial kernel `1 - smoothstep(0.45, 1.55, d)` of every burning (active) and
burned cell within the 5 x 5 neighbourhood of the nearest cell, in grid units;
`fire_edge_noise` adds world-space value noise at three scales (±0.4 at ~80 m,
±0.25 at ~20 m, ±0.08 at ~4 m, sampled in local grid metres with north
positive, as the ground shader does); a point burns when `active > 0.02` and
`active + noise > 0.6`, and is charred when not burning, `burned > 0.02` and
`burned + noise > 0.6`. The `> 0.02` guards mean noise can only reshape an
outline that exists — the outline wanders up to about a cell past the
squares, occasionally leaves unburned islands inside, and never ignites ground
with no burning or burned cell nearby. Emitters sample a jittered sub-grid over
every burning cell *and* its neighbours and keep the samples on the burning
side; crown flames use the tree's own position in the field. Plants react
through the `PlantFireState` storage buffer (slot 5) and the fire grid in the
96-byte `VegetationStyle` block (`vegetation_fire_state` remains the exact
nearest-cell CPU helper): burned grass is reduced to stubble and burned trees
to 90 %, burned plants are charred and burning foliage flickers as self-lit
embers. The ground fire shader gates burn and flame by the same fuel factor,
scales flames by `0.45 + 0.55 fuel`, mottles the char, and adds a slow flicker
to the flame outline. The large-scale shape still comes from the simulation:
the 8-neighbour threshold automaton spreads by a Chebyshev metric and grows a
square in uniform fuel without wind or slope; the renderer softens that but a
distance-corrected or stochastic spread rule would be a simulation change.

The viewer rebuilds emitters when the replay frame or the vegetation placement
changes, draws them after the transparent object queue, hides them with the
fire layer and freezes their clock in smoke runs. The Mesa `--fire-test`
renders the ground fire alone, the effects at t = 0 and t = 0.6 s and the ground
fire again: flames and smoke must be drawn, at least 200 pixels change, at least
40 changed pixels lie more than three rows above the tallest ground-fire pixel
of their column (flames have height), at least a fifth of the changed region
moves between the two times, and the final frame equals the first. Fire
lighting of the surroundings, ember particles, heat distortion and wind-driven
flame lean beyond the lean term remain future work.


## Weather

`weather.hpp` (graphics library) models wind, daylight, cloud forms,
thunderstorms and their interaction with the terrain, deterministically and
without any GPU type. The viewer packs the per-frame result into the 400-byte
std140 `Atmosphere` block (slot 15, twenty-five float4s: camera basis with
tangent half-FOVs and the moon altitude, eye and enabled flag, light direction
and intensity, light color and ambient intensity, ambient color and daylight,
zenith color and sun altitude, horizon color and lightning, fog color and
visibility, cloud base/top/coverage/density, cloud detail/drift/type,
orographic texture origin and inverse extent, base-shift scale/thickening/
top-lift/pattern scale, wind direction/speed/time, precipitation/snow/wetness/
stars, true sun direction and valley floor, lightning channel midpoint and
flash, drop diameter/ponding/rainbow/convection, lifecycle time/flood
rise/moon diameter, moon direction and lit fraction, cloud light color and
intensity, cloud light direction and moonlight, horizon color opposite the
sun, freezing level/melting layer/runoff/snow cover, and the crag relief
amplitude in the lifecycle row's fourth lane) plus the RGBA8
`OrographicField` texture (slot 1) and, for the Terrain and Wildfire programs,
the RGBA16 `RunoffField` texture (slot 2; `AtmosphereBinding::runoff`, bound
by `TerrainTileRenderer::draw` and the viewer's ground draws only). Every world program —
Terrain, Wildfire, Water, Vegetation, Fire and the Sky, Precipitation,
Lightning and Spray identities — declares the block (Lightning without the sampler); the
shared validator rejects a bound block shorter than 400 bytes or a missing
field texture, and OpenGL reflects the block size (25 x 16 bytes) and forces
the block's stages to the contract (fragment only, both stages for
Precipitation) because Mesa's reference flags are unreliable for it. `AtmosphereGpu` owns a neutral block and a one-texel
neutral field; `TerrainTileRenderer::draw`, `VegetationRenderer::draw` and
`FireEffectsRenderer::draw` take the `AtmosphereBinding` as an argument and
bind both on every draw. With `eye.w == 0` (neutral) every GLSL helper returns
its input or the legacy formula, so the earlier CPU references
(`terrain_surface_color`, `water_surface_sample`) remain exact.

**Wind** (`wind_at`): the speed is `min + (max - min) u` and the direction
`mean + swing (2 v - 1)` where `u`, `v` are contrast-stretched three-octave
seeded value noise over `t / period` and `t / (1.7 period)`, clamped so the
speed never leaves its thresholds and the direction never leaves its swing;
equal thresholds and zero swing give a constant wind. `WeatherSimulation::advance`
integrates the cloud drift with the wind sampled at the start of each step.
`WeatherSimulation::wind` samples the *effective* configuration: `storm_wind`
adds up to 12 m/s to the upper threshold, 25 degrees to the swing and cuts the
period by 45 % in proportion to the layer's storm likelihood (capped at the
configuration limits).

**Daylight** (`sun_for_hour`, `sun_at`): declination `23.44 sin(2 pi (N - 81)/365)`,
hour angle `15 (h - 12)`, altitude from `sin a = sin phi sin d + cos phi cos d cos H`,
azimuth from the standard formula (east before noon), direction
`(cos a sin Az, sin a, -cos a cos Az)` in the engine frame. Piecewise-linear
ramps over the altitude give the zenith and horizon colors, daylight, stars,
the direct light color and intensity (zero below -3 degrees, 0.1 at the
horizon, 0.72 at 30 degrees), and the ambient color and intensity (0.035 at
night .. 0.30 by day). Civil, nautical and astronomical twilight are the
-6/-12/-18 degree bands. The moon follows a simplified ephemeris
(`body_position`, the same altitude/azimuth formula): its phase is
`frac((day - 1.5) / 29.530589)` unless overridden, its hour angle the sun's
minus `phase x 360` degrees and its declination the sun's times `cos(2 pi
phase)`, so a full moon stands opposite the sun and a new moon with it; the
lit fraction is `(1 - cos(2 pi phase)) / 2` and the moonlight `0.11 x
fraction^2.2`, bluish, fading in as the sun drops from -3 to -9 degrees and
only while the moon is more than a few degrees up. The moon takes over as the
ground's light when it outshines the sun's ramp; the light never comes from
below the ground. The cloud layer has its own light (`cloud_light_*`): the
true sun through a ramp that keeps 0.58 at the horizon, 0.4 at -2 degrees in
orange-red, 0.12 at -5 in deep red and nothing at -8, then the moon; and the
horizon opposite the sun (`horizon_away_color`) runs from the day haze through
a cool purple band at sunset to the night colour.

**Cloud forms** (`cloud_profile`): per genus the typical base above the valley
floor, depth, sky cover, optical density, edge detail (0 smooth sheet .. 1
towers), precipitation, storm likelihood, surface visibility and the expected
weather. `cloud_layer_for` derives the layer: the base is the lifting
condensation level `125 (T - Td)` above the valley floor (cirrus keeps at
least its own altitude; an explicit base overrides), the top adds the depth,
coverage may be overridden, precipitation scales with cover, visibility blends
from 50 km toward the form's value with cover, and the pattern scale is 1.1 km
for cumulus, 2.6 km for cumulonimbus, 4 km for cirrus and 2.2 km otherwise.
`precipitation_is_snow` applies the 6.5 K/km lapse rate to the ground altitude;
`lightning_at` is a seeded slot schedule of double pulses. Each form carries a
rainfall intensity (drizzle 0.5, nimbostratus 8, cumulonimbus 25 mm/h at rate
one); `WeatherConfig::precipitation_mm_h` (a replay's sample) overrides it so
the rate (`mm / 12`, capped at one) and intensity reproduce the millimetres.

**Thunderstorms.** In `WeatherSimulation::state` the rate is multiplied by
`storm_rain_band` (three-octave noise over `t / 20 s`, stretched so a full
storm swings between 35 % and 100 %); `surface_temperature` is the reference
plus a diurnal swing of ±2 degrees with daylight, minus 6 degrees times the
rain rate and 2 degrees times the storm, then lapsed to the ground altitude;
`relative_humidity_from_dew_point` (Magnus, raised to at least 70..95 % while
it rains) and the rainfall give `fire_weather = compute::weather_term(...)`,
the v3 contract's own term. `lightning_strike_at` extends the schedule with
the strike's slot, age, a seeded position across the terrain footprint and a
seed; a strike is active for 0.6 s from its start. The viewer maps the
position onto the field, puts the channel midpoint halfway between the cloud
base and the ground into the block's `strike` row, and `LightningRenderer`
rebuilds the channel once per slot (`build_lightning_bolt`: a seeded random
walk from the cloud base to the ground with lateral jitter of 12 % of the
height, up to two levels of branches that die out before the ground, each
segment a camera-facing quad pair — a wide halo at glow 0 and a narrow core
at glow 1, eight vertices and twelve indices per segment, at most 4096
segments) and draws it while the flash lasts. In GLSL the flash adds a sheet
term (0.35 of the flash) to every lit surface and the sky, the channel a point
light falling off over a kilometre on the ground and 500 m inside the cloud.

**Sea state and trees.** `WeatherSimulation::water_for` derives the water
style of the moment: the wave height is scaled by `clamp(0.35 + (U / 7)^1.6,
0.35, 4)` (capped at 5 m), the length by `factor^0.8`, the speed by
`sqrt(factor)`, the direction is the mean wind plus 40 % of the instantaneous
veer, the choppiness `0.3 + 0.7 gustiness`, the foam depth `0.8 + 0.5 factor`.
The water shader dimples the normal with two fast fine noise fields scaled by
the precipitation (not for snow). The vegetation shader's sway strength is
`0.02 + 0.012 U + 0.0007 U^2`, a flutter term fades in between 8 and 20 m/s,
and the crown leans downwind by `0.012 (U - 6)` of the height above 6 m/s.
`FireStyle::vigour` (0.1 .. 3, `params.w`) scales the flame height and, at
60 %, the smoke height; the smoke fragment stage thickens and whitens the
plume with the precipitation (steam). The viewer sets `vigour = clamp(1 + 2.5
fire_weather, 0.25, 1.6)`.

**Terrain interaction** (`sample_height_field`, `compute_orographic_field`):
the render mesh is binned into a height field of at most 128 texels per side
(holes are NaN and sample as the valley floor). For each texel the field
marches up to 96 steps up- and downwind (fetch 6 km) and records the highest
crest crossed and its distance, the lowest upwind valley and the highest
barrier ahead; the local slope along the flow gives the vertical velocity
`U s`. Thickening is `0.7 smoothstep(0, 0.8 m/s, U s)` plus a cumulative-ascent
term (which vanishes once the air has dropped below the crest it crossed) plus
half of the blocked pile-up in front of a barrier; dissipation is the foehn
term `smoothstep(0, 300 m, descent) exp(-crest distance / 4 km)`, or full when
blocked air cannot cross a crest that reaches into the layer; the base shifts
up by `0.6 descent` (capped at the layer depth) in the lee and down 60 m on
the windward side; precipitation is `0.5 + 0.5 thickening - 0.5 dissipation`.
Blocking uses the Froude number `U / (N H)` of the barrier, `1 - smoothstep(0.7, 1.5, Fr)`,
only for crests within 100 m of the base or above it. Every term is scaled by
a coupling that fades from 1 when the base is within the relief of the crests
to 0 two and a half kilometres (or four reliefs) above them. Texels pack as
RGBA8: thickening, dissipation, base shift (0.5 neutral over +-scale) and
precipitation (0.5 neutral). The field is recomputed between passes when the
wind veers more than 3 degrees or changes more than 0.75 m/s.

**Runoff, cascades and the snow line.** `compute_runoff_field` (once per
terrain, from a height field of up to 256 texels a side) routes every texel
to its steepest lower neighbour (D8) from the highest texel down,
accumulating the contributing area; a texel stores the log-scaled
accumulation (`log1p(a) / log1p(max)`), the slope to that neighbour,
`waterfall = smoothstep(40, 60, slope) smoothstep(0.3, 0.6, flow)`, the flow
direction (the downhill gradient smoothed over a 9 x 9 gaussian, so it turns
smoothly across a face) with its steepness (the smoothed slope / 60, zeroed
where the texel's own descent is under 18..32 degrees so a crest stays
level), and the cascade terms: walking the drainage from the top, every
falling texel hands the height its water has dropped since the lip on to
the texel it drains into (`drop_meters`; a lip is a falling texel nothing
falls into), and the first gentle texel below a fall is its plunge pool
(`impact = smoothstep(3, 40, drop)`). `pack_runoff_texture` writes RGBA16:
flow, `0.5 + 0.5 direction * max(steepness, 0.08)` (x, z), and either `0.85
sqrt(drop / 250)` on a falling texel or `0.9 + 0.1 impact` on a pool, so
interpolation toward a crest never reads as a pool; sixteen bits because the
shader builds its along/across frame from the direction and an 8-bit step,
a kilometre from the origin, shifts that frame by metres.
`waterfall_spray_emitters` turns every lip (seated 0.15 texel up the slope),
every falling texel below it (a ledge contact, weighted by the drop) and every
pool (0.3 texel back toward the cliff) into a `SprayEmitter` — position,
downhill unit vector, a face normal leaning by the texel's steepest descent
(at least 40 degrees; a pool's at most 20), kind, strength, drop and the
texel width — strongest first and capped (96 by default).
`freezing_level_meters` is the ground altitude
plus `max(T, 0) / 0.0065`; the state carries it with a 300 m melting layer,
`runoff = clamp(1.5 rate + 0.8 ponding)` (zero while it snows at the ground)
and `snow_cover = rainfall / 15`; `WeatherSimulation::set_rainfall_mm`
presets the rain already on the ground (`--rainfall`). In GLSL `applyRunoff`
(terrain and wildfire) takes the liquid share `1 - smoothstep(fl - 150, fl +
50, elevation)`, darkens and glosses the ground by the film `runoff (0.25 +
0.75 flow)` (the gloss damped on cliffs), scrolls streaks downhill at `0.3 +
2.5 steep` m/s, draws stream threads where `flow > 0.45` on slopes that are
not cliffs, boils the plunge pool with two scrolling noises, and on cliff
texels (`fall` from the lattice steepness, the rendered slope above 12..32
degrees and the flow) paints the cascade in the lattice's along/across
frame: `waterfallRope` (shared with the Spray program) mixes two noises
across the flow (periods 11 m and 5 m, blended by `smoothstep(0, 120, drop)`
so the ropes divide lower down) with a slow meander and a surge, the crags
(`ridgedFbm`, the field that displaces the rock) deflect the ropes by up to
+-2 m and favour the creases, `strands` fray the edges, the rope's core is
opaque water shaded by streaks that scroll at the fall's speed
(`sqrt(2 g drop)`, 2.5..18 m/s, stretched and whitened as it accelerates)
and bursts white where a rock crest stands in the rope, and a thin mist
veil with a forward glint and `bowWeight` hangs over the pour;
`applySnowCover` whitens the ground above `fl - 120 .. fl + 80` by the
cover.

**Spray.** `SprayRenderer` holds `particles_per_emitter` (224) quads for up
to `max_emitters` (96) emitters; `set_emitters` (between passes) writes one
vertex record per corner — CORNER, the particle index within the emitter,
ORIGIN, FLOW (downhill x, z and the launch speed: `1.5 + 2.5 strength` over
a lip, `min(0.35 sqrt(2 g drop), 9)` off a ledge, `min(0.3 sqrt(2 g drop),
8)` out of a pool), FACE and SHAPE (kind, strength, drop, width) — and
`draw` submits every loaded emitter's particles while `runoff > 0`. The
vertex stage lifts the emitter by the crag relief (`atmoLife.w`, on rock by
slope, never sinking it more than a metre), picks the drop's across position
from three candidates by the same `waterfallRope` (a rope near the texel's
middle wins), draws its diameter (75 % fine drizzle off a lip, 45 % below;
0.06..0.35 mm fine, 0.35..2 mm coarse; puffs 0.16 mm), terminal velocity
(Atlas et al.) and a relaxation time — `terminal / g` for a ballistic drop,
0.7..1.2 s for a fine one riding the parcel of air it left with — and
integrates the closed form `p0 + carried t + (v0 - carried) tau (1 -
exp(-t / tau))` with the wind (big drops lag it), the fall's own wind
(fine drops sink 0.6..0.8 m/s faster off a lip or ledge, rise 0.4 m/s over a
pool, and the sheet drags them down with `1.5 t^2` capped at 4 m/s), eddies,
and a lifetime of 1..2.8 s (longer over a pool, shorter for big drops);
drops that fall below a pool or behind a face vanish. Lighting per drop:
`lit = exp(-2.4 density depth) clump rock atmoSun.w cloudShadow` where
`depth` is the share of the plume (radius 2.5 + 2.5 strength + 0.04 drop)
the light crosses to reach the drop, `clump` a drifting noise (0.3..1) and
`rock = mix(smoothstep(-0.35, 0.15, face . light), 1, escape)` with `escape
= smoothstep(0.5, 6, distance off the face) smoothstep(0.05, 0.45,
light.y)` (a lip escapes at least 0.6); the colour is `0.86 (skyLight +
sunColor lit (0.9 + 2.6 cos^5))` dimmed by 65 % of the bow amount plus
`bow sunColor lit 2.4`, where the bow is the primary (40.6..42.4 degrees
from the anti-light point) and secondary (50..53.5) spectra widened up to
2.2 x and paled toward white for drops under 0.35 mm, offset +-0.45 degrees
per drop and present on about half of them. A drop is a camera-facing streak
(`0.025 speed` long, at least two pixels wide); the first 14 particles of
every emitter are puffs — gaussian discs of `2.5 + 4 h + 0.05 drop` metres
swelling with age, pushed `1.5 + 0.5 size` off the face, alpha 0.5 weight
fading as `(1 - age / life)^0.7` — with the pale bow. The viewer draws the
spray after the lightning and before the precipitation and, on a smoke
frame with running water, reports `WEATHER_SPRAY_JSON` (emitters,
particles, the pixels the spray changed, lit by more than 30 levels and
tinted — brightened with channels spread more than 10 levels apart). The Precipitation program judges the
phase at the camera altitude: `snowiness = smoothstep(fl - melt, fl, eye.y)`
mixes the drop's terminal velocity with a flake's 0.8..1.3 m/s (halved when
the wind lofts it above 6..15 m/s), the wind coupling (flakes follow fully
and are carried 30 % faster in a gale), a sway and a vertical bob, the streak
length and width, the shape (streak to soft disc) and the brightness, so
sleet sits between rain and snow.

**Cell lifecycle, volume and the rest of the surface weather.** The state
carries `convection` (`convective x clamp(0.1 + 0.9 smoothstep(0.15, 0.7,
daylight) smoothstep(4, 22, T))`), `cloud_time_seconds` (`time x
clock_acceleration`, the solar time scale capped at 60x), the form's
`drop_diameter_mm`, the accumulated `rainfall_mm` (`advance` integrates
`rate x intensity / 3600` on the accelerated clock and decays it with a
21600 s constant), `ponding = rainfall / 25` and `flood_rise = min(3, 0.02
rainfall)`, and `rainbow = (sun intensity / 0.74) smoothstep(0.05, 0.4, rate)
(1 - smoothstep(0.5, 0.95, cover)) smoothstep(0.5, 3, altitude)` when the sun
is the light.

**GLSL.** `atmosphereSkyColor` blends the horizon between the colour toward
the sun and the colour opposite it by `(0.5 + 0.5 cos(azimuth difference))^1.5`
before the zenith gradient; `cloudAlong` lights the layer along `cloudLight`
(the true sun below the horizon reaches undersides) with the cloud light
colour, the moon's share lifted 2.6x, and a Henyey-Greenstein forward lobe
(g = 0.6, clamped to 3) for silver linings and moonlit rims. The Sky program
draws the moon (`moonDisc`): a sphere of the configured apparent diameter
around `moon`, its normal shaded by the true sun direction so the terminator
follows the phase, `moonSurface` adding maria (dark two-octave noise patches)
and craters on two cell scales (bowls darkening the sun-facing wall, rims lit
on the far side), 2.5 % earthshine at night, a halo of 12 % of the lit
fraction, and the disc occluding the stars (by day it blends into the bright
sky). `cloudDensityAt(p, oro, detailed)` (0..1) shifts the base and
lifts the top by the orographic texel, grows the coverage with thickening and
thins it with dissipation, advects a four-octave contrast-stretched noise by
the drift (each octave rotated so no lattice direction survives; cirrus
streaks are drawn out 4.5:1 along the wind) and raises the threshold with
height for cumuliform towers. `cloudDecayAt` samples a 3D lifecycle noise over
the column and `cloud_time / 1800 s` against the feed `convection + 0.6
thickening - 0.5 dissipation`: decayed cells (scaled by the form's detail)
raise the threshold by 0.18, widen the edge transition by 0.5 (fuzzy
remnants) and lose 70 % of their density. With `detailed` set, two octaves of
3D noise (260 m) erode the shell — strongly at the edges, little in the core —
for cauliflower volume. `cloudAlong` marches the layer along a ray segment
with a fixed step of a twenty-fourth of the layer depth — so neighbouring
pixels sample the same pattern and no seams or bands appear — stretched only
when a path would need more than 96 samples (at most three visibility lengths
or 16 km), with a full-step white-noise jitter per pixel, accumulating ambient
(with a darker base: `1.1 + 1.1 h`) and sun-lit scattering — the optical depth
toward the light from three cone samples at 60/170/360 m, Beer extinction
`exp(-2.6 od)` times a powder term `0.55 + 0.45 (1 - exp(-4 d))` that darkens
thin edges — with a forward lobe, lightning and the strike's point light,
hazed with distance, and returns premultiplied in-scatter and transmittance; `cloudShadow` samples the layer above a ground point along the
light; `atmosphereLit` is ambient + direct (with shadow, wet darkening and
lightning) or the legacy `ambient + (1 - ambient) diffuse`; `applyAtmosphere`
composes the clouds between eye and surface and the haze `1 - exp(-d / visibility)`;
`atmosphereSkyColor` is the gradient, aerial perspective through a 1.5 km
boundary layer, sun glow/disc and lightning. The Sky program pins one triangle
to depth 0.9999 and adds stars and the moon; Precipitation derives each streak
from its index, the time and the wind in a world-anchored box around the eye
(each drop's diameter scatters exponentially around the form's mean,
`D = mean (0.45 + 1.1 (-ln(1 - u))^0.7)` clamped to 0.15..6 mm; its terminal
velocity is `9.65 - 10.3 exp(-0.6 D)` m/s; the wind carries it with
`1 - 0.25 smoothstep(1, 4, D)` of its speed; snow falls at 0.8..1.3 m/s and
sways; streaks are `0.033 x speed` long and `0.0012 D` wide, brighter for
bigger drops), keeps a minimum screen size, exists only inside and below the
local layer, and thins in the rain shadow (`particles x rate^0.6`).
`applyPuddles` (terrain and wildfire programs) collects water in the hollows
of a two-scale noise on ground below 8..16 degrees, growing with the ponding,
and blends a darkened soil with the Fresnel-weighted reflected sky. The Sky
program adds `rainbowAt` (primary bow at 40.6..42.4 degrees from the
anti-solar point with a spectral ramp, secondary reversed at 50.1..53.5 at
30 %, Alexander's band darkening 8 % between) and a gaussian glow around the
channel of an active strike. The water shader builds `rainRings` from a
jittered grid of impact points on two scales (0.35 and 0.8 m cells, each
repeating every 0.4..1 s, spreading at 0.35 m/s with crests of 0.04 + 0.012 D
m that decay as `exp(-3.5 age)`) and uses a Beckmann lobe for the sun
(roughness `0.03 + 0.07 H/5 + 0.05 chop + 0.12 rain`, normalised so the peak
falls as the lobe widens) and dulls the reflected sky by 40 % of the rain.
The Lightning program draws halo strips whose width is the larger of 14 x the
core and 1.2 % of the distance to the eye, with a signed across coordinate:
the core is `exp(-60 x^2)`, the halo `0.45 exp(-2.2 x^2)`; the flash pulses
with the leader and two return strokes (0.12 s and 0.3 s later).

**CPU twins**: `atmosphere_shade` (albedo x (ambient + light x diffuse) with
wetness and lightning), `atmosphere_haze` and `atmosphere_sky_color` mirror
the GLSL for the cloud-free case; the cloud march, shadows, the strike's point
light and precipitation are GPU-only. The Mesa `--weather-test` (demo smoke
mode, default weather and wind options, no assets or other scripted test)
renders ten frames with a low camera and plain materials: (0) weather off, whose clear-color pixels
define the sky mask; (1) clear noon — the dome must cover 99 % of the mask,
60 % of it brighter than 0.4 luminance and bluer than red, and the lit terrain
must match the CPU twin at 60 % of at least 100 visible interior vertices
(12/255); (2) clear midnight — night phase, ground below 30 % of the noon
brightness, sky below 0.12, at least ten star pixels, twin match; (3) civil
twilight at -3 degrees (bisected) — brightness between night x 1.25 and
day x 0.8, the 24 rows above the silhouette redder than blue by 15 %, twin
match; (4) stratus at noon — 80 % of the sky within 28 levels of grey, ground
below 95 % of the clear noon, summit texels inside the cloud and windward
texels thickened, and the drizzle pass must change at least 200 pixels; (5)
cumulus at t = 0 and (6) 120 s later — at least a twentieth of the sky changes and
the wind speed differs while staying inside its thresholds; (7) a
cumulonimbus at the first bright strike over the middle of the terrain — the
storm must widen the upper wind threshold by more than 6 m/s, the flash
exceed 0.5, the channel draw and change at least 40 pixels, the rain exceed
5 mm/h, the fire weather fall below -0.1, the surface air cool by more than 3
degrees and the waves rise above the authored height; (8) four weather hours
of nimbostratus rain (the solar clock at ten seconds per hour) — at least
8 mm on the ground, ponding above 0.3, a flood rise above 0.15 m, the water
rebuilt above its base level, runoff above 0.5 below a freezing level above
the scene, channels, waterfalls, lips and plunge pools in the demo's field and
the spray drawn over at least 100 pixels; (9) the same rain
in air at -3 C — snow at the ground, a freezing level at the ground, a snow
cover above 0.3, no runoff, no spray and flakes drawn; (10) weather off must restore
frame 0 exactly, including the water rebuilt at its base level. Smoke runs without `--weather` stay neutral;
`--weather auto` enables the weather without choosing a form.

The viewer builds the config from the CLI (or from a replay: a v2/v3
request's wind seeds the direction and 0.6..1.3 x its speed, and a v3
weather series sets, per shown frame, the temperature, the dew point from the
humidity, the rainfall and — unless `--weather` names a form — the implied
cloud form at its typical base), samples the height field once, advances the
simulation with the engine clock (frozen in smoke runs; `--weather-time`
starts it at a moment), feeds the storm-widened gusting wind to the
vegetation and fire styles and the sea state to the water style, recomputes
and re-uploads the field between passes, updates the block after the camera,
draws the sky after the opaque terrain and before the water, the lightning
after the fire effects and the precipitation last. Imported OBJ/glTF assets
keep their fixed light and are not fogged; asset fog and cloud shadows,
thunder, hail, wet specular ground, lee-wave (lenticular) clouds, temporal
filtering of the cloud march and a physically based sky remain future work.


## Fixed-tick replay clock

`replay_clock.hpp` provides a resource-free ReplayClock for 1..720 source frames.
Call tick once per 60 Hz engine update. The original 0.35-second source interval
is 84 integer units of 1/240 second; supported rates add 1, 2, 4, 8 or 16 units
per tick. This represents 0.25x, 0.5x, 1x, 2x and 4x exactly and eliminates
floating threshold drift. No wall-clock reads or mutation of replay/compute data
occur in the controller. Stalls/catch-up remain governed by FixedStepScheduler's
existing clamp and update cap; speed scales replay progression only.

set_speed accepts only those five values and preserves the current frame,
play state and pending source time. Invalid rates (including NaN/infinity) reject
without changing state. Pause/resume clears phase on a play-state transition;
repeating an unchanged play state leaves partial progress intact. seek validates
its index, selects it and pauses with zero phase. Forward/back stepping clamps at
the ends and pauses. Arrival at the final source frame stops immediately rather
than waiting an extra source interval; a single-frame replay is always paused.
Toggle from a paused final frame rewinds to zero and plays when multiple frames
exist. Rate survives reset, seek, stepping and endpoint restart.

The viewer replaces its local floating accumulator with this controller inside
the existing engine update callback. Space/Home/comma/period call its play/seek/
step methods. Edge-triggered keys 1..5 select the five rates, and --replay-speed
sets the startup rate. Engine and independent scene clocks are not multiplied.
Flame animation continues to use engine time; the replay rate changes which
precomputed frame is shown, not the physical simulation timestep or flame wobble.
Only a changed selected frame re-applies fire data and updates the GPU field.

REPLAY_JSON reports zero-based frame, playing, speed and pending_seconds in smoke
or enabled diagnostics when frame/controls change. Window titles show speed/play
state even for changes that do not select a new frame. BENCHMARK_JSON.replay_speed
is null without replay input. Rates and clock state remain session-only; no scene
or residency sidecar schema changes are introduced.

Native tests exercise all five rates over exact tick intervals, partial progress
through a speed change, invalid rates/counts/seeks, no paused backlog, single-step
bounds, immediate endpoint stop, restart and single-frame behavior. The existing
replay suite now has nine test groups; total native CTest remains 15 suites.

The seven-frame Mesa --replay-controls-test requires at least three source frames,
default initial speed, no assets and no other scripted test. Each render advances
six 60 Hz engine ticks, then applies its diagnostic command: start/reset, 4x,
pause, forward, back, resume, Home. It checks expected frame/phase/play state and
engine time, reconstructs the expected CPU fire field, and verifies that backward
and reset restore exact pixels. Shader animation is frozen for that diagnostic.
CI covers direct and resident/tiled/offscreen paths, the quarter-speed CLI and an
unsupported rate rejection. Layer isolation tests also continue to check timing
using the new clock.

Next: refresh the baseline checkpoint and remaining-work list. Physical GPU
validation and performance measurements remain outstanding.
