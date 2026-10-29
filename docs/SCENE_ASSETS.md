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
The importer is the shared engine infrastructure; the viewer currently bakes
the object's world transform into its upload data once, so it does not yet
support moving individual objects after GPU upload. That requires a model
transform in the rendering command and shader in the next stage.
