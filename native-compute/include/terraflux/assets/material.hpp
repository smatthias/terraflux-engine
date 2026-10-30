#pragma once
#include <terraflux/math.hpp>
#include <terraflux/texture_sampling.hpp>
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
  TextureSampling albedo_sampling{};
  std::string metallic_roughness_map,normal_map;
  TextureSampling metallic_roughness_sampling{},normal_sampling{};
  double normal_scale=1.0;
  // glTF PBR scalar factors, applied by the OpenGL material shader.
  double metallic_factor=1.0;
  double roughness_factor=1.0;
  std::string occlusion_map,emissive_map;
  TextureSampling occlusion_sampling{},emissive_sampling{};
  double occlusion_strength=1.0;
  math::Vec3 emissive_factor{0.0,0.0,0.0};
  enum class AlphaMode { Opaque, Mask };
  AlphaMode alpha_mode=AlphaMode::Opaque;
  double alpha_factor=1.0,alpha_cutoff=0.5;
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
