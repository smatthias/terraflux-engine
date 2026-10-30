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
- stride 28 bytes; wildfire rendering uses the same upload stride but reads fire state from an SSBO
- `Camera` uniform buffer at vertex-stage binding slot 0

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
configured in the vertex array. The backend also supports object and PBR pipelines, texture sampling,
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
| Terrain | Terrain vertex layout and camera uniform |
| Wildfire | Terrain upload stride, camera and FireState storage |
| Object | Local untextured geometry and object uniform |
| TexturedObject | Textured local geometry, object uniform and albedo sampler |
| InstancedTexturedObject | Textured geometry with InstanceData storage |
| PbrObject | Textured per-object PBR material/resources; optional sorted alpha blend |
| PbrInstanced | Opaque instanced PBR material/resources |
| ContractOnly | CPU validation of custom layouts; rejected by GPU creation |

Unspecified and unknown IDs reject. A declared built-in must match its full
layout (stride, attribute format/offset/location and binding name/kind/slot/
stages), independent of declaration order. Transparent blending requires
PbrObject with depth testing enabled and depth writes disabled; transparent
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
a pass restores the previous read/draw framebuffer and renderbuffer bindings.
Pass end preserves the attachment storage and leaves it bound for GL diagnostic
readback. A later window pass explicitly rebinds framebuffer zero. Destruction
of an inactive bound framebuffer follows OpenGL's unbinding behavior.

OpenGL owns one framebuffer and two renderbuffers per target. Storage allocation
checks GL error status and framebuffer completeness before publication. Failure
restores bindings, deletes partial objects and removes the validating mirror's
live allocation. RenderResourceOwner tracks targets for reverse-order cleanup;
the device also deletes any remaining target objects before context destruction.
No attachment is exposed as a sampled TextureHandle in this increment.

`ResourceBytes::render_target_bytes` accounts for width × height × 8 bytes,
separately from sampled texture bytes. It contributes to total bytes, shared
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

This test is separate from CTest so the 14 native suites remain display-independent.
It reads back color and float depth, checks independent target storage, color-only
and depth-only clears, preserved depth occlusion, actual triangle drawing,
creation while a pass is active, window restoration, budget rejection and repeated
replacement/cleanup. The existing viewer continues to render directly to the
window. Next: sampled offscreen color and a compositing pass. Physical GPU testing
is still required before hardware performance conclusions.
