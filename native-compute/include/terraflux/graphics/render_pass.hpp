#pragma once
#include <terraflux/graphics/render_device.hpp>
#include <stdexcept>

namespace terraflux::graphics {
// Declare after target/resource owners so unwinding ends the pass before their
// storage is released. Device and context must outlive this nonmovable scope.
class RenderPassScope {
 public:
  RenderPassScope(RenderDevice& device,const RenderPassDesc& desc):device_(device) {
    device_.begin_render_pass(desc);
    pass_id_=device_.active_render_pass_id();
  }
  ~RenderPassScope() noexcept {
    // Never replace a drawing exception or close somebody else's newer pass.
    try{if(active())device_.end_render_pass();}catch(...){}
  }
  RenderPassScope(const RenderPassScope&)=delete;
  RenderPassScope& operator=(const RenderPassScope&)=delete;
  RenderPassScope(RenderPassScope&&)=delete;
  RenderPassScope& operator=(RenderPassScope&&)=delete;

  bool active() const noexcept {return pass_id_ && device_.active_render_pass_id()==pass_id_;}
  // Idempotent after successful closure. Report raw-API ownership violations;
  // a failed end leaves this scope armed so its destructor can retry cleanup.
  void end() {
    if(!pass_id_)return;
    if(!active())throw std::logic_error("render pass scope no longer owns the active pass");
    device_.end_render_pass();
    pass_id_=0;
  }
 private:
  RenderDevice& device_;
  std::uint64_t pass_id_=0;
};
} // namespace terraflux::graphics
