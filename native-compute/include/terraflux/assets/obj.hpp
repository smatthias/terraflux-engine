#pragma once

#include <terraflux/math.hpp>

#include <cstdint>
#include <istream>
#include <vector>

namespace terraflux::assets {

struct MeshVertex {
  math::Vec3 position;
  math::Vec3 normal;
  math::Vec2 uv{};
};

struct Mesh {
  std::vector<MeshVertex> vertices;
  std::vector<std::uint32_t> indices;
};

// Initial restricted Wavefront OBJ importer:
// v, vn and f (v, v/vt, v//vn, v/vt/vn); positive and negative indices,
// convex polygons fan-triangulated; normals synthesized if not provided.
// UVs, materials, smoothing groups and concave polygons are not yet supported.
// No external dependencies, and no filesystem access in the parser.
Mesh import_obj(std::istream& source);

}  // namespace terraflux::assets
