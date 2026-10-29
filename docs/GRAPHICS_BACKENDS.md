# TerraFlux graphics backend contract

## Boundary

The simulation/grid/terrain mesh remains in `terraflux_compute`. The graphics
abstraction is implemented in the independent `terraflux_graphics` static
library. Its public API uses no OpenGL, windowing-library or PlayStation SDK
types. The current implementation includes a CPU validating device, **not a
GPU renderer**.

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

Buffers are immutable and initialized fully in this first version of the
interface. Index draws currently use unsigned 32-bit indices and triangles.

## Cross-platform shader metadata

`ShaderLayout` is a declared contract, **not actual compiled shader
reflection**. Backend implementations must extract compiled GLSL or PSSL
resource metadata and call `validate_shader_reflection(expected, actual)`.
The comparison is order independent but checks names, locations, data formats,
offsets, resource kinds, slots, and stage visibility.

OpenGL will later require shader compilation, resource creation, real GL
object lifetime, render passes and context management. Actual PS4 GNM/GNMX and
PSSL support depends on authorized Sony SDK access and hardware validation.
Do not assume that validating GLSL here proves PSSL compatibility.

## Verification

Both GCC Release and Clang Debug CI build the graphics library and exercise
the CTest graphics-contract test executable. Simulation parity must remain
unchanged. The validating device checks basic resource lifetime, bound vertex
and index buffer sizes, triangle draw ranges, and referenced vertex indices,
but it does not render pixels.
