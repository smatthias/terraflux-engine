#pragma once

#include <array>
#include <cstddef>

namespace terraflux::math {

// Right-handed coordinates. Terrain maps east to +X, up to +Y, north to -Z.
// All math is independent of the graphics backend; matrices are column-major.
struct Vec2 {
  double x = 0.0, y = 0.0;
  constexpr Vec2 operator+(Vec2 rhs) const { return {x + rhs.x, y + rhs.y}; }
  constexpr Vec2 operator-(Vec2 rhs) const { return {x - rhs.x, y - rhs.y}; }
  constexpr Vec2 operator*(double scale) const { return {x * scale, y * scale}; }
  constexpr Vec2 operator/(double scale) const { return {x / scale, y / scale}; }
  constexpr double dot(Vec2 rhs) const { return x * rhs.x + y * rhs.y; }
  double length() const;
  Vec2 normalized() const;
};

struct Vec3 {
  double x = 0.0, y = 0.0, z = 0.0;
  constexpr Vec3 operator+(Vec3 rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
  constexpr Vec3 operator-(Vec3 rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
  constexpr Vec3 operator*(double scale) const { return {x * scale, y * scale, z * scale}; }
  constexpr Vec3 operator/(double scale) const { return {x / scale, y / scale, z / scale}; }
  constexpr double dot(Vec3 rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }
  constexpr Vec3 cross(Vec3 rhs) const {
    return {y * rhs.z - z * rhs.y, z * rhs.x - x * rhs.z, x * rhs.y - y * rhs.x};
  }
  double length() const;
  Vec3 normalized() const;
};

struct Vec4 {
  double x = 0.0, y = 0.0, z = 0.0, w = 0.0;
  constexpr Vec4 operator+(Vec4 rhs) const {
    return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
  }
  constexpr Vec4 operator*(double scale) const {
    return {x * scale, y * scale, z * scale, w * scale};
  }
  constexpr double dot(Vec4 rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w; }
};

// Stored column-major: element(row, column) = values[column * 4 + row].
// API-specific projection depth ranges are explicit: NO = [-1,1], ZO = [0,1].
struct Mat4 {
  std::array<double, 16> values{};
  constexpr double at(std::size_t row, std::size_t column) const {
    return values[column * 4 + row];
  }
  constexpr double& at(std::size_t row, std::size_t column) {
    return values[column * 4 + row];
  }
  static Mat4 identity();
  static Mat4 translation(Vec3 offset);
  static Mat4 scaling(Vec3 factors);
  static Mat4 rotationX(double radians);
  static Mat4 rotationY(double radians);
  static Mat4 rotationZ(double radians);
  static Mat4 lookAtRH(Vec3 eye, Vec3 target, Vec3 up);
  static Mat4 perspectiveRH_NO(double fov_y_radians, double aspect, double near_plane, double far_plane);
  static Mat4 perspectiveRH_ZO(double fov_y_radians, double aspect, double near_plane, double far_plane);

  Mat4 operator*(const Mat4& rhs) const;
  Vec4 transform(Vec4 value) const;
  Vec3 transformPoint(Vec3 value) const;
  Vec3 transformDirection(Vec3 value) const;
};

}  // namespace terraflux::math
