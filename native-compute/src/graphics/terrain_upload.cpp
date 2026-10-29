#include <terraflux/graphics/terrain_upload.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace terraflux::graphics {
namespace {
float checked_float(double value) {
  if (!std::isfinite(value) ||
      value > std::numeric_limits<float>::max() ||
      value < -std::numeric_limits<float>::max())
    throw std::invalid_argument("terrain contains non-finite or unrepresentable GPU coordinates");
  return static_cast<float>(value);
}
}  // namespace

static_assert(sizeof(TerrainGpuVertex) == 6 * sizeof(float));
static_assert(std::is_standard_layout_v<TerrainGpuVertex>);

ShaderLayout terrain_shader_layout() {
  return {
      static_cast<std::uint32_t>(sizeof(TerrainGpuVertex)),
      {{"POSITION", 0, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(TerrainGpuVertex, position))},
       {"NORMAL", 1, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(TerrainGpuVertex, normal))}},
      {{"Camera", ShaderResourceKind::UniformBuffer, 0, kVertexStage}}
  };
}

TerrainUpload prepare_terrain_upload(const terrain::TerrainMesh& mesh) {
  TerrainUpload result;
  result.vertices.reserve(mesh.vertices.size());
  result.indices = mesh.indices;
  for (const auto& v : mesh.vertices) {
    result.vertices.push_back({{
        checked_float(v.position.x), checked_float(v.position.y), checked_float(v.position.z)
      }, {
        checked_float(v.normal.x), checked_float(v.normal.y), checked_float(v.normal.z)
      }});
  }
  for (const auto index : result.indices)
    if (index >= result.vertices.size())
      throw std::invalid_argument("terrain mesh contains an out-of-range triangle index");
  return result;
}

}  // namespace terraflux::graphics
