#include <terraflux/graphics/hud.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>

namespace terraflux::graphics {

ShaderLayout hud_shader_layout() {
  return {static_cast<std::uint32_t>(sizeof(HudVertex)),
          {{"POSITION", 0, VertexFormat::Float2, 0}, {"COLOR", 1, VertexFormat::Float4, 8}},
          {}};
}

void build_hud_geometry(std::span<const HudBar> bars, std::vector<HudVertex>& vertices, std::vector<std::uint32_t>& indices) {
  vertices.clear();
  indices.clear();
  const auto quad = [&](float x0, float y0, float x1, float y1, const std::array<float, 4>& c) {
    const auto base = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({{x0, y0}, {c[0], c[1], c[2], c[3]}});
    vertices.push_back({{x1, y0}, {c[0], c[1], c[2], c[3]}});
    vertices.push_back({{x1, y1}, {c[0], c[1], c[2], c[3]}});
    vertices.push_back({{x0, y1}, {c[0], c[1], c[2], c[3]}});
    for (const auto i : {0u, 1u, 2u, 0u, 2u, 3u}) indices.push_back(base + i);
  };
  for (const auto& b : bars) {
    if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.width) || !std::isfinite(b.height) || b.width <= 0 || b.height <= 0)
      throw std::invalid_argument("hud bar needs finite position and positive size");
    const float fill = std::clamp(std::isfinite(b.fill) ? b.fill : 0.0f, 0.0f, 1.0f);
    quad(b.x, b.y, b.x + b.width, b.y + b.height, b.back);
    if (fill > 0) quad(b.x, b.y, b.x + b.width * fill, b.y + b.height, b.color);
  }
}

HudRenderer::HudRenderer(RenderDevice& device, std::size_t max_bars)
    : device_(device), resources_(device), max_bars_(max_bars) {
  if (max_bars == 0 || max_bars > 1024) throw std::invalid_argument("hud bars must be 1..1024");
  std::vector<HudVertex> zero(max_bars * 8);
  std::vector<std::uint32_t> zero_indices(max_bars * 12, 0);
  const auto vertex_bytes = std::as_bytes(std::span(zero));
  const auto index_bytes = std::as_bytes(std::span(zero_indices));
  vb_ = resources_.create_buffer({BufferUsage::Vertex, vertex_bytes.size()}, vertex_bytes);
  ib_ = resources_.create_buffer({BufferUsage::Index, index_bytes.size()}, index_bytes);
  pipeline_ = resources_.create_pipeline({ShaderProgramId::Hud, hud_shader_layout(), false, true, false});
}

std::size_t HudRenderer::draw(std::span<const HudBar> bars) {
  if (!device_.active_render_pass_id()) throw std::logic_error("hud requires an active pass");
  if (bars.empty()) return 0;
  if (bars.size() > max_bars_) bars = bars.subspan(0, max_bars_);
  build_hud_geometry(bars, vertices_, indices_);
  device_.update_buffer(vb_, 0, std::as_bytes(std::span(vertices_)));
  device_.update_buffer(ib_, 0, std::as_bytes(std::span(indices_)));
  device_.draw_indexed({pipeline_, vb_, ib_, 0, static_cast<std::uint32_t>(indices_.size()), {}, {}, {}});
  return bars.size();
}

}  // namespace terraflux::graphics
