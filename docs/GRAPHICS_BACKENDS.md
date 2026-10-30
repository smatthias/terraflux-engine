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
modules, render targets/passes or PS4/PSSL support. Native tests cover all built-in
identities, invalid/missing IDs, layout mismatch, metadata ordering and contract-only
boundaries; existing Mesa paths exercise actual compile/link/reflection and draws.
