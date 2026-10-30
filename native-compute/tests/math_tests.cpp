#include <terraflux/math.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>

using terraflux::math::Mat4;
using terraflux::math::Vec2;
using terraflux::math::Vec3;
using terraflux::math::Vec4;

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
bool near(double a, double b, double tolerance = 1e-10) {
  return std::abs(a - b) <= tolerance;
}
void test_vec2() {
  Vec2 v{3, 4};
  require(near(v.length(), 5), "Vec2 length");
  require(near(v.normalized().x, 0.6), "Vec2 normalized");
  require(near(v.dot({4, -3}), 0), "Vec2 dot");
}
void test_vec3() {
  Vec3 x{1, 0, 0}, y{0, 1, 0};
  Vec3 z = x.cross(y);
  require(near(z.z, 1) && near(z.x, 0) && near(z.y, 0), "right handed cross product");
  require(near(z.dot(x), 0) && near(z.dot(y), 0), "cross orthogonality");
  require(near(Vec3{3, 4, 0}.normalized().length(), 1), "Vec3 unit vector");
}
void test_vec4() {
  require(near(Vec4{1, 2, 3, 4}.dot({4, 3, 2, 1}), 20), "Vec4 dot product");
}
void test_zero_normalization() {
  bool thrown = false;
  try { (void)Vec3{}.normalized(); } catch (const std::domain_error&) { thrown = true; }
  require(thrown, "zero vector must reject normalization");
}
void test_identity_translation() {
  Vec3 p{1, 2, 3};
  const Vec3 result = (Mat4::translation({5, -2, 4}) * Mat4::identity()).transformPoint(p);
  require(near(result.x, 6) && near(result.y, 0) && near(result.z, 7), "translation/identity");
  const Vec3 direction = Mat4::translation({100, 100, 100}).transformDirection(p);
  require(near(direction.x, 1) && near(direction.y, 2), "direction must ignore translation");
}
void test_transform_order() {
  const Vec3 p = (Mat4::translation({10, 0, 0}) * Mat4::scaling({2, 3, 4})).transformPoint({1, 2, 3});
  require(near(p.x, 12) && near(p.y, 6) && near(p.z, 12), "column-major transform order");
  constexpr double kHalfPi = 1.57079632679489661923;
  const Vec3 rotated = Mat4::rotationY(kHalfPi).transformDirection({1, 0, 0});
  require(near(rotated.x, 0) && near(rotated.z, -1), "Y rotation handedness");
}
void test_camera() {
  const Mat4 view = Mat4::lookAtRH({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
  const Vec3 origin = view.transformPoint({0, 0, 0});
  require(near(origin.x, 0) && near(origin.y, 0) && near(origin.z, -10), "view matrix depth");
}
void test_projection_depth() {
  constexpr double kHalfPi = 1.57079632679489661923;
  const Mat4 no = Mat4::perspectiveRH_NO(kHalfPi, 1.0, 1.0, 11.0);
  const Mat4 zo = Mat4::perspectiveRH_ZO(kHalfPi, 1.0, 1.0, 11.0);
  require(near(no.transformPoint({0, 0, -1}).z, -1), "OpenGL near plane depth");
  require(near(no.transformPoint({0, 0, -11}).z, 1), "OpenGL far plane depth");
  require(near(zo.transformPoint({0, 0, -1}).z, 0), "zero-to-one near plane depth");
  require(near(zo.transformPoint({0, 0, -11}).z, 1), "zero-to-one far plane depth");
  bool invalid = false;
  try { (void)Mat4::perspectiveRH_NO(0, 1, 1, 100); }
  catch (const std::invalid_argument&) { invalid = true; }
  require(invalid, "invalid FOV must be rejected");
}
}  // namespace

int main() {
  try {
    test_vec2(); test_vec3(); test_vec4(); test_zero_normalization();
    test_identity_translation(); test_transform_order(); test_camera(); test_projection_depth();
    std::cout << "TerraFlux math: 8 tests passed\n";
  } catch (const std::exception& e) {
    std::cerr << "TerraFlux math test failed: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
