#pragma once

#include <terraflux/graphics/render_device.hpp>

#include <cstddef>
#include <algorithm>
#include <stdexcept>
#include <span>
#include <utility>
#include <vector>

namespace terraflux::graphics {

// Scope-bound owner of backend-neutral GPU handles. A RenderDevice must
// outlive this scope. The destructor releases pipelines, textures then buffers,
// including when asset loading or shader compilation throws partway through.
// This object is deliberately not copyable or movable.
class RenderResourceOwner {
 public:
  explicit RenderResourceOwner(RenderDevice& device):device_(device){}
  ~RenderResourceOwner() noexcept {reset();}
  RenderResourceOwner(const RenderResourceOwner&)=delete;
  RenderResourceOwner& operator=(const RenderResourceOwner&)=delete;
  RenderResourceOwner(RenderResourceOwner&&)=delete;
  RenderResourceOwner& operator=(RenderResourceOwner&&)=delete;

  BufferHandle create_buffer(BufferDesc desc,std::span<const std::byte> data) {
    auto handle=device_.create_buffer(desc,data);
    try{buffers_.push_back(handle);}
    catch(...){device_.destroy_buffer(handle);throw;}
    return handle;
  }
  TextureHandle create_texture2d(TextureDesc desc,std::span<const std::byte> data) {
    auto handle=device_.create_texture2d(desc,data);
    try{textures_.push_back(handle);}
    catch(...){device_.destroy_texture(handle);throw;}
    return handle;
  }
  PipelineHandle create_pipeline(PipelineDesc desc) {
    auto handle=device_.create_pipeline(std::move(desc));
    try{pipelines_.push_back(handle);}
    catch(...){device_.destroy_pipeline(handle);throw;}
    return handle;
  }
  // Selective release: forget only after backend destruction succeeds.
  void release_buffer(BufferHandle handle) {
    const auto it=std::find(buffers_.begin(),buffers_.end(),handle);
    if(it==buffers_.end())throw std::invalid_argument("buffer is not owned by this scope");
    device_.destroy_buffer(handle);
    buffers_.erase(it);
  }
  // Best-effort cleanup. Backend-specific destroy failures must not escape
  // a destructor during unwinding. The device owns the underlying backend.
  void reset() noexcept {
    for(auto it=pipelines_.rbegin();it!=pipelines_.rend();++it) {
      try{device_.destroy_pipeline(*it);}catch(...){}
    }
    for(auto it=textures_.rbegin();it!=textures_.rend();++it) {
      try{device_.destroy_texture(*it);}catch(...){}
    }
    for(auto it=buffers_.rbegin();it!=buffers_.rend();++it) {
      try{device_.destroy_buffer(*it);}catch(...){}
    }
    pipelines_.clear();textures_.clear();buffers_.clear();
  }
  std::size_t tracked_buffer_count() const{return buffers_.size();}
  std::size_t tracked_texture_count() const{return textures_.size();}
  std::size_t tracked_pipeline_count() const{return pipelines_.size();}

 private:
  RenderDevice& device_;
  std::vector<BufferHandle> buffers_;
  std::vector<TextureHandle> textures_;
  std::vector<PipelineHandle> pipelines_;
};

} // namespace terraflux::graphics
