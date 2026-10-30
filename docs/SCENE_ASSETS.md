# TerraFlux scene graph and OBJ assets

This milestone adds a portable C++20 **scene hierarchy** and a first standalone
asset importer. The scene is an append-only node graph with stable integer IDs,
parent/child relationships, validated local transforms, and world-matrix
composition. Cycles and invalid scales are rejected.

Transforms are right-handed/Y-up with column-major matrices:
`world = parent * translation * rotationZ * rotationY * rotationX * scale`.

The OBJ importer supports `v`, `vn`, polygon `f` with positive and negative
indices, and the `v/vt/vn` / `v//vn` face spellings. It fan-triangulates
convex faces; vertex normals are generated if omitted. UV maps, indexed mesh
deduplication, glTF, materials, scene serialization and nested scene-file
importing are **not** implemented yet. For now, OpenGL uses the existing terrain
lighting shader for imported objects.

## Desktop demonstration

Build with `-DTERRAFLUX_WITH_OPENGL=ON`, then:

```bash
./build-gl/terraflux-terrain-viewer --obj examples/assets/pyramid.obj
```

This shows the usual terrain with a pyramid mesh centered and scaled to the
landscape. You can substitute a simple local OBJ model:

```bash
./build-gl/terraflux-terrain-viewer --obj /path/to/model.obj
```

`--obj` can be combined with wildfire replay `--request` and `--response`.
The importer is shared engine infrastructure. The viewer now uploads the mesh
in its original local coordinates and draws **two independent, rotating
instances** from the same vertex and index buffers. Each object has its own
std140 uniform at binding 2 containing a model matrix, inverse-transpose
normal matrix and RGB material tint. Per-frame scene transforms refresh
only those small uniform buffers, never the mesh geometry.

The new object shader retains a verified Camera block at binding 0 and
validates the Object block's reflected resource name, slot, stage and
144-byte size. This is an initial per-object material *color*, not a full
material or texture pipeline. Texture imports, glTF, animation systems and
render passes remain future stages.

## Initial textured material pipeline

OBJ `vt` coordinates are now parsed (including negative indices and normal
combinations) and uploaded in a 32-byte position/normal/UV vertex layout.
Each imported scene object uses the same RGBA8 albedo texture, independently
multiplied by its object tint and directional lighting.

The backend-neutral `TextureDesc`, `TextureHandle` and `TextureBinding`
provide validated GPU resources with explicit lifetimes. OpenGL 4.3 binds a
`sampler2D AlbedoTexture` at slot 3, and validates sampler reflection against
the shared shader contract. `Material` currently describes base tint.

Run with the included, portable ASCII PPM P3 checkerboard:
```bash
./build-gl/terraflux-terrain-viewer --obj examples/assets/pyramid.obj \
  --texture examples/assets/checker.ppm
```

Without `--texture`, the viewer generates an in-memory checkerboard, so
older `--obj` commands continue to work. P3 decoding handles comments,
checks pixel counts and limits dimensions; it flips incoming top-first pixels
to the bottom-first row convention used by OpenGL UVs. PNG, JPEG, normal maps,
MTL loading, mipmapping and PBR remain future work.


## Wavefront material libraries and shared textures

OBJ `mtllib` references and `usemtl` groups are supported. An imported mesh
preserves indexed draw ranges for each material section; the viewer reads
`newmtl`, diffuse `Kd`, and plain `map_Kd` texture paths from MTL.
Textures are cached by normalized source path so all sections and both scene
instances reuse the same GPU texture. `--texture` explicitly overrides maps.

```bash
./build-gl/terraflux-terrain-viewer --obj examples/assets/pyramid_materials.obj
```

MTL and texture references must be relative and cannot traverse `..`.
This is not yet a complete MTL parser: material options and advanced PBR
channels aren't supported, and PPM P3 remains the initial image format.

## Static glTF 2.0 support (first milestone)

The C++ engine imports .gltf JSON containing embedded base64 or local external
binary buffers. It verifies bufferView offsets, accessor types and ranges,
triangle indices, material references and supported primitive modes. A mesh
with multiple TRIANGLES primitives becomes several GPU material draw ranges.
PBR baseColorFactor is mapped to the existing diffuse material color and
rendered over a plain white 1x1 texture.

Run the sample model:
```bash
./build-gl/terraflux-terrain-viewer --gltf examples/assets/tetra.gltf
```

