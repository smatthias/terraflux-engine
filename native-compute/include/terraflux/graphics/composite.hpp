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
} // namespace terraflux::graphics
