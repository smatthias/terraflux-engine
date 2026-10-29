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

The first vertex layout is:
- `POSITION`, location 0, three floats at byte offset 0
- `NORMAL`, location 1, three floats at byte offset 12
- stride 24 bytes
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
configured in the vertex array. This backend currently supports the terrain
pipeline and uniform-buffer binding only; texture/sampler pipelines will follow.

The GLFW demo creates a procedural elevation grid using the existing compute
types, builds the shared TerrainMesh, uploads GPU buffers and renders the mesh
with an orbit camera. It is an interactive native 3D preview, **not yet an
importer for saved wildfire simulations or an animated fire visualizer**. Actual PS4 GNM/GNMX and
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
`sudo apt install libglfw3-dev libgl-dev`). A GPU/driver supporting OpenGL 4.3
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
