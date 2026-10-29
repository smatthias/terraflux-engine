#pragma once

#include <terraflux/graphics/render_device.hpp>
#include <terraflux/terrain_mesh.hpp>

#include <cstdint>
#include <vector>

namespace terraflux::graphics {

// Compact, deterministic, backend-independent GPU upload representation.
// Position and normal are 32-bit floats; the simulation keeps double precision.
struct TerrainGpuVertex {
  float position[3];
  float normal[3];
  float fire_state = 0.0f; // 0 unburned, 0.5 burned, 1 active
};

struct TerrainUpload {
  std::vector<TerrainGpuVertex> vertices;
  std::vector<std::uint32_t> indices;
};

ShaderLayout terrain_shader_layout();
TerrainUpload prepare_terrain_upload(const terrain::TerrainMesh& mesh);

}  // namespace terraflux::graphics
