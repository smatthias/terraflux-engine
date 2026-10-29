#pragma once
#include <terraflux/math.hpp>
#include <cstdint>
#include <istream>
#include <vector>
namespace terraflux::assets {
struct ImageRgba8 {
  std::uint32_t width=0,height=0;
  std::vector<std::uint8_t> pixels; // bottom-to-top, RGBA8
};
struct Material {
  math::Vec3 base_color{1.0,1.0,1.0};
};
ImageRgba8 read_ppm_p3(std::istream& input);
ImageRgba8 checkerboard_image(std::uint32_t dimension=16);
} // namespace terraflux::assets
