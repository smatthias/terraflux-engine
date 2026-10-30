#pragma once
#include <terraflux/math.hpp>
#include <cstdint>
#include <istream>
#include <span>
#include <map>
#include <string>
#include <vector>
namespace terraflux::assets {
struct ImageRgba8 {
  std::uint32_t width=0,height=0;
  std::vector<std::uint8_t> pixels; // bottom-to-top, RGBA8
};
struct Material {
  math::Vec3 base_color{1.0,1.0,1.0};
  std::string albedo_map;
  // Imported glTF PBR factors; current OpenGL renderer draws base color
  // only. These are preserved for a future metallic/roughness shader.
  double metallic_factor=1.0;
  double roughness_factor=1.0;
};
using MaterialLibrary = std::map<std::string,Material>;
MaterialLibrary parse_mtl(std::istream& input);
ImageRgba8 read_ppm_p3(std::istream& input);
ImageRgba8 checkerboard_image(std::uint32_t dimension=16);
// Safe, bounded PNG/JPEG decode from encoded bytes. Result is bottom-first
// RGBA8 for the existing OpenGL texture upload convention.
ImageRgba8 decode_image(std::span<const std::uint8_t> encoded,
                        const std::string& mime_type={});
} // namespace terraflux::assets