Current scope: one static mesh with FLOAT positions, normals and UVs, and
unsigned indices. Unsupported features are rejected or explicitly outside
scope: node/scene transforms, GLB, images/PNG/JPEG, skinning, animation, morph
targets, sparse accessors, compressed extensions and advanced PBR.
External binary URIs are resolved beneath the glTF file directory; absolute
and parent-directory paths are disallowed. OBJ and wildfire modes still work.

## glTF scene hierarchy and shared mesh instances

The importer now honors glTF 2.0 scene selection, named parent/child nodes,
TRS composition (including normalized quaternion rotation), and explicit
column-major node matrices. It validates parent uniqueness, cycles,
unresolvable references, singular transforms, and malformed scene roots.
Inactive-scene nodes are preserved but not drawn.

The viewer renders each active node's composed world matrix with a separate
model uniform while reusing **one shared GPU mesh and its textures**. It fits
the assembled model, not an individual primitive, onto the terrain. Unlike the
OBJ demonstration's two intentionally rotating copies, glTF nodes retain
their authored relative positions, orientations and scale.

```bash
./build-gl/terraflux-terrain-viewer --gltf examples/assets/tetra.gltf
```

The fixture now contains a root assembly with two transformed child mesh
instances and an unused secondary scene. The importer still supports **one
underlying mesh** and static hierarchies; multiple independent mesh assets,
animation, skeletons, morph targets, full glTF materials and images are next.

## Multiple glTF meshes per scene

Static glTF assets may now contain up to 128 **distinct mesh definitions** with
their own primitives, indices, UVs, normals, and material ranges. Scene nodes
reference a mesh by index; multiple nodes referencing the same mesh share its
uploaded vertex/index buffers. The viewer allocates one GPU geometry resource
pair per mesh and one model uniform per active node, while diffuse textures
remain deduplicated across the whole asset.

```bash
./build-gl/terraflux-terrain-viewer --gltf examples/assets/multi_mesh.gltf
```

The example contains two mesh definitions with different triangle counts and
four scene instances; an unused alternate scene remains excluded. Imported
mesh definitions are separate (they are not merged into one vertex/index
buffer), allowing later independent culling, resource lifetime management,
and material caching. Multi-mesh support is static only; there are still no
glTF image textures, animations, skinning, GLB or complete PBR materials.


## Phase 8: Scene JSON and fixed-step runtime

The native engine now serializes/deserializes a versioned, bounded
`terraflux.scene` JSON format preserving hierarchy, stable node IDs,
local translation/rotation/scale and explicit `mesh_instance` markers.
It validates parent references, cycles, node bounds, names, transforms and
the schema version. It is deliberately a **primary-mesh scene**:
`--scene` currently requires `--obj` and instantiates that OBJ mesh for
every marked drawable node. Scene asset paths and arbitrary component types
are future work. glTF already has its own node hierarchy.

```bash
./build-gl/terraflux-terrain-viewer \
  --obj examples/assets/pyramid.obj \
  --scene examples/scenes/twin_pyramids.json
```

The update loop now uses `runtime::FixedStepScheduler` with deterministic
1/60-second update steps, bounded catch-up work and a fractional interpolation
factor. OBJ example animations and wildfire replay advance by simulated time
rather than unpredictable wall-clock deltas. Input and rendering still
occur each displayed frame. A versioned scene-file roundtrip and scheduler
regression suite runs in native CTest even without OpenGL.


## Phase 8: Spin components, GPU ownership and CPU profiling

Scene nodes now support an optional serializable `spin` component containing
angular velocity (radians per second). The 60 Hz fixed-step scheduler
updates Spin components and their scene-local rotations, including parent
nodes affecting child transforms.

```bash
./build-gl/terraflux-terrain-viewer \
  --obj examples/assets/pyramid.obj \
  --scene examples/scenes/twin_pyramids.json --profile
```

Both pyramids rotate according to scene JSON Spin components. `--profile`
prints rolling CPU mean update/render times, worst combined frame, and
average draw calls per frame over 120 samples. This is **CPU-side** profiling,
not GPU timestamps.

All viewer GPU allocations now use the backend-neutral
`graphics::RenderResourceOwner`. Buffers, textures, and pipelines are
released at normal exit or when asset loading fails. Per-asset shared buffers
remain supported; dynamic asset unloading is still future work.


## Phase 9: Conservative frustum culling and opaque state grouping

The engine computes a local bounding sphere once per imported OBJ/glTF mesh,
transforms it conservatively for each scene instance, and rejects objects
outside the camera's six OpenGL clip planes before issuing an asset draw.
Nonuniform scales and shears use an upper-bound matrix norm, so objects are
not incorrectly culled by a shrinking bounding sphere. Terrain and wildfire
rendering remain outside object culling.

