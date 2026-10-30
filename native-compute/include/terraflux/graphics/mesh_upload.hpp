#pragma once

#include <terraflux/assets/obj.hpp>
#include <terraflux/graphics/terrain_upload.hpp>
#include <terraflux/math.hpp>
#include <array>
#include <vector>

namespace terraflux::graphics {

// Upload-ready world-space asset data using the established 28-byte vertex
// format. Normal directions are transformed by the inverse transpose of the
// model matrix (so nonuniform scales remain correctly lit).
TerrainUpload prepare_asset_upload(const assets::Mesh& mesh, const math::Mat4& world);
// Upload asset in original local/object space for instanced scene nodes.
TerrainUpload prepare_asset_upload(const assets::Mesh& mesh);

// std140, binding 2. A unique buffer per drawable object, but geometry can be
// shared between objects. Never changes the vertex/index buffers on animation.
struct ObjectUniform {
  std::array<float, 16> model{};
  std::array<float, 16> normal_matrix{};
  std::array<float, 4> tint{};
};
static_assert(sizeof(ObjectUniform) == 144);

struct TexturedGpuVertex {
  float position[3]{},normal[3]{},uv[2]{};
};
static_assert(sizeof(TexturedGpuVertex)==8*sizeof(float));
struct TexturedMeshUpload {
  std::vector<TexturedGpuVertex> vertices;
  std::vector<std::uint32_t> indices;
};
TexturedMeshUpload prepare_textured_asset_upload(const assets::Mesh& mesh);
ShaderLayout textured_object_shader_layout();
ShaderLayout instanced_textured_object_shader_layout();

ObjectUniform make_object_uniform(const math::Mat4& world,
                                  math::Vec3 tint = {0.19, 0.55, 0.94});

}  // namespace terraflux::graphics
