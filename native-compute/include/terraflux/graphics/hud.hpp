#pragma once

#include <terraflux/graphics/render_device.hpp>
#include <terraflux/graphics/resource_owner.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace terraflux::graphics {

// Screen-space bars (health, body heat, strength ...): a dark back plate and
// a fill of `fill` of its width, in normalized device coordinates, drawn with
// the Hud program (no depth, alpha blended) over the finished frame.
struct HudBar {
  float x = -0.95f, y = -0.9f, width = 0.3f, height = 0.04f;  // NDC, lower-left corner and size
  float fill = 1.0f;                                           // 0 .. 1
  std::array<float, 4> color{0.9f, 0.2f, 0.2f, 0.9f};
  std::array<float, 4> back{0.05f, 0.05f, 0.05f, 0.6f};
};
struct HudVertex { float position[2]; float color[4]; };
inline constexpr std::size_t kHudVertexBytes = 24;
static_assert(sizeof(HudVertex) == kHudVertexBytes);
// POSITION (0, float2 NDC), COLOR (1, float4); no uniforms.
ShaderLayout hud_shader_layout();
// The vertices and indices of a set of bars (two quads each). Clamps fills to 0 .. 1.
void build_hud_geometry(std::span<const HudBar> bars, std::vector<HudVertex>& vertices, std::vector<std::uint32_t>& indices);

class HudRenderer {
 public:
  explicit HudRenderer(RenderDevice& device, std::size_t max_bars = 16);
  HudRenderer(const HudRenderer&) = delete;
  HudRenderer& operator=(const HudRenderer&) = delete;
  // Inside an active pass, after the scene. Returns the number of bars drawn.
  std::size_t draw(std::span<const HudBar> bars);
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  std::size_t max_bars_;
  BufferHandle vb_, ib_;
  PipelineHandle pipeline_;
  std::vector<HudVertex> vertices_;
  std::vector<std::uint32_t> indices_;
};

}  // namespace terraflux::graphics
