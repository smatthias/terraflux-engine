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
ResizableRenderTarget::~ResizableRenderTarget() noexcept {
  try{reset();}catch(...){} // Device still owns storage if the caller left a pass active.
}
bool ResizableRenderTarget::resize(std::uint32_t width,std::uint32_t height) {
  if(!width || !height || (target_ && target_->width==width && target_->height==height))return false;
  const auto candidate=device_.create_render_target({width,height});
  try{if(target_)device_.destroy_render_target(target_->handle);}
  catch(...){device_.destroy_render_target(candidate);throw;}
  target_=RenderTargetDesc{width,height,RenderTargetKind::Offscreen,candidate};
  return true;
}
void ResizableRenderTarget::reset() {
  if(target_)device_.destroy_render_target(target_->handle);
  target_.reset();
}
TextureHandle ResizableRenderTarget::color() const {
  if(!target_)throw std::logic_error("offscreen target has no allocation");
  return device_.render_target_color(target_->handle);
}
} // namespace terraflux::graphics
