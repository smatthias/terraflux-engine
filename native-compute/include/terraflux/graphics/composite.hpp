#pragma once
#include <terraflux/graphics/resource_owner.hpp>

namespace terraflux::graphics {
// Reusable indexed fullscreen copy. The caller begins/ends the destination
// pass and keeps the source texture/target alive. Nearest filtering is used by
// target color views; no flip, tone mapping, blending or depth copy is applied.
class FullscreenComposite {
 public:
  explicit FullscreenComposite(RenderDevice& device);
  void draw(TextureHandle source);
 private:
  RenderDevice& device_;
  RenderResourceOwner resources_;
  PipelineHandle pipeline_;
  BufferHandle vertices_,indices_;
};
// Resize only between passes. A candidate is admitted before retiring the old
// target; any failure preserves the old handle, view and extent. Zero extents
// represent a minimized window and leave storage alone.
class ResizableRenderTarget {
 public:
  explicit ResizableRenderTarget(RenderDevice& device):device_(device){}
  ~ResizableRenderTarget() noexcept;
  ResizableRenderTarget(const ResizableRenderTarget&)=delete;
  ResizableRenderTarget& operator=(const ResizableRenderTarget&)=delete;
  bool resize(std::uint32_t width,std::uint32_t height);
  void reset();
  std::optional<RenderTargetDesc> target() const {return target_;}
  TextureHandle color() const;
 private:
  RenderDevice& device_;
  std::optional<RenderTargetDesc> target_;
};
} // namespace terraflux::graphics
