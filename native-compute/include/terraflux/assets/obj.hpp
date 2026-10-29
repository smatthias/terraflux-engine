#pragma once

#include <terraflux/math.hpp>

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace terraflux::assets {

struct MeshVertex {
  math::Vec3 position;
  math::Vec3 normal;
  math::Vec2 uv{};
};

struct MeshSection {
  std::uint32_t first_index = 0, index_count = 0;
  std::string material_name;  // empty => fallback material
};

struct Mesh {
  std::vector<MeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<std::string> material_libraries;
  std::vector<MeshSection> sections;
};

// Initial restricted Wavefront OBJ importer:
// v, vn and f (v, v/vt, v//vn, v/vt/vn); positive and negative indices,
// convex polygons fan-triangulated; normals synthesized if not provided.
// OBJ material group ranges and library names are preserved. Concave polygons are unsupported.
// No external dependencies, and no filesystem access in the parser.
Mesh import_obj(std::istream& source);

}  // namespace terraflux::assets
