#include <terraflux/math.hpp>

#include <cmath>
#include <stdexcept>

namespace terraflux::math {
namespace {
double checked_length(double squared_length) {
  if (!(squared_length > 0.0) || !std::isfinite(squared_length))
    throw std::domain_error("cannot normalize zero-length or non-finite vector");
  return std::sqrt(squared_length);
}
void validate_perspective(double fov, double aspect, double near_plane, double far_plane) {
  constexpr double kPi = 3.14159265358979323846;
  if (!std::isfinite(fov) || !std::isfinite(aspect) ||
      !std::isfinite(near_plane) || !std::isfinite(far_plane) ||
      !(fov > 0.0 && fov < kPi && aspect > 0.0 && near_plane > 0.0 && far_plane > near_plane))
    throw std::invalid_argument("invalid perspective projection parameters");
}
}  // namespace

double Vec2::length() const { return std::sqrt(dot(*this)); }
Vec2 Vec2::normalized() const { return *this / checked_length(dot(*this)); }
double Vec3::length() const { return std::sqrt(dot(*this)); }
Vec3 Vec3::normalized() const { return *this / checked_length(dot(*this)); }

Mat4 Mat4::identity() {
  Mat4 result{};
  for (std::size_t i = 0; i < 4; ++i) result.at(i, i) = 1.0;
  return result;
}

Mat4 Mat4::translation(Vec3 offset) {
  Mat4 result = identity();
  result.at(0, 3) = offset.x;
  result.at(1, 3) = offset.y;
  result.at(2, 3) = offset.z;
  return result;
}

Mat4 Mat4::scaling(Vec3 factors) {
  Mat4 result = identity();
  result.at(0, 0) = factors.x;
  result.at(1, 1) = factors.y;
  result.at(2, 2) = factors.z;
  return result;
}

Mat4 Mat4::rotationY(double radians) {
  Mat4 result = identity();
  const double s = std::sin(radians), c = std::cos(radians);
  result.at(0, 0) = c;  result.at(0, 2) = s;
  result.at(2, 0) = -s; result.at(2, 2) = c;
  return result;
}

Mat4 Mat4::lookAtRH(Vec3 eye, Vec3 target, Vec3 up) {
  const Vec3 forward = (target - eye).normalized();
  const Vec3 right = forward.cross(up).normalized();
  const Vec3 corrected_up = right.cross(forward);
  Mat4 result = identity();
  result.at(0, 0) = right.x; result.at(0, 1) = right.y; result.at(0, 2) = right.z;
  result.at(1, 0) = corrected_up.x; result.at(1, 1) = corrected_up.y; result.at(1, 2) = corrected_up.z;
  result.at(2, 0) = -forward.x; result.at(2, 1) = -forward.y; result.at(2, 2) = -forward.z;
  result.at(0, 3) = -right.dot(eye);
  result.at(1, 3) = -corrected_up.dot(eye);
  result.at(2, 3) = forward.dot(eye);
  return result;
}

Mat4 Mat4::perspectiveRH_NO(double fov, double aspect, double near_plane, double far_plane) {
  validate_perspective(fov, aspect, near_plane, far_plane);
  Mat4 result{};
  const double focal = 1.0 / std::tan(fov * 0.5);
  result.at(0, 0) = focal / aspect;
  result.at(1, 1) = focal;
  result.at(2, 2) = -(far_plane + near_plane) / (far_plane - near_plane);
  result.at(2, 3) = -(2.0 * far_plane * near_plane) / (far_plane - near_plane);
  result.at(3, 2) = -1.0;
  return result;
}

Mat4 Mat4::perspectiveRH_ZO(double fov, double aspect, double near_plane, double far_plane) {
  validate_perspective(fov, aspect, near_plane, far_plane);
  Mat4 result{};
  const double focal = 1.0 / std::tan(fov * 0.5);
  result.at(0, 0) = focal / aspect;
  result.at(1, 1) = focal;
  result.at(2, 2) = -far_plane / (far_plane - near_plane);
  result.at(2, 3) = -(far_plane * near_plane) / (far_plane - near_plane);
  result.at(3, 2) = -1.0;
  return result;
}

Mat4 Mat4::operator*(const Mat4& rhs) const {
  Mat4 result{};
  for (std::size_t col = 0; col < 4; ++col)
    for (std::size_t row = 0; row < 4; ++row)
      for (std::size_t k = 0; k < 4; ++k)
        result.at(row, col) += at(row, k) * rhs.at(k, col);
  return result;
}

Vec4 Mat4::transform(Vec4 v) const {
  return {
    at(0, 0)*v.x + at(0, 1)*v.y + at(0, 2)*v.z + at(0, 3)*v.w,
    at(1, 0)*v.x + at(1, 1)*v.y + at(1, 2)*v.z + at(1, 3)*v.w,
    at(2, 0)*v.x + at(2, 1)*v.y + at(2, 2)*v.z + at(2, 3)*v.w,
    at(3, 0)*v.x + at(3, 1)*v.y + at(3, 2)*v.z + at(3, 3)*v.w
  };
}

Vec3 Mat4::transformPoint(Vec3 point) const {
  const Vec4 p = transform({point.x, point.y, point.z, 1.0});
  if (!std::isfinite(p.w) || p.w == 0.0)
    throw std::domain_error("point projects to invalid homogeneous W");
  return {p.x / p.w, p.y / p.w, p.z / p.w};
}

Vec3 Mat4::transformDirection(Vec3 v) const {
  const Vec4 p = transform({v.x, v.y, v.z, 0.0});
  return {p.x, p.y, p.z};
}

}  // namespace terraflux::math
