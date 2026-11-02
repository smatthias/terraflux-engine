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

Arrow keys orbit the camera, + and - zoom, Tab switches to a first-person
camera standing on the terrain (W/A/S/D walk, Shift runs, arrows and the mouse
look; see [Camera rig](#camera-rig-world-view-and-first-person)), Escape exits. Run
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
| Vegetation | POSITION/NORMAL/COLOR/FLEX/TINT_WEIGHT plant meshes, camera, VegetationStyle and Atmosphere uniforms, OrographicField sampler, 80-byte InstanceData records and PlantFireState storage |
| Fire | POSITION/TEXCOORD billboard quad, camera, FireStyle and Atmosphere uniforms, OrographicField sampler, 48-byte InstanceData records; alpha blend, depth test, no depth write |
| Hud | POSITION (NDC float2)/COLOR (float4) 24-byte vertices, no uniforms; alpha blend, no depth test or write |
| Structure | The Object layout and uniform for built things (roads, bridges, buildings); the tint's alpha is a `SurfaceMaterial` textured procedurally in world space; lit with a hemisphere ambient so shaded walls stay light |
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
end, eye, the fire grid and its origin with the instance-fire flag) and 80-byte
std430 `InstanceData` records (position and height, rotation, phase, bend,
kind, tint; then the tree fire model's fall direction, fall angle and consumed
amount, and its char and glow) at slot 4; the validator sizes instance
capacity by that record for this program (144 bytes for objects). With the
instance-fire flag set the vertex stage tilts a tree about its base by the fall
angle toward the fall direction (Rodrigues rotation of positions and normals),
draws the foliage in toward the trunk and shortens it as the tree is consumed
(charred stubs remain), stops the sway of a fallen tree, chars the colors by
the char amount and lights them by the glow; grass, and trees without the
model, keep following the cell front. The vertex
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


## Camera rig: world view and first person

`camera_rig.hpp` is a backend-neutral, resource-free description of the two
viewer cameras. Both share one convention with the rest of the viewer: yaw 0
looks north (-Z), positive yaw turns clockwise seen from above
(`horizontal_forward(yaw) = (-sin yaw, 0, -cos yaw)`,
`horizontal_right(yaw) = (cos yaw, 0, -sin yaw)`), pitch is radians above the
horizon, and `look_direction(yaw, pitch)` is the unit vector of both.

- `OrbitCamera` (the world view): a target, yaw, pitch (0.1..1.48) and distance;
  `eye()` is `target + distance (sin yaw cos pitch, sin pitch, cos yaw cos pitch)`.
  The viewer keeps the target `kOrbitTargetHeight` (1.5 m) above the standing
  height when W/A/S/D pan it.
- `FirstPersonCamera`: a standing point (`feet`), yaw, pitch (clamped to
  `kFirstPersonMaxPitch`, 85 degrees, by `look`), an eye height (1.7 m), a
  walking speed (`kMinWalkSpeed`..`kMaxWalkSpeed`, 1..60 m/s, default 2.5,
  scaled in octaves by `adjust_walk_speed`) and a run multiplier (3). `eye()` is the
  feet plus the eye height; `target()` one look direction ahead.
- `StandingHeightFn` is the caller's `(x, z) -> optional<double>`: the height
  a viewer stands at, or nullopt over holes and outside the terrain. The viewer
  supplies the measured ground (`TerrainSampler`), lifted to the water surface
  over submerged ground (so a rising flood lifts the viewer) and by the rock
  weight of the material times the rendered rock relief (so the eye never sinks
  into a displaced crag). It is pure and deterministic; both cameras use it.
- `walk(camera, WalkInput{forward, strafe, run}, dt, standing, lower, upper)`
  moves along the horizontal look direction (diagonals normalized, so they are
  not faster), clamps the plan position to the terrain bounds, and rejects a
  step whose destination has no standing height — the camera stays. It returns
  the distance actually walked. Non-finite input and negative time steps throw.
- `place_on_ground` puts the camera at a plan position or, over a hole, at the
  nearest point of the first ring (of the given step, up to the given radius)
  that has ground; it reports failure rather than inventing ground.
- `enter_first_person(person, orbit, ...)` stands the person under the orbit
  target (searching outward over holes) looking level along the orbit's yaw,
  and leaves the person untouched when it fails. `enter_world_view(orbit,
  person)` sets the orbit target to the feet plus `kOrbitTargetHeight`, keeps
  the person's yaw and leaves pitch and distance alone. Yaw is wrapped into
  (-pi, pi] only when outside it, so a switch and return without movement
  restores the orbit camera bit for bit.

The viewer builds a `CameraPose` (eye, target, near and far plane) from the
active camera each frame: the orbit pose keeps the 1 m near plane and the far
plane at max(12 km, 8 x distance); the first-person pose moves the near plane in
to 0.3 m (grass and rock at the feet) and keeps the far plane at
max(12 km, 8 x the framing distance) so the whole terrain stays visible. The
Camera uniform block, frustum culling, the weather camera basis (forward from
the pose), the PBR eye, transparent sorting, vegetation, water, fire and
precipitation all take the eye from that pose; the weather's ground altitude is
the standing height in first person and the orbit target otherwise. Tab
(edge-triggered, inactive in smoke and benchmark runs) switches; first person
captures the cursor (raw motion when supported) and the first cursor sample
after a switch is a reference, not a turn. A/D strafe in first person and the
scene-node attach/detach keys are suspended there. `--view world|first-person`
selects the starting camera, `--walk-speed` the speed, and every switch prints
`CAMERA_JSON` (view, eye, feet, yaw, pitch, walking speed).

### Walking collision

`collision.hpp` describes what a walker bumps into. An `Obstacle` is a
vertical cylinder (axis x/z, radius, bottom, top); a `WalkerBody` has a radius
(0.35 m), a height (the eye height), a step height (0.5 m) and a maximum
ascent slope (45 degrees; 1..89, validated). `obstacle_blocks` is the one
rule: an obstacle is in the way when its vertical extent overlaps
[feet + step height, feet + body height] — so grass (never an obstacle),
pebbles, tufts and low ledges are stepped over, a crown high above is passed
under and a trunk is solid. `ObstacleField` is an immutable plan-space hash
(16 m cells, obstacles inserted into every cell their footprint touches) with
`for_each_near` (a cell superset) and `clearance`. `tree_obstacles` makes one
cylinder per placed tree from `mesh_footprint_radius` of its mesh slot (the
largest plan radius of the unit-height model's vertices below 15 % height:
the trunk of a procedural tree, the base of a custom OBJ; kind k uses
min(k, n-1) like the renderer) scaled by the instance height, standing from
its ground point to its top; grass contributes nothing. `asset_obstacle`
wraps an imported instance's world bounding sphere. `push_out_of_obstacles`
moves a position out of every blocking obstacle (up to six passes so leaving
one does not enter another; exact contact is allowed; on an axis it steps
out east).

The walker (`camera_rig.hpp`) takes a `WalkEnvironment`: the standing height,
the bounds, the obstacle fields (null entries skipped) and the body. `walk`
splits the motion into sub-steps no longer than half the body radius (so a
24 m running step still meets a 0.05 m trunk), and for each sub-step pushes
the point out of obstacles, clamps it to the bounds, rejects it over a hole,
and rejects it as a wall when the standing height rises by more than
run x tan(max slope); a rejected sub-step is retried along the contour of the
ground (the component of the motion perpendicular to the standing-height
gradient by central differences at 0.25 m), and the walk stops when a
sub-step cannot move. The plain `walk(camera, input, dt, standing, lower,
upper)` overload keeps the earlier behaviour (no obstacles, an 89-degree
limit). `settle` pushes the feet out of obstacles where they stand and
re-samples the standing height; the viewer calls it on entering first person
and every frame so animated assets and a rising flood are honoured.

The viewer keeps a tree field (rebuilt with every vegetation re-placement and
mesh swap, reported as `COLLISION_JSON`) and an asset field rebuilt from the
bounding spheres enqueued each frame; `--max-slope` sets the limit and
`--no-collision` empties the fields and lifts it. The standing height's rock
relief allowance mirrors the shader's `rockRelief()` amount without its noise
term (rock weight, damped under snow and ice, none below and just above the
water line), which makes craggy rock a wall from about 30 degrees while soil
is walkable to the limit. `terraflux-collision-tests` covers the body and
field validation, the blocking rule, the hash query, push-out, walking into,
around and between trunks, stepping over a stone, anti-tunnelling, a ring of
trunks, an 80-degree face (stopped, slid along, descended), 30/60-degree ramps
with the limit raised, holes, settling and the tree/asset obstacle builders on
procedural and OBJ meshes. `--collision-test` (demo or replay smoke mode,
world-view start, vegetation and collision enabled, no assets or other
scripted test) probes the real placement: a run at a trunk on gentle open
ground must stop at body radius, a walk up the steepest facet must not climb
(skipped with `COLLISION_TEST_NOTE` when nothing is steeper than the limit
plus five degrees), a walk through a grass tuft must pass, and the same run
with collision disabled must pass through and change the image.

### Gravity and jumping

`WalkerPhysics` (in `WalkEnvironment`) is the vertical model: gravity
(9.81 m/s², 0.1..100), the standing-jump apex (`jump_height_meters`, 0
disables, ≤ 10), `air_control` (0..1, the per-second blend of the airborne
momentum toward the input), a terminal velocity (1..200 m/s) and the landing
dip (per m/s of impact, a maximum, and an exponential recovery rate);
`validate_walker_physics` rejects anything else. `jump_takeoff_speed` is
√(2gh) and `jump_air_time` 2√(2h/g); the walker hits both to within one
integration step. `FirstPersonCamera` carries the state: `grounded`,
`vertical_velocity_mps`, the horizontal `velocity_mps` (momentum while
airborne), `crouch_meters` (the landing dip, subtracted from the eye) and
`air_time_seconds`.

`walk` is time-stepped. Grounded: the input velocity moves the feet in
sub-steps of at most half the body radius; a sub-step whose support lies more
than the step height below the feet is a ledge — the feet leave the ground at
their height with the current velocity as momentum. `WalkInput::jump` (an
edge the caller derives from the Space press) while grounded sets the vertical
velocity to the take-off speed. Airborne: momentum is blended toward the
input by `air_control` per second, the horizontal move is pushed out of
obstacles at the current feet height (so a rock below the feet is cleared)
and dies against a hole or ground more than the step height above the feet;
gravity integrates semi-implicitly in steps of at most 1/120 s, capped at the
terminal velocity; when the vertical velocity is downward and the feet reach
the support the walker lands: the feet snap to the support, momentum is
cleared, the dip grows by impact speed × `landing_dip_per_mps` up to the
maximum. The dip decays by exp(−recovery × dt) and snaps to zero below
0.1 mm so a jump in place restores the standing eye exactly.

`support_height(environment, x, z, feet_y)` is what the feet rest on: the
standing height, or the top of an obstacle whose footprint plus half the
body radius covers the point when that top is within the step height above
the feet — a wide rock or ledge can be landed on and walked off. An obstacle
top within the step height is a step up however abrupt; only ground is
subject to the slope limit. `settle` re-samples the support only when
grounded, so it never pulls a jumper down.

`terraflux-camera-tests` covers the physics validation, the analytic
take-off speed and air time, a standing jump (apex within 4 cm, air time
within a tick, landing where it started with a dip that recovers to exactly
zero), no double jump, standing still being exactly still, a running jump
carrying 85..105 % of speed × air time, limited air control, walking off a
2 m ledge and landing after √(2h/g), a 0.4 m drop being a step, jumping over
a 1.2 m rock that blocks walking, landing on a wide rock and running off it,
the support rule, lunar gravity, a zero jump height and settling in the air.
`--jump-test` (demo or replay smoke mode, world-view start, positive jump
height, no assets or other scripted test) renders the standing reference, the
apex (feet at the jump height ± 5 cm, image different), the landing (after
the analytic air time ± one tick, eye dipped), the recovery two seconds later
(image restored exactly) and a running jump along the most level heading
clear of trunks (carry within 80..110 % of speed × air time). In the viewer
Space jumps in first person and keeps pausing the replay in the world view;
`--jump-height` and `--gravity` set the constants and `CAMERA_JSON` reports
them.

### Gait

`GaitStyle` (in `WalkEnvironment`, validated by `validate_gait_style`) makes
walking a sequence of steps. `gait_step_length(gait, v)` is
`max(base + per_mps x v, v / max_cadence)` (0.45 m + 0.22 s x v, at most
4.5 steps/s). The camera carries `gait_speed_mps` (the mean ground speed),
`gait_direction`, `gait_phase` (0 at a footfall, 1 at the next),
`stance_foot`, `footfalls`, the head offsets (`bob_meters`, `sway_meters`,
`roll_radians`, `footfall_dip_meters`) and, during a stop, `braking_mps2`
with the frozen `braking_step_length_meters`.

Grounded with the gait enabled, each sub-step (at most 1/120 s and half the
body radius) first controls the speed: with input, accelerate toward the
wanted speed at `acceleration_mps2` (or brake to it at
`stop_deceleration_mps2` when slowing from a run); without input, on the
first sub-step of the release compute a uniform braking
v² / 2d where d is the distance to the footfall ending the current step
(extended by whole steps until the braking is within the limit) and freeze
the step length, then brake linearly. The instantaneous speed is
`gait_speed x (1 - speed_pulse x cos(2 pi phase))`: slowest at the footfall,
fastest mid-step. The resulting step vector goes through the same obstacle,
bounds, hole and slope handling as before; the distance actually moved
advances the phase by moved / step length; crossing 1 swaps the stance foot,
counts a footfall, adds `footfall_dip x clamp(speed / 4, 0.25, 3)` to the
jolt and, during a stop, sets the speed to zero with the phase exactly 0 (a
stop never overshoots its footfall, and a speed reaching zero a hair early
plants the foot too). The head pose is
`bob = fade x bob(v) x (1 - cos 2 pi phase) / 2`,
`sway = fade x sway(v) x side x sin(pi phase)` and the roll likewise, with
`side` −1 for the left stance foot and +1 for the right and `fade = min(1, v)`,
so everything is exactly zero at rest; `FirstPersonCamera::eye()` adds the
bob, subtracts the landing and footfall dips and shifts along the right
vector by the sway, and `up()` tilts the camera up vector by the roll (the
viewer passes it to the look-at). Jumping carries `gait_direction x
gait_speed` as momentum and rests the gait; landing and stepping off a ledge
rest it too, so walking resumes from rest. With the gait disabled the walker
moves at the wanted speed from the first tick (the behaviour of the plain
`walk` overload, which the kinematic tests use).

`terraflux-camera-tests` covers the validation and step length, acceleration
from rest and the walking speed reached within a second, the per-second
distance at the walking speed, the per-step speed pulse, the footfall count
against the step length, the bob, the alternating sway and roll, the eye and
up vector carrying them, a release that finishes the step and stops on a
footfall within two step lengths with the head exactly at rest, the jolt
recovering, standing still being exactly still, a harder jolt at a run within
the cadence limit, a jump carrying the walking speed with the head still in
the air, the walk resuming from rest after landing, and the glide when
disabled. `--gait-test` (demo or replay smoke mode, world-view start, gait
enabled, no assets or other scripted test) renders the standing reference, a
mid-step frame after walking the most level heading clear of trunks (walking
speed reached, pulse, footfalls, bob and sway measured, image different), the
stop a second after release (on a footfall, within two step lengths, head
exactly at rest) and the return to the start (image restored exactly).
`--no-gait` disables the gait in the viewer and `CAMERA_JSON` reports it.

`terraflux-camera-tests` covers the direction conventions, the orbit eye, look
clamping and wrapping, speed limits, walking over a synthetic field with a
river hole and a lake surface (ground following, hole rejection, edge clamping,
diagonal and running speeds), ring placement and both transitions including
exact restoration and refusal without ground. `--first-person-test` (demo or
replay smoke mode, world-view start, visible terrain, no assets or other
scripted test) renders five frames: the world view with its target snapped to
the ground, first person under it (eye at eye height on the standing height,
same heading, level, ground filling the lowest twentieth of the image, image
different from the world view), a quarter turn, a second of running (the feet
move by the walked distance along the ground, within the running speed) and
the return, which must restore the orbit camera and the reference image exactly.
Smoke runs do not require visible fire in first-person frames. None of this
affects propagation, replay timing or the simulation contracts.

## Tree-level fire

`tree_fire.hpp` follows every placed tree through a fire, in replay seconds,
deterministically, on top of the grid simulation: the cells still decide where
the front is (a cell is active for one frame, then burned), the model decides
what each tree does from there. Nothing feeds back into the simulation, its
contracts or fixtures; the ground fire overlay is untouched.

- **Bodies** (`tree_body`): from the instance height, the mesh footprint as
  trunk radius and the seed — a tapered trunk volume times a wood density
  (conifer-like 450, broadleaf-like 650 kg/m³) plus 30 % crown, each tree's
  density and modulus of rupture (60 / 90 MPa) varied by ±`variation` (0.3),
  a breaking moment `MOR x pi r³ / 4`, a standing lean (0..5°) in a seeded
  direction, moisture from the ground it stands on, and the fuel as a
  consume time `consume_seconds x (h / reference)^exponent x (1 + slowdown x
  moisture)` (8 s for a dry 16 m tree). No two trees weigh or hold the same.
- **Burning**: ignition (from an active cell, or from a tree) flames up after
  `ignition_delay` (0.6 s); the consumed fraction grows at `1 / consume_time x
  (1 + wind_burn_factor x wind)`; intensity ramps in over a second and fades
  as the last fuel goes; at 1.0 the tree is burned out. Burning crowns drop
  burning branches (`branch_drop_rate` x intensity per second while 8..92 %
  consumed) that fall under the walker's gravity from a point along the
  (possibly tilted) trunk, land on the standing height, burn there for
  `branch_burn_seconds` and ignite trunks within `ember_ignition_radius`.
- **Falling**: the charred strength is `breaking moment x (1 - consumed)³`;
  the load is `standing_load`: gravity `m g (h / 2) sin(lean_eff)` along the
  effective lean (the standing lean plus `burn_shift x consumed` toward the
  wind — the flames wrap round the lee — or the lean when calm) plus the wind
  moment `½ ρ Cd A v² x 0.65 h` on the half-porous crown (less as it burns).
  Load past strength topples the tree as a rigid rod about its base, `α =
  1.5 g sin θ / h` plus the wind term, toward the load direction. A standing
  neighbour the trunk sweeps into is caught at the angle where the trunk meets
  its top: if its charred strength bears `½ m g h_prop sin θ` the faller leans
  on it (`Leaning`) and, if burning, lights it by contact; otherwise the prop
  breaks (`Collapsed`), falls the same way and is alight at once if the faller
  was. A leaner drops the rest of the way when its prop burns through, breaks
  or falls. On the ground (`Grounded`, 88°) the trunk keeps burning until no
  fuel is left, lights trees it lies against by contact, and becomes a chain of
  low obstacles (`obstacles`) the walker steps or jumps over (a thin log) or
  has to walk round (a thick one); `obstacle_version` bumps on every fall.
- **Spread** (`spread`, the viewer's `--tree-fire model`): contact along
  leaning and fallen trunks after `contact_ignition_seconds`, radiant heat
  with reach `radiant_reach x (h / reference) x intensity x (1 +
  radiant_wind_factor x wind x downwind)` and probability `radiant_rate x
  intensity x (1 - d / reach)` per second, and embers. With spread off (`grid`)
  trees still burn, drop branches and fall, but ignite only from their cells.
- **Time**: `advance` integrates in fixed 1/60 s sub-steps with the remainder
  carried, so the story is independent of the frame rate; `reset` and
  `ignite_from_field` (with `settle_burned`, trees in already-burned cells are
  set burned out, about half of them down) restore a deterministic state for
  seeks. Every ignition, flame-up, branch, topple, lean, collapse, grounding
  and burn-out is an event; `stats` summarises stages, ignition sources,
  branches, and the mass and strength range.
- **Rendering**: `apply_to_placement` writes fall direction/angle, consumed,
  char and glow into the instance records; `emit` adds flames along burning
  trunks (one crown flame, a second lower one past 30 % consumed, three along
  a lying trunk), a smoke plume per burning tree, smoulder for some burned-out
  trunks, ground flames for landed branches and kind-3 ember billboards (a
  glowing core with a flame tail in the Fire shader) for airborne ones.

The viewer builds the model from the placement, the tree meshes, the standing
height and the ground's fuel moisture whenever a replay and trees are present
and `--tree-fire` is not `off`; rebuilds it with the vegetation; syncs it from
the field on every replay frame (the first sync settles cells burned before
it); advances it by the replay clock's own progress (scaled by the replay
speed, held while paused), resetting and re-syncing from a rebuilt field when
the replay is wound back; writes the instances and rebuilds the walking
obstacles (at most twice a second) every frame; draws its flames and embers as
the fire effects' dynamic emitters (the cell crown flames are then off); and
reports `TREE_FIRE_JSON`. `terraflux-tree-fire-tests` covers bodies (size,
species, moisture, variation, determinism, strength against load, wind),
ignition from cells and the delay, consumption and burn-out, wind speeding the
burn, seeks, branches under gravity with landing and burn-out, emitters,
toppling with the wind onto a neighbour that holds and catches, the leaner
dropping when the prop burns through, a sapling crushed and lit, logs as
obstacles, a calm fall toward the lean, faster spread downwind, no spread when
off and determinism. `--tree-fire-test` (replay smoke mode, both layers,
vegetation and the model) stands twelve metres from the nearest tree the
mid-replay front ignited and renders time zero (reference; trees vary in mass
and strength), three seconds (flames, embers, image changed), eighteen seconds
(trees down and burned out, neighbours lit by trees in model mode and none in
grid mode, logs in the obstacle field, image changed) and a reset (reference
restored exactly).

### Storms: wind on the walker, windthrow, lightning

`WindPush` (in `WalkEnvironment`, with `wind_velocity_mps` set by the viewer
from `weather.wind()` every frame) gives a standing walker the drift
`wind_drift`: zero up to `threshold_mps` (12), then `push_factor x (speed -
threshold)²` downwind (0.02, capped at `max_drift_mps` 4). Grounded, the drift
is added to every sub-step (through the same obstacle, slope and hole
handling; a walker standing still in a gale is dragged without steps);
airborne, drag `air_drag x |relative| x relative` (0.0056 per metre, ½ρCdA/m
of a person) accelerates the momentum toward the wind. `lightning_effect(d)`
classifies a strike at distance d: struck (≤ 15 m: knocked down 4 s, eye
1.2 m down, shake 0.35 m), dangerous (≤ 50 m: 1.5 s, 0.8 m, 0.18 m), close
(≤ 200 m: a shake fading with distance), none. `knock_down` stuns the walker
(input ignored, the eye down at once, recovering with the landing dip) and
`shake` sets a trembling amplitude that decays at 4/s and jitters the eye at
37 rad/s. `TreeFireModel` gained root anchorage (`anchor_factor x m g h`,
varied), a windthrow pass (the sound-tree load against the roots; uprooted
trees fall downwind unburned, event `Windthrown`) and `strike(x, z, wind)`:
the nearest standing tree within `strike_radius_meters` is split (lit as
`Lightning`, flames at once, toppling away from the bolt or downwind, event
`Struck`) and the unburned trees within `strike_flash_radius_meters` catch
with `strike_flash_chance` x dryness. The model now exists without a replay
too (advanced on the engine clock), so a storm alone can start a fire.

The viewer detects a new strike by the weather's strike slot, applies it to
the trees and, in first person, to the walker by its distance, prints
`LIGHTNING_JSON` and shows the hazard in the title for six seconds.
`terraflux-camera-tests` covers the push validation and drift law, dragging,
walking with and against a gale, jumping in one, the hazard levels, the
knock-down (no movement or jumping, eye down, recovery) and the shake.
`terraflux-tree-fire-tests` covers anchorage, windthrow by wind strength (none
in a breeze, some in a gale, more in a hurricane, downwind, unburned, as
obstacles and events) and strikes (nearest tree split and alight and down,
flash ignition, distant trees untouched, empty ground). `--storm-test` (demo
or replay smoke mode, visible layers, vegetation, the model, default weather
options, no assets or other scripted test) runs a cumulonimbus, places the
walker ten metres from a bright strike beside a tree and checks the split
tree, the struck walker and the warning, the recovery and the burning log six
seconds later, and a 36 m/s gust front (trees uprooted, not the whole wood,
the walker dragged).

## Flash floods and the walker in water

`flood.hpp`: `FloodModel(runoff_field, FloodStyle)` holds the flood wave
`flash` (0..1), advanced by `advance(accelerated_seconds, rain_mm_h)` toward
`rain / storm_rain_mm_h` (25) with `rise_seconds` (900) while rising and
`fall_seconds` (3600) while draining. `channel_depth(x, z)` is
`channel_depth_meters (1.5) x ((flow - threshold) / (1 - threshold))^exponent
(2) x flash` from the bilinearly interpolated runoff texels (flow is the
log-scaled catchment), zero below `flow_threshold` (0.25) or `min_depth`
(0.03 m). `sample(x, z, ground_y, lake_level)` gives a `FloodSample`: depth,
absolute surface, the current (the drainage direction at Manning's speed
`depth^(2/3) sqrt(tan slope) / n`, capped at `max_current_mps`), whether it is
channel water and whether it goes over a waterfall texel; the lake wins where
it is deeper (still water). `build_mesh(terrain, lake_level)` is a
`WaterMesh` with one vertex per terrain vertex at ground + depth (ground where
dry), triangles where any corner is wet, vertices under the lake left out; the
Water shader now takes the depth per vertex (`POSITION.y - BED` in the vertex
stage, `surfacePosition.y - bedHeight` in the fragment stage), so the same
pipeline draws a flat lake and a surface lying on the channels. The viewer
keeps a muddy, nearly opaque `WaterStyle` for it, rebuilds it when the wave
moves by 0.05, draws it after the lake in the terrain layer and reports
`FLASH_FLOOD_JSON`; `--flash-flood` presets the wave.

`WaterRules` (in `WalkEnvironment`, with `water` = the viewer's
`flood.sample` over the walker's ground) puts the walker in the water:
`support_at` reads the sample over the ground (or the obstacle top) and, from
`swim_depth_meters` (1.3), floats the walker at surface − swim depth (never a
wall). `walk` scales the wanted speed by `max(min_wade_factor, 1 -
wade_slowdown x depth)` when wading and to `swim_speed` when afloat, adds the
current to the drift scaled by a smoothstep from `grip_depth` over
`grip_fade` (fully when swimming), forbids jumping afloat, rests the gait in
deep water, and — wading in a current of at least `sweep_current_mps` (2) at
least `sweep_depth_meters` (0.5) deep — knocks the walker down for
`sweep_stun_seconds` (1.5) with a small shake and sets `swept` for that step.
The camera carries `water_depth_meters`, `current_mps` and `swimming`. The
walker's standing height is the ground with the crag allowance (the orbit
target still hovers above the water); returning to the world view re-snaps
the target above the water, so the view round trip stays exact.
`terraflux-flood-tests` and `--flood-test` are described in the README.

### Climbing

`ClimbRules` (climb speed along the face 0.6 m/s, `stamina_seconds` 40,
`recovery_seconds` 20, `min_stamina` 0.1, `resume_stamina` 0.5) and a
`ClimbFn` `(x, z) -> ClimbSurface {climbable, max_slope_degrees, name}` in
`WalkEnvironment`. When a grounded sub-step is refused as a wall (ground
rising faster than the body's slope limit) and a climb function is present,
`attempt_step` asks the surface at the target point: climbable and the step's
slope within its limit, and the walker not exhausted, the step shrinks to
`climb_speed x cos(slope) x step_seconds` of horizontal progress and is taken
as a climb (reported in the camera as `climbing`, `climb_slope_degrees`,
`climb_surface`); otherwise `climb_refusal` names the reason (the surface's
name, "too steep" or "exhausted"). Climbing drains `stamina` by step time /
stamina seconds; any grounded step that is not a climb restores it by dt /
recovery seconds; at `min_stamina` the walker is `exhausted` and ascends
again only once stamina has recovered to `resume_stamina` (descending is
ordinary walking). Swimming walkers do not climb. The viewer's surface
function reads the terrain material (rock ≥ 0.5 → rock 75°, or 62° wet; snow
≥ 0.5 → 40°; ice ≥ 0.5 → none; otherwise soil 55°, wet 48°; wet = rain over
0.5 mm/h or runoff over 0.2), refuses running channel water deeper than
10 cm, and refuses a waterfall texel (runoff field `waterfall` over 0.3) while
water runs (runoff over 0.05 or the flood wave up). The title shows the climb,
the strength and the refusal. Tests: `terraflux-camera-tests` (climbing) and
`--climb-test` (README).

### The body: vitals, crouching, grip, heading

`vitals.hpp`: `VitalsStyle` holds every rate (validated); `Exposure` is what
the viewer gathers per frame (temperature, wind, rain, water depth, swimming,
exhausted, fire heat and smoke from `TreeFireModel::exposure_at` and the
ground's fire front, the lightning hazard of the step, the landing impact of
the step, crouched); `advance_vitals` integrates wetness (rain soaks over
`soak_seconds` at `soak_rain_mm_h`, water at once, drying over `dry_seconds`
scaled by warmth), body heat (loss below comfort x (1 + chill x wind) x wet
factor x water factor; gain above comfort when not soaked; warming by fire
heat below the burn threshold) and health (lightning at once, fire by heat x
rate, smoke, falls linearly from the safe to the lethal impact, debris wind,
drowning when swimming exhausted, hypothermia below the heat threshold); the
biggest single loss of a step names `last_cause`; health 0 collapses (stays
so until `reset_vitals`). `hazard_for(distance, crouched)` maps a strike to
the hazard with the crouched shelter. In the rig, `CrouchRules` (speed
0.5, body height 0.6, ease 0.25 s) shape the held crouch (`crouched`,
`crouch_blend` lowering the eye by `crouch_drop_meters`, the shortened body
for obstacle passing, no running, jumping or climbing); `GripFn` and
`GripRules` scale the gait's acceleration by grip and its braking by
`min(1, 2.5 grip²)`, integrate a downhill `slide_mps` on low-grip slopes
above `slide_slope_degrees` (gravity along the slope x (1 - grip), bled by a
grip-dependent drag) and knock the walker down (`slipped`) at
`slip_speed_mps` on ice; `HeadingPush` turns the yaw downwind by
`turn_per_mps` x (crosswind - threshold) per second; landings set
`last_impact_mps` with `landed` for one step; swimming spends stamina over
`swim_stamina_seconds`. `hud.hpp` adds the `Hud` program (POSITION float2 in
NDC + COLOR float4, 24-byte vertices, no uniforms; the validator requires no
depth test or write and alpha blending) and `HudRenderer` drawing bars (a
back plate and a fill) at the end of the object pass in first person (smoke
runs only when scripted). The viewer gathers the exposure, advances the
vitals every interactive frame, collapses the walker (knocked down, a message
in the title), resets the body on entering first person, reads the grip from
the terrain material and the weather, and shows the state in the title.
Tests: `terraflux-vitals-tests`, the camera suite (crouch, ice, falls,
heading, swim stamina) and `--vitals-test` (README).

### The body in view, and swimming

`body_view.hpp`: `BodyState` (activity, phase, stance foot, speed fraction,
crouch, pitch) is reduced from the rig by `body_state_from` (down when
stunned; dive/crawl/swim afloat; climb; crouch; idle/walk/run by the gait
speed against the walking speed). `body_pose(state, style)` places six
`BodySegment`s (left and right upper arm, forearm, hand: a transform in
camera space — x right, y up, z forward, origin at the eye — a size and a
color) from a per-activity hand target in body space (level with the body,
rotated into the pitched camera), a two-segment IK (elbow toward a pole,
reach clamped to the arm length) and the style's lengths; `hand_positions`
reads the hands back. `make_unit_box` is a six-face box in the object vertex
format with outward winding; `camera_to_world` maps camera space to the
world along the camera basis. `BodyRenderer` draws the six boxes with the
`Object` program (its own uniform buffers, depth-tested) after the scene; the
viewer builds the basis from the frame's pose (forward to the target, the
gait-rolled up) and skips it with `--no-arms`. Swimming in the rig:
`WaterRules` gained the crawl factor, stroke time and pulse, breath and
recovery times and the dive speed factor; afloat, strokes advance
`swim_phase` (counting `strokes`) and surge the speed by the pulse, Shift
sets `crawling` (speed and stamina cost x crawl factor), Ctrl sets `diving`
(from half a breath; `breath` drains, surfaces at zero; `dive_blend` eases
the eye down by `dive_drop_meters`), `underwater()` reports an eye under the
surface, and the viewer draws a Hud veil under water and zeroes fire and
smoke exposure. `--view-pitch` presets the first-person pitch (−85..85).

### Hands, and things to pick up

The pose grew hands: per arm the upper arm, forearm, palm, a two-segment
thumb and four two-segment fingers (`kArmSegments` = 13), both arms and one
segment for the held item (`kBodySegments` = 27, `kHeldSegment` = 26; an
empty hand's item has size zero). `HandShape` (curl, spread, thumb curl) is
chosen per activity and held kind by `hand_shapes` and blended in the pose;
`fingertips(pose, side)` reads the tips back (thumb first). `BodyState`
carries `held` (`HeldKind`: Nothing, Stone, Branch, Torch) and `held_size`;
the held segment is placed in the right palm (a stone as its block, a branch
or torch along its length). `BodyStyle` gained `palm_width`, `finger_radius`
and `hand_length`; `BodyRenderer` draws `kBodySegments` boxes.

`items.hpp`: `Stone` (position, radius, the block drawn, present) placed by
`place_stones(terrain, terrain_style, sampler, StoneStyle, water_level)` on a
jittered grid (spacing, radii, per-material chances for rock, ridge, bare and
soil, seed, cap; validated), never under the water line or on snow/ice, and
deterministic; `stone_obstacles` turns the present ones into low cylinders
for the `ObstacleField` (small ones are stepped over by the body's step
height). `DroppedBranch` lies on the ground; burning branches belong to the
tree fire model, which gained `take_branch(x, z, radius)` (returns the burn
time left and removes it) and `add_burning_branch(position, seconds)` (which
ignites a trunk it lies beside, source `Ember`). `HeldItem` (kind, size,
torch seconds, the stone's block and source index) is what the hand holds;
`kTorchSeconds` = 90. `Projectile` flies by `advance_projectile` under the
walker's gravity, sub-stepped, against the standing-height function (it keeps
falling past the terrain's bounds) and is `landed` at rest on the ground;
`throw_velocity(forward, speed, lift_degrees)` (speed ≤ 100). Reach queries:
`stone_in_reach` / `branch_in_reach` (the nearest present thing within a
distance of the eye and a cone of the look) and `trunk_in_reach` (any trunk
within a radius of the feet). `PropRenderer` draws up to N boxes with the
`Object` program from `props_near` (stones and branches within a radius,
nearest first). The viewer: E interacts (pick up a stone / take a burning
branch / pick up a branch / break one off the tree in reach of the feet;
light a held branch where the tree fire's `exposure_at` heat ≥ 0.3; set an
unburned tree alight with a torch — `ignite(i, Ember)`; otherwise drop at the
feet), Q throws (12 m/s, +12°) and landed projectiles become stones, dropped
branches or burning branches; a torch counts down, warms (exposure heat ≥
0.18) and emits a flame at the hand (`held_world_position`, taken from the
previous frame's pose). `--no-stones`, `--hold stone|branch|torch`,
`STONES_JSON`, `--items-test` (`ITEMS_TEST_JSON` per frame).

### Tools: rope, fire axe, chainsaw

`tools.hpp`. `ToolKind` (None, Rope, Axe, Chainsaw; named, parsed by
`tool_kind_from_name`) lives in `body_view.hpp` with the pose: `BodyState`
gained `tool`, `swing` (an axe chop 0 .. 1), `sawing` and `saw_phase`;
`kToolSegments` = 2 boxes follow the held item (`kToolSegment` = 27,
`kBodySegments` = 29) — the axe's haft and head, the chainsaw's body and
bar, the rope's coil — and `item_hand` says the held thing is in the left
hand (0) while a tool is out; `tool_tip` reads the working end back. The
hands are needed climbing, swimming and down: no tool is drawn then.
`ToolBelt` (selected, chainsaw fuel, swing, sawing) with `select` toggling.
`AxeRules` (chop depth 6 cm, swing 0.7 s, reach 1.6 m) and `ChainsawRules`
(10 cm/s, 300 s of fuel, 0.5 s per branch, reach) are validated;
`chops_to_fell(radius)` / `seconds_to_saw(radius)` count to
`kFellCutFraction` (0.7) of the diameter; `branch_can_be_cut` halves down to
`kMinBranchPiece` (0.25 m).

The tree model: `TreeFireState` gained `cut` (fraction of the diameter),
`cut_direction` and `felled`; `TreeFireModel::cut(tree, depth, toward)`
deepens the cut on a standing tree (`standing(tree)`: not falling, not
down, not leaning) and at `kFellCut` fells it — stage Falling toward
`toward`, event `Felled`, source unchanged (a sound tree falls unlit). A cut
trunk keeps `(1 - cut)^2` of its strength: burning, it breaks at the cut the
cut's way (Felled) before its charred strength alone would give, and in
wind it breaks at the cut when the load exceeds that share of the breaking
moment. `split_branch(x, z, radius, min_piece)` halves the nearest landed
burning branch (the halves burn on). `stats()` counts `felled` and `cut`;
`ignition_source_name` names sources.

The rope: `RopeRules` (length 20 m, throw 10 m/s lifted 12°, hang drop 2 m,
face slope tangent 1, climb 0.8 m/s, stamina 60 s, slide 4 m/s, grab reach
1.5 m, step-off 0.5 m; validated). `Rope` is Coiled, Flying (a `Projectile`
coil), Laid (anchor = the thrower's feet, end = the landing) or Hung
(anchor = the lip or the branch point, end = the foot or the ground below;
`branch_tree` ≥ 0 for a branch; `thrower_feet` and `landing` for the tails
lying on the ground; `span_meters`, `point_at(t)`). `throw_rope` starts the
flight; `advance_rope` integrates it with `advance_projectile`, keeps the
coil within the rope's length of the hand (taut: the outward velocity is
removed), asks the `BranchCatchFn` for each step's segment (the viewer's
answers with a standing tree's crown: within a quarter of its height of the
trunk, 30–95 % up) and, landed, walks the ground from the feet to the
landing in quarter-metre steps: the first step falling faster than the face
slope is the lip, the steep run to where it eases is the face, and a face
at least the hang drop tall hangs the rope (else Laid; lost down a hole,
Coiled again). `rope_grab_point` (the anchor from the feet for a pegged
rope, the nearest point of the line from the hands at shoulder height, the
end from the feet) and `grab_rope` put the walker on it (`FirstPersonCamera`
gained `on_rope`, `rope_t_meters`, `rope_phase`, `rope_ground_meters`;
climbing = true, surface "rope"). `climb_rope` replaces `walk()` while on
it: W up (stamina / stamina_seconds), S down (free), hanging a third,
exhausted (below the climb rules' min stamina) slides at the slide speed;
the feet follow the line but never sink below the ground it runs over;
letting down onto ground within the step-off distance at the bottom, or
walkable ground (no steeper than the body's max slope around it) for a
metre of rope past the first 1.5 m, steps off (`stepped_off`, grounded);
`at_top` at t = 0; `jump` releases (`let_go`, falling); a rope no longer
hung releases too. `step_off_rope_top` moves the feet inward from a pegged
rope's anchor to ground within the step-off distance (a branch rope has
none: a perch). `coil_rope` resets. `rope_props` draws the line (thin boxes
per 4 m), a flight's trail and coil, and a pegged rope's three runs.

Vitals: `DamageCause::Crushed`, `Exposure::crushed` (a fraction of health at
once), `crush_damage(mass_kg)` = mass / 1000 clamped. The viewer checks each
tree `Grounded` event against the walker (along the trunk, within its
radius plus the body's plus 0.6 m; not up a rope above it): knocked down
3 s, shaken, crushed by the mass. Viewer: keys 1/2/3, E uses the tool first
(`use_tool`: throw / take hold; start a swing whose chop lands at 0.5;
the chainsaw runs while E is held, `saw_step`), 1 pulls a rope in,
`--tool`, `--rope-length`, `--chainsaw-fuel`, `--tools-test`
(`TOOLS_TEST_JSON` per frame), `TERRAFLUX_SCREENSHOT_EVERY_FRAME=1` with
`--screenshot` writes every frame of a scripted run.

### Roads and bridges

`roads.hpp`. `RoadStyle` (lane width 3 m, shoulders 0.5 m — `road_width` is
7 m — sample spacing 2.5 m, surface lift 0.12 m, marking width/dash/gap,
deck thickness 0.6 m, railing height 1.1 m with posts every 4 m, pier
spacing 20 m and width 1.2 m, 6 m stubs of a destroyed span; speed factors
road 1.3, bridge 1.25, rubble 0.7; blockage reach 6 m; fire damage per
second of a burning trunk 0.02 on a road and 0.05 on a deck; washout
current 3 m/s, depth 1 m, 45 s; collapse damage 0.35; seed) is validated.
`RoadNetworkDef` is nodes (plan positions) and segments (`a`, `b`,
`RoadKind::Road | Bridge`); `validate_road_network` rejects bad indices,
self-loops, duplicates, zero length, nodes joined to nothing, a bridge
without a road at either end and a disconnected graph. `RoadPlanStyle`
(road grade 0.18, bridge grade 0.06, span 400 m, dip 2 m, freeboard 1.5 m)
drives `plan_road_network(routes, ground, water_level, style, plan)`: each
leg is sampled; a sample is *good* when dry and with no interval steeper
than the road grade on either side; at the first bad sample ahead a deck is
tried from the last good sample (moved back to the last one standing the
freeboard above the water when water lies ahead, never nearer the previous
node than four samples) to the first landing sample beyond with four good
samples after it, a chord within the bridge grade, the ground nowhere more
than 0.3 m above the chord and a dip of at least `min_dip` or water under
it (landing the freeboard above the water when it crosses water); no
landing within the span limit leaves the road on the ground. Waypoints
within a centimetre are one node. `parse_road_network` reads the JSON forms
(positions as `{x, z}` or `[x, z]`, an origin added) and validates.

`RoadNetwork(def, style, ground)` founds the node surfaces at ground + lift
(a node off the terrain is rejected) and keeps per-segment `RoadSegmentState`
(damage, destroyed, destroyed_by, washout_seconds, blockages) and
`RoadBlockage` (segment, tree, along, burning). `sample(x, z)` finds the
segment whose carriageway covers the point (nearest centre line): `along`,
signed `offset`, `surface` (a road: ground there + lift; a bridge: the deck
plane between its end surfaces), `speed_factor` (rubble when a road is
destroyed; 1 within the blockage reach of a trunk; else the kind's factor
less its excess times the damage), `blocked`, `destroyed`, `deck_missing`
(a destroyed bridge between its stubs). `platform_height` is the surface
where it stands (nullopt off the network or over a missing span);
`obstacles()` is the piers (cylinders from the ground to the slab bottom,
at least 0.5 m tall) of every bridge and, on standing deck, chains of
0.15 m posts along both railings. `log_landed(tree, base, direction,
length, burning, mass_kg = 0)` registers a trunk lying within the half
width of a centre line (segment–segment approach) — on a bridge only where
the deck is within 1.5 m of the ground — once per tree and segment (event
`Blocked`) and cracks the surface by its mass: tonnes / `impact_tonnes`
(12 t; a deck two thirds of it) of damage (`Damaged`; a deck smashed
outright is destroyed by "impact" and collapses). `advance(dt,
log_intensity, water)` clears the events, lets every burning trunk damage
its segment at the kind's rate (the first damage raises `Damaged` on a
road, `Alight` on a bridge; full damage destroys by "fire"), counts
running water at any pier of a bridge (current and depth over the
thresholds, from `water(x, z, ground)`) toward the washout (recovering
when it ebbs; full, destroyed by "flood"), and a channel's torrent over a
road's surface (sampled every 10 m along it; a lake standing on it does
not count) toward twice the washout time. `destroy(segment, cause)`
raises `Destroyed` and `WashedOut` (cause "flood") or, for a bridge,
`Collapsed`. `reset()` restores everything; `version()` changes
with the blockages and the destruction. `stats()` counts and measures.

`build_meshes()` gives `MeshPart`s (`mesh_parts.hpp`: position/normal
vertices, indices, a colour, `what`; `add_box`, `add_quad`, `add_triangle`
build them, `MeshPartRenderer` uploads and draws them with the `Structure`
program; `RoadMeshPart` and `RoadRenderer` are aliases): a surface ribbon per segment (left, centre and right
samples following the ground, tinted toward char by the damage, rubble when
a road is destroyed), the markings (edge lines and centre dashes where the
deck stands), the deck slab, railings (rails at full and half height and
posts) on the standing deck or the stubs, tilted slabs of rubble on the
ground under a destroyed span, and the piers. `emit` adds flames along a
deck alight and a plume, and smouldering plumes at the stubs of a span
burned through (and at the trunks on a road burned through). `RoadRenderer`
uploads the parts (replacing the previous ones; an index out of range is
rejected) and draws one Object-program draw per part, identity model and
the part's colour; it needs an active pass. `clear_vegetation_along`
removes the trees within the half width + 2 m and the grass within + 0.3 m
of a centre line and rebuilds the chunk ranges.

The walker: `WalkEnvironment` gained `platforms` (`PlatformFn`) and
`surface_speed` (`SurfaceSpeedFn`). `support_at` takes a platform above the
ground and no higher than the step height above the feet as the support
(`on_obstacle` — never a wall — when it stands more than the step height
over the ground, so a deck is reached however steep the ground under it
while a surface on the ground keeps the slope rule); swimming overrides it.
`walk` multiplies the wanted speed by the surface factor (must be finite
and positive; clamped 0.05..5; reported as `surface_speed_factor`, and
`on_platform` says what the feet rest on), and — new for any support that
has gone — a grounded walker more than the step height above its support
(a collapsed deck) leaves the ground and falls; `swimming` is only set once
the feet have reached the water, not while falling toward it. Vitals:
`DamageCause::Collapse`, `Exposure::collapse` (a fraction of health at
once). The viewer builds the network before the vegetation (demo routes,
replay routes or `--roads file.json`; `--roads off`; smoke runs only with
`--roads demo`/a file or `--roads-test`), clears the plants on it, adds the
railings and piers as an obstacle field, hooks the platform and speed
functions, advances the network on the engine clock with the tree model's
intensities and the flood's water, registers every `Grounded` trunk,
handles the events (messages; a `Collapsed`/`WashedOut` segment with the
walker standing on its missing span: knocked down 2 s, shaken, `collapse`
damage), draws the parts in the terrain pass before the water and rebuilds
them when the version changes, emits the fire with the trees', and reports
`ROADS_JSON`. `--roads-test` (README) and `terraflux-roads-tests`.

### Buildings

`buildings.hpp`. `BuildingKind` (Bungalow, House, Hospital, Skyscraper;
named and parsed), `BuildingSpec` per kind (`building_spec`: footprint
9×7/11×9/40×20/30×30 m, storeys 1/2/5/20 of 2.8/3.0/3.6/3.8 m, setback
6/7/12/14 m, path width, the ground step the site may have 2.5/3/5/6 m;
`validate_building_spec` also holds a hospital to at least five storeys, a
skyscraper to at least twenty, a bungalow to one). `BuildingSite` is a road
segment, a distance along it and a side; `BuildingRequest` a kind with an
optional site. `place_buildings(roads, requests, ground, water_level,
preferred, BuildingPlacementStyle)` lays each request out (`lay_out`: the
footpath's end at the carriageway edge, the door the setback beyond it, the
centre half the depth further, the front wall along the road, the ground
sampled on a 3×3 over the footprint and along the path; the floor is the
ground at the door + 0.15 m) and accepts it when `suitable`: ground span
within the spec's step, ground behind no more than `max_buried_meters` over the floor (1.8 m; 2.5–3.5 m for the big footprints),
every sample above the water plus the margin, a 5×5 of samples off every
carriageway (by `sample` and by distance to the centre lines), clear of the
placed buildings (circumscribed circles plus the gap) and of their paths.
Automatic requests take the suitable candidate nearest `preferred` over
every road segment (never a bridge), `candidate_spacing` apart, `end_margin`
clear of the ends, both sides; a request without a site is left out; a given
unsuitable site throws. `parse_building_requests` reads the `"buildings"`
list of a network file.

`BuildingSet(buildings, ground, style)`: `platform_height` (the footpath:
ground + lift within its corridor from the door to the carriageway, never
lower than floor − 0.3 × the distance from the door — the ramp up to a
raised door; the floor itself inside an enterable building and in its
doorway), `speed_factor` (the path's on the path or the floor, 1
elsewhere), `building_at` (footprint plus margin), `entrance_near`,
`inside(i, x, z)` / `inside_of` (within the walls of an enterable
building), `obstacles()` (posts of 0.35 m touching along the four walls
from 0.5 m under the lowest ground corner to the roof top — on the walls'
centre lines with the front chains ending at the doorway's jambs in an
enterable building, plus its solid furniture as posts along each piece's
longer axis, as thick as its shorter one, over the piece's height),
`stats()` (with `interiors`), and `build_meshes()`: a foundation box from below the lowest
corner to the floor, the body box in the wall material (plank brown, stone
grey, white concrete, glass blue), a gable roof (two slab boxes up the pitch
with the overhang, gable triangles in the wall material, a chimney on the
house) or a flat slab with a parapet (dark grey; white with a plant room on
the tower), windows as thin boxes proud of every face per storey (glass
blue-grey, open holes near black on the bungalow; none where the door is),
the tower's floor bands and mullions, the door on the front wall, a canopy
on posts over the hospital's and the tower's doors, the hospital's red
cross on the front above the entrance and flat on the roof, the footpath
ribbon following the ground (ramping up to a raised door, with side and
end quads down to the ground where it stands over it).
`clear_vegetation_for_buildings` removes the plants on the plots and paths
(plus a margin) and rebuilds the chunk ranges.

Interiors (`building_enterable`: the bungalow). `lay_out` raises an
enterable building's floor over the highest ground sample under it (+0.15 m)
and `suitable` holds the door to at most `max_buried_meters` + 0.15 above
the ground at it (1.5 m for the bungalow; the ramp reaches it within the
setback). `build_meshes` builds it hollow: the foundation stops 0.1 m under
the floor, a timber plank floor slab lies on it between the walls, the body
is four wall slabs `kInteriorWallThickness` (0.25 m) thick with the window
panes' rectangles and the doorway (the leaf's width plus 0.15 m of
clearance each side, the leaf's height plus 0.05 m) cut out as real holes
(each wall is the full-height pieces between the openings plus a piece
under and a piece over each opening), a plain ceiling slab 0.1 m under the
wall top, a door frame (jambs filling the clearance and a lintel) in place
of the painted door, a stone skirt round the walls instead of the plinth
block, and the furniture of `furnish(spec)` — `Furnishing` boxes in the
local frame (along the front wall, toward the road) with a name, colour,
material, `solid` and `drawn` flags: a table is drawn as a top and four legs
and blocks as one undrawn box; a chair as a seat, legs and a backrest
blocking as one box; the bed frame is not solid but the mattress on it is;
pieces that would not fit between the walls or would crowd the entrance
are left out. The drawn pieces are merged into one part per name across
every enterable building (a dozen draws for a whole suburb).

Hazards (`BuildingHazards`: an external heat function, a log intensity
function, a `WaterFn`, the wind and the rain; `advance(dt, hazards)`).
`material_hazard(SurfaceMaterial)` (mesh_parts.hpp) is the table: whether
the material burns, its ignition heat and time and burn time, the heat
threshold and time that damage a non-flammable one, its soak time, the
mass that smashes it and the wind that strips it. A building's `BuildingState`
holds an `ElementState` per `BuildingElement` (Walls, Roof, Windows, Door,
Interior; `building_element_material(kind, element)` gives each its
material or nullopt — the bungalow has no glass, only an enterable kind an
interior), the flood depth over the floor, the scour seconds, roof_fallen,
destroyed (+ by what), strikes and burn seconds. Each step samples the
hazards once per building (heat at the centre, the door and the corners,
plus the other buildings' intensity by footprint distance, `spread_heat` ×
the building's own strongest flames for its other elements, a burning log's
intensity on the roof; water at the door and corners for the surface over
the floor and a torrent; then sub-steps of 0.25 s): a submerged element
(walls over 0.3 m, windows 1 m, door 0.1 m, interior 0.05 m) is put out and
soaks (damage at 1 / soak time); a flammable one past its ignition heat for
its ignition time ignites, alight it burns at intensity × (1 + wind factor
× wind) / (1 + rain / slowdown) / burn time and burns out at damage 1; a
non-flammable one past its heat threshold takes 1 / heat damage time; the
wind past the material's wind speed takes (wind / that)² / wind time. A
roof at damage 1 falls (`roof_falls`: event RoofFell, the interior smashed
by half and lit if the roof was burning); walls at damage 1 `destroy` the
building (Collapsed with the walls' cause; WashedOut for a flood);
`flood_scour_seconds` of torrent destroy it by "flood". `ignite(building,
element, cause)` lights a flammable, present, dry, unlit element;
`strike(x, z)` hits the tallest building within `strike_radius` of its
walls (the roof lit or scorched by `strike_scorch`); `log_landed(tree, base,
dir, length, burning, mass)` hits every building the trunk's line crosses
(Liang–Barsky in the local frame): roof damage = tonnes / the roof
material's impact mass, walls and windows half, and keeps the log for its
heat. `exposure_at(x, z)` gives the burning buildings' heat (1 within 3 m
of the footprint, 0 by `heat_reach`) and smoke; `sheltered(x, z)` is
inside with a roof; `emit` puts flames over the burning roof's slopes, up
the outside of burning walls, at burning windows, the door and in the
room, a plume over any burning building and a smoulder over one that
burned down. A destroyed building: `platform_height` is floor + 0.45 over
the footprint at `rubble_speed_factor`, `inside` false, no wall or door
obstacles (the chimney's post stays), no door. `build_meshes` chars the
colours by damage (`charred`), draws burst windows as dark "burst" panes
and a burned door as a dark doorway, leaves out a fallen roof and a
burned-out room's furniture and ceiling, and draws a collapsed building as
a "rubble" heap (black "ashes" after a fire) with a leaning wall slab and
the chimney. `version()` bumps on every change of what is drawn, walked
on or collided with (damage in tenths, roof fallen, windows burst, door
gone, destroyed, reset); `door_version()` also when a door burns away.

Rooms and levels (`interior_plan(spec)` → `InteriorPlan`: `InteriorRoom`s
with a level, `InteriorWall` partitions (local plan segments with a
thickness and a material, bottom/top over the ground floor,
`InteriorOpening` doorways by their centre along the wall and their sill
height — one per floor on the tower's core), `InteriorStair` flights (a
straight flight from its foot to its head, `base` to `base + rise`),
`InteriorPlatform` landings, `InteriorHole`s in the floor slabs (a
stairwell, a shaft), `InteriorRail` post chains, an optional
`InteriorLift` (its shaft, its door, its speed), `floors` / `floor_height`
/ `slab`). The bungalow is one room. The
house (`house_layout`): the stair's strip 1 m wide along the left wall from
0.55 m inside the front wall toward the back, 1.4 × the storey height long
(35°); the partition behind the living room at the stair's head with the
kitchen doorway a metre past the stair and the bathroom doorway mid-way
along the bathroom, up to the underside of the upper slab; a blind
partition between the kitchen and the bathroom at a = min(1, iw − 3); the
rooms living room / stairs / kitchen / bathroom on level 0 and the bedroom
on level 1; one rail along the stair's open side from the front wall to the
head, from −0.5 m to a metre over the upper floor (it is the banister too).
The house's pieces stand per room (the toilet, washbasin, sofa, television
and low table are drawn as parts and block as one undrawn box each;
upstairs pieces carry the upper floor in `bottom`).

The skyscraper (`tower_layout`): the core 6 × 8 m at the centre (concrete
walls the full height: front face with the stairwell door and the lift
door on every floor, back, sides, the wall between the stairwell and the
shaft, the shaft's back, and a 0.1 m spine between the two flights that
stops 0.6 m short of the door side, where the flights are within a step
of each other and the walker crosses to the door); per storey two flights
1.4 m wide (from 3.0 m inside the front face to −1.6 m, each climbing half
a storey) with the landing behind them and a stairwell hole and a shaft
hole through the floor above; the rooms: the gallery on the ground floor,
and on every floor above the halls before, behind and beside the core,
the stairwell and the lift, and four apartments of living room, kitchen,
bedroom and bathroom in the corners (`AptFrame`: an apartment's own frame
from its inner corner, mirrored per quadrant; its wall to the hall with
the door 1.5 m from the corner, its wall to the side hall, a partition
between the living room / bedroom and the kitchen / bathroom with two
doorways, one between the living room and the bedroom). `furnish` lays
the pieces out with a `Placer` (an origin and two unit axes in the local
frame, the pieces' half sizes swapped when the axes are turned, a level
lift) through shared builders — `cooking_area`, `table_and_chairs` (4 or
2 chairs), `bed`, `bathroom_fixtures`, `living_room_set` — for the
bungalow, the house, the gallery (desk, stone top, chair, two sofa sets)
and the 76 apartments. Plans are cached per building (`plan(i)`).
`platform_height(x, z, feet_y)`: inside, `surfaces_at` — every floor
whose slab is not holed there, every flight (`flight_height_at`: a linear
ramp), every landing — or, in the shaft, the lift car's height; the
highest no more than 0.5 m over the feet is returned (the viewer's
`built_surface` passes the feet, so a jump under a floor still lands on
the floor below and the rig climbs a ramp by its slope rule). `level_at`
is the floor the feet are on; `room_at` names the room by position and
level (the lift in the shaft; the stairs on a flight or a landing within
0.6 m of the feet). The lift (`LiftState`: height, floor, target, moving,
open; `lift_state`, `in_lift`, `lift_door_near` (a shaft door within reach
at the feet's floor), `call_lift(i, floor)` (closes and travels),
`ride_lift(i, ±n)`; `advance` moves the car at its speed and opens it on
arrival, bumping `door_version`); `door_obstacles` / `build_door_meshes`
carry the shaft doors of every floor but the open one (0.08 m posts; metal
panels) and the car (floor, three walls, ceiling). The rig: a support that
moves under standing feet within the step height carries them (a lift's
car, a rising deck). `obstacles()` adds the partitions as post chains with
the doorways open (0.35 m for walls of the wall thickness, thickness / 2 +
0.1 for a thin spine; from 0.5 m under the wall's bottom to its top, so a
storey's partitions do not reach the floor above) and the rails as 0.08 m
posts over their vertical extent (the house's handrail spans both
storeys); furniture posts start at their floor. `build_meshes(focus,
radius)` draws the rooms — partitions as plaster (or concrete, the core)
pieces round their doorways (stacked holes — a window over the door, a
storey over a storey, a door a floor — are cut by splitting the wall at
every hole edge and cutting each strip's holes in height order), flights
as solid steps (0.19 m risers; timber in the house, concrete in the
tower) with landings, the house's rail posts and tilted rail, every upper
floor as planks slabs round its holes (their undersides the ceilings
below), glazed kinds' panes in their holes (the bungalow's stay open),
furniture merged into a part per kind, material, colour and char — only
for the enterable buildings within `radius` of `focus` (`interiors_near`
lists them; the viewer uses 120 m round the walker or the orbit target and
re-uploads when the list or the version changes); the exteriors, floors
and ceilings always.

Doors. `DoorState{open 0..1, opening}` per building (`doors()`);
`toggle_door(i)` sets an enterable building's door swinging the other way
(false for a solid one), `advance(dt)` moves the swinging leaves at 1/0.6 s
and bumps `door_version()` while any moves, `door_obstacles()` gives each
leaf as posts of 0.08 m along its current line (hinged on the right-hand
jamb looking in, at the wall's centre plane, turning inward by open × 90°)
and `build_door_meshes()` the timber leaf (0.04 m thick, 2 cm short of the
doorway) with a metal handle on both faces. They are kept apart from the
static meshes and obstacles so the viewer re-uploads and rebuilds only
them, and only when the version changes.

The walker: `attempt_step` now treats a step up onto a platform within the
step height from ground (or from under the platform) as a kerb rather than
a wall, so a road edge or a footpath is stepped onto whatever the sub-step
length. The viewer places the buildings after the roads (the four kinds
for the demo and replay networks, a file's list otherwise; `--buildings
off`), clears the plants on them, adds the walls to the obstacle fields,
combines road and building surfaces in the platform and speed hooks, names
the entrance in the title, draws the parts after the roads and reports
`BUILDINGS_JSON` (with `interiors`, `door_parts` and `door_obstacles`). The
doors: a `door_field` `ObstacleField` of `door_obstacles()` in the walk
environment and a second `MeshPartRenderer` of `build_door_meshes()`, both
rebuilt when `door_version()` changes; `advance()` on the engine clock
beside the roads; `door_in_reach()` (an enterable building's door within
2 m of the feet) puts `E: open the door` / `E: close the door` first among
the interaction options and E calls `toggle_door`; the title says `inside
the bungalow (door open|closed)` from `inside_of`. The hazards:
`advance_buildings(dt)` runs `advance` with the tree fire's `exposure_at`
heat, the trees' intensities for the logs, `walk_env.water`, the weather's
wind and the last rain, handles the events (messages; RoofFell / Collapsed
/ WashedOut with the walker on the building's footprint → `pending_collapse`
of `roof_fall_damage` / `collapse_damage`, knock-down and shake) and lights
the nearest unburned standing tree within 8 m of a building every 3 s of
its full blaze (`IgnitionSource::Radiation`); `logs_on_roads` passes every
grounded trunk (with its mass) to `RoadNetwork::log_landed` and
`BuildingSet::log_landed`; the lightning hook calls `strike` and, with the
walker `sheltered`, skips the walker's own hazard ("sheltered inside");
`gather_exposure` adds the buildings' heat and smoke and sets
`Exposure::sheltered` (no rain soak under a roof); `fire_heat_at_hand`
lets a branch be lit at a burning building; `torch_target` (the room the
walker is in, else the walls beside it unless at the door) gives the torch
its `E: set the …'s walls alight` option; the meshes and the obstacle field
are re-uploaded / rebuilt when `version()` changes; `emit` joins the fire
emitters; the title adds the building's state. `BUILDINGS_JSON` stays the
placement report. `--buildings-test`, `--interior-test` and
`--hazards-test` (README) and `terraflux-buildings-tests`.

### The city

`roads.hpp` grew classes and lifts: `RoadClass` (Street 2 lanes, Avenue 4,
Highway 6 + `highway_median_meters`, Ramp 2, Runway/Taxiway/Apron of
`runway_width_meters` 45 / `taxiway_width_meters` 20 / `apron_width_meters`
60; `road_class_lanes`, `road_class_width`), `RoadSegment::cls`,
`RoadNode::lift_meters` (0..60; a road reaching a lifted node is rejected),
and the bridge rule as chains (union-find over bridge segments: every chain
touches roads at two nodes). `RoadRoute` (points, class, lifts) plans a
viaduct leg as one bridge between lifted waypoints without sampling;
`plan_road_network(vector<Vec2>)` remains the street form. The network
keeps a 64 m plan index of segment corridors (`for_each_segment_near`,
`distance_to_centreline`, `max_width`), per-segment `width`, and mitres:
at a node where exactly two segments of one width meet, the ribbon's end
section follows the bisector scaled by 1/cos of the half angle (skipped past
90°). `sample(x, z, feet_y)` prefers the highest covering surface within a
metre above the feet (else the nearest centre line); `platform_height` and
`speed_factor` have the same overloads and `PlatformFn` now takes the feet's
height. Piers are skipped where they would stand on another carriageway.
Meshes: sound surfaces merge per class colour (asphalt, highway, runway,
pavement), damaged ones stay separate; markings per class (street: edge
lines and centre dashes; avenue: lane dashes and a double yellow; highway:
yellow inner lines, lane dashes and a raised median part; ramp: edge lines;
runway: centreline dashes, edge lines, threshold stripes; taxiway: a yellow
centreline). `RoadStats` counts classes, `highway_meters`, `runway_meters`
and `curve_joints` (degree-2 nodes turning more than 2°). JSON: node
`"lift"`, segment `"class"`, route objects `{"points", "class", "lifts"}`.

`buildings.hpp`: kinds PoliceStation ("police"), Terminal, ControlTower
("tower"), Hangar with specs (24×16 two storeys; 90×30 two of 4.5 m; 8×8
seven of 3.5 m; 45×40 one of 5 m), materials and parts (a blue `band` and
`beacon`, a glazed `cab`, `metal` walls, a 20×9 m door, canopies for the
police station and the whole terminal front). `BuildingRequest` gained
`preferred` and `search_radius_meters` for automatic placement near a
point; `place_buildings` gained `skip_unsuitable_sites` and a
`PlacementReport` (placed / no_site / uneven / buried / wet / on_road /
no_ground / taken). Suitability uses the network's index and per-segment
widths, and judges overlap by oriented rectangles plus the gap (paths
crossing footprints either way are rejected). `kMaxBuried` is 1.8 m.

`city.hpp`: `CityZone` (Cbd / Suburb / Airport, name, centre, radius,
rotation), `CityPlan` (zones, highway corner polylines, bounds and margin,
CBD/suburb block sizes, warp amplitude and wavelength, fillet radius,
interchange lift, runway length, seed; validated — one airport, warps that
cannot make lines meet). `plan_city` builds each lattice (indices i, j on a
block grid, positions warped by `amp·sin(j·b/λ + φ)` per line, clipped to
the disc and the bounds, half-block points between nodes for the curve,
avenues every third line of a CBD, a smaller block in a small zone), the
airport (runway, parallel taxiway linked at both ends, an apron off the
taxiway's middle, wanted buildings on the far side, a clearing), fillets
the highway corners into arcs (radius limited to 45 % of the legs; a closed
loop is filleted at its closing corner too), snaps highway ends onto other
highways (T-junctions, or the nearest arc vertex within 60 m), and for each
zone chooses an interchange site: candidates every 50 m along straight runs
(140 m clear of the run's ends), off the zone's disc, within 450 m of a
lattice node, on dry ground within the bounds on the viaduct's run and the
zone's quadrant, clear of other junctions (ramp reach + extents), not inside
another zone, with an access avenue that crosses no other highway or zone.
The diamond inserts waypoints at ±110 (ramps), ±80 (lift 0), ±40 (0.62
lift) and 0 (lift) into the run, an avenue from the zone's nearest node to
the near ramp junction (60 m out) and on under the crest to the far one (or
a 15 m stub when the far side is out of bounds or wet: a half diamond), and
the ramps as 8-point quadratic Béziers bending round the quadrant corners.
Otherwise the shortest link from a zone node to a dry highway waypoint, an
interchange avenue end or a connected zone's node is an avenue at grade.
Routes are planned into the network; lots are then parcelled along every
Street/Avenue segment whose midpoint lies in a zone (both sides, from 8 m
in, pitch = kind width + 8 m in the CBD / 6 m in suburbs; CBD lots are
skyscrapers, suburb lots houses (60 %) or bungalows by hash), after the
civic requests (hospital and police station near the CBD centre, a police
station near every other suburb) and the airport's (sites projected onto
the apron segment, the side away from the runway). `CityLayout` is the
network definition, the requests, the junctions (`CityInterchange`: zone,
position, diamond or not), the clearings and `CityStats`.
`clear_vegetation_in` empties the clearings. `demo_city_plan` and
`generic_city_plan` (hexagonal ring at half the extent, suburbs and the
airport on an orbit outside it) are the built-in plans.

The viewer: `--roads city` (the default of interactive runs; `auto` means
the demo network for the roads and buildings tests and nothing in other
smoke runs), `--city-test`, `CITY_JSON`, the zone name in the title, the
class and lanes of the road underfoot, `RoadNetwork::sample` by the feet's
height for the title, the speed and the collapse check.

### Procedural materials

`mesh_parts.hpp`: `SurfaceMaterial` (Plain, Asphalt, Concrete, Thatch,
Planks, Stone, Tiles, Glass, Metal, Paint, Brick, Timber, Pavement, Window;
named) on every `MeshPart`; `MeshPartRenderer` writes it into the Object
uniform's `tint.a`. The `Structure` vertex stage passes the world position;
its fragment stage fits a frame to the face (u = horizontal across the face,
v = up the slope; along z on a flat surface), takes `uv` in metres and the
pixel footprint from `fwidth`, and evaluates `materialShade(material, uv,
pixel)`: a colour factor and a relief slope per material (value noise and
ridged noise from the shared GLSL, a `masonry(course, unit, joint, jitter)`
helper for stone and brick, sines for tiles and corrugation, hashes per
board, stone, pane or slab). The relief bends the normal before the
hemisphere-and-sun lighting; fine terms are scaled by
`terrainDetailFade(pixel, frequency)`. Roads assign asphalt (carriageways
and runways), pavement (taxiways and aprons), paint, concrete (decks, piers,
medians, rubble) and metal (railings); buildings assign by kind (thatch over
planks on a stone plinth with a brick chimney for the bungalow; stone under
tiles with a timber door for the house; concrete, glass facades, single
window panes, metal hangars).

Vegetation: `make_tree_mesh` adds `push_tube` branches (broadleaf limbs
from the trunk top into the crown; conifer whorls of five under each tier
and three dead stubs) as untinted wood; the vertex stage passes
`plantFoliage`, `plantGrass` and `plantSeed`; the fragment stage shades bark
(ridged fissures along y, grain, a tangent tilt of the normal) and foliage
(triplanar `leafNoise`/`leafFbm` weighted by the normal: clusters, leaves,
flecks toward the light, a discard where the gap noise is low on foliage
above the trunk, and a normal tilted by the cluster slope). Grass keeps the
plain shading. Both fade with the pixel footprint.

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
