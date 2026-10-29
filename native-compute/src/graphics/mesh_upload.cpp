#include <terraflux/graphics/mesh_upload.hpp>

#include <cmath>
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

}  // namespace terraflux::graphics
