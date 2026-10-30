#pragma once
#include <cstdint>
namespace terraflux {
// Neutral texture sampling settings. The glTF parser converts numeric glTF
// enum values at the import boundary; OpenGL converts these to GL enums.
enum class TextureMinFilter : std::uint8_t {
  Nearest, Linear, NearestMipNearest, LinearMipNearest,
  NearestMipLinear, LinearMipLinear
};
enum class TextureMagFilter : std::uint8_t { Nearest, Linear };
enum class TextureWrap : std::uint8_t { Repeat, ClampToEdge, MirroredRepeat };
struct TextureSampling {
  TextureMinFilter min=TextureMinFilter::Nearest;
  TextureMagFilter mag=TextureMagFilter::Nearest;
  TextureWrap wrap_s=TextureWrap::Repeat;
  TextureWrap wrap_t=TextureWrap::Repeat;
  constexpr bool operator==(const TextureSampling&) const = default;
  constexpr bool uses_mipmaps() const {
    return min!=TextureMinFilter::Nearest && min!=TextureMinFilter::Linear;
  }
};
} // namespace terraflux
