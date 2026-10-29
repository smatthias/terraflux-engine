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
