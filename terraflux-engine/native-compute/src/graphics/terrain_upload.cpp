#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/graphics/terrain_shading.hpp>

#include <algorithm>
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

static_assert(sizeof(TerrainGpuVertex) == 10 * sizeof(float));
static_assert(std::is_standard_layout_v<TerrainGpuVertex>);

namespace {
// Interleaved terrain vertex stream plus the camera block; shared by the
// terrain, wildfire and simple object shaders before their own bindings.
ShaderLayout terrain_vertex_layout() {
  return {
      static_cast<std::uint32_t>(sizeof(TerrainGpuVertex)),
      {{"POSITION", 0, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(TerrainGpuVertex, position))},
       {"NORMAL", 1, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(TerrainGpuVertex, normal))},
       {"FIRE_STATE", 2, VertexFormat::Float1, static_cast<std::uint32_t>(offsetof(TerrainGpuVertex, fire_state))},
       {"SURFACE", 3, VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(TerrainGpuVertex, surface))}},
      {{"Camera", ShaderResourceKind::UniformBuffer, 0, kVertexStage}}
  };
}
void drop_attribute(ShaderLayout& layout, const char* semantic) {
  std::erase_if(layout.attributes, [&](const VertexAttribute& a) { return a.semantic == semantic; });
}
}  // namespace

ShaderBinding terrain_style_binding() {
  // Both stages: the vertex stage displaces rock relief, the fragment stage shades.
  return {"TerrainStyle", ShaderResourceKind::UniformBuffer, kTerrainStyleSlot, kVertexStage | kFragmentStage};
}

ShaderLayout terrain_shader_layout() {
  ShaderLayout layout = terrain_vertex_layout();
  layout.bindings.push_back(terrain_style_binding());
  return layout;
}

ShaderLayout wildfire_shader_layout() {
  ShaderLayout layout = terrain_vertex_layout();
  // The wildfire shader uses the same interleaved vertex format but reads
  // fire data independently from a sparse-aware, clamped grid cell buffer.
  drop_attribute(layout, "FIRE_STATE"); // Fire state comes from the storage buffer instead.
  layout.bindings.front().stages = kVertexStage | kFragmentStage;
  layout.bindings.push_back({"FireState", ShaderResourceKind::StorageBuffer, 1, kFragmentStage});
  layout.bindings.push_back({"LayerVisibility",ShaderResourceKind::UniformBuffer,10,kFragmentStage});
  layout.bindings.push_back(terrain_style_binding());
  return layout;
}

ShaderLayout object_shader_layout() {
  ShaderLayout layout = terrain_vertex_layout();
  drop_attribute(layout, "FIRE_STATE"); // Objects have neither fire nor surface channels.
  drop_attribute(layout, "SURFACE");
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
      }, 0.0f, {
        checked_float(v.fuel_load), checked_float(v.fuel_moisture), checked_float(v.curvature)
      }});
  }
  for (const auto index : result.indices)
    if (index >= result.vertices.size())
      throw std::invalid_argument("terrain mesh contains an out-of-range triangle index");
  return result;
}

}  // namespace terraflux::graphics
