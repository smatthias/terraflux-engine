#include <terraflux/graphics/composite.hpp>
#include <array>

namespace terraflux::graphics {
FullscreenComposite::FullscreenComposite(RenderDevice& device):device_(device),resources_(device) {
  // Two triangles cover clip space. UV origin matches OpenGL framebuffer
  // storage, so a same-size copy preserves vertical orientation exactly.
  constexpr std::array<float,16> vertices{-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
  constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
  vertices_=resources_.create_buffer({BufferUsage::Vertex,sizeof(vertices)},std::as_bytes(std::span(vertices)));
  indices_=resources_.create_buffer({BufferUsage::Index,sizeof(indices)},std::as_bytes(std::span(indices)));
  pipeline_=resources_.create_pipeline({ShaderProgramId::Composite,
      shader_program_layout(ShaderProgramId::Composite),false,false,false});
}
void FullscreenComposite::draw(TextureHandle source) {
  device_.draw_indexed({pipeline_,vertices_,indices_,0,6,{}, {}, {{0,source}}});
}
} // namespace terraflux::graphics
