#include <terraflux/graphics/mesh_upload.hpp>

#include <cmath>
#include <algorithm>
#include <type_traits>
#include <cstddef>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace terraflux::graphics {
namespace {
float checked_float(double value) {
  if (!std::isfinite(value) ||
      value > std::numeric_limits<float>::max() ||
      value < -std::numeric_limits<float>::max())
    throw std::invalid_argument("imported mesh coordinate exceeds GPU float range");
  return static_cast<float>(value);
}
struct NormalMatrix {
  double c[3][3]{};
  double determinant = 0;
};
NormalMatrix normal_matrix(const math::Mat4& m) {
  // Cofactor matrix C = det(M) * inverse(transpose(M)).
  const auto a = [&](int row,int col){return m.at(row,col);};
  NormalMatrix n;
  n.c[0][0] = a(1,1)*a(2,2)-a(1,2)*a(2,1);
  n.c[0][1] = a(1,2)*a(2,0)-a(1,0)*a(2,2);
  n.c[0][2] = a(1,0)*a(2,1)-a(1,1)*a(2,0);
  n.c[1][0] = a(0,2)*a(2,1)-a(0,1)*a(2,2);
  n.c[1][1] = a(0,0)*a(2,2)-a(0,2)*a(2,0);
  n.c[1][2] = a(0,1)*a(2,0)-a(0,0)*a(2,1);
  n.c[2][0] = a(0,1)*a(1,2)-a(0,2)*a(1,1);
  n.c[2][1] = a(0,2)*a(1,0)-a(0,0)*a(1,2);
  n.c[2][2] = a(0,0)*a(1,1)-a(0,1)*a(1,0);
  n.determinant = a(0,0)*n.c[0][0] + a(0,1)*n.c[0][1] + a(0,2)*n.c[0][2];
  if (!std::isfinite(n.determinant) || std::abs(n.determinant) < 1e-15)
    throw std::invalid_argument("non-invertible mesh transform");
  return n;
}

math::Vec3 transformed_normal(const NormalMatrix& matrix, math::Vec3 n) {
  const auto& c = matrix.c;
  math::Vec3 result{
     (c[0][0]*n.x + c[0][1]*n.y + c[0][2]*n.z)/matrix.determinant,
     (c[1][0]*n.x + c[1][1]*n.y + c[1][2]*n.z)/matrix.determinant,
     (c[2][0]*n.x + c[2][1]*n.y + c[2][2]*n.z)/matrix.determinant
  };
  return result.normalized();
}
}  // namespace

TerrainUpload prepare_asset_upload(const assets::Mesh& mesh, const math::Mat4& world) {
  if (mesh.vertices.empty() || mesh.indices.empty())
    throw std::invalid_argument("imported mesh must contain triangles");
  const auto normal_transform = normal_matrix(world);
  TerrainUpload output;
  output.vertices.reserve(mesh.vertices.size());
  output.indices = mesh.indices;
  for (const auto& v : mesh.vertices) {
    const auto p = world.transformPoint(v.position);
    const auto n = transformed_normal(normal_transform, v.normal);
    output.vertices.push_back({{
        checked_float(p.x), checked_float(p.y), checked_float(p.z)
      }, {
        checked_float(n.x), checked_float(n.y), checked_float(n.z)
      }, 0.0f});
  }
  for (const auto id : output.indices)
    if (id >= output.vertices.size())
      throw std::invalid_argument("imported mesh has out-of-range triangle index");
  return output;
}

TerrainUpload prepare_asset_upload(const assets::Mesh& mesh) {
  return prepare_asset_upload(mesh, math::Mat4::identity());
}

TexturedMeshUpload prepare_textured_asset_upload(const assets::Mesh& mesh) {
  if (mesh.vertices.empty() || mesh.indices.empty())
    throw std::invalid_argument("textured mesh requires triangles");
  TexturedMeshUpload out;
  out.vertices.reserve(mesh.vertices.size());
  out.indices=mesh.indices;
  for (const auto& v : mesh.vertices) {
    out.vertices.push_back({{
        checked_float(v.position.x),checked_float(v.position.y),checked_float(v.position.z)
      },{
        checked_float(v.normal.x),checked_float(v.normal.y),checked_float(v.normal.z)
      },{
        checked_float(v.uv.x),checked_float(v.uv.y)
      }});
  }
  for (const auto id : out.indices)
    if (id>=out.vertices.size())
      throw std::invalid_argument("textured mesh references missing vertex");
  return out;
}

ShaderLayout textured_object_shader_layout() {
  return {static_cast<std::uint32_t>(sizeof(TexturedGpuVertex)),
    {{"POSITION",0,VertexFormat::Float3,
          static_cast<std::uint32_t>(offsetof(TexturedGpuVertex,position))},
     {"NORMAL",1,VertexFormat::Float3,
          static_cast<std::uint32_t>(offsetof(TexturedGpuVertex,normal))},
     {"TEXCOORD",3,VertexFormat::Float2,
          static_cast<std::uint32_t>(offsetof(TexturedGpuVertex,uv))}},
    {{"Camera",ShaderResourceKind::UniformBuffer,0,kVertexStage},
     {"Object",ShaderResourceKind::UniformBuffer,2,kVertexStage},
     {"AlbedoTexture",ShaderResourceKind::SampledTexture,3,kFragmentStage}}};
}

ShaderLayout instanced_textured_object_shader_layout() {
  auto layout=textured_object_shader_layout();
  layout.bindings.erase(std::remove_if(layout.bindings.begin(),layout.bindings.end(),
      [](const ShaderBinding& b){return b.name=="Object";}),layout.bindings.end());
  layout.bindings.push_back({"InstanceData",ShaderResourceKind::StorageBuffer,4,kVertexStage});
  validate_shader_layout(layout);
  return layout;
}

ShaderLayout pbr_textured_object_shader_layout(bool instanced) {
  auto layout=instanced?instanced_textured_object_shader_layout():
                       textured_object_shader_layout();
  layout.bindings.push_back({"PbrMaterial",ShaderResourceKind::UniformBuffer,5,kFragmentStage});
  validate_shader_layout(layout);
  return layout;
}

ObjectUniform make_object_uniform(const math::Mat4& world, math::Vec3 tint) {
  const auto normal = normal_matrix(world);
  ObjectUniform out;
  for (std::size_t i = 0; i < 16; ++i)
    out.model[i] = checked_float(world.values[i]);
  out.normal_matrix = {};
  for (int i = 0; i < 4; ++i) out.normal_matrix[i*4+i] = 1.0f;
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row)
      out.normal_matrix[column*4+row] =
          checked_float(normal.c[row][column] / normal.determinant);
  }
  for (double component : {tint.x, tint.y, tint.z})
    if (!(component >= 0.0 && component <= 1.0) || !std::isfinite(component))
      throw std::invalid_argument("material tint must be finite and within [0, 1]");
  out.tint = {checked_float(tint.x), checked_float(tint.y), checked_float(tint.z), 1.0f};
  return out;
}

}  // namespace terraflux::graphics
