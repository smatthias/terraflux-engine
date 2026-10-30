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

static_assert(sizeof(TerrainGpuVertex) == 7 * sizeof(float));
static_assert(std::is_standard_layout_v<TerrainGpuVertex>);

ShaderLayout terrain_shader_layout() {
  return {
      static_cast<std::uint32_t>(sizeof(TerrainGpuVertex)),
      {{"POSITION", 0, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(TerrainGpuVertex, position))},
       {"NORMAL", 1, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(TerrainGpuVertex, normal))},
       {"FIRE_STATE", 2, VertexFormat::Float1, static_cast<std::uint32_t>(offsetof(TerrainGpuVertex, fire_state))}},
      {{"Camera", ShaderResourceKind::UniformBuffer, 0, kVertexStage}}
  };
}

ShaderLayout wildfire_shader_layout() {
  ShaderLayout layout = terrain_shader_layout();
  // The wildfire shader uses the same interleaved vertex format but reads
  // fire data independently from a sparse-aware, clamped grid cell buffer.
  layout.attributes.pop_back(); // FIRE_STATE is no longer a vertex input.
  layout.bindings.front().stages = kVertexStage | kFragmentStage;
  layout.bindings.push_back({"FireState", ShaderResourceKind::StorageBuffer, 1, kFragmentStage});
  layout.bindings.push_back({"LayerVisibility",ShaderResourceKind::UniformBuffer,10,kFragmentStage});
  return layout;
}

ShaderLayout object_shader_layout() {
  ShaderLayout layout = terrain_shader_layout();
  layout.attributes.pop_back(); // no FIRE_STATE vertex channel for objects
  layout.bindings.push_back({"Object", ShaderResourceKind::UniformBuffer, 2, kVertexStage});
  return layout;
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
      }, 0.0f});
  }
  for (const auto index : result.indices)
    if (index >= result.vertices.size())
      throw std::invalid_argument("terrain mesh contains an out-of-range triangle index");
  return result;
}

}  // namespace terraflux::graphics