Opaque draw commands go through a stable sorted queue by shader pipeline,
mesh buffers, and material texture. The queue refreshes each object's uniform
buffer immediately before its own draw, maintaining correct material tints
when the same buffer serves multiple material ranges. This **groups state
changes**; it is not hardware instancing or multiple draws merged into one.

Run any asset viewer with `--profile` to see periodic lines reporting
visible and culled instances, opaque draw calls, and state groups. The
`--smoke-test` option prints the same diagnostics for one frame.


## Phase 9: GPU elapsed queries and repeatable culling A/B benchmark

The OpenGL renderer can sample `GL_TIME_ELAPSED` with an eight-slot
asynchronous query ring. The timed section covers **GPU framebuffer clearing
and draw submissions**, not CPU scene updates, buffer loading or the display
swap. Live `--profile` prints GPU mean timing and sample counts alongside CPU
timings; unavailable query slots are skipped rather than blocking frames.

```bash
python3 examples/benchmark_culling.py --frames 120
```

The script uses a static 100-object JSON scene with **20 near-camera nodes
and 80 deliberately off-screen nodes**, then runs culling disabled and
enabled, with fixed 60 Hz simulation deltas, a fixed camera, no VSync,
eight warmup frames and identical resolution. It reports CPU render
submission, GPU elapsed time, draw calls, and their differences. A machine
with a real hardware OpenGL driver/display is recommended. Mesa software
timings are useful for CI functionality checks, **not representative GPU
performance claims**. Repeat runs to quantify noise.

Individual runs:
```bash
./build-gl/terraflux-terrain-viewer \
  --obj examples/assets/pyramid.obj \
  --scene examples/scenes/benchmark_culling.json --benchmark-frames 120

./build-gl/terraflux-terrain-viewer \
  --obj examples/assets/pyramid.obj \
  --scene examples/scenes/benchmark_culling.json \
  --benchmark-frames 120 --no-culling
```
Each run emits one machine-readable `BENCHMARK_JSON=` line, including GPU
query sample count. This is an A/B benchmark foundation, not hardware
instancing or a demonstration of universal frame-rate improvement.


## Phase 9: Hardware instancing of opaque mesh/material draws

The backend-neutral `DrawIndexed.instance_count` now supports indexed hardware
instancing via OpenGL 4.3 `glDrawElementsInstanced`. The instanced vertex
shader reads one 144-byte model/normal/tint record per object from a
std430 shader storage buffer (binding 4). Its shader layout is reflected
and validated like the existing shaders. Frustum culling runs **before**
batching; the render queue groups equal mesh, primitive range, camera
and texture combinations, and chunks to at most 256 objects per draw.

To compare rendering with the same camera, scene and settings:

```bash
python3 examples/benchmark_instancing.py --frames 120
```

For a 100-object scene, 20 objects are visible initially. The new path can
reduce **20 separate opaque object draw calls to one**, plus the terrain
draw call (total 2), versus 21 with `--no-instancing`. This does **not**
change the visibility calculation, shading or original OBJ/glTF materials.
`--no-instancing` selects the legacy path for debugging/benchmarking.

The existing culling A/B script forces legacy per-object rendering to isolate
culling from instancing. Per-instance GPU buffer updates still occur for
each distinct draw group: persistent mapped instance-buffer streaming,
indirect draws and multi-material instancing remain future optimizations.


## Phase 9: Triple-frame SSBO streaming

Hardware instancing now uses a **three-frame ring** of GPU storage buffers.
Each material/mesh draw group in a frame has its own buffer, so a later
submission cannot overwrite object transforms still needed by an earlier
draw. On reuse, OpenGL calls `glBufferData(..., nullptr, GL_STREAM_DRAW)` to
orphan the previous storage allocation before writing fresh instance data.
There is no explicit GPU wait or CPU-side mapping. Buffer capacity grows
only when a group exceeds its previous size; old buffers are destroyed,
and all ring-owned buffers are released on viewer shutdown or an exception.

`--no-instancing` still selects the legacy per-object draw path. The
`BENCHMARK_JSON` output includes `stream_buffers` and `stream_orphans` for
diagnosing ring reuse and allocation behavior. OpenGL driver internals may
still impose costs; measure real GPU performance rather than assuming
faster frame times from streaming alone.


## Phase 7: glTF PNG/JPEG base-color textures

The glTF importer now loads `images` and `textures` from data URIs, external
files, and embedded bufferViews, decodes PNG/JPEG with libpng and libjpeg,
and caches each decoded image by image index. Multiple materials using the
same source image reuse the uploaded GPU texture. The viewer also supports
PNG/JPEG files for OBJ/MTL `map_Kd` and `--texture` overrides alongside PPM P3.

```bash
./build-gl/terraflux-terrain-viewer \
  --gltf examples/assets/textured_multi_mesh.gltf
```

This example contains two textured material groups: one embedded PNG and
one embedded JPEG, each applied to separate mesh primitives. Decoder
dimensions are bounded and MIME signatures are checked. Only
`TEXCOORD_0`, repeat wrapping and nearest sampling are supported in this
initial implementation; unsupported sampler modes reject clearly rather
than silently changing appearance.

The importer also **preserves** glTF metallic and roughness scalar factors.
However, the current fragment shader still provides diffuse sunlight
shading of the base color, **not physically based metallic/roughness
lighting**. It rejects metallic-roughness textures, normal maps,
occlusion/emissive textures, transparency and required extensions.
Full Blender/glTF PBR compatibility will require those features and
configurable samplers in a follow-up.


## Phase 6/7: Metallic-roughness materials and glTF samplers

The glTF texture pipeline now honors `minFilter`, `magFilter`, `wrapS`
and `wrapT`, including all six standard minification modes,
three wrap modes, and automatic mipmap generation when needed. The default
sampler follows glTF (linear mag, trilinear min, repeat wrapping).
Images are cached by source **and** sampling state, preventing accidental
sharing of incompatible OpenGL sampling configurations.

The glTF viewer uses a dedicated metallic/roughness fragment shader with
GGX microfacet distribution, Schlick Fresnel and Smith geometry masking,
per-material `metallicFactor`/`roughnessFactor`, sRGB conversion
and filmic tone mapping. This currently supplies **one directional light
and a simple ambient approximation**; it is not a full glTF PBR renderer.
There is no image-based lighting, normal map, occlusion/emissive texture,
metallic-roughness texture, transmission, transparency, or full GLB model
support yet. The legacy diffuse OBJ renderer remains unchanged.

```bash
./build-gl/terraflux-terrain-viewer \
  --gltf examples/assets/textured_multi_mesh.gltf
```

The sample uses both mipmapped/clamp/mirror and linear/repeat textures with
different material metallic/roughness settings.


## Phase 7: GLB, metallic-roughness and normal mapping

The bounded GLB 2.0 loader accepts a JSON chunk followed by an optional
embedded BIN chunk, with strict magic, version, declared-size, padding and
chunk-boundary validation. The existing viewer's `--gltf` option detects
binary GLB by magic bytes. Embedded geometry, external URI resources and
base-color textures remain supported.

glTF materials now accept `pbrMetallicRoughness.metallicRoughnessTexture`
(green channel = roughness, blue = metallic), and `normalTexture`
with `scale`. For normal maps the fragment shader reconstructs a local
tangent frame using the world-position and UV gradients, which works for
supported meshes without supplied tangents. UV set 0 only; malformed map
references, unsupported UV sets, transparency, occlusion and emissive
textures remain rejected rather than silently discarded.

```bash
python3 examples/pack_gltf_fixture.py \
  examples/assets/textured_multi_mesh.gltf /tmp/terraflux-textured.glb
./build-gl/terraflux-terrain-viewer --gltf /tmp/terraflux-textured.glb
```

This is a **direct-light PBR subset**. It does not yet implement image-based
lighting, precomputed tangent attributes, emissive/occlusion textures, full
alpha modes or all glTF extensions.


## Phase 7: Occlusion, emissive and alpha-mask materials

The glTF loader accepts occlusionTexture (red channel and strength), emissiveTexture and emissiveFactor. The PBR fragment shader applies occlusion to ambient light, adds emissive contribution and supports alphaMode MASK with alphaCutoff and baseColorFactor alpha. Opaque materials use neutral fallback textures; unsupported alphaMode BLEND is rejected because transparent sorting and blending have not been implemented. Normal-map tangent frames remain derivative-based; full IBL and extensions remain outside this milestone.


### Close-up material preview

Use `--asset-preview --gltf examples/assets/textured_multi_mesh.gltf` to choose a closer, higher-scale material-inspection camera and lift the model slightly above the terrain. It is deliberately opt-in: terrain placement and benchmark camera behavior remain unchanged. This helps assess normal, AO, emissive and alpha-cutout effects, but does not replace a GPU screenshot comparison.
